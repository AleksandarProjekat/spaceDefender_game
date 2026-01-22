// Space Defender — Serijska verzija (dt-based)
// Brzina objekata je vezana za realno vreme (sekunde), a ne za FPS.
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

struct Bullet {
    float x{0}, y{0};
    float v{720.0f};     // px/s (npr. 12px po frame-u na ~60fps ≈ 720 px/s)
    bool active{false};
};

struct Asteroid {
    float x{0}, y{0};
    int r{20};
    float v{140.0f};     // px/s (osnovna brzina)
    bool alive{true};
};

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

int main(int argc, char** argv){
    int W = 1024, H = 768;
    int targetScore = 30;
    int asteroidCount = 5;

    for(int i=1;i<argc;i++){
        std::string a = argv[i];
        if(a=="--asteroids" && i+1<argc) asteroidCount = std::max(2, std::atoi(argv[++i]));
        else if(a=="--target" && i+1<argc) targetScore = std::max(1, std::atoi(argv[++i]));
        else if(a=="--width" && i+1<argc)  W = std::max(400, std::atoi(argv[++i]));
        else if(a=="--height" && i+1<argc) H = std::max(300, std::atoi(argv[++i]));
    }

    // brod
    int shipW = 40;
    float shipX = float(W/2);
    int shipY = H-40;
    int shipStep = 35;

    // RNG
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> randAstX(40, std::max(40, W-40));
    std::uniform_int_distribution<int> randAstSpawnY(-H, -20);
    std::uniform_int_distribution<int> randStarX(0, std::max(0, W-1));
    std::uniform_int_distribution<int> randStarY(0, std::max(0, H-1));

    // sporiji asteroidi (px/s)
    std::uniform_int_distribution<int> randAstV(90, 160); // px/s

    auto init_asteroids = [&](){
        std::vector<Asteroid> a(asteroidCount);
        for(auto& it : a){
            it.x = float(randAstX(rng));
            it.y = float(randAstSpawnY(rng));
            it.r = 20;
            it.v = float(randAstV(rng));   // px/s
            it.alive = true;
        }
        return a;
    };

    std::vector<Asteroid> ast = init_asteroids();
    std::vector<Bullet> bullets; bullets.reserve(64);

    // zvezde
    std::vector<cv::Point> stars;
    stars.reserve(200);
    for(int i=0;i<200;i++) stars.emplace_back(randStarX(rng), randStarY(rng));

    const char* WIN = "Space Defender (Sequential, dt)";
    cv::namedWindow(WIN, cv::WINDOW_NORMAL);
    cv::resizeWindow(WIN, W, H);
    cv::Mat frame(H, W, CV_8UC3);

    int score = 0;
    bool win = false, over = false;

    // dt (sekunde)
    auto tPrev = std::chrono::steady_clock::now();
    bool leftHeld = false, rightHeld = false;

    while(true){
        auto tNow = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;

        // clamp dt (ako se prozor zamrzne, alt-tab, breakpoint...)
        if(dt > 0.05f) dt = 0.05f;

        int k = cv::waitKey(1);
        if(k != -1) k = std::tolower(k & 0xFF);

        if(k == 27 || k == 'q') break;

        if(k == 'a' || k == 2424832 || k == 81) leftHeld = true;
        if(k == 'd' || k == 2555904 || k == 83) rightHeld = true;

        if(k == 'w' || k == ' '){
            Bullet b;
            b.active = true;
            b.x = shipX;
            b.y = float(shipY - 24);
            bullets.push_back(b);
        }

        if(k == 'r'){
            ast = init_asteroids();
            bullets.clear(); bullets.reserve(64);
            score = 0; win = false; over = false;
            shipX = float(W/2);
            continue;
        }

        if(cv::getWindowProperty(WIN, cv::WND_PROP_VISIBLE) < 1) break;

        if(!over){
            int step_now = shipStep + score/8;
            if(leftHeld && !rightHeld) shipX -= float(step_now);
            else if(rightHeld && !leftHeld) shipX += float(step_now);

            shipX = std::clamp(shipX, float(shipW), float(W - shipW));

            // metci (dt-based)
            for(auto& b : bullets){
                if(!b.active) continue;
                b.y -= b.v * dt;
                if(b.y < 0) b.active = false;
            }

            bullets.erase(
                std::remove_if(bullets.begin(), bullets.end(),
                               [](const Bullet& b){ return !b.active; }),
                bullets.end()
            );

            // kolizije + respawn (x,y,v)
            for(auto& b : bullets){
                if(!b.active) continue;
                for(auto& a : ast){
                    if(a.alive && hit(b, a)){
                        b.active = false;
                        score++;

                        a.x = float(randAstX(rng));
                        a.y = float(randAstSpawnY(rng));
                        a.v = float(randAstV(rng));
                        break;
                    }
                }
            }

            // asteroidi (dt-based)
            for(auto& a : ast){
                if(!a.alive) continue;

                // blagi rast brzine sa score-om (u px/s)
                float v_now = a.v + float(score) * 3.0f;  // 3 px/s po poenu
                a.y += v_now * dt;

                if(a.y >= float(H - 10))
                    over = true;
            }

            if(score >= targetScore){
                win = true;
                over = true;
            }
        }

        // FPS samo informativno
        float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;

        frame.setTo(cv::Scalar(10,10,20));
        for(const auto& p : stars)
            cv::circle(frame, p, 1, cv::Scalar(120,120,140), cv::FILLED);

        for(const auto& a : ast)
            cv::circle(frame, {int(a.x), int(a.y)}, a.r,
                       cv::Scalar(70,170,240), cv::FILLED, cv::LINE_AA);

        for(const auto& b : bullets)
            if(b.active)
                cv::circle(frame, {int(b.x), int(b.y)}, 5,
                           cv::Scalar(240,240,240), cv::FILLED, cv::LINE_AA);

        draw_ship(frame, int(shipX), shipY, shipW);

        cv::putText(frame, "Score: " + std::to_string(score), {20, 40},
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(200,200,200), 2, cv::LINE_AA);

        cv::putText(frame, cv::format("FPS: %.1f", fps), {20, 70},
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(200,200,200), 2, cv::LINE_AA);

        if(over){
            std::string msg = win ? "You won" : "Game over";
            cv::putText(frame, msg, {W/2-150, H/2},
                        cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(255,255,255), 3, cv::LINE_AA);
            cv::putText(frame, "Press R to restart", {W/2-220, H/2+60},
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(220,220,220), 2, cv::LINE_AA);
        }

        cv::imshow(WIN, frame);

        leftHeld = rightHeld = false;
    }

    cv::destroyAllWindows();
    return 0;
}

