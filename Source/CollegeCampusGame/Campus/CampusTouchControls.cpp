#include "Campus/CampusTouchControls.h"

#include "Campus/CampusCharacter.h"
#include "Campus/CampusMatchState.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace TouchStyle
{
	// Colours from hud.css.
	const FLinearColor Icon(FColor(70, 70, 70, 225));
	const FLinearColor IconOn(FColor(0xF0, 0xA0, 0x20));
	const FLinearColor FireBase(FColor(150, 150, 150, 204));
	const FLinearColor FirePressed(FColor(110, 110, 110, 230));
	const FLinearColor FireDisc(FColor(255, 255, 255, 235));
	const FLinearColor FireIcon(FColor(0x55, 0x55, 0x55));
	const FLinearColor ReloadDisc(FColor(255, 255, 255, 235));
	const FLinearColor ReloadRing(FColor(0xFF, 0xC8, 0x1E));
	const FLinearColor ButtonBack(FColor(255, 255, 255, 150));
	const FLinearColor JoyBase(FColor(70, 70, 70, 191));
	const FLinearColor JoyRing(FColor(255, 255, 255, 217));
	const FLinearColor JoyKnob(FColor(0xC4, 0xC4, 0xC4));
	const FLinearColor JoyKnobBorder(FColor(0xEC, 0xEC, 0xEC));
	const FLinearColor PanelDark(FColor(15, 18, 24, 178));
	const FLinearColor Minimap(FColor(0x1D, 0x3B, 0x4F, 230));
	const FLinearColor Red(FColor(0xE5, 0x3E, 0x3E));
	const FLinearColor Blue(FColor(0x3E, 0x8E, 0xE5));
	const FLinearColor LagOrange(FColor(0xFF, 0x8A, 0x3D));
	const FLinearColor Crosshair(FColor(90, 90, 90, 230));

	// hud.js look tuning: 0.0045 rad per CSS px; a phone's short side is ~400 CSS px.
	constexpr float LookRadPerCssPx = 0.0045f;
	constexpr float CssShortSide = 400.f;
	constexpr float Sensitivity = 1.f;
	constexpr float FireSensitivity = 1.f;
	constexpr float JoyReach = 1.6f;
	constexpr float EdgeTurnRadPerSec = 2.4f;
}

SCampusTouchControls::SCampusTouchControls()
{
}

const FSlateBrush* SCampusTouchControls::RoundBrush(float Radius, float OutlineWidth, const FLinearColor& OutlineColor) const
{
	const uint32 R = uint32(FMath::Max(1, FMath::RoundToInt(Radius)));
	const uint32 O = uint32(FMath::Max(0, FMath::RoundToInt(OutlineWidth)));
	const uint64 Key = (uint64(R) << 40) | (uint64(O & 0xFFFF) << 24) | (OutlineColor.ToFColor(true).DWColor() & 0xFFFFFF);
	if (const TUniquePtr<FSlateRoundedBoxBrush>* Found = BrushCache.Find(Key))
	{
		return Found->Get();
	}
	FSlateRoundedBoxBrush* Brush = O == 0
		? new FSlateRoundedBoxBrush(FLinearColor::White, float(R))
		: new FSlateRoundedBoxBrush(FLinearColor::Transparent, float(R), OutlineColor, float(O));
	BrushCache.Add(Key, TUniquePtr<FSlateRoundedBoxBrush>(Brush));
	return Brush;
}

void SCampusTouchControls::DrawPill(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, const FVector2D& Size, const FLinearColor& Color) const
{
	const float Radius = FMath::RoundToFloat(FMath::Min(14.f, FMath::Min(Size.X, Size.Y) * 0.5f));
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Min))), RoundBrush(Radius), ESlateDrawEffect::None, Color);
}

void SCampusTouchControls::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	bShowButtons = InArgs._ShowButtons;
	WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	SetVisibility(EVisibility::HitTestInvisible);
}

void SCampusTouchControls::SetActive(bool bInActive)
{
	if (bActive == bInActive)
	{
		return;
	}
	bActive = bInActive;
	if (!bActive)
	{
		ReleaseAll();
	}
	SetVisibility(bActive && bShowButtons ? EVisibility::Visible : EVisibility::HitTestInvisible);
}

ACampusCharacter* SCampusTouchControls::GetCharacter() const
{
	const APlayerController* PC = Controller.Get();
	return PC ? Cast<ACampusCharacter>(PC->GetPawn()) : nullptr;
}

float SCampusTouchControls::DegreesPerUnit() const
{
	const float ShortSide = FMath::Max(1.f, FMath::Min(CachedLayout.W, CachedLayout.H));
	return FMath::RadiansToDegrees(TouchStyle::LookRadPerCssPx) * TouchStyle::CssShortSide / ShortSide;
}

// ---------------------------------------------------------------------------------------------
// Layout (hud.css positions; CSS right/top offsets are converted to centres)

