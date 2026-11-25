#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/graphics/UiSystem.hpp"

using tryengine::graphics::UiSystem;

namespace {

//! Script-visible UI. UiSystem is a process singleton by design (one engine per process, and
//! both editor contexts share it), so the bindings do not need a daslang Context at all. A
//! headless daslang CLI has no UiSystem and gets zeroed answers instead of a crash.
UiSystem* Ui() {
    return UiSystem::ForScripts();
}

}  // namespace

// A frame-local string table: the script interns text when it emits a text command, and the
// UI pass resolves the ids while expanding the draw list. Cleared at Ui::Begin.
uint32_t UiTextId(const char* text) {
    auto* ui = Ui();
    return ui != nullptr ? ui->InternText(text) : 0u;
}

void UiFrameBegin() {
    if (auto* ui = Ui())
        ui->ResetFrameStrings();
}

// Metrics are read-only: the UI script measures text to lay widgets out; the pass rebuilds
// the same glyph quads from the same font, so both sides agree by construction.
das::float2 UiTextMeasure(const char* text, float px, uint64_t font_id) {
    if (auto* ui = Ui())
        return ui->Measure(text, px, font_id);
    return das::float2(0.0f, 0.0f);
}

float UiTextLineHeight(float px, uint64_t font_id) {
    if (auto* ui = Ui())
        return ui->LineHeight(font_id, px);
    return 0.0f;
}

das::float2 UiWindowSize() {
    if (auto* ui = Ui())
        return ui->WindowSize();
    return das::float2(1.0f, 1.0f);
}

bool UiIsReady() {
    auto* ui = Ui();
    return ui != nullptr && ui->IsReady();
}

uint32_t UiAtlasVersion() {
    if (auto* ui = Ui())
        return ui->AtlasVersion();
    return 0u;
}

//! Authored path of the base UI material. The game adds it to the boot request explicitly
//! (`AddAsset("material", UiMaterialPath())`), so the loader, not the UI, reads it from disk.
const char* UiMaterialPath() {
    return tryengine::graphics::kUiMaterialPath;
}

class Module_Ui : public das::Module {
public:
    Module_Ui() : das::Module("tryUi") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();

        // Every binding is accessGlobal: the compiler must never fold a measurement or a frame
        // state into a constant, or a headless compile would bake zeroes into the program.
        das::addExtern<DAS_BIND_FUN(UiTextId)>(*this, lib, "UiTextId", das::SideEffects::accessGlobal, "UiTextId");
        das::addExtern<DAS_BIND_FUN(UiFrameBegin)>(*this, lib, "UiFrameBegin", das::SideEffects::accessGlobal,
                                                   "UiFrameBegin");
        das::addExtern<DAS_BIND_FUN(UiTextMeasure)>(*this, lib, "UiTextMeasure", das::SideEffects::accessGlobal,
                                                    "UiTextMeasure");
        das::addExtern<DAS_BIND_FUN(UiTextLineHeight)>(*this, lib, "UiTextLineHeight", das::SideEffects::accessGlobal,
                                                       "UiTextLineHeight");
        das::addExtern<DAS_BIND_FUN(UiWindowSize)>(*this, lib, "UiWindowSize", das::SideEffects::accessGlobal,
                                                   "UiWindowSize");
        das::addExtern<DAS_BIND_FUN(UiIsReady)>(*this, lib, "UiIsReady", das::SideEffects::accessGlobal, "UiIsReady");
        das::addExtern<DAS_BIND_FUN(UiAtlasVersion)>(*this, lib, "UiAtlasVersion", das::SideEffects::accessGlobal,
                                                     "UiAtlasVersion");
        das::addExtern<DAS_BIND_FUN(UiMaterialPath)>(*this, lib, "UiMaterialPath", das::SideEffects::none,
                                                     "UiMaterialPath");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Ui, Module_Ui);
REGISTER_MODULE(Module_Ui);
