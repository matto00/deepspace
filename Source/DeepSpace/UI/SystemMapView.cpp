#include "UI/SystemMapView.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UI/ShipScreenWidget.h"

namespace
{
    /** Line widths, px: a ring is 2 px (decision 3), the ship and the
     *  target ring a little finer so they read as marks rather than orbits. */
    constexpr float RingThickness = 2.0f;
    constexpr float MarkThickness = 1.5f;

    /** The numerals, points. Smaller than the rows' 14: they sit among the
     *  rings, and the rows are where a world is read. */
    constexpr int32 NumeralSize = 11;

    /** The least a world's colour is lifted to, in luminance: albedo colours
     *  are dim by nature, and a world drawn at its albedo on a near-black
     *  panel would vanish. The hue is the window's; the brightness is the
     *  panel's. */
    constexpr float MinWorldLuminance = 0.35f;

    /** A circle as a closed line strip, with segments enough that no facet
     *  shows at its size: about one every 3 px of circumference. */
    TArray<FVector2D> Circle(const FVector2D& Centre, double Radius)
    {
        const int32 Segments = FMath::Clamp(FMath::CeilToInt32(2.0 * UE_DOUBLE_PI * Radius / 3.0), 16, 256);
        TArray<FVector2D> Points;
        Points.Reserve(Segments + 1);
        for (int32 Index = 0; Index <= Segments; ++Index)
        {
            const double Angle = 2.0 * UE_DOUBLE_PI * Index / Segments;
            Points.Add(Centre + Radius * FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)));
        }
        return Points;
    }

    void Disc(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2D& Centre,
              double Diameter, const FLinearColor& Colour)
    {
        // A rounded box whose corners are half its size is a disc. The brush
        // is copied into the element, so a local one is safe.
        const FSlateRoundedBoxBrush Brush(FLinearColor::White, static_cast<float>(0.5 * Diameter));
        const FVector2D Size(Diameter, Diameter);
        FSlateDrawElement::MakeBox(Out, Layer,
            Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Centre - 0.5 * Size))),
            &Brush, ESlateDrawEffect::None, Colour);
    }

    void Lines(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const TArray<FVector2D>& Points,
               const FLinearColor& Colour, float Thickness)
    {
        TArray<FVector2f> Strip;
        Strip.Reserve(Points.Num());
        for (const FVector2D& Point : Points)
        {
            Strip.Add(FVector2f(Point));
        }
        FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Strip, ESlateDrawEffect::None, Colour,
                                     true, Thickness);
    }
}

USystemMapView::USystemMapView(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Visible, not merely drawn: the pointer's click must land on the view
    // itself, which has no child to take it.
    SetVisibility(ESlateVisibility::Visible);
    SetIsFocusable(false);
}

void USystemMapView::SetDrawing(const SystemMap::FMapLayout& InLayout, const FLinearColor& InStarColour,
                                const TArray<FLinearColor>& InWorldColours)
{
    Layout = InLayout;
    StarColour = InStarColour;
    WorldColours = InWorldColours;
    bHasDrawing = true;
}

void USystemMapView::ClearDrawing()
{
    Layout = SystemMap::FMapLayout();
    WorldColours.Reset();
    bHasDrawing = false;
    Ship.Reset();
    TargetOrbit.Reset();
}

TOptional<int32> USystemMapView::ClickAt(const FVector2D& Point)
{
    if (!bHasDrawing)
    {
        return {};
    }
    const TOptional<int32> Picked = SystemMap::Pick(Layout, Point);
    if (Picked)
    {
        OnPicked.ExecuteIfBound(*Picked);
    }
    return Picked;
}

