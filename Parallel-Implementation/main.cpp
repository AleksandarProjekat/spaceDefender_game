// Space Defender — Paralelna verzija (dt-based)
//
// Kontrole:
//   A / D ili strelice – kretanje levo / desno
//   W ili SPACE        – pucanje
//   R                  – restart igre
//   ESC ili Q          – izlaz iz programa
//
// Parametri komandne linije:
//   --asteroids N   broj asteroida
//   --target N      broj poena za pobedu
//   --width W       sirina prozora
//   --height H      visina prozora
//
// Autor: Aleksandar Vig

#include <opencv2/opencv.hpp>
#include <vector>
#include <random>
#include <chrono>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <pthread.h>
#include <atomic>
#include <thread>

// ------------------------------------------------------------
// Strukture podataka
// ------------------------------------------------------------

struct Bullet {
    float x{0}, y{0};
    float v{720.0f};       // px/s
    bool active{false};
};

struct Asteroid {
    float x{0}, y{0};
    int r{20};
    float v{140.0f};       // px/s (osnovna brzina)
    bool alive{true};
};

// ------------------------------------------------------------
// Pomocne funkcije
// ------------------------------------------------------------

static inline float sqr(float x){ return x*x; }

static bool hit(const Bullet& b, const Asteroid& a){
    return sqr(b.x - a.x) + sqr(b.y - a.y) <= sqr(float(a.r + 6));
}

static void draw_ship(cv::Mat& img, int cx, int cy, int w){
    cv::Point pts[1][3];
    pts[0][0] = {cx,       cy-26};
    pts[0][1] = {cx-w/2,   cy+22};
    pts[0][2] = {cx+w/2,   cy+22};

    const cv::Point* ppt[1] = { pts[0] };
    int npt[] = {3};

    cv::fillPoly(img, ppt, npt, 1, cv::Scalar(255,255,255), cv::LINE_AA);
    cv::circle(img, {cx, cy+8}, 5, cv::Scalar(0,0,0), cv::FILLED, cv::LINE_AA);
}

// ------------------------------------------------------------
// Deljeno stanje (zasticeno mutex-om)
// ------------------------------------------------------------

struct InputState {
    std::atomic<int>  move_impulse{0};    // -1 levo, +1 desno, 0 nista
    std::atomic<bool> fire_impulse{false};
};

struct GameState {
    int W = 1024, H = 768;
    int targetScore = 30;
    int asteroidCount = 5;

    int shipW = 40;
    float shipX = 0.0f;
    int shipY = 0;
    int shipStep = 35;

    int score = 0;
    bool win = false;
    bool over = false;

    bool running = true;

    std::vector<Asteroid> ast;
    std::vector<Bullet> bullets;

    std::vector<cv::Point> stars;

    std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> randAstX;
    std::uniform_int_distribution<int> randAstSpawnY;
    std::uniform_int_distribution<int> randAstV; // px/s

    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
};

// Kreiranje pocetnih asteroida 
static std::vector<Asteroid> init_asteroids(GameState& gs){
    std::vector<Asteroid> a(gs.asteroidCount);
    for(auto& it : a){
        it.x = float(gs.randAstX(gs.rng));
        it.y = float(gs.randAstSpawnY(gs.rng));
        it.r = 20;
        it.v = float(gs.randAstV(gs.rng));
        it.alive = true;
    }
    return a;
}

static void reset_round(GameState& gs){
    gs.score = 0;
    gs.win = false;
    gs.over = false;

    gs.shipX = float(gs.W / 2);
    gs.shipY = gs.H - 40;
    gs.shipW = 40;
    gs.shipStep = 35;

    gs.ast = init_asteroids(gs);

    gs.bullets.clear();
    gs.bullets.reserve(64);
}

// ------------------------------------------------------------
// Nit za brod (kretanje + pucanje)
// ------------------------------------------------------------

struct ShipArgs {
    GameState* gs;
    InputState* in;
};

