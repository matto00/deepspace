// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DeepSpace : ModuleRules
{
	public DeepSpace(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// This module uses a flat layout (Ship/, Player/, Core/) rather than the
		// Public/Private convention UBT auto-adds include paths for, so the
		// module root must be declared explicitly. Without this, headers in
		// subdirectories cannot be included as "Ship/Foo.h".
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG" });

		// Json: the movement-contract test reads Tools/movement_contract.json.
		// Slate/SlateCore: the ship's screens are real Slate built in C++ -- the
		// widget trees live in Source/DeepSpace/UI, not in .uasset files, so that
		// screen logic stays diffable and reviewable (ADR 0002).
		// RHI: DeepSpace.Sky.MaterialContract runs the material translator on
		// the sky's generated graphs, which needs the shader platform to
		// translate for; a commandlet compiles no shaders, so nothing else
		// would notice a broken graph.
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "Slate", "SlateCore", "RHI" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