SCampusTouchControls::FLayout SCampusTouchControls::MakeLayout(const FVector2D& Size) const
{
	FLayout L;
	L.W = Size.X;
	L.H = Size.Y;
	L.U = FMath::Min(L.W * 0.01f, L.H * 0.018f);
	const float U = L.U;

	// A control with CSS left/top (or right/top) and a diameter, as a centre.
	auto FromLeft = [&L](float LeftPct, float TopPct, float Diameter) { return FVector2D(L.W * LeftPct + Diameter * 0.5f, L.H * TopPct + Diameter * 0.5f); };
	auto FromRight = [&L](float RightPx, float TopPct, float Diameter) { return FVector2D(L.W - RightPx - Diameter * 0.5f, L.H * TopPct + Diameter * 0.5f); };

	L.JoyR = 8.f * U;
	L.JoyHome = FromLeft(0.115f, 0.54f, 16.f * U);
	L.JoyZoneMin = FVector2D(0.f, L.H * 0.40f);
	L.JoyZoneMax = FVector2D(L.W * 0.42f, L.H);

	L.FireR = 10.f * U;
	L.Fire = FromRight(L.W * 0.124f, 0.55f, 20.f * U);
	L.FireLeftR = 1.8f * U;
	L.FireLeft = FromLeft(0.225f, 0.25f, 3.6f * U);
	L.ScopeR = 4.f * U;
	L.Scope = FromRight(L.W * 0.17f, 0.35f, 8.f * U);
	L.ZoomSize = FVector2D(5.f * U, 3.f * U);
	L.Zoom = FromRight(L.W * 0.255f, 0.37f, 5.f * U);
	L.ReloadR = FMath::Max(3.f * U, 22.f);
	L.Reload = FromRight(L.W * 0.124f + 20.f * U + 4.f, 0.52f, L.ReloadR * 2.f);
	L.JumpR = 3.75f * U;
	L.Jump = FromRight(L.W * 0.056f, 0.55f, 7.5f * U);
	L.CrouchR = 3.75f * U;
	L.Crouch = FromRight(L.W * 0.056f, 0.78f, 7.5f * U);
	L.ProneR = 3.5f * U;
	L.Prone = FromRight(L.W * 0.275f, 0.84f, 7.f * U);
	L.SprintR = 2.5f * U;
	L.Sprint = FromLeft(0.165f, 0.34f, 5.f * U);

	// Weapons panel, top right (26% x 20% in the web HUD; kept compact here).
	L.WeaponSize = FVector2D(L.W * 0.17f, L.H * 0.13f);
	L.WeaponMin = FVector2D(L.W - L.WeaponSize.X - L.W * 0.01f, L.H * 0.12f);
	return L;
}

SCampusTouchControls::EControl SCampusTouchControls::HitTest(const FLayout& L, const FVector2D& P, bool bAiming) const
{
	auto In = [&P](const FVector2D& C, float R) { return FVector2D::DistSquared(P, C) <= R * R; };
	// Slightly generous hit areas for thumbs.
	const float Pad = 1.15f;
	if (In(L.Fire, L.FireR * Pad)) return EControl::Fire;
	if (In(L.FireLeft, L.FireLeftR * 1.6f)) return EControl::FireLeft;
	if (In(L.Reload, L.ReloadR * Pad)) return EControl::Reload;
	if (In(L.Scope, L.ScopeR * Pad)) return EControl::Scope;
	if (bAiming && FMath::Abs(P.X - L.Zoom.X) <= L.ZoomSize.X * 0.6f && FMath::Abs(P.Y - L.Zoom.Y) <= L.ZoomSize.Y * 0.6f) return EControl::Zoom;
	if (In(L.Jump, L.JumpR * Pad)) return EControl::Jump;
	if (In(L.Crouch, L.CrouchR * Pad)) return EControl::Crouch;
	if (In(L.Prone, L.ProneR * Pad)) return EControl::Prone;
	if (In(L.Sprint, L.SprintR * 1.4f)) return EControl::Sprint;
	if (P.X >= L.WeaponMin.X && P.Y >= L.WeaponMin.Y && P.X <= L.WeaponMin.X + L.WeaponSize.X && P.Y <= L.WeaponMin.Y + L.WeaponSize.Y) return EControl::Weapon;
	return EControl::None;
}

// ---------------------------------------------------------------------------------------------
// Input

void SCampusTouchControls::ApplyLook(const FVector2D& Delta, float Multiplier) const
{
	if (ACampusCharacter* Character = GetCharacter())
	{
		const float Scale = DegreesPerUnit() * TouchStyle::Sensitivity * Multiplier * Character->GetLookScale();
		// Finger right turns right; finger up looks up (screen Y grows downward, so negate it).
		Character->AddTouchLook(FVector2D(Delta.X * Scale, -Delta.Y * Scale));
	}
}

void SCampusTouchControls::PressControl(EControl Control)
{
	ACampusCharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	switch (Control)
	{
	case EControl::Fire:
	case EControl::FireLeft:
		Character->SetFireHeld(true);
		break;
	case EControl::Scope:
		Character->SetAiming(!Character->IsAiming());
		break;
	case EControl::Zoom:
		Character->CycleZoom();
		break;
	case EControl::Reload:
		Character->Reload();
		break;
	case EControl::Jump:
		Character->Jump();
		break;
	case EControl::Crouch:
		Character->ToggleCrouch();
		break;
	case EControl::Sprint:
		bAutoRun = !bAutoRun;
		Character->SetSprintHeld(bAutoRun);
		break;
	case EControl::Weapon:
		Character->ToggleArmed();
		break;
	default:
		break; // Prone: no prone animation in the game yet.
	}
}

void SCampusTouchControls::ReleaseControl(EControl Control)
{
	ACampusCharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	if (Control == EControl::Fire || Control == EControl::FireLeft)
	{
		// Keep firing while any finger still holds a fire button.
		bool bStillHeld = false;
		for (const TPair<int32, FFinger>& Pair : Fingers)
		{
			bStillHeld |= Pair.Value.Role == ERole::FireDrag;
		}
		Character->SetFireHeld(bStillHeld);
	}
	else if (Control == EControl::Jump)
	{
		Character->StopJumping();
	}
}

