#include "Ship/ShipMapScreen.h"

#include "Components/WidgetComponent.h"
#include "UI/SystemMapWidget.h"

AShipMapScreen::AShipMapScreen()
{
    // 68 cm across the desk screen's 70 x 50 cm face, a centimetre of bezel
    // all round, like the chart beside it; drawn at 600 x 424, 8.8 px/cm,
    // which is what the helm sees of it.
    PanelWidthCm = 68.0f;
    DrawSizePixels = FVector2D(600.0f, 424.0f);
    BezelCm = FVector2D(1.0f, 1.0f);

    // Nothing sits you down here. The viewing distance is the chart's, for
    // the chart chair's zoom (decision 13), which place_map_screen also sets
    // per instance.
    bUsable = false;
    ViewDistanceCm = 60.0f;

    Screen->SetWidgetClass(USystemMapWidget::StaticClass());
    SetPanelWidthCm(PanelWidthCm);
}

FText AShipMapScreen::GetZoomPrompt() const
{
    return NSLOCTEXT("DeepSpace", "ZoomMap", "Map");
}
