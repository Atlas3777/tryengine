#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

using namespace das;

class Module_Renderer : public das::Module {
public:
    Module_Renderer() : Module("tryRenderer") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();
    }
};

REGISTER_DYN_MODULE(Module_Renderer, Module_Renderer);
REGISTER_MODULE(Module_Renderer);