void SCampusTouchControls::ReleaseAll()
{
	Fingers.Reset();
	JoyFinger = INDEX_NONE;
	MoveAxis = FVector2D::ZeroVector;
	bAutoRun = false;
	if (ACampusCharacter* Character = GetCharacter())
	{
		Character->SetTouchMove(FVector2D::ZeroVector);
		Character->SetFireHeld(false);
		Character->SetSprintHeld(false);
		Character->StopJumping();
	}
}

FReply SCampusTouchControls::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	if (!bActive || !bShowButtons)
	{
		return FReply::Unhandled();
	}
	const FVector2D P = FVector2D(MyGeometry.AbsoluteToLocal(TouchEvent.GetScreenSpacePosition()));
	const ACampusCharacter* Character = GetCharacter();
	const FLayout& L = CachedLayout;

	FFinger Finger;
	Finger.Last = P;
	Finger.Control = HitTest(L, P, Character && Character->IsAiming());

	if (Finger.Control == EControl::Fire || Finger.Control == EControl::FireLeft)
	{
		Finger.Role = ERole::FireDrag;
		Finger.Offset = P - (Finger.Control == EControl::Fire ? L.Fire : L.FireLeft);
	}
	else if (Finger.Control != EControl::None)
	{
		Finger.Role = ERole::Button;
	}
	else if (JoyFinger == INDEX_NONE && P.X >= L.JoyZoneMin.X && P.X <= L.JoyZoneMax.X && P.Y >= L.JoyZoneMin.Y
		&& FVector2D::Distance(P, L.JoyHome) <= L.JoyR * TouchStyle::JoyReach)
	{
		// Floating stick: jumps under the thumb, kept on screen.
		Finger.Role = ERole::Joystick;
		JoyFinger = TouchEvent.GetPointerIndex();
		JoyCenter = FVector2D(FMath::Clamp(P.X, L.JoyR + 4.f, L.W - L.JoyR - 4.f), FMath::Clamp(P.Y, L.JoyR + 4.f, L.H - L.JoyR - 4.f));
		JoyKnob = JoyCenter;
		bAutoRun = false;
	}
	else
	{
		Finger.Role = ERole::Look;
	}

	Fingers.Add(TouchEvent.GetPointerIndex(), Finger);
	if (Finger.Role == ERole::FireDrag || Finger.Role == ERole::Button)
	{
		PressControl(Finger.Control);
	}
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SCampusTouchControls::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	FFinger* Finger = Fingers.Find(TouchEvent.GetPointerIndex());
	if (!Finger)
	{
		return FReply::Unhandled();
	}
	const FVector2D P = FVector2D(MyGeometry.AbsoluteToLocal(TouchEvent.GetScreenSpacePosition()));
	const FVector2D Delta = P - Finger->Last;
	Finger->Last = P;
	const FLayout& L = CachedLayout;

	switch (Finger->Role)
	{
	case ERole::Joystick:
	{
		const float MaxTravel = L.JoyR * 0.62f;
		FVector2D Offset = P - JoyCenter;
		const float Length = Offset.Size();
		if (Length > MaxTravel)
		{
			Offset *= MaxTravel / Length;
		}
		JoyKnob = JoyCenter + Offset;
		MoveAxis = FVector2D(Offset.X / MaxTravel, -Offset.Y / MaxTravel);
		// Push to the top edge to sprint (hud.js: move.y > 0.95 and at the rim).
		if (ACampusCharacter* Character = GetCharacter())
		{
			Character->SetSprintHeld(MoveAxis.Y > 0.95f && Length > MaxTravel * 0.98f);
		}
		break;
	}
	case ERole::FireDrag:
	{
		// The fire finger also aims.
		Finger->Offset = P - (Finger->Control == EControl::Fire ? L.Fire : L.FireLeft);
		ApplyLook(Delta, TouchStyle::FireSensitivity);
		break;
	}
	case ERole::Look:
		ApplyLook(Delta, 1.f);
		break;
	default:
		break;
	}
	return FReply::Handled();
}

FReply SCampusTouchControls::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	FFinger Finger;
	if (!Fingers.RemoveAndCopyValue(TouchEvent.GetPointerIndex(), Finger))
	{
		return FReply::Unhandled();
	}
	if (Finger.Role == ERole::Joystick)
	{
		JoyFinger = INDEX_NONE;
		MoveAxis = FVector2D::ZeroVector;
		if (ACampusCharacter* Character = GetCharacter())
		{
			Character->SetSprintHeld(bAutoRun);
		}
	}
	else if (Finger.Role == ERole::FireDrag || Finger.Role == ERole::Button)
	{
		ReleaseControl(Finger.Control);
	}
	return FReply::Handled().ReleaseMouseCapture();
}

