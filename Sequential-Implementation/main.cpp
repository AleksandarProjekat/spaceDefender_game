// Space Defender — Serijska (poboljsana, brzi avion)
// Kontrole: A/D ili strelice (levo/desno), W ili SPACE (pucaj), R (restart), ESC/Q (izlaz).
// Flagovi: --asteroids N  --target N  --width W  --height H
// Prozor ostaje otvoren dok ne pritisnes ESC/Q ili ga ne zatvoris.

#include <opencv2/opencv.hpp>
#include <vector>
#include <random>
#include <chrono>
#include <string>
#include <algorithm>

struct Bullet { int x{0}, y{0}; int v{12}; bool active{false}; };
struct Asteroid { int x{0}, y{0}, r{20}; float v{2.0f}; bool alive{true}; };

static inline int sqr(int x){ return x*x; }
static bool hit(const Bullet& b, const Asteroid& a){
    return sqr(b.x - a.x) + sqr(b.y - a.y) <= sqr(a.r + 6);
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
    // Podesivi parametri
    int W = 1024, H = 768;     // default velicina prozora
    int targetScore = 30;      // cilj za pobedu
    int asteroidCount = 5;     // broj asteroida (min 2)

    // Flagovi
    for(int i=1;i<argc;i++){
        std::string a = argv[i];
        if(a=="--asteroids" && i+1<argc) asteroidCount = std::max(2, std::atoi(argv[++i]));
        else if(a=="--target" && i+1<argc) targetScore = std::max(1, std::atoi(argv[++i]));
        else if(a=="--width" && i+1<argc)  W = std::max(400, std::atoi(argv[++i]));
        else if(a=="--height" && i+1<argc) H = std::max(300, std::atoi(argv[++i]));
    }

    // Stanje igraca
    int shipW = 40;
    int shipX = W/2;
    int shipY = H-40;
    int shipStep = 35; // 🚀 brzi avion (pre je bilo 12)

    // RNG i distribucije
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> randAstX(40, std::max(40, W-40));
    std::uniform_int_distribution<int> randAstSpawnY(-H, -20);
    std::uniform_int_distribution<int> randStarX(0, std::max(0, W-1));
    std::uniform_int_distribution<int> randStarY(0, std::max(0, H-1));
    std::uniform_int_distribution<int> randAstV(2, 4);

    // Inicijalizacija asteroida
    auto init_asteroids = [&](){
        std::vector<Asteroid> a(asteroidCount);
        for(auto& it : a){
            it.x = randAstX(rng);
            it.y = randAstSpawnY(rng); // start iznad ekrana
            it.r = 20;
            it.v = float(randAstV(rng));
            it.alive = true;
        }
        return a;
    };

    std::vector<Asteroid> ast = init_asteroids();
    std::vector<Bullet> bullets; bullets.reserve(64);

    // Zvezde (ispravno kao int, bez narrowing warninga)
    std::vector<cv::Point> stars;
    stars.reserve(200);
    for(int i=0;i<200;i++) stars.emplace_back(randStarX(rng), randStarY(rng));

    // UI
    const char* WIN = "Space Defender (Sequential)";
    cv::namedWindow(WIN, cv::WINDOW_NORMAL);
    cv::resizeWindow(WIN, W, H);
    cv::Mat frame(H, W, CV_8UC3);

    // Stanje igre
    int score = 0; bool win = false, over = false;

    // FPS
    auto tPrev = std::chrono::steady_clock::now();
    bool leftHeld = false, rightHeld = false;

    while(true){
        // Tastatura
        int k = cv::waitKey(1);
        if(k != -1) k = std::tolower(k & 0xFF);

        if(k == 27 || k == 'q') break;                 // ESC/Q → izlaz
        if(k == 'a' || k == 2424832) leftHeld = true;  // ←
        if(k == 'd' || k == 2555904) rightHeld = true; // →
        if(k == 'w' || k == ' '){                      // pucaj
            Bullet b; b.active = true; b.x = shipX; b.y = shipY - 24;
            bullets.push_back(b);
        }
        if(k == 'r'){ // restart
            ast = init_asteroids();
            bullets.clear(); bullets.reserve(64);
            score = 0; win = false; over = false;
            shipX = W/2; shipY = H-40;
            continue;
        }
        if(cv::getWindowProperty(WIN, cv::WND_PROP_VISIBLE) < 1) break;

        // Logika
        if(!over){
            // kretanje broda — brzo + blago ubrzanje sa score-om (kao u paralelnoj)
            int step_now = shipStep + score/8; // 🚀 brže i brže
            if(leftHeld && !rightHeld) shipX -= step_now;
            else if(rightHeld && !leftHeld) shipX += step_now;
            shipX = std::clamp(shipX, shipW, W - shipW);

            // metci
            for(auto& b : bullets){
                if(!b.active) continue;
                b.y -= b.v;
                if(b.y < 0) b.active = false;
            }

            // kolizije metak–asteroid + respawn
            for(auto& b : bullets){
                if(!b.active) continue;
                for(auto& a : ast){
                    if(a.alive && hit(b, a)){
                        b.active = false;
                        score++;
                        a.x = randAstX(rng);
                        a.y = randAstSpawnY(rng);
                        a.v = float(randAstV(rng));
                        break;
                    }
                }
            }

            // kretanje asteroida (blagi rast brzine sa skorom)
            for(auto& a : ast){
                if(!a.alive) continue;
                float v = a.v + score * 0.03f;
                a.y += int(v);
                if(a.y >= H - 10) over = true; // dosao do dna → game over
            }

            if(score >= targetScore){ win = true; over = true; }
        }

        // FPS racunanje
        auto tNow = std::chrono::steady_clock::now();
        double dtMs = std::chrono::duration<double, std::milli>(tNow - tPrev).count();
        tPrev = tNow;
        double fps = dtMs > 0.0 ? 1000.0 / dtMs : 0.0;

        // Crtanje
        frame.setTo(cv::Scalar(10,10,20));
        for(const auto& p : stars) cv::circle(frame, p, 1, cv::Scalar(120,120,140), cv::FILLED);

        for(const auto& a : ast)
            if(a.alive)
                cv::circle(frame, {a.x, a.y}, a.r, cv::Scalar(70,170,240), cv::FILLED, cv::LINE_AA);

        for(const auto& b : bullets)
            if(b.active)
                cv::circle(frame, {b.x, b.y}, 5, cv::Scalar(240,240,240), cv::FILLED, cv::LINE_AA);

        draw_ship(frame, shipX, shipY, shipW);

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

        // edge-trigger reset (zadrzavamo fluidnost, bez gomilanja inputa)
        leftHeld = rightHeld = false;
    }

    cv::destroyAllWindows();
    return 0;
}

