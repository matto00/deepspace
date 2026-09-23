#include "Player/DeepSpaceAnimInstance.h"

#include "Player/DeepSpaceCharacter.h"

void UDeepSpaceAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    // The editor's animation preview has no character: leave the defaults.
    const ADeepSpaceCharacter* Character = Cast<ADeepSpaceCharacter>(TryGetPawnOwner());
    if (!Character)
    {
        return;
    }
    Speed = Character->GetVelocity().Size2D();
    Posture = Character->GetPosture();
}
