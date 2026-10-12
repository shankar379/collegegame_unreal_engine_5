// Sharp-edge UI primitives (Garena Free Fire look), shared by the lobby Slate screens and the
// hand-painted in-match HUD:
//   * FCampusShape      - a rectangle whose top edge is pushed sideways (parallelogram) and whose
//                         corners can be clipped. One struct describes every panel, tab and button.
//   * DrawSlantedPanel  - fills a shape with one FSlateDrawElement::MakeCustomVerts call.
//   * DrawCutCornerBox  - the same for a plain clipped-corner box.
//   * SCampusAngularPanel / SCampusAngularButton - Slate wrappers around the two painters.
//   * DrawGlyph / SCampusGlyph - the small vector icons (gear, mail, friends, rank, ...) so the UI
//                         needs no texture assets.
//
// Phone budget: every painter reuses static scratch vertex arrays, so after the first frame the
// shapes allocate nothing, and each shape is one draw element (plus one for its border).

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SButton;

/** A rectangle with its top edge pushed sideways (parallelogram) and any corner clipped. */
struct FCampusShape
{
	/** Top edge shifted right of the bottom edge, in local px. Free Fire's forward-leaning slant. */
	float Slant = 0.f;
	/** Corner cuts, in local px, measured along both edges that meet at the corner. */
	float CutTL = 0.f;
	float CutTR = 0.f;
	float CutBR = 0.f;
	float CutBL = 0.f;

	FCampusShape() = default;

	/** A pure parallelogram: slanted buttons, tabs, counters. */
	static FCampusShape Slanted(float InSlant)
	{
		FCampusShape Shape;
		Shape.Slant = InSlant;
		return Shape;
	}

	/** All four corners clipped: cards and dialogs. */
	static FCampusShape Clipped(float Cut)
	{
		FCampusShape Shape;
		Shape.CutTL = Shape.CutTR = Shape.CutBR = Shape.CutBL = Cut;
		return Shape;
	}

	/** Only the diagonally opposite corners clipped: the standard dark panel / HUD chip. */
	static FCampusShape Notched(float Cut)
	{
		FCampusShape Shape;
		Shape.CutTR = Shape.CutBL = Cut;
		return Shape;
	}

	/** Slanted with the trailing corners squared off: the left-hand menu tabs. */
	static FCampusShape Tab(float InSlant, float Cut = 0.f)
	{
		FCampusShape Shape;
		Shape.Slant = InSlant;
		Shape.CutTR = Cut;
		return Shape;
	}

	/** Slanted with diagonal cut ends: the yellow START button. */
	static FCampusShape Wedge(float InSlant, float Cut)
	{
		FCampusShape Shape;
		Shape.Slant = InSlant;
		Shape.CutTR = Shape.CutBL = Cut;
		return Shape;
	}

	bool IsRectangle() const
	{
		return Slant == 0.f && CutTL == 0.f && CutTR == 0.f && CutBR == 0.f && CutBL == 0.f;
	}
};

namespace CampusAngular
{
	// ---- colour tokens. One accent, one "go" green, neutrals. Nothing else. -------------------
	/** #FFB800 - the Free Fire yellow: START, selected tabs, counters, logo accent. */
	extern const FLinearColor Accent;
	/** #F08A24 - the deeper orange used for pressed / secondary accent. */
	extern const FLinearColor AccentDeep;
	/** Dark translucent panel fill. */
	extern const FLinearColor Ink;
	/** Opaque panel fill for dialogs that must stay readable over the 3D lobby. */
	extern const FLinearColor InkStrong;
	/** 1px panel border. */
	extern const FLinearColor Hairline;
	extern const FLinearColor Text;
	extern const FLinearColor TextMuted;
	extern const FLinearColor Go;
	extern const FLinearColor Danger;
	extern const FLinearColor Gem;
	extern const FLinearColor CoinGold;

	// ---- type. Three roles, one family (Roboto). ----------------------------------------------
	/** Bold condensed italic: headings, labels, buttons, counters. Always uppercase copy. */
	FSlateFontInfo Display(int32 Size);
	/** Bold condensed upright: numbers in tables and counters, where italic hurts alignment. */
	FSlateFontInfo Numeric(int32 Size);
	/** Regular upright: paragraphs (terms, hints). Condensed italic is unreadable at length. */
	FSlateFontInfo Body(int32 Size);

	/** The shape outline, clockwise from the top-left, in local space. Up to 8 points. */
	void BuildOutline(const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape, TArray<FVector2f>& OutPoints);

