#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DeepSpaceEditorScripting.generated.h"

class UBlendSpace;

/**
 * Engine operations the editor-side Python scripts under Tools/ need but that
 * Unreal does not expose to Python. Editor-only: every function does nothing
 * and returns false in a cooked game.
 */
UCLASS()
class DEEPSPACE_API UDeepSpaceEditorScripting : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Rebuild a blend space's runtime interpolation data from its samples.
     *
     * Setting a blend space's samples and axes from Python stores them but
     * never builds the table the engine blends from -- that happens only in
     * the blend space editor's change notifications. A blend space built from
     * a script without this saves and loads cleanly and then blends nothing.
     */
    UFUNCTION(BlueprintCallable, Category = "DeepSpace|Editor")
    static bool RebuildBlendSpace(UBlendSpace* BlendSpace);
};