void SCampusTouchControls::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	CachedLayout = MakeLayout(FVector2D(AllottedGeometry.GetLocalSize()));
	SpinAngle = FMath::Fmod(SpinAngle + InDeltaTime * UE_TWO_PI / 0.7f, UE_TWO_PI);

	ACampusCharacter* Character = GetCharacter();
	if (!Character)
	{
		LastHealth = LagHealth = 100.f;
		return;
	}

	// Health lag bar catches up 0.6 s after a hit; red vignette for 0.5 s.
	const float Health = Character->GetHealth();
	if (Health < LastHealth)
	{
		DamageFlash = 0.5f;
		LagDelay = 0.6f;
	}
	LastHealth = Health;
	DamageFlash = FMath::Max(0.f, DamageFlash - InDeltaTime);
	if (LagDelay > 0.f)
	{
		LagDelay -= InDeltaTime;
	}
	else
	{
		LagHealth = FMath::FInterpConstantTo(LagHealth, Health, InDeltaTime, 120.f);
	}

	if (!bActive || !bShowButtons)
	{
		return;
	}

	// Joystick slides home when released.
	if (JoyFinger == INDEX_NONE)
	{
		JoyCenter = FMath::Vector2DInterpTo(JoyCenter.IsZero() ? CachedLayout.JoyHome : JoyCenter, CachedLayout.JoyHome, InDeltaTime, 1.f / 0.15f);
		JoyKnob = JoyCenter;
	}
	Character->SetTouchMove(bAutoRun && JoyFinger == INDEX_NONE ? FVector2D(0.f, 1.f) : MoveAxis);

	// Edge hold: a fire finger dragged past the button rim keeps turning the view.
	for (TPair<int32, FFinger>& Pair : Fingers)
	{
		FFinger& Finger = Pair.Value;
		Finger.HeldTime += InDeltaTime;
		if (Finger.Role != ERole::FireDrag)
		{
			continue;
		}
		const float Radius = Finger.Control == EControl::Fire ? CachedLayout.FireR : CachedLayout.FireLeftR;
		const float Length = Finger.Offset.Size();
		if (Length <= Radius)
		{
			continue;
		}
		const float Strength = FMath::Min(1.f, (Length - Radius) / (0.8f * Radius));
		const float Ramp = FMath::Min(1.f, Finger.HeldTime / 0.15f);
		const float Degrees = FMath::RadiansToDegrees(TouchStyle::EdgeTurnRadPerSec) * TouchStyle::Sensitivity * TouchStyle::FireSensitivity
			* Strength * Ramp * InDeltaTime * Character->GetLookScale();
		const FVector2D Dir = Finger.Offset / Length;
		Character->AddTouchLook(FVector2D(Dir.X * Degrees, -Dir.Y * Degrees * 0.6f));
	}
}

// ---------------------------------------------------------------------------------------------
// Drawing

void SCampusTouchControls::DrawCircle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Center, float Radius, const FLinearColor& Color) const
{
	const float R = FMath::Max(1.f, FMath::RoundToFloat(Radius));
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(R * 2.f, R * 2.f), FSlateLayoutTransform(FVector2f(Center - FVector2D(R)))),
		RoundBrush(R), ESlateDrawEffect::None, Color);
}

void SCampusTouchControls::DrawRing(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Center, float Radius, const FLinearColor& Color) const
{
	const float R = FMath::Max(1.f, FMath::RoundToFloat(Radius));
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(R * 2.f, R * 2.f), FSlateLayoutTransform(FVector2f(Center - FVector2D(R)))),
		RoundBrush(R, 2.f, Color), ESlateDrawEffect::None, FLinearColor::Transparent);
}

void SCampusTouchControls::DrawRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, const FVector2D& Size, const FLinearColor& Color) const
{
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Min))), WhiteBrush, ESlateDrawEffect::None, Color);
}

void SCampusTouchControls::DrawText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& Text, const FVector2D& Anchor, int32 Size,
	const FLinearColor& Color, float AlignX, float AlignY) const
{
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", FMath::Max(Size, 6));
	const auto Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
	const FVector2D TextSize(Measured.X, Measured.Y);
	const FVector2D Min = Anchor - FVector2D(TextSize.X * AlignX, TextSize.Y * AlignY);
	FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(Min))), Text, Font, ESlateDrawEffect::None, Color);
}

void SCampusTouchControls::DrawPolyline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness) const
{
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
}

