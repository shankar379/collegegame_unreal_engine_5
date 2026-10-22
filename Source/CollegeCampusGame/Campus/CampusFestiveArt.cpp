#include "Campus/CampusFestiveArt.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace CampusFestive
{
	const FSlateBrush* White()
	{
		static const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
		return Brush;
	}

	/** A deterministic 0..1 for particle Index, salt Salt: same layout on every phone, every frame. */
	float Hash(int32 Index, int32 Salt)
	{
		uint32 X = uint32(Index) * 747796405u + uint32(Salt) * 2891336453u + 1u;
		X = ((X >> ((X >> 28u) + 4u)) ^ X) * 277803737u;
		X = (X >> 22u) ^ X;
		return float(X & 0xFFFFFFu) / float(0xFFFFFF);
	}

	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Centre, float W, float H,
		const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Out, Layer,
			G.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(Centre - FVector2f(W * 0.5f, H * 0.5f))),
			White(), ESlateDrawEffect::None, Color);
	}

	/** A diamond (a box turned 45 degrees): the sparkle, the flame and the light bulb of this file. */
	void Diamond(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Centre, float Size,
		const FLinearColor& Color)
	{
		FSlateDrawElement::MakeRotatedBox(Out, Layer,
			G.ToPaintGeometry(FVector2f(Size, Size), FSlateLayoutTransform(Centre - FVector2f(Size * 0.5f, Size * 0.5f))),
			White(), ESlateDrawEffect::None, UE_PI * 0.25f, TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, Color);
	}

	/** A soft glow: three stacked diamonds, big and faint to small and bright. */
	void Glow(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Centre, float Size,
		const FLinearColor& Color, float Alpha)
	{
		Diamond(Out, Layer, G, Centre, Size * 3.2f, Color.CopyWithNewOpacity(0.07f * Alpha));
		Diamond(Out, Layer, G, Centre, Size * 1.9f, Color.CopyWithNewOpacity(0.16f * Alpha));
		Diamond(Out, Layer + 1, G, Centre, Size, Color.CopyWithNewOpacity(0.95f * Alpha));
	}

	void Sky(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLinearColor& Top,
		const FLinearColor& Mid, const FLinearColor& Bottom)
	{
		TArray<FSlateGradientStop> Stops;
		const FVector2f Size = G.GetLocalSize();
		Stops.Add(FSlateGradientStop(FVector2f(0.f, 0.f), Top));
		Stops.Add(FSlateGradientStop(FVector2f(0.f, Size.Y * 0.55f), Mid));
		Stops.Add(FSlateGradientStop(FVector2f(0.f, Size.Y), Bottom));
		FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(), MoveTemp(Stops), Orient_Horizontal);
	}

	/** A sagging string of lights across the top: Sags arcs, Count bulbs, twinkling out of step. */
	void Garland(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float T, int32 Sags, int32 Count,
		const TArray<FLinearColor>& Colors, float Bulb)
	{
		const FVector2f Size = G.GetLocalSize();
		const float Top = FMath::Min(Size.Y * 0.06f, 14.f);
		const float Sag = FMath::Min(Size.Y * 0.16f, 46.f);
		TArray<FVector2f> Wire;
		const int32 Steps = 64;
		auto At = [&](float U)
		{
			const float Local = FMath::Fmod(U * Sags, 1.f);
			return FVector2f(U * Size.X, Top + Sag * 4.f * Local * (1.f - Local));
		};
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			Wire.Add(At(float(Step) / Steps));
		}
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Wire, ESlateDrawEffect::None,
			FLinearColor(0.02f, 0.02f, 0.02f, 0.65f), true, 1.5f);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float U = (Index + 0.5f) / Count;
			const float Twinkle = 0.55f + 0.45f * FMath::Sin(T * 3.1f + Index * 1.7f);
			Glow(Out, Layer + 1, G, At(U) + FVector2f(0.f, Bulb * 0.6f), Bulb, Colors[Index % Colors.Num()], Twinkle);
		}
	}
}

