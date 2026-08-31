#include "GameApp.hpp"

int main() {
    trygame::GameApp game_app;
    game_app.Init();
    game_app.Run();
    game_app.Shutdown();
}