void SCampusTouchControls::DrawIcon(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, EControl Control, const FVector2D& C, float R,
	const FLinearColor& Color, float Spin) const
{
	const float T = FMath::Max(2.f, R * 0.12f);
	auto P = [&C](float X, float Y) { return FVector2f(C.X + X, C.Y + Y); };
	switch (Control)
	{
	case EControl::Fire:
	case EControl::FireLeft:
	{
		// Bullet: rounded tip over a casing.
		const float S = R * 0.55f;
		DrawPolyline(Out, Layer, G, { P(-S * 0.3f, S), P(-S * 0.3f, -S * 0.2f), P(0.f, -S), P(S * 0.3f, -S * 0.2f), P(S * 0.3f, S), P(-S * 0.3f, S) }, Color, T);
		DrawPolyline(Out, Layer, G, { P(-S * 0.3f, S * 0.45f), P(S * 0.3f, S * 0.45f) }, Color, T);
		break;
	}
	case EControl::Scope:
	{
		const float S = R * 0.5f;
		TArray<FVector2f> Ring;
		for (int32 I = 0; I <= 24; ++I)
		{
			const float A = UE_TWO_PI * I / 24.f;
			Ring.Add(P(FMath::Cos(A) * S, FMath::Sin(A) * S));
		}
		DrawPolyline(Out, Layer, G, Ring, Color, T);
		DrawPolyline(Out, Layer, G, { P(-S * 1.3f, 0.f), P(-S * 0.35f, 0.f) }, Color, T);
		DrawPolyline(Out, Layer, G, { P(S * 0.35f, 0.f), P(S * 1.3f, 0.f) }, Color, T);
		DrawPolyline(Out, Layer, G, { P(0.f, -S * 1.3f), P(0.f, -S * 0.35f) }, Color, T);
		DrawPolyline(Out, Layer, G, { P(0.f, S * 0.35f), P(0.f, S * 1.3f) }, Color, T);
		break;
	}
	case EControl::Reload:
	{
		// Circular arrow, spinning while reloading.
		const float S = R * 0.5f;
		TArray<FVector2f> Arc;
		for (int32 I = 0; I <= 20; ++I)
		{
			const float A = Spin + UE_PI * 0.25f + (UE_TWO_PI * 0.8f) * I / 20.f;
			Arc.Add(P(FMath::Cos(A) * S, FMath::Sin(A) * S));
		}
		DrawPolyline(Out, Layer, G, Arc, Color, T);
		const FVector2f Tip = Arc.Last();
		const float A = Spin + UE_PI * 0.25f + UE_TWO_PI * 0.8f;
		const FVector2f Tangent(-FMath::Sin(A), FMath::Cos(A));
		const FVector2f Normal(FMath::Cos(A), FMath::Sin(A));
		DrawPolyline(Out, Layer, G, { Tip - Tangent * S * 0.45f + Normal * S * 0.35f, Tip, Tip - Tangent * S * 0.45f - Normal * S * 0.35f }, Color, T);
		break;
	}
	case EControl::Jump:
	{
		const float S = R * 0.42f;
		DrawPolyline(Out, Layer, G, { P(-S, S * 0.2f), P(0.f, -S * 0.8f), P(S, S * 0.2f) }, Color, T * 1.2f);
		DrawPolyline(Out, Layer, G, { P(-S, S * 0.9f), P(0.f, -S * 0.1f), P(S, S * 0.9f) }, Color, T * 1.2f);
		break;
	}
	case EControl::Crouch:
	{
		const float S = R * 0.42f;
		DrawPolyline(Out, Layer, G, { P(-S, -S * 0.9f), P(0.f, S * 0.1f), P(S, -S * 0.9f) }, Color, T * 1.2f);
		DrawPolyline(Out, Layer, G, { P(-S, S * 0.75f), P(S, S * 0.75f) }, Color, T * 1.2f);
		break;
	}
	case EControl::Prone:
	{
		const float S = R * 0.5f;
		DrawPolyline(Out, Layer, G, { P(-S, S * 0.4f), P(S, S * 0.4f) }, Color, T * 1.4f);
		DrawCircle(Out, Layer, G, FVector2D(C.X - S * 1.05f, C.Y + S * 0.05f), S * 0.22f, Color);
		break;
	}
	case EControl::Sprint:
	{
		const float S = R * 0.4f;
		DrawPolyline(Out, Layer, G, { P(-S, -S), P(0.f, 0.f), P(-S, S) }, Color, T);
		DrawPolyline(Out, Layer, G, { P(0.f, -S), P(S, 0.f), P(0.f, S) }, Color, T);
		break;
	}
	default:
		break;
	}
}

