#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipScreen.h"
#include "ShipMapScreen.generated.h"

/**
 * The system map: the middle cockpit desk screen, on the ship's centre line
 * between the helm and the chart chair, showing USystemMapWidget (system map
 * spec, decision 1).
 *
 * Read from the helm, never sat at. The pilot needs the map while flying,
 * with the mouse already turning their head, so E here sits nobody down:
 * there is no chair in front of it, and a sit-down map would send the pilot
 * out of the helm, where the map is needed. It is driven by the view-aimed
 * pointer, standing as every screen is; from a seat once AShipScreen can say
 * so (stage 4 of the build order: IsDrivableSeated and
 * IsZoomableFromChartChair, overridden here).
 *
 * Drawn at the resolution it is seen at. From the helm's eye the 68 cm panel
 * spans about 575 x 424 screen pixels on the 4K display, and a widget
 * component's render target is sampled without mips, so the chart's 816 x
 * 576 would be minified 1.4 times and every 1-2 px ring would shimmer. At
 * 600 x 424 one widget pixel is close to one screen pixel at the helm, and
 * standing close the panel is magnified instead: soft, never aliased.
 *
 * It faces -X at yaw 0 and its origin is the centre of its glass, like the
 * chart, so Tools/hauler_layout.py places the panel itself.
 */
UCLASS()
class DEEPSPACE_API AShipMapScreen : public AShipScreen
{
    GENERATED_BODY()

public:
    AShipMapScreen();
};