	/** Fills Shape and optionally strokes its outline. One draw element each. */
	void DrawSlantedPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape,
		const FLinearColor& Fill, const FLinearColor& Border = FLinearColor::Transparent, float BorderWidth = 0.f);

	/** DrawSlantedPanel with every corner clipped by Cut and no slant. */
	void DrawCutCornerBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, float Cut,
		const FLinearColor& Fill, const FLinearColor& Border = FLinearColor::Transparent, float BorderWidth = 0.f);

	/** The accent stripe down the leading (left) edge of a selected tab. */
	void DrawAccentEdge(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape, const FLinearColor& Color, float Width);

	/** Fills Shape with a texture instead of a colour, cropped "cover" so the art keeps its aspect
	 *  however the card is stretched. FocusY 0 keeps the top of the picture, 1 the bottom.
	 *  The caller owns the resource handle so nothing is looked up per frame. */
	void DrawShapedImage(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape,
		const FSlateResourceHandle& Handle, const FVector2f& ImageSize, const FLinearColor& Tint, float FocusY = 0.5f);

	/** Fills Shape with a top-to-bottom two-colour ramp: the scrim that keeps a title legible over
	 *  art, and the stand-in when a picture is missing. One draw element. */
	void DrawVerticalFade(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape,
		const FLinearColor& Top, const FLinearColor& Bottom);

	/** Small vector icons; no texture assets, so they never fail to load. */
	enum class EGlyph : uint8 { Gear, Mail, Friends, Plus, Rank, Coin, Gem, Wifi, Crown, Close, Chevron, Lock, Clock, Tick };

	void DrawGlyph(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, EGlyph Glyph,
		const FVector2f& Center, float Radius, const FLinearColor& Color, float Thickness = 0.f);

	/** "CAMPUS / COMBAT" lockup: condensed italic, accent on the O, accent underline. */
	TSharedRef<SWidget> Logo(int32 Size);
}

/** Paints one FCampusShape behind its child content. */
class SCampusAngularPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusAngularPanel)
		: _Shape(FCampusShape())
		, _Fill(CampusAngular::Ink)
		, _Border(FLinearColor::Transparent)
		, _BorderWidth(0.f)
		, _AccentEdge(FLinearColor::Transparent)
		, _AccentEdgeWidth(0.f)
		, _ContentPadding(FMargin(0.f))
		{}
		SLATE_ARGUMENT(FCampusShape, Shape)
		SLATE_ATTRIBUTE(FLinearColor, Fill)
		SLATE_ATTRIBUTE(FLinearColor, Border)
		SLATE_ARGUMENT(float, BorderWidth)
		SLATE_ATTRIBUTE(FLinearColor, AccentEdge)
		SLATE_ARGUMENT(float, AccentEdgeWidth)
		SLATE_ARGUMENT(FMargin, ContentPadding)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FCampusShape Shape;
	TAttribute<FLinearColor> Fill;
	TAttribute<FLinearColor> Border;
	TAttribute<FLinearColor> AccentEdge;
	float BorderWidth = 0.f;
	float AccentEdgeWidth = 0.f;
};

/** An angular, tappable plate. Keyboard / touch behaviour comes from a borderless SButton inside. */
class SCampusAngularButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusAngularButton)
		: _Shape(FCampusShape())
		, _Fill(CampusAngular::Ink)
		, _Border(CampusAngular::Hairline)
		, _BorderWidth(1.f)
		, _AccentEdge(FLinearColor::Transparent)
		, _AccentEdgeWidth(0.f)
		, _ContentPadding(FMargin(0.f))
		{}
		SLATE_ARGUMENT(FCampusShape, Shape)
		SLATE_ARGUMENT(FLinearColor, Fill)
		SLATE_ARGUMENT(FLinearColor, Border)
		SLATE_ARGUMENT(float, BorderWidth)
		SLATE_ARGUMENT(FLinearColor, AccentEdge)
		SLATE_ARGUMENT(float, AccentEdgeWidth)
		SLATE_ARGUMENT(FMargin, ContentPadding)
		SLATE_EVENT(FOnClicked, OnClicked)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FCampusShape Shape;
	FLinearColor Fill = CampusAngular::Ink;
	FLinearColor Border = CampusAngular::Hairline;
	FLinearColor AccentEdge = FLinearColor::Transparent;
	float BorderWidth = 1.f;
	float AccentEdgeWidth = 0.f;
	TSharedPtr<SButton> Inner;
};

/** One CampusAngular::EGlyph as a sized widget. */
class SCampusGlyph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusGlyph)
		: _Glyph(CampusAngular::EGlyph::Gear)
		, _Size(24.f)
		, _Color(CampusAngular::Text)
		, _Thickness(0.f)
		{}
		SLATE_ARGUMENT(CampusAngular::EGlyph, Glyph)
		SLATE_ARGUMENT(float, Size)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ARGUMENT(float, Thickness)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	CampusAngular::EGlyph Glyph = CampusAngular::EGlyph::Gear;
	float Size = 24.f;
	float Thickness = 0.f;
	TAttribute<FLinearColor> Color;
};