using namespace CampusFestive;

void SCampusFestiveArt::Construct(const FArguments& InArgs)
{
	const FString Theme = InArgs._Theme.ToLower();
	Kind = Theme == TEXT("diwali") ? EKind::Diwali : (Theme == TEXT("christmas") ? EKind::Christmas : EKind::Generic);
	Intensity = FMath::Clamp(InArgs._Intensity, 0.2f, 1.f);
	SetVisibility(EVisibility::HitTestInvisible);
}

FLinearColor SCampusFestiveArt::AccentFor(const FString& Theme)
{
	const FString Lower = Theme.ToLower();
	if (Lower == TEXT("diwali"))
	{
		return FLinearColor(1.f, 0.66f, 0.12f, 1.f);   // diya gold
	}
	if (Lower == TEXT("christmas"))
	{
		return FLinearColor(0.92f, 0.20f, 0.22f, 1.f); // holly red
	}
	return FLinearColor(1.f, 0.72f, 0.12f, 1.f);
}

int32 SCampusFestiveArt::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 8.f || Size.Y < 8.f)
	{
		return LayerId;
	}
	// Time from the app, wrapped so floats keep their precision over a long session.
	const float T = float(FMath::Fmod(Args.GetCurrentTime(), 3600.0));
	switch (Kind)
	{
	case EKind::Diwali:    return PaintDiwali(OutDrawElements, LayerId, AllottedGeometry, T);
	case EKind::Christmas: return PaintChristmas(OutDrawElements, LayerId, AllottedGeometry, T);
	default:               return PaintGeneric(OutDrawElements, LayerId, AllottedGeometry, T);
	}
}

