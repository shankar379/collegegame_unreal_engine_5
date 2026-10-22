// FESTIVAL ART, DRAWN - the look an event wears before (or instead of) uploaded banner art (backend migration
// 034, live_events.theme). One leaf widget, no textures, no allocation per frame: a sky gradient and a few
// dozen boxes and lines whose positions are pure functions of time, so the same frame looks the same on every
// phone and costs a handful of draw elements.
//
//   diwali     night sky turning to a warm horizon, a garland of twinkling lights, a row of flickering diyas
//              and golden sparks rising off them
//   christmas  navy to pine green, falling snow (six-armed flakes up close, dots far away), coloured lights
//              and a glowing star
//   anything   the generic festival: a dusk gradient and drifting sparkles
//
// Paint only. It never takes a click: put it UNDER the button or plate that does.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class SCampusFestiveArt : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusFestiveArt) : _Theme(), _Intensity(1.f) {}
		/** "diwali" | "christmas" | anything else (generic). */
		SLATE_ARGUMENT(FString, Theme)
		/** 0..1: how busy it is. The small lobby banner runs lighter than the event sheet's head card. */
		SLATE_ARGUMENT(float, Intensity)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** The colour an event of this theme uses for its accents (borders, chips) next to the art. */
	static FLinearColor AccentFor(const FString& Theme);

private:
	enum class EKind : uint8 { Generic, Diwali, Christmas };
	EKind Kind = EKind::Generic;
	float Intensity = 1.f;

	int32 PaintDiwali(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float T) const;
	int32 PaintChristmas(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float T) const;
	int32 PaintGeneric(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float T) const;
};
