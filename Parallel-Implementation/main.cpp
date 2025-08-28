// Space Defender — Paralelna (OpenCV + pthreads), po PDF-u
// Avion (brod) se sada kreće VEOMA brzo (step = 35) i dodatno ubrzava sa score-om (+score/8).
// Kontrole: A/D ili ←/→ (kretanje), W/SPACE (pucaj), R (restart), ESC/Q (izlaz).
// Win/Lose: prozor ostaje otvoren; možeš više puta da restartuješ sa R.

#include <opencv2/opencv.hpp>
#include <vector>
#include <random>
#include <chrono>
#include <string>
#include <algorithm>
#include <atomic>
#include <pthread.h>
#include <thread>

struct Bullet { int x{0}, y{0}; int v{12}; bool active{false}; };
struct Asteroid { int x{0}, y{0}, r{20}; float v{2.5f}; bool alive{true}; };
struct Ship { int x{0}, y{0}, w{40}; int step{35}; }; // 🚀 još brži brod (35 px po impulsu)

struct InputState {
    std::atomic<int> move_impulse{0};   // -1 = levo, +1 = desno, 0 = ništa
    std::atomic<bool> fire_impulse{false};
};

struct GameState {
    int W=1024, H=768;
    int targetScore=30;
    int asteroidCount=5;

    std::atomic<bool> running{true};     // ceo program
    std::atomic<bool> game_active{true}; // runda aktivna
    bool win=false, over=false;
    int score=0;

    Ship ship;
    std::vector<Asteroid> ast;
    std::vector<Bullet> bullets;

    std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> randAstX{40, 1000};
    std::uniform_int_distribution<int> randAstY{-768, -20};
    std::uniform_int_distribution<int> randAstV{2,4};

    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
};

static inline int sqr(int x){ return x*x; }
static bool hit(const Bullet& b, const Asteroid& a){
    return sqr(b.x-a.x)+sqr(b.y-a.y) <= sqr(a.r+6);
}

static void draw_ship(cv::Mat& img,int cx,int cy,int w){
    cv::Point pts[1][3];
    pts[0][0]={cx,cy-26}; pts[0][1]={cx-w/2,cy+22}; pts[0][2]={cx+w/2,cy+22};
    const cv::Point* ppt[1]={pts[0]}; int npt[]={3};
    cv::fillPoly(img,ppt,npt,1,cv::Scalar(255,255,255),cv::LINE_AA);
    cv::circle(img,{cx,cy+8},5,cv::Scalar(0,0,0),cv::FILLED,cv::LINE_AA);
}

// ---------------- ASTEROID THREAD ----------------
struct AstArg { GameState* gs; int idx; };
void* asteroid_worker(void* arg){
    std::unique_ptr<AstArg> A((AstArg*)arg);
    GameState* gs=A->gs; int idx=A->idx;

    while(gs->running){
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        pthread_mutex_lock(&gs->mtx);
        if(!gs->running){ pthread_mutex_unlock(&gs->mtx); break; }

        Asteroid& a = gs->ast[idx];
        if(gs->game_active){
            if(a.alive){
                a.y += int(a.v + gs->score*0.03f);
                if(a.y >= gs->H-10){
                    gs->over=true; gs->win=false; gs->game_active=false;
                }
            }else{
                a.x=gs->randAstX(gs->rng);
                a.y=gs->randAstY(gs->rng);
                a.v=float(gs->randAstV(gs->rng));
                a.alive=true;
            }
        }
        pthread_mutex_unlock(&gs->mtx);
    }
    return nullptr;
}

// ---------------- SHIP THREAD ----------------
struct ShipArg { GameState* gs; InputState* in; };
void* ship_worker(void* arg){
    std::unique_ptr<ShipArg> S((ShipArg*)arg);
    GameState* gs=S->gs; InputState* in=S->in;

    while(gs->running){
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        pthread_mutex_lock(&gs->mtx);
        if(!gs->running){ pthread_mutex_unlock(&gs->mtx); break; }

        if(gs->game_active){
            int dir = in->move_impulse.exchange(0);
            if(dir != 0){
                // 🚀 brži avion: osnovni step + pojačanje sa score-om
                int step_now = gs->ship.step + gs->score/8;
                gs->ship.x += dir * step_now;
                gs->ship.x = std::clamp(gs->ship.x, gs->ship.w, gs->W - gs->ship.w);
            }
            if(in->fire_impulse.exchange(false)){
                Bullet b; b.active=true; b.x=gs->ship.x; b.y=gs->ship.y-24;
                gs->bullets.push_back(b);
            }
        }
        pthread_mutex_unlock(&gs->mtx);
    }
    return nullptr;
}

// ---------------- RESET ----------------
static void reset_round(GameState& gs){
    gs.score=0; gs.win=false; gs.over=false; gs.game_active=true;
    gs.ship.x = gs.W/2; gs.ship.y = gs.H-40; gs.ship.w=40; gs.ship.step=35; // još brži start
    gs.bullets.clear(); gs.bullets.reserve(64);
    for(auto& a:gs.ast){
        a.x=gs.randAstX(gs.rng);
        a.y=gs.randAstY(gs.rng);
        a.v=gs.randAstV(gs.rng);
        a.r=20; a.alive=true;
    }
}

