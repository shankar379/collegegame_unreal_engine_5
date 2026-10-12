#include "Campus/CampusAngular.h"

#include <initializer_list>

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace CampusAngular
{
	const FLinearColor Accent(FColor(0xFF, 0xB8, 0x00));
	const FLinearColor AccentDeep(FColor(0xF0, 0x8A, 0x24));
	const FLinearColor Ink(0.027f, 0.035f, 0.047f, 0.82f);
	const FLinearColor InkStrong(0.027f, 0.035f, 0.047f, 0.96f);
	const FLinearColor Hairline(1.f, 1.f, 1.f, 0.14f);
	const FLinearColor Text(1.f, 1.f, 1.f, 1.f);
	const FLinearColor TextMuted(1.f, 1.f, 1.f, 0.58f);
	const FLinearColor Go(FColor(0x2E, 0xB8, 0x5C));
	const FLinearColor Danger(FColor(0xE5, 0x4B, 0x4B));
	const FLinearColor Gem(FColor(0x5C, 0xD6, 0xFF));
	const FLinearColor CoinGold(FColor(0xFF, 0xD3, 0x4D));

	FSlateFontInfo Display(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("BoldCondensedItalic", FMath::Max(Size, 6));
	}

	FSlateFontInfo Numeric(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("BoldCondensed", FMath::Max(Size, 6));
	}

	FSlateFontInfo Body(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Normal", FMath::Max(Size, 6));
	}

	namespace
	{
		/** The 1x1 white texture every solid shape samples. */
		const FSlateBrush* WhiteBrush()
		{
			static const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
			return Brush;
		}

		/** Slate paints on the game thread only, so one set of scratch buffers is enough and
		 *  keeping their capacity means the shapes allocate nothing after the first frame. */
		TArray<FSlateVertex>& ScratchVerts()
		{
			static TArray<FSlateVertex> Verts;
			return Verts;
		}

		TArray<SlateIndex>& ScratchIndices()
		{
			static TArray<SlateIndex> Indices;
			return Indices;
		}

		TArray<FVector2f>& ScratchOutline()
		{
			static TArray<FVector2f> Points;
			return Points;
		}

		TArray<FVector2f>& ScratchStroke()
		{
			static TArray<FVector2f> Points;
			return Points;
		}

		void FillOutline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
			const TArray<FVector2f>& Points, const FLinearColor& Fill)
		{
			if (Points.Num() < 3 || Fill.A <= 0.f)
			{
				return;
			}
			const FSlateRenderTransform Transform = Geometry.ToPaintGeometry().GetAccumulatedRenderTransform();
			const FColor Color = Fill.ToFColor(false);

			TArray<FSlateVertex>& Verts = ScratchVerts();
			TArray<SlateIndex>& Indices = ScratchIndices();
			Verts.Reset(Points.Num());
			Indices.Reset((Points.Num() - 2) * 3);
			for (const FVector2f& Point : Points)
			{
				// A solid fill samples the middle of the white texture, so no UV mapping is needed.
				Verts.Add(FSlateVertex::Make(Transform, Point, FVector2f(0.5f, 0.5f), Color));
			}
			for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
			{
				Indices.Add(0);
				Indices.Add(SlateIndex(Index));
				Indices.Add(SlateIndex(Index + 1));
			}
			static FSlateResourceHandle Handle;
			if (!Handle.IsValid())
			{
				Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush());
			}
			FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Verts, Indices, nullptr, 0, 0);
		}

		void StrokeOutline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
			const TArray<FVector2f>& Points, const FLinearColor& Color, float Width)
		{
			if (Points.Num() < 3 || Color.A <= 0.f || Width <= 0.f)
			{
				return;
			}
			TArray<FVector2f>& Closed = ScratchStroke();
			Closed.Reset(Points.Num() + 1);
			Closed.Append(Points);
			Closed.Add(Points[0]);
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Closed, ESlateDrawEffect::None, Color, true, Width);
		}

		/** A polyline helper the glyphs share; copies into the scratch buffer to avoid allocating. */
		void Line(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
			std::initializer_list<FVector2f> Points, const FLinearColor& Color, float Width)
		{
			TArray<FVector2f>& Buffer = ScratchStroke();
			Buffer.Reset(int32(Points.size()));
			for (const FVector2f& Point : Points)
			{
				Buffer.Add(Point);
			}
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Buffer, ESlateDrawEffect::None, Color, true, Width);
		}

		void Arc(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Center,
			float Radius, float FromRad, float ToRad, int32 Segments, const FLinearColor& Color, float Width)
		{
			TArray<FVector2f>& Buffer = ScratchStroke();
			Buffer.Reset(Segments + 1);
			for (int32 Index = 0; Index <= Segments; ++Index)
			{
				const float Angle = FMath::Lerp(FromRad, ToRad, float(Index) / float(Segments));
				Buffer.Add(FVector2f(Center.X + FMath::Cos(Angle) * Radius, Center.Y + FMath::Sin(Angle) * Radius));
			}
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Buffer, ESlateDrawEffect::None, Color, true, Width);
		}
	}

	void BuildOutline(const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape, TArray<FVector2f>& OutPoints)
	{
		OutPoints.Reset(8);
		const float W = FMath::Max(Size.X, 1.f);
		const float H = FMath::Max(Size.Y, 1.f);
		// Keep the slant inside the box: the top edge leans right, the bottom edge leans left.
		const float Slant = FMath::Clamp(Shape.Slant, 0.f, W * 0.5f);

		const FVector2f Corners[4] = {
			FVector2f(Min.X + Slant, Min.Y),          // top-left
			FVector2f(Min.X + W, Min.Y),              // top-right
			FVector2f(Min.X + W - Slant, Min.Y + H),  // bottom-right
			FVector2f(Min.X, Min.Y + H)               // bottom-left
		};
		const float Cuts[4] = { Shape.CutTL, Shape.CutTR, Shape.CutBR, Shape.CutBL };

		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector2f& Here = Corners[Index];
			const float Cut = FMath::Min(Cuts[Index], FMath::Min(W, H) * 0.45f);
			if (Cut <= 0.5f)
			{
				OutPoints.Add(Here);
				continue;
			}
			const FVector2f& Prev = Corners[(Index + 3) % 4];
			const FVector2f& Next = Corners[(Index + 1) % 4];
			const FVector2f ToPrev = (Prev - Here).GetSafeNormal();
			const FVector2f ToNext = (Next - Here).GetSafeNormal();
			OutPoints.Add(Here + ToPrev * Cut);
			OutPoints.Add(Here + ToNext * Cut);
		}
	}

	void DrawSlantedPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape,
		const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		// A plain rectangle is cheaper as a box than as custom verts.
		if (Shape.IsRectangle() && Border.A <= 0.f)
		{
			FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Min)),
				WhiteBrush(), ESlateDrawEffect::None, Fill);
			return;
		}
		TArray<FVector2f>& Points = ScratchOutline();
		BuildOutline(Min, Size, Shape, Points);
		FillOutline(Out, Layer, Geometry, Points, Fill);
		if (Border.A > 0.f && BorderWidth > 0.f)
		{
			// StrokeOutline uses a different scratch buffer, so Points stays valid.
			StrokeOutline(Out, Layer + 1, Geometry, Points, Border, BorderWidth);
		}
	}

	void DrawCutCornerBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, float Cut,
		const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth)
	{
		DrawSlantedPanel(Out, Layer, Geometry, Min, Size, FCampusShape::Clipped(Cut), Fill, Border, BorderWidth);
	}

	void DrawAccentEdge(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape, const FLinearColor& Color, float Width)
	{
		if (Width <= 0.f || Color.A <= 0.f)
		{
			return;
		}
		// A narrow copy of the same shape, pinned to the leading edge, so the slant matches exactly.
		FCampusShape Edge = Shape;
		Edge.CutTR = 0.f;
		Edge.CutBR = 0.f;
		DrawSlantedPanel(Out, Layer, Geometry, Min, FVector2f(Width, Size.Y), Edge, Color);
	}

	void DrawGlyph(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, EGlyph Glyph,
		const FVector2f& Center, float Radius, const FLinearColor& Color, float Thickness)
	{
		const float R = FMath::Max(Radius, 3.f);
		const float T = Thickness > 0.f ? Thickness : FMath::Max(1.5f, R * 0.16f);
		auto At = [&Center](float X, float Y) { return FVector2f(Center.X + X, Center.Y + Y); };

		switch (Glyph)
		{
		case EGlyph::Gear:
		{
			// One closed star polyline for the teeth, one ring for the hub: two draw elements.
			const int32 Teeth = 8;
			TArray<FVector2f>& Buffer = ScratchStroke();
			Buffer.Reset(Teeth * 4 + 1);
			for (int32 Index = 0; Index < Teeth * 4; ++Index)
			{
				const float Angle = UE_TWO_PI * float(Index) / float(Teeth * 4);
				// out, out, in, in - square-ish teeth rather than a spiky star.
				const int32 Phase = Index % 4;
				const float Reach = (Phase == 0 || Phase == 1) ? R : R * 0.68f;
				Buffer.Add(At(FMath::Cos(Angle) * Reach, FMath::Sin(Angle) * Reach));
			}
			Buffer.Add(Buffer[0]);
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), Buffer, ESlateDrawEffect::None, Color, true, T);
			Arc(Out, Layer, Geometry, Center, R * 0.3f, 0.f, UE_TWO_PI, 12, Color, T);
			break;
		}
		case EGlyph::Mail:
		{
			const float W = R * 0.95f;
			const float H = R * 0.66f;
			Line(Out, Layer, Geometry, { At(-W, -H), At(W, -H), At(W, H), At(-W, H), At(-W, -H) }, Color, T);
			Line(Out, Layer, Geometry, { At(-W, -H), At(0.f, H * 0.35f), At(W, -H) }, Color, T);
			break;
		}
		case EGlyph::Friends:
		{
			// Two heads and two shoulder arcs.
			Arc(Out, Layer, Geometry, At(-R * 0.34f, -R * 0.32f), R * 0.3f, 0.f, UE_TWO_PI, 12, Color, T);
			Arc(Out, Layer, Geometry, At(R * 0.42f, -R * 0.4f), R * 0.24f, 0.f, UE_TWO_PI, 12, Color, T);
			Arc(Out, Layer, Geometry, At(-R * 0.34f, R * 0.62f), R * 0.56f, UE_PI, UE_TWO_PI, 12, Color, T);
			Arc(Out, Layer, Geometry, At(R * 0.42f, R * 0.5f), R * 0.44f, UE_PI * 1.15f, UE_TWO_PI * 0.98f, 10, Color, T);
			break;
		}
		case EGlyph::Plus:
		{
			Line(Out, Layer, Geometry, { At(-R * 0.72f, 0.f), At(R * 0.72f, 0.f) }, Color, T * 1.4f);
			Line(Out, Layer, Geometry, { At(0.f, -R * 0.72f), At(0.f, R * 0.72f) }, Color, T * 1.4f);
			break;
		}
		case EGlyph::Rank:
		{
			// Chevron stack: the rank pip under a squad name plate.
			Line(Out, Layer, Geometry, { At(-R * 0.7f, R * 0.1f), At(0.f, -R * 0.55f), At(R * 0.7f, R * 0.1f) }, Color, T * 1.2f);
			Line(Out, Layer, Geometry, { At(-R * 0.7f, R * 0.7f), At(0.f, R * 0.05f), At(R * 0.7f, R * 0.7f) }, Color, T * 1.2f);
			break;
		}
		case EGlyph::Crown:
		{
			Line(Out, Layer, Geometry, { At(-R * 0.85f, R * 0.55f), At(-R * 0.85f, -R * 0.5f), At(-R * 0.3f, R * 0.05f),
				At(0.f, -R * 0.75f), At(R * 0.3f, R * 0.05f), At(R * 0.85f, -R * 0.5f), At(R * 0.85f, R * 0.55f), At(-R * 0.85f, R * 0.55f) }, Color, T);
			break;
		}
		case EGlyph::Coin:
		{
			Arc(Out, Layer, Geometry, Center, R * 0.82f, 0.f, UE_TWO_PI, 16, Color, T);
			Line(Out, Layer, Geometry, { At(-R * 0.3f, -R * 0.34f), At(R * 0.3f, -R * 0.34f) }, Color, T);
			Line(Out, Layer, Geometry, { At(0.f, -R * 0.34f), At(0.f, R * 0.34f) }, Color, T);
			Line(Out, Layer, Geometry, { At(-R * 0.3f, R * 0.34f), At(R * 0.3f, R * 0.34f) }, Color, T);
			break;
		}
		case EGlyph::Gem:
		{
			const float W = R * 0.8f;
			Line(Out, Layer, Geometry, { At(0.f, -R * 0.9f), At(W, -R * 0.1f), At(0.f, R * 0.9f), At(-W, -R * 0.1f), At(0.f, -R * 0.9f) }, Color, T);
			Line(Out, Layer, Geometry, { At(-W, -R * 0.1f), At(W, -R * 0.1f) }, Color, T * 0.8f);
			break;
		}
		case EGlyph::Wifi:
		{
			// Three nested arcs over a dot: the connection strength indicator.
			Arc(Out, Layer, Geometry, At(0.f, R * 0.7f), R * 0.95f, UE_PI * 1.22f, UE_PI * 1.78f, 10, Color, T);
			Arc(Out, Layer, Geometry, At(0.f, R * 0.7f), R * 0.6f, UE_PI * 1.22f, UE_PI * 1.78f, 8, Color, T);
			Arc(Out, Layer, Geometry, At(0.f, R * 0.7f), R * 0.26f, UE_PI * 1.15f, UE_PI * 1.85f, 6, Color, T);
			break;
		}
		case EGlyph::Close:
		{
			Line(Out, Layer, Geometry, { At(-R * 0.6f, -R * 0.6f), At(R * 0.6f, R * 0.6f) }, Color, T * 1.4f);
			Line(Out, Layer, Geometry, { At(R * 0.6f, -R * 0.6f), At(-R * 0.6f, R * 0.6f) }, Color, T * 1.4f);
			break;
		}
		case EGlyph::Chevron:
		{
			Line(Out, Layer, Geometry, { At(-R * 0.4f, -R * 0.6f), At(R * 0.4f, 0.f), At(-R * 0.4f, R * 0.6f) }, Color, T * 1.3f);
			break;
		}
		case EGlyph::Lock:
		{
			// Shackle over a body: the "not yours yet" mark on a locked map or mode.
			Arc(Out, Layer, Geometry, At(0.f, -R * 0.18f), R * 0.46f, UE_PI, UE_TWO_PI, 10, Color, T);
			Line(Out, Layer, Geometry, { At(-R * 0.68f, -R * 0.14f), At(R * 0.68f, -R * 0.14f), At(R * 0.68f, R * 0.72f),
				At(-R * 0.68f, R * 0.72f), At(-R * 0.68f, -R * 0.14f) }, Color, T);
			Line(Out, Layer, Geometry, { At(0.f, R * 0.14f), At(0.f, R * 0.42f) }, Color, T * 1.1f);
			break;
		}
		case EGlyph::Clock:
		{
			Arc(Out, Layer, Geometry, Center, R * 0.82f, 0.f, UE_TWO_PI, 16, Color, T);
			Line(Out, Layer, Geometry, { At(0.f, -R * 0.46f), At(0.f, 0.f), At(R * 0.4f, R * 0.1f) }, Color, T);
			break;
		}
		case EGlyph::Tick:
		{
			Line(Out, Layer, Geometry, { At(-R * 0.6f, 0.f), At(-R * 0.16f, R * 0.46f), At(R * 0.64f, -R * 0.5f) }, Color, T * 1.5f);
			break;
		}
		}
	}

	void DrawShapedImage(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape,
		const FSlateResourceHandle& Handle, const FVector2f& ImageSize, const FLinearColor& Tint, float FocusY)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f || !Handle.IsValid() || ImageSize.X <= 0.f || ImageSize.Y <= 0.f || Tint.A <= 0.f)
		{
			return;
		}
		TArray<FVector2f>& Points = ScratchOutline();
		BuildOutline(Min, Size, Shape, Points);
		if (Points.Num() < 3)
		{
			return;
		}
		// "Cover": the shorter side of the picture fills the card and the longer one is cropped,
		// so a 3:1 banner squeezed into a 5:1 slot loses sky, never stretches.
		const float BoxAspect = Size.X / Size.Y;
		const float ArtAspect = ImageSize.X / ImageSize.Y;
		const float USpan = BoxAspect > ArtAspect ? 1.f : ArtAspect / BoxAspect;
		const float VSpan = BoxAspect > ArtAspect ? BoxAspect / ArtAspect : 1.f;
		const float U0 = (1.f - USpan) * 0.5f;
		const float V0 = (1.f - VSpan) * FMath::Clamp(FocusY, 0.f, 1.f);

		const FSlateRenderTransform Transform = Geometry.ToPaintGeometry().GetAccumulatedRenderTransform();
		const FColor Color = Tint.ToFColor(false);
		TArray<FSlateVertex>& Verts = ScratchVerts();
		TArray<SlateIndex>& Indices = ScratchIndices();
		Verts.Reset(Points.Num());
		Indices.Reset((Points.Num() - 2) * 3);
		for (const FVector2f& Point : Points)
		{
			const FVector2f Uv(U0 + USpan * (Point.X - Min.X) / Size.X, V0 + VSpan * (Point.Y - Min.Y) / Size.Y);
			Verts.Add(FSlateVertex::Make(Transform, Point, Uv, Color));
		}
		for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
		{
			Indices.Add(0);
			Indices.Add(SlateIndex(Index));
			Indices.Add(SlateIndex(Index + 1));
		}
		FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Verts, Indices, nullptr, 0, 0);
	}

	void DrawVerticalFade(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2f& Min, const FVector2f& Size, const FCampusShape& Shape,
		const FLinearColor& Top, const FLinearColor& Bottom)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f || (Top.A <= 0.f && Bottom.A <= 0.f))
		{
			return;
		}
		TArray<FVector2f>& Points = ScratchOutline();
		BuildOutline(Min, Size, Shape, Points);
		if (Points.Num() < 3)
		{
			return;
		}
		const FSlateRenderTransform Transform = Geometry.ToPaintGeometry().GetAccumulatedRenderTransform();
		TArray<FSlateVertex>& Verts = ScratchVerts();
		TArray<SlateIndex>& Indices = ScratchIndices();
		Verts.Reset(Points.Num());
		Indices.Reset((Points.Num() - 2) * 3);
		for (const FVector2f& Point : Points)
		{
			// Per-vertex colour: the ramp costs the same one draw element as a flat fill.
			const float Alpha = FMath::Clamp((Point.Y - Min.Y) / Size.Y, 0.f, 1.f);
			Verts.Add(FSlateVertex::Make(Transform, Point, FVector2f(0.5f, 0.5f),
				FMath::Lerp(Top, Bottom, Alpha).ToFColor(false)));
		}
		for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
		{
			Indices.Add(0);
			Indices.Add(SlateIndex(Index));
			Indices.Add(SlateIndex(Index + 1));
		}
		static FSlateResourceHandle Handle;
		if (!Handle.IsValid())
		{
			Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush());
		}
		FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Verts, Indices, nullptr, 0, 0);
	}

	TSharedRef<SWidget> Logo(int32 Size)
	{
		const int32 Small = FMath::Max(10, FMath::RoundToInt(Size * 0.42f));
		auto Word = [](const FString& Letters, int32 FontSize, const FLinearColor& Color)
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Letters))
				.Font(Display(FontSize))
				.ColorAndOpacity(Color);
		};
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[Word(TEXT("CAMPUS"), Small, Accent)]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, -Size * 0.16f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				// Accent on the O, the way Free Fire accents the I.
				+ SHorizontalBox::Slot().AutoWidth()[Word(TEXT("C"), Size, Text)]
				+ SHorizontalBox::Slot().AutoWidth()[Word(TEXT("O"), Size, Accent)]
				+ SHorizontalBox::Slot().AutoWidth()[Word(TEXT("MBAT"), Size, Text)]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Slanted(Size * 0.18f))
				.Fill(Accent)
				[
					SNew(SBox).WidthOverride(Size * 2.6f).HeightOverride(FMath::Max(3.f, Size * 0.07f))
				]
			];
	}
}

