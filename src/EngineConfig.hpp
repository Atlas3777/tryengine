#pragma once
#include <string_view>

struct EngineConfig {
    bool isEditorMode = true;
    bool isServer = false;

    static EngineConfig Parse(int argc, char* argv[]) {
        EngineConfig config;
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--game") config.isEditorMode = false;
            if (arg == "--server") config.isServer = true;
        }
        return config;
    }
};
