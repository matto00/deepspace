#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

/**
 * Maps /Project to Shaders/, so M_SkyBody's Custom node can include
 * /Project/Private/WorldRelief.ush: the one noise file the C++ compiles too
 * (landing decision 1; ADR 0006, amended). Loaded at PostConfigInit
 * (DeepSpace.uproject), before any shader compiles. Nothing else lives here.
 *
 * UE 5.8 already makes this mapping itself: FEngineLoop::PreInit maps
 * /Project to <project>/Shaders whenever that directory exists
 * (LaunchEngineLoop.cpp), before any module loads, so today the guard below
 * always finds it and this module maps nothing. It stays only as the
 * mapping of record should an engine stop doing so; a mutation of the path
 * here survives for exactly that reason. DeepSpace.Surface.ShaderMapping
 * holds what matters -- that the include resolves to this project's file --
 * whoever mapped it. Whether to delete the module is the developer's call.
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
