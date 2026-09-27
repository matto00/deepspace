using UnrealBuildTool;

// The shader-path module (landing spec decision 1): maps /Project to this
// project's Shaders/ at PostConfigInit, before any shader compiles, so a
// material can include /Project/Private/WorldRelief.ush -- the file the
// DeepSpace module compiles into C++ as well. A module of its own because
// DeepSpace loads long after shaders start compiling.
public class DeepSpaceShaders : ModuleRules
{
	public DeepSpaceShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore" });
	}
}