int32 SCampusFestiveArt::PaintDiwali(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float T) const
{
	const FVector2f Size = G.GetLocalSize();
	// Festival magenta: deep purple above, magenta through the middle, a pink glow where the lamps are.
	Sky(Out, Layer, G, FLinearColor(0.09f, 0.02f, 0.20f, 1.f), FLinearColor(0.34f, 0.03f, 0.32f, 1.f),
		FLinearColor(0.50f, 0.08f, 0.28f, 1.f));

	// Soft bokeh drifting slowly: pink and gold, very faint, behind everything else.
	const int32 Bokeh = FMath::RoundToInt(10 * Intensity);
	for (int32 Index = 0; Index < Bokeh; ++Index)
	{
		const float Drift = FMath::Sin(T * 0.25f + Index * 1.3f) * 14.f;
		const FVector2f At(Hash(Index, 31) * Size.X + Drift, Hash(Index, 32) * Size.Y * 0.8f);
		const FLinearColor Tone = Hash(Index, 33) > 0.5f ? FLinearColor(1.f, 0.45f, 0.75f) : FLinearColor(1.f, 0.8f, 0.35f);
		Glow(Out, Layer + 1, G, At, 6.f + Hash(Index, 34) * 10.f, Tone, 0.35f);
	}

	// A few stars in the night half.
	const int32 Stars = FMath::RoundToInt(18 * Intensity);
	for (int32 Index = 0; Index < Stars; ++Index)
	{
		const FVector2f At(Hash(Index, 1) * Size.X, Hash(Index, 2) * Size.Y * 0.45f);
		const float Blink = 0.35f + 0.65f * FMath::Abs(FMath::Sin(T * (0.6f + Hash(Index, 3)) + Index));
		Diamond(Out, Layer + 1, G, At, 2.f + Hash(Index, 4) * 1.5f, FLinearColor(1.f, 0.92f, 0.75f, 0.55f * Blink));
	}

	// The garland: marigold orange, gold and pink.
	static const TArray<FLinearColor> Marigold = {
		FLinearColor(1.f, 0.55f, 0.08f), FLinearColor(1.f, 0.82f, 0.25f), FLinearColor(1.f, 0.35f, 0.55f) };
	Garland(Out, Layer + 2, G, T, 3, FMath::RoundToInt(24 * Intensity), Marigold, FMath::Clamp(Size.Y * 0.035f, 3.f, 7.f));

	// The diyas: a clay bowl, a flame that flickers, and the glow it throws.
	const int32 Lamps = Size.X > 600.f ? 7 : 5;
	const float Bowl = FMath::Clamp(Size.Y * 0.11f, 10.f, 30.f);
	const float Base = Size.Y - Bowl * 0.9f;
	for (int32 Index = 0; Index < Lamps; ++Index)
	{
		const float X = Size.X * (Index + 0.5f) / Lamps;
		const float Flicker = 0.85f + 0.15f * FMath::Sin(T * 9.f + Index * 2.3f) + 0.06f * FMath::Sin(T * 23.f + Index);
		Glow(Out, Layer + 3, G, FVector2f(X, Base - Bowl * 0.9f), Bowl * 1.1f * Flicker, FLinearColor(1.f, 0.6f, 0.1f), 0.9f);
		Box(Out, Layer + 4, G, FVector2f(X, Base), Bowl * 1.6f, Bowl * 0.45f, FLinearColor(0.55f, 0.20f, 0.06f, 1.f));
		Box(Out, Layer + 4, G, FVector2f(X, Base + Bowl * 0.24f), Bowl * 1.0f, Bowl * 0.22f, FLinearColor(0.40f, 0.13f, 0.04f, 1.f));
		Diamond(Out, Layer + 5, G, FVector2f(X, Base - Bowl * 0.62f), Bowl * 0.52f * Flicker, FLinearColor(1.f, 0.72f, 0.18f, 1.f));
		Diamond(Out, Layer + 6, G, FVector2f(X, Base - Bowl * 0.55f), Bowl * 0.24f * Flicker, FLinearColor(1.f, 0.97f, 0.85f, 1.f));
	}

	// Sparks rising off the lamps, drifting, fading out as they climb.
	const int32 Sparks = FMath::RoundToInt(30 * Intensity);
	for (int32 Index = 0; Index < Sparks; ++Index)
	{
		const float Speed = 0.10f + Hash(Index, 5) * 0.18f;
		const float Life = FMath::Fmod(T * Speed + Hash(Index, 6), 1.f);
		const float X = Hash(Index, 7) * Size.X + FMath::Sin(T * 1.3f + Index) * 10.f;
		const float Y = Base - Life * Size.Y * 0.9f;
		const float Alpha = FMath::Sin(Life * UE_PI);
		Diamond(Out, Layer + 6, G, FVector2f(X, Y), 2.f + Hash(Index, 8) * 2.5f, FLinearColor(1.f, 0.8f, 0.3f, 0.85f * Alpha));
	}
	return Layer + 7;
}