// -------------------------------------------------------------------------------------------------
// SCampusAngularPanel

void SCampusAngularPanel::Construct(const FArguments& InArgs)
{
	Shape = InArgs._Shape;
	Fill = InArgs._Fill;
	Border = InArgs._Border;
	BorderWidth = InArgs._BorderWidth;
	AccentEdge = InArgs._AccentEdge;
	AccentEdgeWidth = InArgs._AccentEdgeWidth;
	ChildSlot.Padding(InArgs._ContentPadding)[InArgs._Content.Widget];
}

int32 SCampusAngularPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId, AllottedGeometry, FVector2f::ZeroVector, Size, Shape,
		Fill.Get(CampusAngular::Ink), Border.Get(FLinearColor::Transparent), BorderWidth);
	if (AccentEdgeWidth > 0.f)
	{
		CampusAngular::DrawAccentEdge(OutDrawElements, LayerId + 2, AllottedGeometry, FVector2f::ZeroVector, Size, Shape,
			AccentEdge.Get(FLinearColor::Transparent), AccentEdgeWidth);
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 3, InWidgetStyle, bParentEnabled);
}

// -------------------------------------------------------------------------------------------------
// SCampusAngularButton

void SCampusAngularButton::Construct(const FArguments& InArgs)
{
	Shape = InArgs._Shape;
	Fill = InArgs._Fill;
	Border = InArgs._Border;
	BorderWidth = InArgs._BorderWidth;
	AccentEdge = InArgs._AccentEdge;
	AccentEdgeWidth = InArgs._AccentEdgeWidth;

	ChildSlot
	[
		SAssignNew(Inner, SButton)
		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
		.ContentPadding(InArgs._ContentPadding)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked(InArgs._OnClicked)
		[
			InArgs._Content.Widget
		]
	];
}