void* ship_thread(void* arg){
    ShipArgs* A = (ShipArgs*)arg;
    GameState* gs = A->gs;
    InputState* in = A->in;

    auto tPrev = std::chrono::steady_clock::now();

    while(true){
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        auto tNow = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;
        if(dt > 0.05f) dt = 0.05f;

        pthread_mutex_lock(&gs->mtx);
        if(!gs->running){
            pthread_mutex_unlock(&gs->mtx);
            break;
        }

        if(!gs->over){
            int dir = in->move_impulse.exchange(0);
            if(dir != 0){
                int step_now = gs->shipStep + gs->score / 8;
                gs->shipX += float(dir * step_now);
                gs->shipX = std::clamp(gs->shipX, float(gs->shipW), float(gs->W - gs->shipW));
            }

            if(in->fire_impulse.exchange(false)){
                Bullet b;
                b.active = true;
                b.x = gs->shipX;
                b.y = float(gs->shipY - 24);
                gs->bullets.push_back(b);
            }
        }

        pthread_mutex_unlock(&gs->mtx);
    }

    return nullptr;
}

// ------------------------------------------------------------
// Nit za asteroid (svaki asteroid ima svoju nit)
// ------------------------------------------------------------

struct AstArgs {
    GameState* gs;
    int idx;
};

void* asteroid_thread(void* arg){
    AstArgs* A = (AstArgs*)arg;
    GameState* gs = A->gs;
    int idx = A->idx;

    auto tPrev = std::chrono::steady_clock::now();

    while(true){
        std::this_thread::sleep_for(std::chrono::milliseconds(4));

        auto tNow = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;
        if(dt > 0.05f) dt = 0.05f;

        pthread_mutex_lock(&gs->mtx);
        if(!gs->running){
            pthread_mutex_unlock(&gs->mtx);
            break;
        }

        if(!gs->over){
            Asteroid& a = gs->ast[idx];

            if(!a.alive){
                a.x = float(gs->randAstX(gs->rng));
                a.y = float(gs->randAstSpawnY(gs->rng));
                a.v = float(gs->randAstV(gs->rng));
                a.r = 20;
                a.alive = true;
            }else{
                float v_now = a.v + float(gs->score) * 3.0f;  // px/s, blagi rast po skoru
                a.y += v_now * dt;

                if(a.y >= float(gs->H - 10)){
                    gs->over = true;
                    gs->win = false;
                }
            }
        }

        pthread_mutex_unlock(&gs->mtx);
    }

    return nullptr;
}

// ------------------------------------------------------------
// Main (iscrtavanje + logika metaka/kolizija + input)
// ------------------------------------------------------------