int32 SCampusFestiveArt::PaintChristmas(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float T) const
{
	const FVector2f Size = G.GetLocalSize();
	Sky(Out, Layer, G, FLinearColor(0.015f, 0.03f, 0.10f, 1.f), FLinearColor(0.02f, 0.08f, 0.12f, 1.f),
		FLinearColor(0.02f, 0.16f, 0.09f, 1.f));

	// The star, top right, breathing.
	const float Pulse = 0.8f + 0.2f * FMath::Sin(T * 2.f);
	const FVector2f StarAt(Size.X * 0.86f, Size.Y * 0.30f);
	const float Star = FMath::Clamp(Size.Y * 0.10f, 8.f, 26.f) * Pulse;
	Glow(Out, Layer + 1, G, StarAt, Star, FLinearColor(1.f, 0.88f, 0.45f), 1.f);
	Box(Out, Layer + 2, G, StarAt, Star * 0.35f, Star * 2.6f, FLinearColor(1.f, 0.95f, 0.75f, 0.85f));
	Box(Out, Layer + 2, G, StarAt, Star * 2.6f, Star * 0.35f, FLinearColor(1.f, 0.95f, 0.75f, 0.85f));

	// Lights: red, green, gold, blue.
	static const TArray<FLinearColor> Lights = {
		FLinearColor(0.95f, 0.18f, 0.20f), FLinearColor(0.20f, 0.85f, 0.35f), FLinearColor(1.f, 0.80f, 0.25f),
		FLinearColor(0.35f, 0.65f, 1.f) };
	Garland(Out, Layer + 3, G, T, 2, FMath::RoundToInt(22 * Intensity), Lights, FMath::Clamp(Size.Y * 0.035f, 3.f, 7.f));

	// Snow on the ground.
	Box(Out, Layer + 3, G, FVector2f(Size.X * 0.5f, Size.Y - Size.Y * 0.04f), Size.X, Size.Y * 0.08f,
		FLinearColor(0.88f, 0.94f, 1.f, 0.85f));

	// Falling snow: near flakes are six-armed and drift, far ones are dots.
	const int32 Flakes = FMath::RoundToInt(46 * Intensity);
	for (int32 Index = 0; Index < Flakes; ++Index)
	{
		const bool bNear = Hash(Index, 11) > 0.72f;
		const float Speed = bNear ? 0.16f + Hash(Index, 12) * 0.08f : 0.07f + Hash(Index, 12) * 0.05f;
		const float Fall = FMath::Fmod(T * Speed + Hash(Index, 13), 1.f);
		const float X = FMath::Fmod(Hash(Index, 14) * Size.X + FMath::Sin(T * 0.9f + Index) * (bNear ? 18.f : 8.f) + Size.X, Size.X);
		const float Y = -8.f + Fall * (Size.Y + 16.f);
		if (!bNear)
		{
			Diamond(Out, Layer + 4, G, FVector2f(X, Y), 2.f + Hash(Index, 15) * 1.5f, FLinearColor(1.f, 1.f, 1.f, 0.7f));
			continue;
		}
		const float Arm = 3.5f + Hash(Index, 16) * 3.5f;
		const float Spin = T * 0.8f + Index;
		for (int32 Spoke = 0; Spoke < 3; ++Spoke)
		{
			const float Angle = Spin + Spoke * UE_PI / 3.f;
			const FVector2f D(FMath::Cos(Angle) * Arm, FMath::Sin(Angle) * Arm);
			TArray<FVector2f> Line = { FVector2f(X, Y) - D, FVector2f(X, Y) + D };
			FSlateDrawElement::MakeLines(Out, Layer + 5, G.ToPaintGeometry(), Line, ESlateDrawEffect::None,
				FLinearColor(1.f, 1.f, 1.f, 0.9f), true, 1.4f);
		}
	}
	return Layer + 6;
}

int32 SCampusFestiveArt::PaintGeneric(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float T) const
{
	const FVector2f Size = G.GetLocalSize();
	Sky(Out, Layer, G, FLinearColor(0.04f, 0.04f, 0.10f, 1.f), FLinearColor(0.16f, 0.06f, 0.16f, 1.f),
		FLinearColor(0.30f, 0.14f, 0.06f, 1.f));
	const int32 Sparkles = FMath::RoundToInt(28 * Intensity);
	for (int32 Index = 0; Index < Sparkles; ++Index)
	{
		const float Drift = FMath::Fmod(T * (0.03f + Hash(Index, 21) * 0.05f) + Hash(Index, 22), 1.f);
		const FVector2f At(Hash(Index, 23) * Size.X, (1.f - Drift) * Size.Y);
		Diamond(Out, Layer + 1, G, At, 2.f + Hash(Index, 24) * 3.f, FLinearColor(1.f, 0.85f, 0.4f, 0.8f * FMath::Sin(Drift * UE_PI)));
	}
	return Layer + 2;
}
