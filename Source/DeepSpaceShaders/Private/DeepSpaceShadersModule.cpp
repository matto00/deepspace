#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

/**
 * Maps /Project to Shaders/, so M_SkyBody's Custom node can include
 * /Project/Private/WorldRelief.ush: the one noise file the C++ compiles too
 * (landing decision 1; ADR 0006, amended). Loaded at PostConfigInit
 * (DeepSpace.uproject), before any shader compiles. Nothing else lives here.
 */
class FDeepSpaceShadersModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // Once: the engine asserts on a second mapping of one virtual path,
        // and a module can be started twice in one process.
        if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/Project")))
        {
            AddShaderSourceDirectoryMapping(TEXT("/Project"),
                FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"))));
        }
    }
};

IMPLEMENT_MODULE(FDeepSpaceShadersModule, DeepSpaceShaders)
