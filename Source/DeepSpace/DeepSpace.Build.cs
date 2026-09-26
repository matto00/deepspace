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

		// No unity build. Unity concatenates many .cpp files into one
		// translation unit, which merges their anonymous namespaces: two test
		// files that each keep a private "Frame" or "Root" collide, and under
		// -Wshadow that is an error. Worse, UBT's adaptive unity compiles
		// git-modified files on their own, so a working tree builds green and
		// the same code committed does not -- and this project is built by
		// agents in parallel worktrees whose work only meets at the merge.
		// Slice 1 hit exactly that. Each file as its own unit also catches the
		// missing #include that a unity blob silently supplies from a neighbour.
		bUseUnity = false;

		// AudioMixer: the ship's hum is a USynthComponent synthesising in C++,
		// with no sound assets and no MetaSound graph (lived-in decision 9). It
		// is public because UShipHumComponent's header derives from it.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "AudioMixer" });

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