// ---------------- MAIN ----------------
int main(int argc,char**argv){
    GameState gs; InputState in;

    for(int i=1;i<argc;i++){
        std::string a=argv[i];
        if(a=="--asteroids"&&i+1<argc) gs.asteroidCount=std::max(2,atoi(argv[++i]));
        else if(a=="--target"&&i+1<argc) gs.targetScore=std::max(1,atoi(argv[++i]));
        else if(a=="--width"&&i+1<argc) gs.W=std::max(400,atoi(argv[++i]));
        else if(a=="--height"&&i+1<argc) gs.H=std::max(300,atoi(argv[++i]));
    }

    gs.randAstX=std::uniform_int_distribution<int>(40, gs.W-40);
    gs.randAstY=std::uniform_int_distribution<int>(-gs.H, -20);

    gs.ship.x=gs.W/2; gs.ship.y=gs.H-40; gs.ship.w=40; gs.ship.step=35;
    gs.ast.resize(gs.asteroidCount);
    for(auto& a:gs.ast){ a.x=gs.randAstX(gs.rng); a.y=gs.randAstY(gs.rng); a.v=gs.randAstV(gs.rng); a.alive=true; }
    gs.bullets.reserve(64);

    const char* WIN = "Space Defender (Parallel)";
    cv::namedWindow(WIN, cv::WINDOW_NORMAL);
    cv::resizeWindow(WIN, gs.W, gs.H);
    cv::Mat frame(gs.H,gs.W,CV_8UC3);

    std::vector<cv::Point> stars;
    std::uniform_int_distribution<int> randStarX(0,gs.W-1), randStarY(0,gs.H-1);
    for(int i=0;i<220;i++) stars.emplace_back(randStarX(gs.rng), randStarY(gs.rng));

    auto tPrev=std::chrono::steady_clock::now();

    // niti
    std::vector<pthread_t> ast_threads(gs.asteroidCount);
    for(int i=0;i<gs.asteroidCount;i++){
        pthread_create(&ast_threads[i],nullptr,asteroid_worker,new AstArg{&gs,i});
    }
    pthread_t ship_thread;
    pthread_create(&ship_thread,nullptr,ship_worker,new ShipArg{&gs,&in});

    // glavna petlja
    while(gs.running){
        int k=cv::waitKey(1);
        if(k!=-1) k=tolower(k&0xff);

        if(k==27||k=='q') gs.running=false;
        if(cv::getWindowProperty(WIN,cv::WND_PROP_VISIBLE)<1) gs.running=false;

        if(k=='a'||k==2424832) in.move_impulse = -1;
        if(k=='d'||k==2555904) in.move_impulse = +1;
        if(k=='w'||k==' ')    in.fire_impulse = true;

        if(k=='r'){
            pthread_mutex_lock(&gs.mtx);
            reset_round(gs);
            pthread_mutex_unlock(&gs.mtx);
        }

        // logika metaka + kolizije
        pthread_mutex_lock(&gs.mtx);
        if(gs.game_active){
            for(auto& b:gs.bullets){
                if(!b.active) continue;
                b.y -= b.v;
                if(b.y<0) b.active=false;
            }
            for(auto& b:gs.bullets){
                if(!b.active) continue;
                for(auto& a:gs.ast){
                    if(a.alive && hit(b,a)){
                        b.active=false;
                        gs.score++;
                        a.alive=false;
                        break;
                    }
                }
            }
            if(gs.score >= gs.targetScore){
                gs.win=true; gs.over=true; gs.game_active=false;
            }
        }
        pthread_mutex_unlock(&gs.mtx);

        auto tNow=std::chrono::steady_clock::now();
        double dt=std::chrono::duration<double,std::milli>(tNow-tPrev).count();
        tPrev=tNow; double fps=dt>0?1000.0/dt:0.0;

        frame.setTo(cv::Scalar(10,10,20));
        for(const auto& s:stars) cv::circle(frame,s,1,{120,120,140},cv::FILLED);

        pthread_mutex_lock(&gs.mtx);
        for(const auto& a:gs.ast)
            if(a.alive) cv::circle(frame,{a.x,a.y},a.r,{70,170,240},cv::FILLED,cv::LINE_AA);
        for(const auto& b:gs.bullets)
            if(b.active) cv::circle(frame,{b.x,b.y},5,{240,240,240},cv::FILLED,cv::LINE_AA);
        draw_ship(frame,gs.ship.x,gs.ship.y,gs.ship.w);

        cv::putText(frame,"Score: "+std::to_string(gs.score),{20,40},cv::FONT_HERSHEY_SIMPLEX,1.0,{200,200,200},2);
        cv::putText(frame,cv::format("FPS: %.1f",fps),{20,70},cv::FONT_HERSHEY_SIMPLEX,1.0,{200,200,200},2);

        if(gs.over){
            std::string msg = gs.win ? "You won" : "Game over";
            cv::putText(frame,msg,{gs.W/2-150,gs.H/2},cv::FONT_HERSHEY_SIMPLEX,2.0,{255,255,255},3);
            cv::putText(frame,"Press R to restart",{gs.W/2-220,gs.H/2+60},cv::FONT_HERSHEY_SIMPLEX,1.0,{220,220,220},2);
        }
        pthread_mutex_unlock(&gs.mtx);

        cv::imshow(WIN,frame);
    }

    pthread_join(ship_thread,nullptr);
    for(auto& t:ast_threads) pthread_join(t,nullptr);

    cv::destroyAllWindows();
    return 0;
}