void SCampusTouchControls::DrawControls(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const
{
	const ACampusCharacter* Character = GetCharacter();
	const bool bAiming = Character && Character->IsAiming();
	const bool bScoped = bAiming && Character->GetZoom() >= 2.f && Character->GetAimBlend() > 0.5f;
	auto IsHeld = [this](EControl Control)
	{
		for (const TPair<int32, FFinger>& Pair : Fingers)
		{
			if (Pair.Value.Control == Control)
			{
				return true;
			}
		}
		return false;
	};

	// Joystick: grey base, dotted inner ring, four arrows, light knob.
	const float R = L.JoyR;
	const FVector2D JoyC = JoyCenter.IsZero() ? L.JoyHome : JoyCenter;
	DrawCircle(Out, Layer, G, JoyC, R, TouchStyle::JoyBase);
	for (int32 I = 0; I < 36; I += 2)
	{
		const float A0 = UE_TWO_PI * I / 36.f;
		const float A1 = UE_TWO_PI * (I + 1) / 36.f;
		const float Rr = R * 0.94f;
		DrawPolyline(Out, Layer + 1, G, { FVector2f(JoyC.X + FMath::Cos(A0) * Rr, JoyC.Y + FMath::Sin(A0) * Rr), FVector2f(JoyC.X + FMath::Cos(A1) * Rr, JoyC.Y + FMath::Sin(A1) * Rr) }, TouchStyle::JoyRing, 2.f);
	}
	for (int32 I = 0; I < 4; ++I)
	{
		const float A = UE_HALF_PI * I;
		const FVector2f Dir(FMath::Cos(A), FMath::Sin(A));
		const FVector2f Side(-Dir.Y, Dir.X);
		const FVector2f Tip = FVector2f(JoyC) + Dir * R * 0.82f;
		const float S = R * 0.12f;
		DrawPolyline(Out, Layer + 1, G, { Tip - Dir * S + Side * S, Tip, Tip - Dir * S - Side * S }, FLinearColor::White, 2.f);
	}
	const FVector2D Knob = JoyFinger != INDEX_NONE ? JoyKnob : JoyC;
	const float KnobR = R * 0.32f;
	DrawCircle(Out, Layer + 2, G, Knob, KnobR + 4.f, FLinearColor(FColor(0x88, 0x88, 0x88)));
	DrawCircle(Out, Layer + 2, G, Knob, KnobR + 3.f, TouchStyle::JoyKnobBorder);
	DrawCircle(Out, Layer + 3, G, Knob, KnobR, TouchStyle::JoyKnob);

	// Fire: grey disc, white inner disc that follows the aiming finger.
	const float FireAlpha = bScoped ? 0.7f : 1.f;
	const bool bFireHeld = IsHeld(EControl::Fire);
	FVector2D Inner = L.Fire;
	for (const TPair<int32, FFinger>& Pair : Fingers)
	{
		if (Pair.Value.Control == EControl::Fire)
		{
			FVector2D Offset = Pair.Value.Offset;
			if (Offset.Size() > L.FireR)
			{
				Offset = Offset.GetSafeNormal() * L.FireR;
			}
			Inner = L.Fire + Offset;
		}
	}
	FLinearColor FireColor = bFireHeld ? TouchStyle::FirePressed : TouchStyle::FireBase;
	FireColor.A *= FireAlpha;
	DrawCircle(Out, Layer, G, L.Fire, L.FireR, FireColor);
	const float InnerR = L.FireR * 0.48f * (bFireHeld ? 0.94f : 1.f);
	DrawCircle(Out, Layer + 1, G, Inner, InnerR, TouchStyle::FireDisc.CopyWithNewOpacity(FireAlpha));
	DrawIcon(Out, Layer + 2, G, EControl::Fire, Inner, InnerR, TouchStyle::FireIcon, 0.f);

	// Small left fire button.
	DrawCircle(Out, Layer, G, L.FireLeft, L.FireLeftR, FireColor);
	DrawIcon(Out, Layer + 1, G, EControl::FireLeft, L.FireLeft, L.FireLeftR * 1.6f, FLinearColor::White, 0.f);

	// Reload: white disc, spinning icon and a filling ring while reloading.
	const bool bReloading = Character && Character->IsReloading();
	DrawCircle(Out, Layer, G, L.Reload, L.ReloadR, TouchStyle::ReloadDisc);
	DrawIcon(Out, Layer + 1, G, EControl::Reload, L.Reload, L.ReloadR * (IsHeld(EControl::Reload) ? 0.92f : 1.f), FLinearColor(FColor(0x33, 0x33, 0x33)), bReloading ? SpinAngle : 0.f);
	if (bReloading)
	{
		const float Progress = Character->GetReloadProgress();
		TArray<FVector2f> Arc;
		const int32 Steps = FMath::Max(2, int32(40 * Progress));
		for (int32 I = 0; I <= Steps; ++I)
		{
			const float A = -UE_HALF_PI + UE_TWO_PI * Progress * I / Steps;
			Arc.Add(FVector2f(L.Reload.X + FMath::Cos(A) * (L.ReloadR - 2.f), L.Reload.Y + FMath::Sin(A) * (L.ReloadR - 2.f)));
		}
		DrawPolyline(Out, Layer + 2, G, Arc, TouchStyle::ReloadRing, 4.f);
	}

	// Plain icon buttons on a faint disc; orange when on.
	auto Plain = [&](EControl Control, const FVector2D& C, float Radius, bool bOn, float Opacity)
	{
		DrawCircle(Out, Layer, G, C, Radius, TouchStyle::ButtonBack.CopyWithNewOpacity(TouchStyle::ButtonBack.A * Opacity));
		const FLinearColor Ic = (bOn || IsHeld(Control)) ? TouchStyle::IconOn : TouchStyle::Icon;
		DrawIcon(Out, Layer + 1, G, Control, C, Radius, Ic.CopyWithNewOpacity(Ic.A * Opacity), 0.f);
	};
	Plain(EControl::Scope, L.Scope, L.ScopeR, bAiming, bScoped ? 0.7f : 1.f);
	Plain(EControl::Jump, L.Jump, L.JumpR, false, 1.f);
	Plain(EControl::Crouch, L.Crouch, L.CrouchR, Character && Character->bIsCrouched, 1.f);
	Plain(EControl::Prone, L.Prone, L.ProneR, false, 0.45f);
	Plain(EControl::Sprint, L.Sprint, L.SprintR, bAutoRun || (Character && Character->IsSprinting()), 1.f);

	// Zoom pill, only while aiming.
	if (bAiming && Character)
	{
		DrawPill(Out, Layer, G, L.Zoom - L.ZoomSize * 0.5f, L.ZoomSize, FLinearColor(0.08f, 0.08f, 0.08f, 0.6f));
		DrawText(Out, Layer + 1, G, FString::Printf(TEXT("%dx"), FMath::RoundToInt(Character->GetZoom())), L.Zoom, FMath::RoundToInt(L.U * 1.4f), FLinearColor::White);
	}
}

void SCampusTouchControls::DrawHud(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const
{
	const APlayerController* PC = Controller.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const ACampusCharacter* Character = GetCharacter();
	const ACampusPlayerState* Mine = PC ? PC->GetPlayerState<ACampusPlayerState>() : nullptr;
	const float U = L.U;

	// Score and clock, top centre: blue | time | red.
	if (State)
	{
		const float Remaining = FMath::Max(0.f, State->EndTime - float(State->GetServerWorldTimeSeconds()));
		const int32 Seconds = FMath::CeilToInt(Remaining);
		const FVector2D Top(L.W * 0.5f, 32.f + U);
		const FVector2D BoxSize(26.f * U, 4.4f * U);
		DrawPill(Out, Layer, G, Top - BoxSize * 0.5f, BoxSize, TouchStyle::PanelDark);
		const int32 Font = FMath::RoundToInt(U * 1.9f);
		DrawText(Out, Layer + 1, G, FString::FromInt(State->BlueScore), Top - FVector2D(BoxSize.X * 0.36f, 0.f), Font, TouchStyle::Blue);
		DrawText(Out, Layer + 1, G, FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60), Top, Font, Remaining < 30.f ? TouchStyle::Red : FLinearColor::White);
		DrawText(Out, Layer + 1, G, FString::FromInt(State->RedScore), Top + FVector2D(BoxSize.X * 0.36f, 0.f), Font, TouchStyle::Red);

		// Alive / kill counter, top right.
		int32 Alive = 0;
		for (const APlayerState* Player : State->PlayerArray)
		{
			Alive += Player && Player->GetPawn() ? 1 : 0;
		}
		DrawText(Out, Layer + 1, G, FString::Printf(TEXT("ALIVE %d    KILL %d"), Alive, Mine ? Mine->Kills : 0),
			FVector2D(L.W - L.W * 0.01f, 3.f * U), FMath::RoundToInt(U * 1.4f), FLinearColor::White, 1.f, 0.5f);

		// Kill feed, right side.
		for (int32 Index = 0; Index < State->Feed.Num(); ++Index)
		{
			const FVector2D Anchor(L.W - L.W * 0.01f, L.H * 0.36f + Index * 3.2f * U);
			const int32 FeedFont = FMath::Max(9, FMath::RoundToInt(U * 1.2f));
			const auto Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(State->Feed[Index], FCoreStyle::GetDefaultFontStyle("Bold", FeedFont));
			const FVector2D Pill(Measured.X + 2.f * U, Measured.Y + 0.8f * U);
			DrawPill(Out, Layer, G, Anchor - FVector2D(Pill.X, Pill.Y * 0.5f), Pill, TouchStyle::PanelDark);
			DrawText(Out, Layer + 1, G, State->Feed[Index], Anchor - FVector2D(U, 0.f), FeedFont, FLinearColor::White, 1.f, 0.5f);
		}

		// Minimap, top left: self and teammates.
		const float MapSize = 17.f * U;
		const FVector2D MapMin(L.W * 0.01f, L.W * 0.01f);
		DrawRect(Out, Layer, G, MapMin, FVector2D(MapSize), TouchStyle::Minimap);
		if (Character && Mine)
		{
			const FVector2D MapCenter = MapMin + FVector2D(MapSize * 0.5f);
			const FVector Origin = Character->GetActorLocation();
			const float Yaw = FMath::DegreesToRadians(PC->GetControlRotation().Yaw);
			const float WorldToMap = MapSize * 0.5f / 3000.f;
			for (const APlayerState* Player : State->PlayerArray)
			{
				const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player);
				const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
				if (!Campus || !Pawn || Pawn == Character || Campus->Team != Mine->Team)
				{
					continue;
				}
				// Rotate so "forward" on the map is where the camera looks.
				const FVector Delta = Pawn->GetActorLocation() - Origin;
				const float Fwd = Delta.X * FMath::Cos(Yaw) + Delta.Y * FMath::Sin(Yaw);
				const float Right = -Delta.X * FMath::Sin(Yaw) + Delta.Y * FMath::Cos(Yaw);
				const FVector2D Dot = MapCenter + FVector2D(FMath::Clamp(Right * WorldToMap, -MapSize * 0.47f, MapSize * 0.47f), FMath::Clamp(-Fwd * WorldToMap, -MapSize * 0.47f, MapSize * 0.47f));
				DrawCircle(Out, Layer + 1, G, Dot, U * 0.6f, Campus->Team == ECampusTeam::Red ? TouchStyle::Red : TouchStyle::Blue);
			}
			const float S = U * 0.9f;
			DrawPolyline(Out, Layer + 2, G, { FVector2f(MapCenter.X - S * 0.7f, MapCenter.Y + S * 0.7f), FVector2f(MapCenter.X, MapCenter.Y - S), FVector2f(MapCenter.X + S * 0.7f, MapCenter.Y + S * 0.7f) }, FLinearColor::White, 2.f);
		}
	}

	if (!Character)
	{
		DrawText(Out, Layer + 1, G, TEXT("RESPAWNING..."), FVector2D(L.W * 0.5f, L.H * 0.45f), FMath::RoundToInt(U * 2.4f), FLinearColor::White);
		return;
	}

	// Weapons panel with ammo: big magazine count / reserve; tap to switch to the rifle or fists.
	const bool bArmed = Character->IsArmed();
	DrawPill(Out, Layer, G, L.WeaponMin, L.WeaponSize, TouchStyle::PanelDark.CopyWithNewOpacity(bArmed ? 0.75f : 0.45f));
	DrawText(Out, Layer + 1, G, bArmed ? TEXT("AK") : TEXT("AK  (tap)"), L.WeaponMin + FVector2D(1.2f * U, 1.4f * U), FMath::RoundToInt(U * 1.3f),
		bArmed ? TouchStyle::IconOn : FLinearColor(1.f, 1.f, 1.f, 0.7f), 0.f, 0.5f);
	const int32 Mag = Character->GetAmmoInMag();
	const FVector2D AmmoAnchor = L.WeaponMin + FVector2D(L.WeaponSize.X * 0.55f, L.WeaponSize.Y * 0.62f);
	DrawText(Out, Layer + 1, G, FString::FromInt(Mag), AmmoAnchor, FMath::RoundToInt(U * 2.2f * 1.4f), Mag <= 5 ? TouchStyle::Red : FLinearColor::White, 1.f, 0.5f);
	DrawText(Out, Layer + 1, G, FString::Printf(TEXT("/ %d"), Character->GetAmmoReserve()), AmmoAnchor + FVector2D(0.6f * U, 0.4f * U), FMath::RoundToInt(U * 2.2f * 0.8f), FLinearColor(1.f, 1.f, 1.f, 0.8f), 0.f, 0.5f);

	// Health bar, bottom centre: white fill, orange lag, red when low.
	const float BarWidth = FMath::Min(L.W * 0.34f, 380.f * L.H / 400.f * 0.5f);
	const FVector2D BarMin(L.W * 0.5f - BarWidth * 0.5f, L.H - 3.4f * U);
	const float Max = FMath::Max(1.f, Character->GetMaxHealth());
	const float Health = Character->GetHealth();
	DrawRect(Out, Layer, G, BarMin, FVector2D(BarWidth, 8.f), FLinearColor(0.f, 0.f, 0.f, 0.45f));
	DrawRect(Out, Layer + 1, G, BarMin, FVector2D(BarWidth * FMath::Clamp(LagHealth / Max, 0.f, 1.f), 8.f), TouchStyle::LagOrange);
	DrawRect(Out, Layer + 2, G, BarMin, FVector2D(BarWidth * FMath::Clamp(Health / Max, 0.f, 1.f), 8.f), Health / Max < 0.3f ? TouchStyle::Red : FLinearColor::White);
	DrawText(Out, Layer + 2, G, FString::Printf(TEXT("HP %d/%d"), FMath::RoundToInt(Health), FMath::RoundToInt(Max)), BarMin + FVector2D(0.f, -1.2f * U),
		FMath::RoundToInt(U * 1.2f), FLinearColor::White, 0.f, 0.5f);

	// Crosshair (hidden when scoped), red arms while aiming; gap opens when moving.
	const bool bScoped = Character->IsAiming() && Character->GetZoom() >= 2.f && Character->GetAimBlend() > 0.5f;
	const FVector2D Center(L.W * 0.5f, L.H * 0.5f);
	if (!bScoped && Character->IsArmed())
	{
		const float Moving = Character->GetVelocity().Size2D() > 40.f ? 1.6f : 1.f;
		const float Pose = Character->bIsCrouched ? 0.8f : 1.f;
		const float Gap = FMath::Max(0.5f * U, FMath::Lerp(1.4f * U, 0.5f * U, Character->GetAimBlend()) * Moving * Pose);
		const float Arm = 1.6f * U;
		const float Thick = FMath::Max(2.f, 0.3f * U);
		const FLinearColor Color = Character->IsAiming() ? TouchStyle::Red : TouchStyle::Crosshair;
		DrawRect(Out, Layer, G, Center + FVector2D(Gap, -Thick * 0.5f), FVector2D(Arm, Thick), Color);
		DrawRect(Out, Layer, G, Center + FVector2D(-Gap - Arm, -Thick * 0.5f), FVector2D(Arm, Thick), Color);
		DrawRect(Out, Layer, G, Center + FVector2D(-Thick * 0.5f, Gap), FVector2D(Thick, Arm), Color);
		DrawRect(Out, Layer, G, Center + FVector2D(-Thick * 0.5f, -Gap - Arm), FVector2D(Thick, Arm), Color);
		DrawCircle(Out, Layer, G, Center, Thick * 0.6f, Color);
	}

	// Scope lens: black outside a circle of min(78% w, 78% h), thin crosshair, red centre dot.
	if (bScoped)
	{
		// A ring whose inner edge is the lens and whose outer edge is past the screen corners.
		const float LensR = FMath::RoundToFloat(FMath::Min(L.W, L.H) * 0.39f);
		const float Thickness = FMath::RoundToFloat(FMath::Max(L.W, L.H));
		const float Outer = LensR + Thickness;
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Outer * 2.f, Outer * 2.f), FSlateLayoutTransform(FVector2f(Center - FVector2D(Outer)))),
			RoundBrush(Outer, Thickness, FLinearColor::Black), ESlateDrawEffect::None, FLinearColor::Transparent);
		DrawPolyline(Out, Layer + 1, G, { FVector2f(Center.X - LensR, Center.Y), FVector2f(Center.X + LensR, Center.Y) }, FLinearColor::Black, 1.5f);
		DrawPolyline(Out, Layer + 1, G, { FVector2f(Center.X, Center.Y - LensR), FVector2f(Center.X, Center.Y + LensR) }, FLinearColor::Black, 1.5f);
		DrawCircle(Out, Layer + 2, G, Center, 3.f, TouchStyle::Red);
	}

	// Damage vignette.
	if (DamageFlash > 0.f)
	{
		const float Alpha = DamageFlash / 0.5f * 0.45f;
		const float Edge = L.H * 0.08f;
		const FLinearColor Color = TouchStyle::Red.CopyWithNewOpacity(Alpha);
		DrawRect(Out, Layer + 3, G, FVector2D::ZeroVector, FVector2D(L.W, Edge), Color);
		DrawRect(Out, Layer + 3, G, FVector2D(0.f, L.H - Edge), FVector2D(L.W, Edge), Color);
		DrawRect(Out, Layer + 3, G, FVector2D(0.f, Edge), FVector2D(Edge, L.H - 2.f * Edge), Color);
		DrawRect(Out, Layer + 3, G, FVector2D(L.W - Edge, Edge), FVector2D(Edge, L.H - 2.f * Edge), Color);
	}
}

int32 SCampusTouchControls::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!bActive)
	{
		return LayerId;
	}
	const FLayout L = MakeLayout(FVector2D(AllottedGeometry.GetLocalSize()));
	DrawHud(OutDrawElements, LayerId, AllottedGeometry, L);
	if (bShowButtons)
	{
		DrawControls(OutDrawElements, LayerId + 5, AllottedGeometry, L);
	}
	return LayerId + 10;
}
