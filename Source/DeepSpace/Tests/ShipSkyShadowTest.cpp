#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Surface/SunShadowMap.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowParametersTest, "DeepSpace.Sky.ShadowParameters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The sky bakes each solid world's cast-shadow map and hands it to the
 * world's material: a 16-bit texture, a mip per level, never LOD-biased, its
 * CPU copy discarded once uploaded, its frame the sky's own light
 * (SkyProjection::SunLightOf) and ds.Sky.Shadows clamped to 0..1 as the
 * strength; the ground's instance gets all of it through CopyBodyLook.
 * Worlds with no ground get no map. Each map is keyed by what it is made
 * from, so a jump within the system (a new jump serial, the same system)
 * re-bakes nothing, a reload that moves one world's relief re-bakes that
 * world alone, a new system drops the old maps, and nothing waits for a
 * bake it lets go. A map that lands fades in over ShadowFadeSeconds. At 256
 * columns, so the test bakes in a moment.
 */
bool FShadowParametersTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FScopedCVar Narrow(TEXT("ds.Sky.ShadowMapWidth"), 256.0f);
    FSkyWorld Test(TEXT("ShadowParametersWorld"), 8, EShadows::Maps);
    Test.Sky->bKeepShadowMapsForTest = true;
    Test.BeginPlay();
    Test.Step(1.0f / 60.0f);
    TestTrue(TEXT("the bakes start when the system loads"), Test.Sky->GetShadowBakesPending() > 0);
    Test.Sky->FlushShadowBakesForTest();
    Test.Step(1.0f / 60.0f);
    TestEqual(TEXT("and all land"), Test.Sky->GetShadowBakesPending(), 0);

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const int32 Serial = LocalSystem::Serial(Test.World);
    const TArray<int32> Order = ShipSky::ShadowBakeOrder(Here, Test.Ship->GetFlightState().GetUniversePosition());
    int32 Solid = 0;
    for (int32 Index = 0; Index < Here.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = Here.Bodies[Index];
        const bool bSolid = Body.Ground == EGround::Solid && Body.Kind != ESkyBodyKind::Star;
        Solid += bSolid ? 1 : 0;
        TestEqual(FString::Printf(TEXT("%s is baked exactly when it has ground"), *Body.Id.ToString()), Order.Contains(Index), bSolid);
        UTexture2D* Texture = Test.Sky->GetShadowTexture(Body.Id);
        if (!bSolid)
        {
            TestNull(FString::Printf(TEXT("%s has no map"), *Body.Id.ToString()), Texture);
            continue;
        }
        const FSunShadowMap* Map = Test.Sky->GetShadowMapForTest(Body.Id);
        if (!TestNotNull(FString::Printf(TEXT("%s has its map"), *Body.Id.ToString()), Texture) || !TestNotNull(TEXT("and kept it"), Map))
        {
            continue;
        }
        TestTrue(FString::Printf(TEXT("%s: the texture is the map, 256 x %d, G16, a mip per level"), *Body.Id.ToString(), Map->Rows),
            Texture->GetSizeX() == 256 && Texture->GetSizeY() == Map->Rows && Texture->GetPixelFormat() == PF_G16
            && Texture->GetNumMips() == Map->LevelCount());
        // A device profile's LOD bias would drop mip 0 while the lookup's U
        // is still in level-0 texels: every read in the wrong place.
        TestEqual(TEXT("in an LOD group no profile biases"), static_cast<int32>(Texture->LODGroup), static_cast<int32>(TEXTUREGROUP_Pixels2D));
        TestEqual(TEXT("with no copy left on the CPU once uploaded"), ShipSky::ShadowTextureCpuBytes(*Texture), static_cast<int64>(0));
        TestTrue(TEXT("baked under the sky's own light"), Map->FrameZ.Equals(SkyProjection::SunLightOf(Here, Index).Direction.GetSafeNormal(), 1e-12));
        UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Test.Sky->GetProxy(Index)->GetMaterial(0));
        if (!TestNotNull(TEXT("the proxy draws a dynamic instance"), Instance))
        {
            continue;
        }
        TestTrue(TEXT("which reads that texture"), Instance->K2_GetTextureParameterValue(SkyMaterial::ShadowMap) == Texture);
        TestTrue(TEXT("in the map's frame, PsiLo and Step in w"),
            Instance->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameX) == ShipSky::ShadowFrameX(*Map)
            && Instance->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameZ) == ShipSky::ShadowFrameZ(*Map));
    }
    TestTrue(TEXT("the start system has solid worlds"), Solid > 0);
    for (int32 Rank = 1; Rank < Order.Num(); ++Rank)
    {
        const FUniversePosition Ship = Test.Ship->GetFlightState().GetUniversePosition();
        const FSkyBody& Nearer = Here.Bodies[Order[Rank - 1]];
        const FSkyBody& Farther = Here.Bodies[Order[Rank]];
        TestTrue(TEXT("the bakes run nearest first"),
            Ship.DistanceTo(Nearer.Position) - Nearer.Radius <= Ship.DistanceTo(Farther.Position) - Farther.Radius);
    }

    // The strength: ds.Sky.Shadows, clamped (a flush lands its maps faded in).
    UMaterialInstanceDynamic* Fourth = Cast<UMaterialInstanceDynamic>(Test.Sky->GetProxy(4)->GetMaterial(0));
    for (const TPair<float, float>& Case : { TPair<float, float>(7.0f, 1.0f), TPair<float, float>(-1.0f, 0.0f), TPair<float, float>(0.5f, 0.5f) })
    {
        FScopedCVar Strength(TEXT("ds.Sky.Shadows"), Case.Key);
        Test.Step(1.0f / 60.0f);
        TestEqual(FString::Printf(TEXT("ds.Sky.Shadows %.1f draws %.1f"), Case.Key, Case.Value), ShipSky::ShadowStrength(), Case.Value);
        TestEqual(TEXT("and the world's material has it"), Fourth->K2_GetScalarParameterValue(SkyMaterial::Shadows), Case.Value);
    }

    // The ground gets all of it through the look's one copy.
    UMaterial* GroundMaterial = LoadObject<UMaterial>(nullptr, SkyMaterial::GroundPath);
    if (TestNotNull(TEXT("M_SkyGround is built"), GroundMaterial))
    {
        UMaterialInstanceDynamic* Ground = UMaterialInstanceDynamic::Create(GroundMaterial, Test.World);
        ShipSky::CopyBodyLook(*Fourth, *Ground);
        TestTrue(TEXT("the ground reads the world's map"),
            Ground->K2_GetTextureParameterValue(SkyMaterial::ShadowMap) == Fourth->K2_GetTextureParameterValue(SkyMaterial::ShadowMap));
        TestTrue(TEXT("in its frame"), Ground->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameX) == Fourth->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameX)
            && Ground->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameZ) == Fourth->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameZ));
        TestEqual(TEXT("at its strength"), Ground->K2_GetScalarParameterValue(SkyMaterial::Shadows), Fourth->K2_GetScalarParameterValue(SkyMaterial::Shadows));
        TestEqual(TEXT("with its map's fade"), Ground->K2_GetScalarParameterValue(SkyMaterial::ShadowMapFade), Fourth->K2_GetScalarParameterValue(SkyMaterial::ShadowMapFade));
    }

    // A jump within the system: ShipNavState bumps the jump serial on an
    // in-system arrival, and the sky rebuilds its proxies for it. Neither of a
    // map's inputs moved, so nothing is baked again and every texture stays.
    const FName Nearest = Here.Bodies[Order[0]].Id;
    TMap<FName, UTexture2D*> Held;
    for (const int32 Index : Order)
    {
        Held.Add(Here.Bodies[Index].Id, Test.Sky->GetShadowTexture(Here.Bodies[Index].Id));
    }
    const int32 Started = Test.Sky->GetShadowBakesStarted();
    Test.Sky->RebuildFor(Here);
    Test.Sky->SyncTo(Here, Serial + 1, false);
    Test.Step(1.0f / 60.0f);
    TestEqual(TEXT("a jump within the system re-bakes nothing"), Test.Sky->GetShadowBakesStarted(), Started);
    TestEqual(TEXT("and leaves nothing pending"), Test.Sky->GetShadowBakesPending(), 0);
    bool bSame = true;
    for (const TPair<FName, UTexture2D*>& Was : Held)
    {
        bSame = bSame && Test.Sky->GetShadowTexture(Was.Key) == Was.Value;
    }
    TestTrue(TEXT("and every world keeps its texture"), bSame);

    // In transit the maps are left as they are, whatever the system reads.
    // (The step above rebuilt for the real serial again, so Serial is what is built.)
    Test.Sky->SyncTo(FSkySystem(), Serial, true);
    TestTrue(TEXT("in transit the maps are kept"), Test.Sky->GetShadowTexture(Nearest) == Held[Nearest]);

    // A reload of the priors that moves one world's relief keeps the serial:
    // that world alone is baked again, and its old map is let go at once.
    {
        FSkySystem Reloaded = Here;
        // The seed, not the peak: a new peak moves the steepest slope too,
        // and a key blind to the relief would still see that.
        Reloaded.Bodies[Order[0]].Relief.SeedOffset.X += 1.0 / 256.0;
        Test.Sky->SyncTo(Reloaded, Serial, false);
        TestEqual(TEXT("a reload that moves one world's relief re-bakes that world alone"), Test.Sky->GetShadowBakesStarted(), Started + 1);
        TestNull(TEXT("and lets its stale map go"), Test.Sky->GetShadowTexture(Nearest));
        bool bOthers = true;
        for (const TPair<FName, UTexture2D*>& Was : Held)
        {
            bOthers = bOthers && (Was.Key == Nearest || Test.Sky->GetShadowTexture(Was.Key) == Was.Value);
        }
        TestTrue(TEXT("and every other world keeps its own"), bOthers);
    }

    // A map that lands fades in: a new width re-bakes every map, and the
    // frame the nearest one lands in draws it at strength 0, a second later
    // whole. (The next frame reads the real system again, so the reloaded
    // world is baked back to its own relief with the rest.)
    {
        FScopedCVar Wider(TEXT("ds.Sky.ShadowMapWidth"), 512.0f);
        UTexture2D* Landed = nullptr;
        for (int32 Tries = 0; Tries < 600 && !Landed; ++Tries)
        {
            Test.Step(1.0f / 60.0f);
            FPlatformProcess::Sleep(0.005f);
            Landed = Test.Sky->GetShadowTexture(Nearest);
        }
        if (TestNotNull(TEXT("the nearest world's map lands at the new width"), Landed))
        {
            TestEqual(TEXT("at 512 columns"), Landed->GetSizeX(), 512);
            UMaterialInstanceDynamic* Near = Cast<UMaterialInstanceDynamic>(Test.Sky->GetProxy(Order[0])->GetMaterial(0));
            TestEqual(TEXT("and the frame it lands in draws it faded to 0: no shadow appears in one frame"),
                Near->K2_GetScalarParameterValue(SkyMaterial::ShadowMapFade), 0.0f);
            // The fade is the map's, never the strength's: the ground's
            // vertices carry their own shadow, and it draws before any map
            // has landed, or with ds.Sky.ShadowMaps 0.
            TestEqual(TEXT("at the full strength, which the ground's vertices draw at"),
                Near->K2_GetScalarParameterValue(SkyMaterial::Shadows), ShipSky::ShadowStrength());
            // The fade runs on the world's clock, which FSkyWorld::Step does
            // not move (it ticks the actors, not the world): moved here, a
            // frame at a time, as play moves it.
            for (int32 Tick = 0; Tick < FMath::CeilToInt32(ShipSky::ShadowFadeSeconds * 60.0f) + 2; ++Tick)
            {
                Test.World->TimeSeconds += 1.0f / 60.0f;
                Test.Step(1.0f / 60.0f);
            }
            TestEqual(TEXT("and one fade later, whole"), Near->K2_GetScalarParameterValue(SkyMaterial::ShadowMapFade), 1.0f);
        }
        TestEqual(TEXT("the fade is 0 as a map lands"), ShipSky::ShadowFade(0.0), 0.0f);
        TestEqual(TEXT("half at half the fade"), ShipSky::ShadowFade(0.5 * ShipSky::ShadowFadeSeconds), 0.5f);
        TestEqual(TEXT("and whole after it"), ShipSky::ShadowFade(7.0), 1.0f);
        Test.Sky->FlushShadowBakesForTest();
    }

    // A new system: the old maps go, and the bakes they had are let go.
    Test.Sky->SyncTo(FSkySystem(), Serial + 2, false);
    TestNull(TEXT("a new system drops the old maps"), Test.Sky->GetShadowTexture(Nearest));
    TestEqual(TEXT("and has nothing pending"), Test.Sky->GetShadowBakesPending(), 0);
    Test.Step(1.0f / 60.0f);   // back to the start system: it queues again
    TestTrue(TEXT("and coming back queues its worlds again"), Test.Sky->GetShadowBakesPending() > 0);
    {
        FScopedCVar None(TEXT("ds.Sky.ShadowMaps"), 0.0f);
        Test.Step(1.0f / 60.0f);
        TestEqual(TEXT("with ds.Sky.ShadowMaps 0 nothing is baked, and what was baking is let go"), Test.Sky->GetShadowBakesPending(), 0);
        TestNull(TEXT("and no map is held"), Test.Sky->GetShadowTexture(Nearest));
    }
    return true;
}

#endif
