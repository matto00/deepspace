#include "Editor/DeepSpaceEditorScripting.h"

#include "Animation/BlendSpace.h"

bool UDeepSpaceEditorScripting::RebuildBlendSpace(UBlendSpace* BlendSpace)
{
#if WITH_EDITOR
    if (!BlendSpace)
    {
        return false;
    }
    BlendSpace->ValidateSampleData();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
