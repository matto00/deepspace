#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Player/Posture.h"
#include "DeepSpaceAnimInstance.generated.h"

/**
 * Computes everything the body's animation depends on. ABP_DeepSpaceBody
 * subclasses this and only wires these values into blend spaces: all the
 * deciding happens here, where it can be read, diffed and reviewed.
 */
UCLASS()
class DEEPSPACE_API UDeepSpaceAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
    /** Ground speed in cm/s; drives the locomotion and crouch blend spaces. */
    UPROPERTY(BlueprintReadOnly, Category = "Body")
    float Speed = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Body")
    EPosture Posture = EPosture::Standing;
};