FLinearColor USystemMapView::PanelColour(const FLinearColor& SkyColour)
{
    const float Luminance = SkyColour.GetLuminance();
    if (Luminance >= MinWorldLuminance)
    {
        return FLinearColor(SkyColour.R, SkyColour.G, SkyColour.B, 1.0f);
    }
    if (Luminance <= UE_SMALL_NUMBER)
    {
        return FLinearColor(MinWorldLuminance, MinWorldLuminance, MinWorldLuminance, 1.0f);
    }
    const FLinearColor Lifted = SkyColour * (MinWorldLuminance / Luminance);
    return FLinearColor(FMath::Min(Lifted.R, 1.0f), FMath::Min(Lifted.G, 1.0f), FMath::Min(Lifted.B, 1.0f), 1.0f);
}

FReply USystemMapView::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }
    ClickAt(FVector2D(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition())));
    return FReply::Handled();
}

int32 USystemMapView::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                  const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                                  int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle,
                                 bParentEnabled);
    if (!bHasDrawing)
    {
        return LayerId;
    }

    const SystemMap::FMapPixels& Pixels = Layout.Pixels;
    const int32 RingLayer = LayerId + 1;
    const int32 BodyLayer = LayerId + 2;
    const int32 MarkLayer = LayerId + 3;

    for (const double Radius : Layout.RingPx)
    {
        Lines(OutDrawElements, RingLayer, AllottedGeometry, Circle(Pixels.Centre, Radius), UShipScreenWidget::Dim,
              RingThickness);
    }

    Disc(OutDrawElements, BodyLayer, AllottedGeometry, Pixels.Centre, 2.0 * Pixels.StarPx, StarColour);

    const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", NumeralSize);
    const TSharedPtr<FSlateFontMeasure> Measure = FSlateApplication::IsInitialized()
        ? FSlateApplication::Get().GetRenderer()->GetFontMeasureService() : TSharedPtr<FSlateFontMeasure>();
    for (const SystemMap::FMapDot& Dot : Layout.Dots)
    {
        const FLinearColor Colour = WorldColours.IsValidIndex(Dot.Orbit) ? WorldColours[Dot.Orbit] : UShipScreenWidget::Ink;
        Disc(OutDrawElements, BodyLayer, AllottedGeometry, Dot.Centre, Dot.SizePx, Colour);

        if (TargetOrbit && *TargetOrbit == Dot.Orbit)
        {
            Lines(OutDrawElements, MarkLayer, AllottedGeometry, Circle(Dot.Centre, 0.5 * Dot.TargetRingPx),
                  UShipScreenWidget::Accent, MarkThickness);
        }

        // The numeral outside its dot, away from the star: its box's centre
        // pushed out along Outward until the box clears the dot.
        const FString Numeral = Dot.Numeral;
        const FVector2D Size = Measure ? FVector2D(Measure->Measure(Numeral, Font)) : FVector2D(8.0 * Numeral.Len(), 14.0);
        const FVector2D Half = 0.5 * Size;
        const double Reach = 0.5 * FMath::Max(Dot.SizePx, Dot.TargetRingPx) + SystemMap::NumeralGapPx
            + FMath::Abs(Dot.Outward.X) * Half.X + FMath::Abs(Dot.Outward.Y) * Half.Y;
        const FVector2D TopLeft = Dot.Centre + Dot.Outward * Reach - Half;
        FSlateDrawElement::MakeText(OutDrawElements, MarkLayer,
            AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))),
            Numeral, Font, ESlateDrawEffect::None, UShipScreenWidget::Dim);
    }

    if (Ship)
    {
        Lines(OutDrawElements, MarkLayer, AllottedGeometry, Circle(Ship->Centre, 0.5 * SystemMap::ShipRingPx),
              UShipScreenWidget::Ink, MarkThickness);
        if (Ship->Nose)
        {
            const FVector2D From = Ship->Centre + *Ship->Nose * (0.5 * SystemMap::ShipRingPx);
            Lines(OutDrawElements, MarkLayer, AllottedGeometry, {From, From + *Ship->Nose * SystemMap::ShipTickPx},
                  UShipScreenWidget::Ink, MarkThickness);
        }
    }
    return MarkLayer;
}
