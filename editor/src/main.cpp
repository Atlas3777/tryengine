#include "editor/EditorApp.hpp"

int main() {
    tryeditor::EditorApp editor_app;
    editor_app.Init();
    editor_app.Run();
    editor_app.Shutdown();
}