int main(int argc, char** argv){
    GameState gs;
    InputState in;

    for(int i=1;i<argc;i++){
        std::string a = argv[i];
        if(a=="--asteroids" && i+1<argc) gs.asteroidCount = std::max(2, std::atoi(argv[++i]));
        else if(a=="--target" && i+1<argc) gs.targetScore = std::max(1, std::atoi(argv[++i]));
        else if(a=="--width" && i+1<argc)  gs.W = std::max(400, std::atoi(argv[++i]));
        else if(a=="--height" && i+1<argc) gs.H = std::max(300, std::atoi(argv[++i]));
    }

    gs.shipX = float(gs.W / 2);
    gs.shipY = gs.H - 40;

    gs.randAstX = std::uniform_int_distribution<int>(40, std::max(40, gs.W - 40));
    gs.randAstSpawnY = std::uniform_int_distribution<int>(-gs.H, -20);

    // sporiji asteroidi (px/s)
    gs.randAstV = std::uniform_int_distribution<int>(90, 160);

    gs.ast = init_asteroids(gs);
    gs.bullets.reserve(64);

    // zvezde
    std::uniform_int_distribution<int> randStarX(0, std::max(0, gs.W - 1));
    std::uniform_int_distribution<int> randStarY(0, std::max(0, gs.H - 1));
    gs.stars.reserve(200);
    for(int i=0;i<200;i++) gs.stars.emplace_back(randStarX(gs.rng), randStarY(gs.rng));

    // prozor
    const char* WIN = "Space Defender (Parallel, dt)";
    cv::namedWindow(WIN, cv::WINDOW_NORMAL);
    cv::resizeWindow(WIN, gs.W, gs.H);
    cv::Mat frame(gs.H, gs.W, CV_8UC3);

    // niti
    pthread_t shipT;
    ShipArgs sargs{&gs, &in};
    pthread_create(&shipT, nullptr, ship_thread, &sargs);

    std::vector<pthread_t> astT(gs.asteroidCount);
    std::vector<AstArgs> aargs(gs.asteroidCount);

    for(int i=0;i<gs.asteroidCount;i++){
        aargs[i] = {&gs, i};
        pthread_create(&astT[i], nullptr, asteroid_thread, &aargs[i]);
    }

    auto tPrev = std::chrono::steady_clock::now();

    while(true){
        auto tNow = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;
        if(dt > 0.05f) dt = 0.05f;

        int k = cv::waitKey(1);
        if(k != -1) k = std::tolower(k & 0xFF);

        if(k == 27 || k == 'q'){
            pthread_mutex_lock(&gs.mtx);
            gs.running = false;
            pthread_mutex_unlock(&gs.mtx);
            break;
        }

        if(cv::getWindowProperty(WIN, cv::WND_PROP_VISIBLE) < 1){
            pthread_mutex_lock(&gs.mtx);
            gs.running = false;
            pthread_mutex_unlock(&gs.mtx);
            break;
        }

        if(k == 'a' || k == 2424832 || k == 81) in.move_impulse = -1;
        if(k == 'd' || k == 2555904 || k == 83) in.move_impulse = +1;
        if(k == 'w' || k == ' ')                in.fire_impulse = true;

        if(k == 'r'){
            pthread_mutex_lock(&gs.mtx);
            reset_round(gs);
            pthread_mutex_unlock(&gs.mtx);
        }

        // logika metaka + kolizije
        pthread_mutex_lock(&gs.mtx);

        if(!gs.over){
            for(auto& b : gs.bullets){
                if(!b.active) continue;
                b.y -= b.v * dt;
                if(b.y < 0) b.active = false;
            }

            gs.bullets.erase(
                std::remove_if(gs.bullets.begin(), gs.bullets.end(),
                               [](const Bullet& b){ return !b.active; }),
                gs.bullets.end()
            );

            for(auto& b : gs.bullets){
                if(!b.active) continue;
                for(auto& a : gs.ast){
                    if(a.alive && hit(b, a)){
                        b.active = false;
                        gs.score++;
                        a.alive = false;
                        break;
                    }
                }
            }

            if(gs.score >= gs.targetScore){
                gs.win = true;
                gs.over = true;
            }
        }

        // FPS (informativno)
        float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;

        // crtanje
        frame.setTo(cv::Scalar(10,10,20));
        for(const auto& p : gs.stars)
            cv::circle(frame, p, 1, cv::Scalar(120,120,140), cv::FILLED);

        for(const auto& a : gs.ast)
            if(a.alive)
                cv::circle(frame, {int(a.x), int(a.y)}, a.r,
                           cv::Scalar(70,170,240), cv::FILLED, cv::LINE_AA);

        for(const auto& b : gs.bullets)
            if(b.active)
                cv::circle(frame, {int(b.x), int(b.y)}, 5,
                           cv::Scalar(240,240,240), cv::FILLED, cv::LINE_AA);

        draw_ship(frame, int(gs.shipX), gs.shipY, gs.shipW);

        cv::putText(frame, "Score: " + std::to_string(gs.score), {20, 40},
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(200,200,200), 2, cv::LINE_AA);

        cv::putText(frame, cv::format("FPS: %.1f", fps), {20, 70},
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(200,200,200), 2, cv::LINE_AA);

        if(gs.over){
            std::string msg = gs.win ? "You won" : "Game over";
            cv::putText(frame, msg, {gs.W/2 - 150, gs.H/2},
                        cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(255,255,255), 3, cv::LINE_AA);
            cv::putText(frame, "Press R to restart", {gs.W/2 - 220, gs.H/2 + 60},
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(220,220,220), 2, cv::LINE_AA);
        }

        pthread_mutex_unlock(&gs.mtx);

        cv::imshow(WIN, frame);
    }

    pthread_join(shipT, nullptr);
    for(auto& t : astT) pthread_join(t, nullptr);

    cv::destroyAllWindows();
    return 0;
}