int32 SCampusAngularButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const bool bPressed = Inner.IsValid() && Inner->IsPressed();
	const bool bHovered = Inner.IsValid() && Inner->IsHovered();

	FLinearColor Face = Fill;
	if (!bEnabled)
	{
		// Desaturate rather than just fade, so a disabled accent plate never reads as "tappable".
		const float Grey = Face.GetLuminance() * 0.45f;
		Face = FLinearColor(Grey, Grey, Grey * 1.08f, Face.A * 0.75f);
	}
	else if (bPressed)
	{
		Face = Face * FLinearColor(0.78f, 0.78f, 0.78f, 1.f);
		Face.A = Fill.A;
	}
	else if (bHovered)
	{
		Face = Face * FLinearColor(1.18f, 1.18f, 1.18f, 1.f);
		Face.A = Fill.A;
	}

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId, AllottedGeometry, FVector2f::ZeroVector, Size, Shape,
		Face, bEnabled ? Border : Border.CopyWithNewOpacity(Border.A * 0.4f), BorderWidth);
	if (AccentEdgeWidth > 0.f)
	{
		CampusAngular::DrawAccentEdge(OutDrawElements, LayerId + 2, AllottedGeometry, FVector2f::ZeroVector, Size, Shape,
			AccentEdge, AccentEdgeWidth);
	}
	// Keyboard / gamepad focus must stay visible: a bright outline on the same shape.
	if (bEnabled && Inner.IsValid() && Inner->HasKeyboardFocus())
	{
		TArray<FVector2f> Points;
		CampusAngular::BuildOutline(FVector2f(1.f, 1.f), Size - FVector2f(2.f, 2.f), Shape, Points);
		Points.Add(Points[0]);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points,
			ESlateDrawEffect::None, CampusAngular::Accent, true, 2.f);
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 3, InWidgetStyle, bParentEnabled);
}

// -------------------------------------------------------------------------------------------------
// SCampusGlyph

void SCampusGlyph::Construct(const FArguments& InArgs)
{
	Glyph = InArgs._Glyph;
	Size = InArgs._Size;
	Thickness = InArgs._Thickness;
	Color = InArgs._Color;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SCampusGlyph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Local = AllottedGeometry.GetLocalSize();
	const float Radius = FMath::Min(Local.X, Local.Y) * 0.5f;
	CampusAngular::DrawGlyph(OutDrawElements, LayerId, AllottedGeometry, Glyph, Local * 0.5f, Radius,
		Color.Get(CampusAngular::Text), Thickness);
	return LayerId + 1;
}
