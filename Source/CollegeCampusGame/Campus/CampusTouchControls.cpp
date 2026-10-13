#include "Campus/CampusTouchControls.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBRGameMode.h"
#include "Campus/CampusBRPlane.h"
#include "Campus/CampusBRZone.h"
#include "Campus/CampusCharacter.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusVehicle.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Campus/CampusPlayerControllers.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"

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
	/** BATTLE ROYALE: the one friendly colour. A squad is marked; everyone else is simply an enemy and
	 *  gets no colour at all, which is why this is a single value and not a palette of team tints. */
	const FLinearColor Mate(FColor(0x3C, 0xE0, 0x8A));
	const FLinearColor LagOrange(FColor(0xFF, 0x8A, 0x3D));
	const FLinearColor Crosshair(FColor(90, 90, 90, 230));

	// hud.js look tuning: 0.0045 rad per CSS px; a phone's short side is ~400 CSS px.
	constexpr float LookRadPerCssPx = 0.0045f;
	constexpr float CssShortSide = 400.f;
	constexpr float Sensitivity = 1.f;
	constexpr float FireSensitivity = 1.f;
	constexpr float JoyReach = 1.6f;
	constexpr float EdgeTurnRadPerSec = 2.4f;
	// Free-look eye (hud.js / game.js): drag x2.2, keeps turning at 2.7 rad/s past 40% of the ring.
	constexpr float FreeLookDragScale = 2.2f;
	constexpr float FreeLookHoldRadPerSec = 2.7f;
	constexpr float FreeLookDeadZone = 0.4f;
	const FLinearColor EyeBack(FColor(20, 24, 30, 115));
	const FLinearColor EyeRing(FColor(255, 255, 255, 191));

	// Free Fire chrome: the same accent and ink as the lobby menus, so the two never disagree.
	const FLinearColor Accent = CampusAngular::Accent;
	const FLinearColor Chip(0.016f, 0.02f, 0.027f, 0.78f);
	const FLinearColor ChipStrong(0.016f, 0.02f, 0.027f, 0.9f);
	const FLinearColor Hairline(1.f, 1.f, 1.f, 0.16f);
}

SCampusTouchControls::SCampusTouchControls()
{
}

void SCampusTouchControls::LoadIcons()
{
	static const TCHAR* Names[] = { TEXT("fire"), TEXT("scope"), TEXT("jump"), TEXT("crouch"), TEXT("prone"), TEXT("run"),
		TEXT("reload"), TEXT("eye"), TEXT("joystick"), TEXT("joystick_knob"), TEXT("fist"), TEXT("ak"), TEXT("campusmap") };
	for (const TCHAR* Name : Names)
	{
		const FString Path = FString::Printf(TEXT("/Game/CampusUI/Icons/T_Icon_%s.T_Icon_%s"), Name, Name);
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path))
		{
			IconTextures.Emplace(Texture);
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Brush.DrawAs = ESlateBrushDrawType::Image;
			IconBrushes.Add(FName(Name), Brush);
		}
	}
}

const FSlateBrush* SCampusTouchControls::Icon(FName Name) const
{
	return IconBrushes.Find(Name);
}

void SCampusTouchControls::DrawImage(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FSlateBrush* Brush, const FVector2D& Center,
	const FVector2D& Size, const FLinearColor& Tint, float Angle) const
{
	if (!Brush)
	{
		return;
	}
	const FPaintGeometry Paint = G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Center - Size * 0.5f)));
	if (FMath::IsNearlyZero(Angle))
	{
		FSlateDrawElement::MakeBox(Out, Layer, Paint, Brush, ESlateDrawEffect::None, Tint);
	}
	else
	{
		FSlateDrawElement::MakeRotatedBox(Out, Layer, Paint, Brush, ESlateDrawEffect::None, Angle, TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, Tint);
	}
}

void SCampusTouchControls::DrawGlow(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Center, float Radius) const
{
	DrawCircle(Out, Layer, G, Center, Radius * 1.16f, TouchStyle::IconOn.CopyWithNewOpacity(0.18f));
	DrawCircle(Out, Layer, G, Center, Radius * 1.07f, TouchStyle::IconOn.CopyWithNewOpacity(0.35f));
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
	// Kept for the round joystick / zoom art; every HUD *plate* now goes through the angular
	// painters below, so nothing in the Free Fire chrome is rounded any more.
	const float Radius = FMath::RoundToFloat(FMath::Min(14.f, FMath::Min(Size.X, Size.Y) * 0.5f));
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Min))), RoundBrush(Radius), ESlateDrawEffect::None, Color);
}

void SCampusTouchControls::DrawSlantedPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min,
	const FVector2D& Size, float Slant, const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth) const
{
	CampusAngular::DrawSlantedPanel(Out, Layer, G, FVector2f(Min), FVector2f(Size), FCampusShape::Slanted(Slant), Fill, Border, BorderWidth);
}

void SCampusTouchControls::DrawCutCornerBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min,
	const FVector2D& Size, float Cut, const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth) const
{
	CampusAngular::DrawCutCornerBox(Out, Layer, G, FVector2f(Min), FVector2f(Size), Cut, Fill, Border, BorderWidth);
}

void SCampusTouchControls::DrawNotchedBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min,
	const FVector2D& Size, float Cut, const FLinearColor& Fill, const FLinearColor& Border, float BorderWidth) const
{
	CampusAngular::DrawSlantedPanel(Out, Layer, G, FVector2f(Min), FVector2f(Size), FCampusShape::Notched(Cut), Fill, Border, BorderWidth);
}

void SCampusTouchControls::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	bShowButtons = InArgs._ShowButtons;
	WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	LoadIcons();
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
	// ENTER / EXIT sits immediately above Jump, same radius as Crouch; SWITCH SEAT to its left, smaller.
	L.VehicleR = 3.75f * U;
	L.Vehicle = FromRight(L.W * 0.056f, 0.32f, 7.5f * U);
	L.SeatSwitchR = 3.f * U;
	L.SeatSwitch = L.Vehicle - FVector2D(8.5f * U, 0.f);
	L.ProneR = 3.5f * U;
	L.Prone = FromRight(L.W * 0.275f, 0.84f, 7.f * U);
	// Eye: left 21u, top 12.6u, 4.2u across (min 34 CSS px).
	L.EyeR = FMath::Max(2.1f * U, 17.f * L.H / 400.f);
	L.Eye = FVector2D(21.f * U + L.EyeR, 12.6f * U + L.EyeR);
	L.SprintR = 2.5f * U;
	L.Sprint = FromLeft(0.165f, 0.34f, 5.f * U);
	// team voice toggles, top centre-left (left of the score box)
	L.VoiceR = 2.4f * U;
	L.Mic = FVector2D(L.W * 0.5f - 18.f * U, 32.f + 3.2f * U);
	L.Speaker = L.Mic - FVector2D(6.f * U, 0.f);

	// Settings gear, very top-right corner (Free Fire). 48 px minimum so a thumb always lands.
	L.GearR = FMath::Max(2.3f * U, 24.f);
	L.Gear = FVector2D(L.W - L.W * 0.012f - L.GearR, L.W * 0.012f + L.GearR);
	// ALIVE / KILL chips sit to the left of the gear, on the same baseline.
	L.ChipRight = FVector2D(L.Gear.X - L.GearR - 1.2f * U, L.Gear.Y);
	L.ChipTop = L.Gear.Y - 1.9f * U;

	// The roster lives in the scoreboard (tap the timer), so the minimap takes the top-left corner.
	L.MapSize = 17.f * U;
	L.MapMin = FVector2D(L.W * 0.012f, L.W * 0.012f);

	// Weapons panel, top right, pushed below the gear row. It holds both slots: the top half is the gun
	// in hand, the bottom half the one on your back. Tapping a half takes that gun.
	L.WeaponSize = FVector2D(L.W * 0.17f, L.H * 0.13f);
	L.WeaponMin = FVector2D(L.W - L.WeaponSize.X - L.W * 0.012f, L.Gear.Y + L.GearR + 1.4f * U);
	// PICK UP sits above ENTER, in the same column: both are "act on the thing next to you" buttons.
	L.PickupR = 3.4f * U;
	L.Pickup = L.Vehicle - FVector2D(0.f, L.VehicleR + L.PickupR + 1.6f * U);
	return L;
}

SCampusTouchControls::EControl SCampusTouchControls::HitTest(const FLayout& L, const FVector2D& P, bool bAiming) const
{
	const ACampusCharacter* Holder = GetCharacter();
	const bool bArmed = Holder && Holder->IsArmed();
	const bool bSeated = Holder && Holder->IsSeated();
	const bool bDriving = Holder && Holder->IsDriving();
	ECampusSeat NearSeat = ECampusSeat::None;
	const bool bCarNear = Holder && Holder->FindNearbyVehicle(NearSeat) != nullptr;
	auto In = [&P](const FVector2D& C, float R) { return FVector2D::DistSquared(P, C) <= R * R; };
	// Slightly generous hit areas for thumbs.
	const float Pad = 1.15f;
	// The gear is checked first: it must stay reachable even if a control creeps near the corner.
	if (In(L.Gear, L.GearR * 1.25f)) return EControl::Settings;
	// Only hit-testable when drawn: ENTER near a car, EXIT / SWITCH SEAT while seated.
	if ((bCarNear || bSeated) && In(L.Vehicle, L.VehicleR * Pad)) return EControl::Vehicle;
	if (bSeated && In(L.SeatSwitch, L.SeatSwitchR * Pad)) return EControl::SeatSwitch;
	if (In(L.Fire, L.FireR * Pad)) return EControl::Fire; // BRAKE while driving (see PressControl)
	if (!bDriving && bLeftFireButton && In(L.FireLeft, L.FireLeftR * 1.6f)) return EControl::FireLeft;
	if (!bDriving && In(L.Reload, L.ReloadR * Pad)) return EControl::Reload;
	if (!bDriving && bArmed && In(L.Scope, L.ScopeR * Pad)) return EControl::Scope;
	if (!bDriving && bAiming && FMath::Abs(P.X - L.Zoom.X) <= L.ZoomSize.X * 0.6f && FMath::Abs(P.Y - L.Zoom.Y) <= L.ZoomSize.Y * 0.6f) return EControl::Zoom;
	if (In(L.Jump, L.JumpR * Pad)) return EControl::Jump;
	if (In(L.Crouch, L.CrouchR * Pad)) return EControl::Crouch;
	if (In(L.Prone, L.ProneR * Pad)) return EControl::Prone;
	if (In(L.Sprint, L.SprintR * 1.4f)) return EControl::Sprint;
	if (In(L.Eye, L.EyeR * 1.4f)) return EControl::Eye;
	if (In(L.Mic, L.VoiceR * 1.4f)) return EControl::Mic;
	if (In(L.Speaker, L.VoiceR * 1.4f)) return EControl::Speaker;
	// PICK UP only exists while a box is in reach, like ENTER with a car.
	if (Holder && !bSeated && Holder->FindNearbyPickup() && In(L.Pickup, L.PickupR * Pad)) return EControl::Pickup;
	if (P.X >= L.WeaponMin.X && P.Y >= L.WeaponMin.Y && P.X <= L.WeaponMin.X + L.WeaponSize.X && P.Y <= L.WeaponMin.Y + L.WeaponSize.Y)
	{
		// Top half: the gun in hand. Bottom half: the second slot.
		return P.Y <= L.WeaponMin.Y + L.WeaponSize.Y * 0.58f ? EControl::Weapon : EControl::Weapon2;
	}
	return EControl::None;
}

// ---------------------------------------------------------------------------------------------
// Input

void SCampusTouchControls::ApplyLook(const FVector2D& Delta, float Multiplier) const
{
	if (ACampusCharacter* Character = GetCharacter())
	{
		// LookSensitivity comes from the settings gear; cached in Tick, never read per event.
		const float Scale = DegreesPerUnit() * TouchStyle::Sensitivity * LookSensitivity * Multiplier * Character->GetLookScale();
		// Finger right turns right; finger up looks up (screen Y grows downward, so negate it).
		Character->AddTouchLook(FVector2D(Delta.X * Scale, -Delta.Y * Scale));
	}
}

void SCampusTouchControls::PressControl(EControl Control)
{
	// The gear works even while respawning (no pawn), which is exactly when players reach for it.
	if (Control == EControl::Settings)
	{
		if (ACampusMatchPlayerController* Match = Cast<ACampusMatchPlayerController>(Controller.Get()))
		{
			Match->ToggleSettings();
		}
		return;
	}

	ACampusCharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	switch (Control)
	{
	case EControl::Fire:
	case EControl::FireLeft:
		// The driver's Fire slot is the brake: a driver never fires.
		Character->IsDriving() ? Character->SetBrakeHeld(true) : Character->SetFireHeld(true);
		break;
	case EControl::Vehicle:
		Character->VehiclePressed();
		break;
	case EControl::SeatSwitch:
		Character->VehicleLongPressed();
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
		Character->JumpPressed();
		break;
	case EControl::Crouch:
		Character->ToggleCrouch();
		break;
	case EControl::Sprint:
		if (Character->IsDriving())
		{
			Character->SetSprintHeld(true); // at the wheel the same button is a held handbrake
			break;
		}
		bAutoRun = !bAutoRun;
		Character->SetSprintHeld(bAutoRun);
		break;
	case EControl::Weapon:
	case EControl::Weapon2:
	{
		// Tapping the plate you already hold arms / unarms you (the old behaviour); tapping the other
		// plate switches to that gun.
		const int32 Slot = Control == EControl::Weapon ? 0 : 1;
		if (Slot == Character->GetCurrentSlot() || !Character->HasWeapon(Slot))
		{
			Character->ToggleArmed();
		}
		else
		{
			Character->SwitchWeapon(Slot);
		}
		break;
	}
	case EControl::Pickup:
		Character->PickupPressed();
		break;
	case EControl::Mic:
		if (ACampusMatchPlayerController* Voice = Cast<ACampusMatchPlayerController>(Controller.Get()))
		{
			Voice->SetMicOn(!Voice->IsMicOn());
		}
		break;
	case EControl::Speaker:
		if (ACampusMatchPlayerController* Voice = Cast<ACampusMatchPlayerController>(Controller.Get()))
		{
			Voice->SetSpeakerOn(!Voice->IsSpeakerOn());
		}
		break;
	case EControl::Prone:
		bAutoRun = false;
		Character->SetSprintHeld(false);
		Character->ToggleProne();
		break;
	default:
		break;
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
		// Keep firing (or braking) while any finger still holds a fire button.
		bool bStillHeld = false;
		for (const TPair<int32, FFinger>& Pair : Fingers)
		{
			bStillHeld |= Pair.Value.Role == ERole::FireDrag;
		}
		if (Character->IsDriving())
		{
			Character->SetBrakeHeld(bStillHeld);
		}
		else
		{
			Character->SetFireHeld(bStillHeld);
		}
	}
	else if (Control == EControl::Jump)
	{
		Character->StopJumping();
	}
	else if (Control == EControl::Sprint && Character->IsDriving())
	{
		Character->SetSprintHeld(false); // handbrake released
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
		Character->SetBrakeHeld(false);
		Character->SetFreeLookHeld(false);
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

	// an open full map / scoreboard: any tap closes it
	if (bBigMap || bScoreboard)
	{
		bBigMap = false;
		bScoreboard = false;
		return FReply::Handled();
	}
	// tap the minimap: full map; tap the timer: scoreboard
	{
		if (bShowMinimap && P.X >= L.MapMin.X && P.Y >= L.MapMin.Y && P.X <= L.MapMin.X + L.MapSize && P.Y <= L.MapMin.Y + L.MapSize)
		{
			bBigMap = true;
			return FReply::Handled();
		}
		const FVector2D Top(L.W * 0.5f, 32.f + L.U);
		const FVector2D Box(26.f * L.U, 4.4f * L.U);
		if (FMath::Abs(P.X - Top.X) <= Box.X * 0.5f && FMath::Abs(P.Y - Top.Y) <= Box.Y * 0.6f)
		{
			bScoreboard = true;
			return FReply::Handled();
		}
	}

	FFinger Finger;
	Finger.Last = P;
	Finger.Control = HitTest(L, P, Character && Character->IsAiming());

	if (Finger.Control == EControl::Fire || Finger.Control == EControl::FireLeft)
	{
		Finger.Role = ERole::FireDrag;
		Finger.Offset = P - (Finger.Control == EControl::Fire ? L.Fire : L.FireLeft);
	}
	else if (Finger.Control == EControl::Eye)
	{
		Finger.Role = ERole::FreeLook;
		Finger.Offset = FVector2D::ZeroVector;
		if (ACampusCharacter* Looker = GetCharacter())
		{
			Looker->SetFreeLookHeld(true);
		}
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
		RunRimTime = 0.f;
		bAtRunRim = false;
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
		// Push to the top edge to sprint, and it locks on (keeps running after release, crouched too).
		// Not while driving: there the stick is the throttle.
		if (ACampusCharacter* Character = GetCharacter(); Character && !Character->IsDriving())
		{
			bAtRunRim = MoveAxis.Y > 0.95f && Length > MaxTravel * 0.98f;
			// Steering away from forward while locked turns the lock off.
			if (bAutoRun && MoveAxis.Y < 0.5f && Length > MaxTravel * 0.3f)
			{
				bAutoRun = false;
			}
			Character->SetSprintHeld(bAutoRun || bAtRunRim);
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
	case ERole::FreeLook:
	{
		// Orbit the camera only (x2.2 drag); the knob follows the finger inside the ring.
		Finger->Offset = P - L.Eye;
		if (ACampusCharacter* Character = GetCharacter())
		{
			const float Scale = DegreesPerUnit() * TouchStyle::Sensitivity * TouchStyle::FreeLookDragScale;
			Character->AddFreeLook(Delta.X * Scale, -Delta.Y * Scale);
		}
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
		bAtRunRim = false;
		RunRimTime = 0.f;
		if (ACampusCharacter* Character = GetCharacter())
		{
			Character->SetSprintHeld(bAutoRun);
		}
	}
	else if (Finger.Role == ERole::FireDrag || Finger.Role == ERole::Button)
	{
		ReleaseControl(Finger.Control);
	}
	else if (Finger.Role == ERole::FreeLook)
	{
		if (ACampusCharacter* Character = GetCharacter())
		{
			Character->SetFreeLookHeld(false);
		}
	}
	return FReply::Handled().ReleaseMouseCapture();
}

void SCampusTouchControls::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	CachedLayout = MakeLayout(FVector2D(AllottedGeometry.GetLocalSize()));
	SpinAngle = FMath::Fmod(SpinAngle + InDeltaTime * UE_TWO_PI / 0.7f, UE_TWO_PI);

	// Settings cached once a frame instead of once per look event / draw call.
	if (const ACampusMatchPlayerController* Match = Cast<ACampusMatchPlayerController>(Controller.Get()))
	{
		LookSensitivity = Match->GetLookSensitivity();
		if (const UCampusSave* Save = Match->GetProfile())
		{
			bShowMinimap = Save->bShowMinimap;
			bLeftFireButton = Save->bLeftFireButton;
		}
	}

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
	// Car HP trails the same way, at the car's own scale (600, not 100).
	if (const ACampusVehicle* Car = Character->GetVehicle())
	{
		const float CarHealth = Car->GetHealth();
		LagCarHealth = LagCarHealth < 0.f ? CarHealth : FMath::FInterpConstantTo(LagCarHealth, CarHealth, InDeltaTime, 700.f);
	}
	else
	{
		LagCarHealth = -1.f;
	}

	if (!bActive || !bShowButtons)
	{
		return;
	}

	// At the wheel the stick is throttle and steering, so the run-rim / RUN LOCK machinery is off:
	// holding full throttle for two seconds must not slam the handbrake on.
	if (Character->IsDriving())
	{
		bAutoRun = false;
		bAtRunRim = false;
		RunRimTime = 0.f;
	}
	// Joystick slides home when released.
	if (JoyFinger == INDEX_NONE)
	{
		JoyCenter = FMath::Vector2DInterpTo(JoyCenter.IsZero() ? CachedLayout.JoyHome : JoyCenter, CachedLayout.JoyHome, InDeltaTime, 1.f / 0.15f);
		JoyKnob = JoyCenter;
	}
	if (JoyFinger != INDEX_NONE && bAtRunRim)
	{
		RunRimTime += InDeltaTime;
		if (RunRimTime >= 2.f && !bAutoRun)
		{
			bAutoRun = true;
			Character->SetSprintHeld(true);
		}
	}
	else
	{
		RunRimTime = 0.f;
	}
	Character->SetTouchMove(bAutoRun && JoyFinger == INDEX_NONE ? FVector2D(0.f, 1.f) : MoveAxis);

	// Edge hold: a fire finger dragged past the button rim keeps turning the view.
	for (TPair<int32, FFinger>& Pair : Fingers)
	{
		FFinger& Finger = Pair.Value;
		Finger.HeldTime += InDeltaTime;
		if (Finger.Role == ERole::FreeLook)
		{
			const float Reach = CachedLayout.EyeR * 1.5f; // knob travel: 0.75 x button width
			const float Length = Finger.Offset.Size();
			const float Push = (Length / Reach - TouchStyle::FreeLookDeadZone) / (1.f - TouchStyle::FreeLookDeadZone);
			if (Push > 0.f)
			{
				const float Degrees = FMath::RadiansToDegrees(TouchStyle::FreeLookHoldRadPerSec) * FMath::Min(Push, 1.f) * InDeltaTime;
				const FVector2D Dir = Finger.Offset / Length;
				Character->AddFreeLook(Dir.X * Degrees, -Dir.Y * Degrees);
			}
			continue;
		}
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
	const FLinearColor& Color, float AlignX, float AlignY, bool bTabular) const
{
	// Bold condensed italic everywhere in the HUD, same display face as the lobby menus; upright
	// condensed only where numbers have to line up in a column.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bTabular ? "BoldCondensed" : "BoldCondensedItalic", FMath::Max(Size, 6));
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
	case EControl::Vehicle:
	{
		// A car from the side: cabin line over a body, two wheels under it.
		const float S = R * 0.5f;
		DrawPolyline(Out, Layer, G, { P(-S, S * 0.3f), P(-S, -S * 0.1f), P(-S * 0.45f, -S * 0.55f), P(S * 0.35f, -S * 0.55f),
			P(S, -S * 0.1f), P(S, S * 0.3f), P(-S, S * 0.3f) }, Color, T);
		DrawCircle(Out, Layer, G, FVector2D(C.X - S * 0.5f, C.Y + S * 0.5f), S * 0.22f, Color);
		DrawCircle(Out, Layer, G, FVector2D(C.X + S * 0.5f, C.Y + S * 0.5f), S * 0.22f, Color);
		break;
	}
	case EControl::SeatSwitch:
	{
		// Two arrows trading places.
		const float S = R * 0.45f;
		DrawPolyline(Out, Layer, G, { P(-S, -S * 0.35f), P(S, -S * 0.35f) }, Color, T);
		DrawPolyline(Out, Layer, G, { P(S * 0.4f, -S * 0.8f), P(S, -S * 0.35f), P(S * 0.4f, S * 0.1f) }, Color, T);
		DrawPolyline(Out, Layer, G, { P(S, S * 0.55f), P(-S, S * 0.55f) }, Color, T);
		DrawPolyline(Out, Layer, G, { P(-S * 0.4f, S * 0.1f), P(-S, S * 0.55f), P(-S * 0.4f, S) }, Color, T);
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
	// Driver layout swap: Fire / FireLeft / Scope / Zoom / Reload go away, the Fire slot becomes BRAKE.
	const bool bDriving = Character && Character->IsDriving();
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

	// Joystick: the designer base + knob artwork (three.js #joy.img); drawn fallback otherwise.
	const float R = L.JoyR;
	const FVector2D JoyC = JoyCenter.IsZero() ? L.JoyHome : JoyCenter;
	const FVector2D Knob = JoyFinger != INDEX_NONE ? JoyKnob : JoyC;
	const float KnobR = R * 0.32f;
	if (Icon(TEXT("joystick")) && Icon(TEXT("joystick_knob")))
	{
		DrawImage(Out, Layer, G, Icon(TEXT("joystick")), JoyC, FVector2D(R * 2.f), FLinearColor::White);
		DrawImage(Out, Layer + 3, G, Icon(TEXT("joystick_knob")), Knob, FVector2D(KnobR * 2.f), FLinearColor::White);
	}
	else
	{
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
	DrawCircle(Out, Layer + 2, G, Knob, KnobR + 4.f, FLinearColor(FColor(0x88, 0x88, 0x88)));
	DrawCircle(Out, Layer + 2, G, Knob, KnobR + 3.f, TouchStyle::JoyKnobBorder);
	DrawCircle(Out, Layer + 3, G, Knob, KnobR, TouchStyle::JoyKnob);
	}

	// Run lock: progress arc while holding forward at the rim, then a "RUN LOCK" badge.
	if (bAutoRun || RunRimTime > 0.f)
	{
		const FVector2D Badge = JoyC + FVector2D(0.f, -R * 1.35f);
		if (bAutoRun)
		{
			const FVector2D Size(9.f * L.U, 2.8f * L.U);
			DrawSlantedPanel(Out, Layer + 3, G, Badge - Size * 0.5f, Size, Size.Y * 0.32f, TouchStyle::IconOn.CopyWithNewOpacity(0.92f));
			DrawText(Out, Layer + 4, G, TEXT("RUN LOCK"), Badge, FMath::RoundToInt(L.U * 1.2f), FLinearColor::White);
		}
		else
		{
			const float Progress = FMath::Clamp(RunRimTime / 2.f, 0.f, 1.f);
			TArray<FVector2f> Arc;
			const int32 Steps = FMath::Max(2, int32(32 * Progress));
			for (int32 I = 0; I <= Steps; ++I)
			{
				const float A = -UE_HALF_PI + UE_TWO_PI * Progress * I / Steps;
				Arc.Add(FVector2f(Badge.X + FMath::Cos(A) * L.U * 1.4f, Badge.Y + FMath::Sin(A) * L.U * 1.4f));
			}
			DrawPolyline(Out, Layer + 4, G, Arc, TouchStyle::IconOn, 3.f);
		}
	}

	// Fire: grey disc, white inner disc that follows the aiming finger. While driving the same slot is
	// the brake, so the thumb never has to move and the trigger simply does not exist.
	const float FireAlpha = bScoped ? 0.7f : 1.f;
	const bool bFireHeld = IsHeld(EControl::Fire);
	FLinearColor FireColor = bFireHeld ? TouchStyle::FirePressed : TouchStyle::FireBase;
	FireColor.A *= FireAlpha;
	if (bDriving)
	{
		const FLinearColor Back = bFireHeld ? TouchStyle::IconOn.CopyWithNewOpacity(0.92f) : TouchStyle::ChipStrong;
		DrawNotchedBox(Out, Layer, G, L.Fire - FVector2D(L.FireR), FVector2D(L.FireR * 2.f), L.FireR * 0.4f,
			Back, bFireHeld ? FLinearColor::White : TouchStyle::Hairline, bFireHeld ? 2.f : 1.f);
		DrawText(Out, Layer + 2, G, TEXT("BRAKE"), L.Fire, FMath::RoundToInt(L.U * 1.9f), FLinearColor::White);
	}
	else
	{
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
	DrawCircle(Out, Layer, G, L.Fire, L.FireR, FireColor);
	const float InnerR = L.FireR * 0.48f * (bFireHeld ? 0.94f : 1.f);
	if (const FSlateBrush* FireIcon = Icon(TEXT("fire")))
	{
		DrawImage(Out, Layer + 1, G, FireIcon, Inner, FVector2D(InnerR * 2.f), FLinearColor::White.CopyWithNewOpacity(FireAlpha));
	}
	else
	{
		DrawCircle(Out, Layer + 1, G, Inner, InnerR, TouchStyle::FireDisc.CopyWithNewOpacity(FireAlpha));
		DrawIcon(Out, Layer + 2, G, EControl::Fire, Inner, InnerR, TouchStyle::FireIcon, 0.f);
	}
	}

	// Small left fire button: the same artwork. Switchable in the settings gear.
	if (bLeftFireButton && !bDriving)
	{
		if (const FSlateBrush* FireIcon = Icon(TEXT("fire")))
		{
			DrawImage(Out, Layer, G, FireIcon, L.FireLeft, FVector2D(L.FireLeftR * 2.f) * (IsHeld(EControl::FireLeft) ? 0.94f : 1.f), FLinearColor::White.CopyWithNewOpacity(FireAlpha));
		}
		else
		{
			DrawCircle(Out, Layer, G, L.FireLeft, L.FireLeftR, FireColor);
			DrawIcon(Out, Layer + 1, G, EControl::FireLeft, L.FireLeft, L.FireLeftR * 1.6f, FLinearColor::White, 0.f);
		}
	}

	// Reload: white disc, spinning icon and a filling ring while reloading.
	const bool bReloading = Character && Character->IsReloading();
	if (!bDriving)
	{
	DrawCircle(Out, Layer, G, L.Reload, L.ReloadR, TouchStyle::ReloadDisc);
	const float ReloadScale = IsHeld(EControl::Reload) ? 0.92f : 1.f;
	if (const FSlateBrush* ReloadIcon = Icon(TEXT("reload")))
	{
		DrawImage(Out, Layer + 1, G, ReloadIcon, L.Reload, FVector2D(L.ReloadR * 1.25f * ReloadScale), FLinearColor(FColor(0x33, 0x33, 0x33)), bReloading ? SpinAngle : 0.f);
	}
	else
	{
		DrawIcon(Out, Layer + 1, G, EControl::Reload, L.Reload, L.ReloadR * ReloadScale, FLinearColor(FColor(0x33, 0x33, 0x33)), bReloading ? SpinAngle : 0.f);
	}
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
	}

	// Plain icon buttons on a faint disc; orange when on.
	auto Plain = [&](EControl Control, const FVector2D& C, float Radius, bool bOn, float Opacity)
	{
		const bool bActiveNow = bOn || IsHeld(Control);
		FName IconName = NAME_None;
		switch (Control)
		{
		case EControl::Scope: IconName = TEXT("scope"); break;
		case EControl::Jump: IconName = TEXT("jump"); break;
		case EControl::Crouch: IconName = TEXT("crouch"); break;
		case EControl::Prone: IconName = TEXT("prone"); break;
		case EControl::Sprint: IconName = TEXT("run"); break;
		default: break;
		}
		if (const FSlateBrush* Art = Icon(IconName))
		{
			if (bActiveNow)
			{
				DrawGlow(Out, Layer, G, C, Radius);
			}
			const float Press = IsHeld(Control) ? 0.94f : 1.f;
			DrawImage(Out, Layer + 1, G, Art, C, FVector2D(Radius * 2.f * Press), FLinearColor::White.CopyWithNewOpacity(Opacity));
			return;
		}
		DrawCircle(Out, Layer, G, C, Radius, TouchStyle::ButtonBack.CopyWithNewOpacity(TouchStyle::ButtonBack.A * Opacity));
		const FLinearColor Ic = bActiveNow ? TouchStyle::IconOn : TouchStyle::Icon;
		DrawIcon(Out, Layer + 1, G, Control, C, Radius, Ic.CopyWithNewOpacity(Ic.A * Opacity), 0.f);
	};
	if (Character && Character->IsArmed() && !bDriving)
	{
		Plain(EControl::Scope, L.Scope, L.ScopeR, bAiming, bScoped ? 0.7f : 1.f);
	}
	Plain(EControl::Jump, L.Jump, L.JumpR, false, 1.f);
	Plain(EControl::Crouch, L.Crouch, L.CrouchR, Character && Character->bIsCrouched && !Character->IsProne(), 1.f);
	Plain(EControl::Prone, L.Prone, L.ProneR, Character && Character->IsProne(), 1.f);
	Plain(EControl::Sprint, L.Sprint, L.SprintR, bAutoRun || (Character && Character->IsSprinting()), 1.f);
	if (bDriving)
	{
		// Same button, different job: Sprint is the handbrake at the wheel.
		DrawText(Out, Layer + 2, G, TEXT("HAND"), L.Sprint + FVector2D(0.f, L.SprintR + 1.f * L.U),
			FMath::RoundToInt(L.U * 1.05f), FLinearColor::White);
	}

	// Cars: ENTER appears when one is in reach, EXIT and SWITCH SEAT once you are in it.
	{
		ECampusSeat NearSeat = ECampusSeat::None;
		const bool bSeated = Character && Character->IsSeated();
		const bool bCarNear = Character && Character->FindNearbyVehicle(NearSeat) != nullptr;
		if (bSeated || bCarNear)
		{
			Plain(EControl::Vehicle, L.Vehicle, L.VehicleR, bSeated, 1.f);
			DrawText(Out, Layer + 2, G, bSeated ? TEXT("EXIT") : TEXT("ENTER"),
				L.Vehicle + FVector2D(0.f, L.VehicleR + 1.1f * L.U), FMath::RoundToInt(L.U * 1.15f),
				bSeated ? TouchStyle::IconOn : FLinearColor::White);
		}
		if (bSeated)
		{
			Plain(EControl::SeatSwitch, L.SeatSwitch, L.SeatSwitchR, false, 1.f);
			DrawText(Out, Layer + 2, G, TEXT("SEAT"), L.SeatSwitch + FVector2D(0.f, L.SeatSwitchR + 1.1f * L.U),
				FMath::RoundToInt(L.U * 1.05f), FLinearColor::White);
		}
		// Ground loot: PICK UP with the box's own name on it, so you know what you are taking.
		const ACampusPickup* Near = (Character && !bSeated) ? Character->FindNearbyPickup() : nullptr;
		if (Near)
		{
			DrawCircle(Out, Layer, G, L.Pickup, L.PickupR, TouchStyle::ButtonBack);
			DrawRing(Out, Layer + 1, G, L.Pickup, L.PickupR, TouchStyle::Accent);
			DrawText(Out, Layer + 2, G, TEXT("PICK UP"), L.Pickup, FMath::RoundToInt(L.U * 1.05f), FLinearColor::White);
			DrawText(Out, Layer + 2, G, Near->Label().ToUpper(), L.Pickup + FVector2D(0.f, L.PickupR + 1.1f * L.U),
				FMath::RoundToInt(L.U * 1.15f), TouchStyle::IconOn);
		}
	}

	// team voice: green when on, grey with a slash when off
	if (const ACampusMatchPlayerController* Voice = Cast<ACampusMatchPlayerController>(Controller.Get()))
	{
		auto Toggle = [&](const FVector2D& C, const TCHAR* Label, bool bOn)
		{
			// Notched plates, not discs: the voice toggles match the rest of the angular chrome.
			const FLinearColor Back = bOn ? FLinearColor(0.08f, 0.42f, 0.2f, 0.9f) : FLinearColor(0.02f, 0.025f, 0.035f, 0.72f);
			DrawNotchedBox(Out, Layer, G, C - FVector2D(L.VoiceR), FVector2D(L.VoiceR * 2.f), L.VoiceR * 0.5f,
				Back, bOn ? FLinearColor(1.f, 1.f, 1.f, 0.4f) : TouchStyle::Hairline, 1.f);
			DrawText(Out, Layer + 2, G, Label, C, FMath::RoundToInt(L.U * 1.05f), FLinearColor::White);
			// Colour is never the only signal: the off state also carries a slash.
			if (!bOn)
			{
				const float S = L.VoiceR * 0.7f;
				DrawPolyline(Out, Layer + 3, G, { FVector2f(C.X - S, C.Y + S), FVector2f(C.X + S, C.Y - S) }, TouchStyle::Red, 2.f);
			}
		};
		Toggle(L.Mic, TEXT("MIC"), Voice->IsMicOn());
		Toggle(L.Speaker, TEXT("SPK"), Voice->IsSpeakerOn());
	}

	{
		const bool bEyeHeld = IsHeld(EControl::Eye);
		DrawCircle(Out, Layer, G, L.Eye, L.EyeR, TouchStyle::EyeBack);
		DrawRing(Out, Layer + 1, G, L.Eye, L.EyeR, TouchStyle::EyeRing);
		FVector2D EyeKnob = L.Eye;
		for (const TPair<int32, FFinger>& Pair : Fingers)
		{
			if (Pair.Value.Role == ERole::FreeLook)
			{
				FVector2D Offset = Pair.Value.Offset;
				const float Reach = L.EyeR * 1.5f;
				if (Offset.Size() > Reach)
				{
					Offset = Offset.GetSafeNormal() * Reach;
				}
				EyeKnob = L.Eye + Offset;
			}
		}
		const FLinearColor EyeTint = bEyeHeld ? TouchStyle::IconOn : FLinearColor::White;
		if (const FSlateBrush* EyeIcon = Icon(TEXT("eye")))
		{
			DrawImage(Out, Layer + 2, G, EyeIcon, EyeKnob, FVector2D(L.EyeR * 1.25f), EyeTint);
		}
		else
		{
		// Eye glyph: almond outline and pupil (three.js btn-eye SVG).
		const float EyeSize = L.EyeR * 0.55f;
		TArray<FVector2f> Upper, Lower;
		for (int32 I = 0; I <= 12; ++I)
		{
			const float T = -1.f + 2.f * I / 12.f;
			const float Y = EyeSize * 0.55f * (1.f - T * T);
			Upper.Add(FVector2f(EyeKnob.X + T * EyeSize, EyeKnob.Y - Y));
			Lower.Add(FVector2f(EyeKnob.X + T * EyeSize, EyeKnob.Y + Y));
		}
		const FLinearColor EyeColor = bEyeHeld ? TouchStyle::IconOn : FLinearColor::White;
		DrawPolyline(Out, Layer + 2, G, Upper, EyeColor, 2.f);
		DrawPolyline(Out, Layer + 2, G, Lower, EyeColor, 2.f);
		DrawCircle(Out, Layer + 2, G, EyeKnob, EyeSize * 0.32f, EyeColor);
		}
	}

	// Zoom pill, only while aiming.
	if (bAiming && Character && !bDriving)
	{
		DrawSlantedPanel(Out, Layer, G, L.Zoom - L.ZoomSize * 0.5f, L.ZoomSize, L.ZoomSize.Y * 0.32f, TouchStyle::Chip, TouchStyle::Hairline, 1.f);
		DrawText(Out, Layer + 1, G, FString::Printf(TEXT("%dx"), FMath::RoundToInt(Character->GetZoom())), L.Zoom, FMath::RoundToInt(L.U * 1.4f), FLinearColor::White);
	}
}

void SCampusTouchControls::DrawVehicleHud(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const
{
	const ACampusCharacter* Character = GetCharacter();
	const ACampusVehicle* Car = Character ? Character->GetVehicle() : nullptr;
	if (!Car)
	{
		return;
	}
	const float U = L.U;
	const float BarWidth = FMath::Min(L.W * 0.34f, 380.f * L.H / 400.f * 0.5f);
	const float BarHeight = FMath::Max(8.f, 0.85f * U);
	// Directly under the player's health bar, a little narrower so the two never read as one.
	const FVector2D BarMin(L.W * 0.5f - BarWidth * 0.42f, L.H - 1.75f * U);
	const FVector2D BarSize(BarWidth * 0.84f, BarHeight);
	const float Slant = BarHeight * 0.85f;
	const float Max = FMath::Max(1.f, Car->GetMaxHealth());
	const float Health = Car->GetHealth();
	const float LagFraction = FMath::Clamp((LagCarHealth < 0.f ? Health : LagCarHealth) / Max, 0.f, 1.f);
	const float LiveFraction = FMath::Clamp(Health / Max, 0.f, 1.f);
	DrawSlantedPanel(Out, Layer, G, BarMin, BarSize, Slant, FLinearColor(0.f, 0.f, 0.f, 0.55f), TouchStyle::Hairline, 1.f);
	if (LagFraction > 0.f)
	{
		DrawSlantedPanel(Out, Layer + 1, G, BarMin, FVector2D(BarSize.X * LagFraction, BarSize.Y), Slant * LagFraction, TouchStyle::LagOrange);
	}
	if (LiveFraction > 0.f)
	{
		DrawSlantedPanel(Out, Layer + 2, G, BarMin, FVector2D(BarSize.X * LiveFraction, BarSize.Y), Slant * LiveFraction,
			LiveFraction < 0.3f ? TouchStyle::Red : TouchStyle::Accent);
	}
	// Layout along one line: [seat chip] [car HP bar] [4 wheel pips] [HP number].
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bPopped = Car->GetWheelHealth(Index) == 0;
		const FVector2D Pip(BarMin.X + BarSize.X + (1.1f + Index * 1.1f) * U, BarMin.Y);
		DrawRect(Out, Layer + 3, G, Pip, FVector2D(0.7f * U, BarSize.Y),
			bPopped ? FLinearColor(1.f, 1.f, 1.f, 0.18f) : TouchStyle::Accent);
	}
	DrawText(Out, Layer + 3, G, FString::FromInt(FMath::RoundToInt(Health)),
		FVector2D(BarMin.X + BarSize.X + 6.1f * U, BarMin.Y + BarSize.Y * 0.5f),
		FMath::RoundToInt(U * 1.1f), LiveFraction < 0.3f ? TouchStyle::Red : FLinearColor::White, 0.f, 0.5f);

	// Seat chip: D for the wheel, P1..P3 for the firing seats.
	const ECampusSeat Seat = Character->GetSeat();
	const TCHAR* SeatLabel = Seat == ECampusSeat::Driver ? TEXT("D")
		: (Seat == ECampusSeat::FrontPass ? TEXT("P1") : (Seat == ECampusSeat::RearL ? TEXT("P2") : TEXT("P3")));
	const FVector2D ChipMin(BarMin.X - 3.f * U, BarMin.Y - 0.35f * U);
	const FVector2D ChipSize(2.4f * U, BarSize.Y + 0.7f * U);
	DrawNotchedBox(Out, Layer + 3, G, ChipMin, ChipSize, ChipSize.Y * 0.35f, TouchStyle::ChipStrong, TouchStyle::Hairline, 1.f);
	DrawText(Out, Layer + 4, G, SeatLabel, ChipMin + ChipSize * 0.5f, FMath::RoundToInt(U * 1.2f), TouchStyle::Accent);

	// Burning: three seconds to get out, and the HUD says so in no uncertain terms.
	if (Car->GetBurnSecondsLeft() > 0.f && FMath::Sin(SpinAngle * 3.f) > 0.f)
	{
		DrawText(Out, Layer + 5, G, TEXT("BAIL OUT"), FVector2D(L.W * 0.5f, L.H * 0.3f), FMath::RoundToInt(U * 3.2f), TouchStyle::Red);
	}
}

void SCampusTouchControls::DrawConnection(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const
{
	const APlayerController* PC = Controller.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ACampusPlayerState* Mine = PC ? PC->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!Mine || !World)
	{
		return;
	}
	// The host is the server: it has no ping to itself, so show the link as local instead of "0 ms".
	const bool bClient = World->GetNetMode() == NM_Client;
	const int32 Ping = bClient ? FMath::RoundToInt(Mine->GetPingInMilliseconds()) : 0;
	const FLinearColor Color = !bClient ? FLinearColor(0.3f, 1.f, 0.4f)
		: (Ping < 60 ? FLinearColor(0.3f, 1.f, 0.4f) : Ping < 120 ? FLinearColor(1.f, 0.85f, 0.2f) : TouchStyle::Red);

	const float U = L.U;
	const float Radius = 1.1f * U;
	const FVector2D Center(L.Gear.X - L.GearR - 1.2f * U - Radius, L.Gear.Y + L.GearR * 0.55f + 1.4f * U);
	CampusAngular::DrawGlyph(Out, Layer, G, CampusAngular::EGlyph::Wifi, FVector2f(Center), Radius, Color, FMath::Max(1.5f, U * 0.18f));
	DrawText(Out, Layer, G, bClient ? FString::Printf(TEXT("%d MS"), Ping) : FString(TEXT("HOST")),
		FVector2D(Center.X - Radius - 0.5f * U, Center.Y), FMath::RoundToInt(U * 1.05f), Color, 1.f, 0.5f);
}

void SCampusTouchControls::DrawHud(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const
{
	const APlayerController* PC = Controller.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const ACampusCharacter* Character = GetCharacter();
	const ACampusPlayerState* Mine = PC ? PC->GetPlayerState<ACampusPlayerState>() : nullptr;
	const float U = L.U;

	// The top-centre plate. ROOM: blue | time | red, with the round under it. BATTLE ROYALE: ALIVE and
	// SQUADS, because there is no second team to score against and no round to count.
	const ACampusBRGameState* BattleRoyale = Cast<ACampusBRGameState>(State);
	if (State)
	{
		const float Remaining = FMath::Max(0.f, State->EndTime - float(State->GetServerWorldTimeSeconds()));
		const int32 Seconds = FMath::CeilToInt(Remaining);
		const FVector2D Top(L.W * 0.5f, 32.f + U);
		const FVector2D BoxSize(26.f * U, 4.4f * U);
		const int32 Font = FMath::RoundToInt(U * 1.9f);
		const int32 Small = FMath::RoundToInt(U * 1.05f);
		const FLinearColor Dim(1.f, 1.f, 1.f, 0.65f);
		// Score / clock plate: clipped corners, coloured end flashes.
		DrawCutCornerBox(Out, Layer, G, Top - BoxSize * 0.5f, BoxSize, 1.5f * U, TouchStyle::ChipStrong, TouchStyle::Hairline, 1.f);
		if (BattleRoyale)
		{
			// BOTH flashes are the accent: there is no RED and no BLUE in a battle session, every squad
			// is its own team. The zone clock goes under this plate (DrawBattleRoyaleHud).
			DrawSlantedPanel(Out, Layer + 1, G, Top - BoxSize * 0.5f, FVector2D(0.5f * U, BoxSize.Y), 0.f, TouchStyle::Accent);
			DrawSlantedPanel(Out, Layer + 1, G, Top + FVector2D(BoxSize.X * 0.5f - 0.5f * U, -BoxSize.Y * 0.5f),
				FVector2D(0.5f * U, BoxSize.Y), 0.f, TouchStyle::Accent);
			DrawText(Out, Layer + 2, G, TEXT("ALIVE"), FVector2D(Top.X - BoxSize.X * 0.34f, Top.Y - BoxSize.Y * 0.26f), Small, Dim);
			DrawText(Out, Layer + 2, G, FString::FromInt(BattleRoyale->AliveCombatants),
				FVector2D(Top.X - BoxSize.X * 0.34f, Top.Y + BoxSize.Y * 0.16f), Font, FLinearColor::White);
			DrawText(Out, Layer + 2, G, TEXT("SQUADS"), FVector2D(Top.X + BoxSize.X * 0.34f, Top.Y - BoxSize.Y * 0.26f), Small, Dim);
			DrawText(Out, Layer + 2, G, FString::FromInt(BattleRoyale->AliveSquads),
				FVector2D(Top.X + BoxSize.X * 0.34f, Top.Y + BoxSize.Y * 0.16f), Font, TouchStyle::Accent);
			DrawText(Out, Layer + 2, G, FString::Printf(TEXT("/ %d"), BattleRoyale->TotalCombatants), Top, Small, Dim);
		}
		else
		{
			DrawSlantedPanel(Out, Layer + 1, G, Top - BoxSize * 0.5f, FVector2D(0.5f * U, BoxSize.Y), 0.f, TouchStyle::Blue);
			DrawSlantedPanel(Out, Layer + 1, G, Top + FVector2D(BoxSize.X * 0.5f - 0.5f * U, -BoxSize.Y * 0.5f), FVector2D(0.5f * U, BoxSize.Y), 0.f, TouchStyle::Red);
			DrawText(Out, Layer + 2, G, FString::FromInt(State->BlueScore), Top - FVector2D(BoxSize.X * 0.36f, 0.f), Font, TouchStyle::Blue);
			DrawText(Out, Layer + 2, G, FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60), Top, Font, Remaining < 30.f ? TouchStyle::Red : FLinearColor::White);
			DrawText(Out, Layer + 2, G, FString::FromInt(State->RedScore), Top + FVector2D(BoxSize.X * 0.36f, 0.f), Font, TouchStyle::Red);
			// round wins above, the round under the clock
			const FString RoundText = State->bDecider ? FString(TEXT("DECIDING ROUND"))
				: (State->Phase == ECampusPhase::RoundOver ? FString::Printf(TEXT("ROUND %d DONE  ·  NEXT IN A MOMENT"), State->Round)
				: FString::Printf(TEXT("ROUND %d / %d"), FMath::Max(State->Round, 1), State->Rounds));
			DrawText(Out, Layer + 2, G, RoundText, Top + FVector2D(0.f, BoxSize.Y * 0.85f), FMath::RoundToInt(U * 1.2f), State->bDecider ? TouchStyle::Red : FLinearColor::White);
		}

		// KILL chip (and, in a room, ALIVE beside it), top right, left of the gear. In BR the alive
		// count is the plate's: this list only ever sees the handful of combatants that are real
		// connections or physical bots, never the fifty the session is running.
		int32 Alive = 0;
		for (const APlayerState* Player : State->PlayerArray)
		{
			Alive += Player && Player->GetPawn() ? 1 : 0;
		}
		{
			const float ChipH = 2.9f * U;
			const float ChipW = 7.2f * U;
			const float Slant = ChipH * 0.3f;
			float Right = L.ChipRight.X;
			auto Chipped = [&](const TCHAR* Caption, int32 Value, const FLinearColor& Tint)
			{
				const FVector2D Min(Right - ChipW, L.ChipTop);
				DrawSlantedPanel(Out, Layer, G, Min, FVector2D(ChipW, ChipH), Slant, TouchStyle::Chip, TouchStyle::Hairline, 1.f);
				DrawText(Out, Layer + 2, G, Caption, Min + FVector2D(ChipW * 0.34f, ChipH * 0.5f),
					FMath::RoundToInt(U * 1.05f), FLinearColor(1.f, 1.f, 1.f, 0.65f), 0.5f, 0.5f);
				DrawText(Out, Layer + 2, G, FString::FromInt(Value), Min + FVector2D(ChipW * 0.78f, ChipH * 0.5f),
					FMath::RoundToInt(U * 1.7f), Tint, 0.5f, 0.5f);
				Right = Min.X - 0.6f * U;
			};
			Chipped(TEXT("KILL"), Mine ? Mine->Kills : 0, TouchStyle::Accent);
			if (!BattleRoyale)
			{
				Chipped(TEXT("ALIVE"), Alive, FLinearColor::White);
			}
		}
		// Connection strength under the chips.
		DrawConnection(Out, Layer, G, L);

		// Kill feed, right side: slanted plates with a diagonal cut end.
		for (int32 Index = 0; Index < State->Feed.Num(); ++Index)
		{
			const FVector2D Anchor(L.W - L.W * 0.012f, L.H * 0.36f + Index * 3.2f * U);
			const int32 FeedFont = FMath::Max(9, FMath::RoundToInt(U * 1.2f));
			const auto Measured = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(State->Feed[Index], FCoreStyle::GetDefaultFontStyle("BoldCondensedItalic", FeedFont));
			const FVector2D Plate(Measured.X + 3.2f * U, Measured.Y + 0.9f * U);
			DrawSlantedPanel(Out, Layer, G, Anchor - FVector2D(Plate.X, Plate.Y * 0.5f), Plate, Plate.Y * 0.3f, TouchStyle::Chip, TouchStyle::Hairline, 1.f);
			DrawText(Out, Layer + 2, G, State->Feed[Index], Anchor - FVector2D(1.3f * U, 0.f), FeedFont, FLinearColor::White, 1.f, 0.5f);
		}

		// Minimap top-left (tap the map: full screen). Match info - teams, kills, health - opens from the timer.
		if (bShowMinimap)
		{
			DrawMap(Out, Layer, G, L.MapMin, L.MapSize, false, U);
		}
	}

	// Settings gear, top-right corner. Drawn outside the State block so it is reachable even
	// before the game state has replicated.
	{
		bool bGearHeld = false;
		for (const TPair<int32, FFinger>& Pair : Fingers)
		{
			bGearHeld |= Pair.Value.Control == EControl::Settings;
		}
		const FVector2D GearMin(L.Gear.X - L.GearR, L.Gear.Y - L.GearR);
		const FVector2D GearSize(L.GearR * 2.f, L.GearR * 2.f);
		DrawNotchedBox(Out, Layer + 6, G, GearMin, GearSize, L.GearR * 0.52f,
			bGearHeld ? FLinearColor(0.16f, 0.12f, 0.02f, 0.95f) : TouchStyle::Chip, bGearHeld ? TouchStyle::Accent : TouchStyle::Hairline, bGearHeld ? 2.f : 1.f);
		CampusAngular::DrawGlyph(Out, Layer + 7, G, CampusAngular::EGlyph::Gear, FVector2f(L.Gear), L.GearR * 0.56f,
			bGearHeld ? TouchStyle::Accent : FLinearColor(1.f, 1.f, 1.f, 0.92f), FMath::Max(1.6f, L.GearR * 0.11f));
	}

	if (!Character)
	{
		// BR HAS NO RESPAWN. A pawnless player there is out of the match and watching a squadmate, and
		// promising them a respawn is the HUD lying about the rules of the game type.
		DrawText(Out, Layer + 1, G, BattleRoyale ? TEXT("ELIMINATED") : TEXT("RESPAWNING..."),
			FVector2D(L.W * 0.5f, L.H * 0.45f), FMath::RoundToInt(U * 2.4f),
			BattleRoyale ? TouchStyle::Red : FLinearColor::White);
		return;
	}

	// Weapons panel with ammo: big magazine count / reserve; tap to switch to the rifle or fists.
	const bool bArmed = Character->IsArmed();
	DrawNotchedBox(Out, Layer, G, L.WeaponMin, L.WeaponSize, 1.6f * U,
		TouchStyle::Chip.CopyWithNewOpacity(bArmed ? 0.82f : 0.5f), bArmed ? TouchStyle::Accent.CopyWithNewOpacity(0.55f) : TouchStyle::Hairline, 1.f);
	if (const FSlateBrush* GunArt = Icon(bArmed ? TEXT("ak") : TEXT("fist")))
	{
		const FVector2D ArtSize = bArmed ? FVector2D(L.WeaponSize.X * 0.5f, L.WeaponSize.X * 0.25f) : FVector2D(L.WeaponSize.Y * 0.55f);
		DrawImage(Out, Layer + 1, G, GunArt, L.WeaponMin + FVector2D(ArtSize.X * 0.5f + U, L.WeaponSize.Y * 0.32f), ArtSize, FLinearColor::White);
	}
	else
	{
		const FString Held = FString(Character->GetCurrentWeapon().Short);
		DrawText(Out, Layer + 1, G, bArmed ? Held : Held + TEXT("  (tap)"), L.WeaponMin + FVector2D(1.2f * U, 1.4f * U), FMath::RoundToInt(U * 1.3f),
			bArmed ? TouchStyle::IconOn : FLinearColor(1.f, 1.f, 1.f, 0.7f), 0.f, 0.5f);
	}
	// The second slot, along the bottom of the same plate: its gun and its magazine, tap to take it.
	{
		const int32 Other = Character->GetCurrentSlot() == 0 ? 1 : 0;
		const FVector2D OtherAnchor = L.WeaponMin + FVector2D(1.2f * U, L.WeaponSize.Y * 0.82f);
		if (Character->HasWeapon(Other))
		{
			const FString Name = FString(CampusWeapons::FindOrStarter(Character->GetWeaponId(Other)).Short);
			DrawText(Out, Layer + 1, G, Name, OtherAnchor, FMath::RoundToInt(U * 1.15f), FLinearColor(1.f, 1.f, 1.f, 0.78f), 0.f, 0.5f);
			DrawText(Out, Layer + 1, G, FString::FromInt(Character->GetSlotAmmo(Other)),
				FVector2D(L.WeaponMin.X + L.WeaponSize.X - 1.2f * U, OtherAnchor.Y), FMath::RoundToInt(U * 1.15f),
				FLinearColor(1.f, 1.f, 1.f, 0.6f), 1.f, 0.5f);
		}
		else
		{
			DrawText(Out, Layer + 1, G, TEXT("EMPTY"), OtherAnchor, FMath::RoundToInt(U * 1.05f), FLinearColor(1.f, 1.f, 1.f, 0.35f), 0.f, 0.5f);
		}
	}
	const int32 Mag = Character->GetAmmoInMag();
	const FVector2D AmmoAnchor = L.WeaponMin + FVector2D(L.WeaponSize.X * 0.55f, L.WeaponSize.Y * 0.62f);
	DrawText(Out, Layer + 1, G, FString::FromInt(Mag), AmmoAnchor, FMath::RoundToInt(U * 2.2f * 1.4f), Mag <= 5 ? TouchStyle::Red : FLinearColor::White, 1.f, 0.5f);
	DrawText(Out, Layer + 1, G, FString::Printf(TEXT("/ %d"), Character->GetAmmoReserve()), AmmoAnchor + FVector2D(0.6f * U, 0.4f * U), FMath::RoundToInt(U * 2.2f * 0.8f), FLinearColor(1.f, 1.f, 1.f, 0.8f), 0.f, 0.5f);

	// Health, bottom centre: a slanted trough with the numbers on the plate (Free Fire).
	const float BarWidth = FMath::Min(L.W * 0.34f, 380.f * L.H / 400.f * 0.5f);
	const float BarHeight = FMath::Max(10.f, 1.1f * U);
	const FVector2D BarMin(L.W * 0.5f - BarWidth * 0.5f, L.H - 3.4f * U);
	const float Max = FMath::Max(1.f, Character->GetMaxHealth());
	const float Health = Character->GetHealth();
	const float Slant = BarHeight * 0.85f;
	const FLinearColor HealthColor = Health / Max < 0.3f ? TouchStyle::Red : FLinearColor::White;
	DrawSlantedPanel(Out, Layer, G, BarMin, FVector2D(BarWidth, BarHeight), Slant, FLinearColor(0.f, 0.f, 0.f, 0.55f), TouchStyle::Hairline, 1.f);
	// The lag bar and the live bar keep the trough's lean, so the fill never looks pasted on.
	const float LagFraction = FMath::Clamp(LagHealth / Max, 0.f, 1.f);
	const float LiveFraction = FMath::Clamp(Health / Max, 0.f, 1.f);
	if (LagFraction > 0.f)
	{
		DrawSlantedPanel(Out, Layer + 1, G, BarMin, FVector2D(BarWidth * LagFraction, BarHeight), Slant * LagFraction, TouchStyle::LagOrange);
	}
	if (LiveFraction > 0.f)
	{
		DrawSlantedPanel(Out, Layer + 2, G, BarMin, FVector2D(BarWidth * LiveFraction, BarHeight), Slant * LiveFraction, HealthColor);
	}
	// Numbers above the bar: big current value, small max, so health reads without counting pixels.
	DrawText(Out, Layer + 3, G, FString::FromInt(FMath::RoundToInt(Health)), BarMin + FVector2D(0.f, -1.6f * U),
		FMath::RoundToInt(U * 1.8f), HealthColor, 0.f, 0.5f);
	DrawText(Out, Layer + 3, G, FString::Printf(TEXT("/ %d  HP"), FMath::RoundToInt(Max)), BarMin + FVector2D(2.6f * U, -1.5f * U),
		FMath::RoundToInt(U * 1.1f), FLinearColor(1.f, 1.f, 1.f, 0.7f), 0.f, 0.5f);

	// Car HP, wheels, seat and the bail-out warning sit under the player's own bar.
	DrawVehicleHud(Out, Layer, G, L);

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

	// Hit marker (three.js #hitmark): a white X for 0.25 s when the server confirms a hit, red on a kill.
	if (World)
	{
		const float Since = World->GetTimeSeconds() - Character->GetLastHitConfirmTime();
		if (Since >= 0.f && Since < 0.25f)
		{
			const float Alpha = 1.f - Since / 0.25f;
			const float Arm = 13.f * L.H / 400.f;
			const FLinearColor Color = (Character->WasLastHitAKill() ? TouchStyle::Red : FLinearColor::White).CopyWithNewOpacity(Alpha);
			const float Gap = Arm * 0.35f;
			auto Stroke = [&](float SX, float SY)
			{
				DrawPolyline(Out, Layer + 4, G, { FVector2f(Center.X + SX * Gap, Center.Y + SY * Gap), FVector2f(Center.X + SX * Arm, Center.Y + SY * Arm) }, Color, 3.f);
			};
			Stroke(1.f, 1.f);
			Stroke(-1.f, 1.f);
			Stroke(1.f, -1.f);
			Stroke(-1.f, -1.f);
		}
		// damage number floating up from the crosshair (yellow on a headshot), 0.6 s
		if (Since >= 0.f && Since < 0.6f && Character->GetLastHitDamage() > 0)
		{
			const float Alpha = 1.f - Since / 0.6f;
			const FLinearColor NumberColor = (Character->WasLastHitAHeadshot() ? FLinearColor(1.f, 0.85f, 0.1f) : FLinearColor::White).CopyWithNewOpacity(Alpha);
			DrawText(Out, Layer + 4, G, FString::FromInt(Character->GetLastHitDamage()),
				FVector2D(Center.X + 3.f * L.U, Center.Y - 2.f * L.U - Since * 6.f * L.U), FMath::RoundToInt(L.U * (Character->WasLastHitAHeadshot() ? 2.f : 1.6f)), NumberColor);
		}
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
	// Battle royale only: the circle, its clock, and the alive counter. Draws nothing in TDM.
	DrawBattleRoyaleHud(OutDrawElements, LayerId + 18, AllottedGeometry, L);
	if (bShowButtons)
	{
		DrawControls(OutDrawElements, LayerId + 5, AllottedGeometry, L);
	}
	if (bBigMap)
	{
		const float Size = FMath::Min(L.W, L.H) * 0.9f;
		DrawMap(OutDrawElements, LayerId + 20, AllottedGeometry, FVector2D((L.W - Size) * 0.5f, (L.H - Size) * 0.5f), Size, true, L.U);
	}
	if (bScoreboard)
	{
		DrawScoreboard(OutDrawElements, LayerId + 20, AllottedGeometry, L);
	}
	return LayerId + 30;
}

// ---------------------------------------------------------------------------------------------
// Maps and scoreboard

namespace CampusMapArea
{
	// The square the map picture covers, in Unreal world cm (tools/export-unreal-map.mjs in the three.js repo: the
	// college bounds + 15 m, north = -Y up, +X right; the campus sits at (15710, 8222.6) in L_Match).
	constexpr float MinX = -12210.f + 15710.f;
	constexpr float MinY = -19549.45f + 8222.6f;
	constexpr float Span = 22710.f;

	FVector2D ToUV(const FVector& World)
	{
		return FVector2D((World.X - MinX) / Span, (World.Y - MinY) / Span);
	}

	/** Liang-Barsky against the unit square, in UV. The flight line is launched from OUTSIDE the map
	 *  square (CampusBRDive::LineOvershoot) so the plane is never seen turning, which means the part
	 *  worth drawing is the part that crosses the picture. Clamping each endpoint on its own would
	 *  bend a diagonal route; this keeps the line straight and puts the markers on the edges it
	 *  actually enters and leaves by. False when the line misses the picture completely. */
	bool ClipToPicture(FVector2D& A, FVector2D& B)
	{
		const FVector2D Along = B - A;
		const double Edge[4] = { -Along.X, Along.X, -Along.Y, Along.Y };
		const double Room[4] = { A.X, 1.0 - A.X, A.Y, 1.0 - A.Y };
		double Enter = 0.0;
		double Leave = 1.0;
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (FMath::IsNearlyZero(Edge[Side]))
			{
				if (Room[Side] < 0.0)
				{
					return false; // parallel to this edge and wholly outside it
				}
				continue;
			}
			const double T = Room[Side] / Edge[Side];
			if (Edge[Side] < 0.0)
			{
				Enter = FMath::Max(Enter, T);
			}
			else
			{
				Leave = FMath::Min(Leave, T);
			}
		}
		if (Enter > Leave)
		{
			return false;
		}
		const FVector2D Clipped = A + Along * Enter;
		B = A + Along * Leave;
		A = Clipped;
		return true;
	}

	struct FPlace { const TCHAR* Name; float X; float Y; };
	const FPlace Places[] = {
		{ TEXT("Front Block"), -2530.f, -11370.f }, { TEXT("Front Block"), 2835.f, -11370.f },
		{ TEXT("West Wing"), -5022.f, -6160.f }, { TEXT("East Wing"), 5022.f, -6160.f },
		{ TEXT("Courtyard"), 0.f, -6625.f }, { TEXT("Canteen"), -8870.f, -12152.f },
	};
}

void SCampusTouchControls::DrawMap(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, float Size, bool bBig, float U) const
{
	DrawRect(Out, Layer, G, Min - FVector2D(2.f), FVector2D(Size + 4.f), FLinearColor(0.f, 0.f, 0.f, 0.6f));
	if (const FSlateBrush* Picture = Icon(TEXT("campusmap")))
	{
		DrawImage(Out, Layer + 1, G, Picture, Min + FVector2D(Size * 0.5f), FVector2D(Size), FLinearColor::White);
	}
	else
	{
		DrawRect(Out, Layer + 1, G, Min, FVector2D(Size), TouchStyle::Minimap);
	}
	auto At = [&Min, Size](const FVector& World)
	{
		const FVector2D UV = CampusMapArea::ToUV(World);
		return Min + FVector2D(FMath::Clamp(UV.X, 0.0, 1.0), FMath::Clamp(UV.Y, 0.0, 1.0)) * Size;
	};
	const float Scale = bBig ? Size / 640.f : 1.f;
	if (bBig)
	{
		// grid like the real games: columns A-J, rows 1-10, and the place names
		const int32 GridFont = FMath::RoundToInt(Size / 45.f);
		for (int32 K = 0; K <= 10; ++K)
		{
			const float T = Min.X + Size * K / 10.f;
			const float V = Min.Y + Size * K / 10.f;
			DrawPolyline(Out, Layer + 2, G, { FVector2f(T, Min.Y), FVector2f(T, Min.Y + Size) }, FLinearColor(1.f, 1.f, 1.f, 0.35f), 1.f);
			DrawPolyline(Out, Layer + 2, G, { FVector2f(Min.X, V), FVector2f(Min.X + Size, V) }, FLinearColor(1.f, 1.f, 1.f, 0.35f), 1.f);
			if (K < 10)
			{
				DrawText(Out, Layer + 3, G, FString::Chr(TEXT('A') + K), FVector2D(Min.X + Size * (K + 0.5f) / 10.f, Min.Y + Size / 40.f), GridFont, FLinearColor::White);
				DrawText(Out, Layer + 3, G, FString::FromInt(K + 1), FVector2D(Min.X + Size / 40.f, Min.Y + Size * (K + 0.5f) / 10.f), GridFont, FLinearColor::White);
			}
		}
		for (const CampusMapArea::FPlace& Place : CampusMapArea::Places)
		{
			DrawText(Out, Layer + 3, G, Place.Name, At(FVector(Place.X + 15710.f, Place.Y + 8222.6f, 0.f)), FMath::RoundToInt(Size / 42.f), FLinearColor::White);
		}
		DrawText(Out, Layer + 3, G, TEXT("tap anywhere to close"), FVector2D(Min.X + Size * 0.5f, Min.Y + Size + 2.f * U), FMath::RoundToInt(U * 1.2f), FLinearColor(1.f, 1.f, 1.f, 0.7f));
	}

	// The plane's route, under the player arrows: the line is background, the arrows are you.
	DrawFlightRoute(Out, Layer + 3, G, Min, Size, bBig, Scale);

	// arrows: you (yellow, bigger) and your teammates (ROOM: the team colour. BR: one friendly green,
	// because the only other side in a battle session is "everyone else"); enemies never show
	auto Arrow = [&](const FVector2D& C, float YawDeg, const FLinearColor& Color, float Len)
	{
		const float R = FMath::DegreesToRadians(YawDeg);
		const FVector2D Dir(FMath::Cos(R), FMath::Sin(R)); // +X right, +Y down on the picture
		const FVector2D Side(-Dir.Y, Dir.X);
		const FVector2D Nose = C + Dir * Len;
		const FVector2D BackL = C - Dir * Len * 0.6f + Side * Len * 0.7f;
		const FVector2D BackR = C - Dir * Len * 0.6f - Side * Len * 0.7f;
		DrawCircle(Out, Layer + 4, G, C, Len * 0.45f, Color);
		DrawPolyline(Out, Layer + 5, G, { FVector2f(BackL), FVector2f(Nose), FVector2f(BackR), FVector2f(C - Dir * Len * 0.25f), FVector2f(BackL) }, Color, FMath::Max(2.f, Len * 0.25f));
	};
	const APlayerController* PC = Controller.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const ACampusPlayerState* Mine = PC ? PC->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!State || !Mine)
	{
		return;
	}
	const bool bBattleRoyale = CampusBR::IsBattleRoyale(World);
	for (const APlayerState* Player : State->PlayerArray)
	{
		const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player);
		const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
		if (!Campus || !Pawn || Campus == Mine)
		{
			continue;
		}
		// TEAMMATES ONLY. In BR that is the squad and nothing else: a solo player has no squadmates and
		// so has an empty map, which is exactly right.
		const bool bMate = bBattleRoyale ? CampusBR::AreSquadmates(Campus, Mine) : Campus->Team == Mine->Team;
		if (!bMate)
		{
			continue;
		}
		const FLinearColor MateColor = bBattleRoyale ? TouchStyle::Mate
			: (Campus->Team == ECampusTeam::Red ? TouchStyle::Red : TouchStyle::Blue);
		const FVector2D C = At(Pawn->GetActorLocation());
		Arrow(C, Pawn->GetActorRotation().Yaw, MateColor, 6.f * Scale);
		if (bBig)
		{
			DrawText(Out, Layer + 6, G, Campus->GetPlayerName(), C - FVector2D(0.f, 16.f * Scale),
				FMath::RoundToInt(Size / 55.f), bBattleRoyale ? TouchStyle::Mate : FLinearColor::White);
		}
	}
	if (const APawn* Me = PC->GetPawn())
	{
		Arrow(At(Me->GetActorLocation()), PC->GetControlRotation().Yaw, FLinearColor(FColor(0xFF, 0xD2, 0x1F)), 8.f * Scale);
	}
}

void SCampusTouchControls::DrawFlightRoute(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, float Size,
	bool bBig, float Scale) const
{
	const APlayerController* PC = Controller.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	// EVERY machine reads the same four replicated values (Start, Finish, LaunchTime, FlightSeconds)
	// through these accessors, so the line a remote client draws is the line the host is flying.
	const ACampusBRPlane* Plane = World ? ACampusBRPlane::Get(World) : nullptr;
	if (!Plane || !Plane->HasRoute())
	{
		return; // a room match, or the plane has not been handed a line yet
	}
	FVector2D Head = CampusMapArea::ToUV(Plane->GetRouteStart());
	FVector2D Tail = CampusMapArea::ToUV(Plane->GetRouteFinish());
	if (!CampusMapArea::ClipToPicture(Head, Tail))
	{
		return;
	}
	// The route has done its job once the aircraft has flown the line out. It stays on the picture,
	// faded, rather than vanishing: a player still under the canopy is reading where everyone came from.
	const float Fade = Plane->FlightAlpha() < 1.f ? 1.f : 0.3f;
	const FVector2D From = Min + Head * Size;
	const FVector2D To = Min + Tail * Size;
	const FVector2D Dir = (To - From).GetSafeNormal();
	const FVector2D Side(-Dir.Y, Dir.X);
	const float Weight = FMath::Max(2.f, 2.2f * Scale);
	const FLinearColor Ink(1.f, 1.f, 1.f, 0.9f * Fade);
	const FLinearColor Mark = TouchStyle::Accent.CopyWithNewOpacity(TouchStyle::Accent.A * Fade);
	DrawPolyline(Out, Layer, G, { FVector2f(From), FVector2f(To) }, Ink, Weight);

	// Chevrons along the line: which end the plane came from is never left to be guessed.
	const float Chevron = FMath::Max(3.f, 4.f * Scale);
	for (int32 Step = 1; Step <= 3; ++Step)
	{
		const FVector2D Mid = From + (To - From) * (Step * 0.25f);
		DrawPolyline(Out, Layer, G, {
			FVector2f(Mid - Dir * Chevron + Side * Chevron),
			FVector2f(Mid + Dir * Chevron),
			FVector2f(Mid - Dir * Chevron - Side * Chevron) }, Ink, Weight);
	}

	// START filled, END hollow: the two-marker grammar every battle royale map uses.
	const float Dot = FMath::Max(3.f, 4.f * Scale);
	DrawCircle(Out, Layer, G, From, Dot, Mark);
	DrawRing(Out, Layer, G, To, Dot, Mark);
	if (bBig)
	{
		const int32 Font = FMath::RoundToInt(Size / 50.f);
		DrawText(Out, Layer, G, TEXT("START"), From - Side * Dot * 3.f, Font, Mark);
		DrawText(Out, Layer, G, TEXT("END"), To - Side * Dot * 3.f, Font, Mark);
	}

	// The aircraft itself. Off the picture it is not drawn at all: an icon pinned to the border
	// would read as a plane parked on the map edge, which is exactly what it is not doing.
	const FVector2D Now = CampusMapArea::ToUV(Plane->GetRouteLocation());
	if (Now.X < 0.0 || Now.Y < 0.0 || Now.X > 1.0 || Now.Y > 1.0)
	{
		return;
	}
	// Fuselage, swept wings, tailplane: three strokes still read as an aeroplane at minimap size,
	// where an icon texture is eight pixels of mush.
	const FVector2D Body = Min + Now * Size;
	const float Wing = FMath::Max(4.f, 5.f * Scale);
	DrawCircle(Out, Layer, G, Body, Weight, Mark);
	DrawPolyline(Out, Layer, G, { FVector2f(Body - Dir * Wing), FVector2f(Body + Dir * Wing * 1.3f) }, Ink, Weight);
	DrawPolyline(Out, Layer, G, {
		FVector2f(Body + Side * Wing - Dir * Wing * 0.45f),
		FVector2f(Body),
		FVector2f(Body - Side * Wing - Dir * Wing * 0.45f) }, Ink, Weight);
	DrawPolyline(Out, Layer, G, {
		FVector2f(Body + Side * Wing * 0.45f - Dir * Wing * 0.95f),
		FVector2f(Body - Side * Wing * 0.45f - Dir * Wing * 0.95f) }, Ink, Weight);
}

void SCampusTouchControls::DrawScoreboard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const
{
	const APlayerController* PC = Controller.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const ACampusPlayerState* Mine = PC ? PC->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!State)
	{
		return;
	}
	// Two game types, two boards, one dispatch. The room's board is RED against BLUE and the battle
	// session's is squad against squad; neither has to know the other exists.
	if (const ACampusBRGameState* BattleRoyale = Cast<ACampusBRGameState>(State))
	{
		DrawSquadBoard(Out, Layer, G, L, BattleRoyale, Mine);
		return;
	}
	DrawTeamBoard(Out, Layer, G, L, State, Mine);
}

void SCampusTouchControls::DrawTeamBoard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L,
	const ACampusGameState* State, const ACampusPlayerState* Mine) const
{
	const float U = L.U;
	DrawRect(Out, Layer, G, FVector2D::ZeroVector, FVector2D(L.W, L.H), FLinearColor(0.f, 0.f, 0.f, 0.55f));
	const FString Title = State->bDecider ? FString::Printf(TEXT("DECIDING ROUND  ·  RED %d - %d BLUE"), State->RedScore, State->BlueScore)
		: FString::Printf(TEXT("ROUND %d / %d  ·  RED %d - %d BLUE"), FMath::Max(State->Round, 1), State->Rounds, State->RedScore, State->BlueScore);
	DrawText(Out, Layer + 2, G, Title, FVector2D(L.W * 0.5f, L.H * 0.1f), FMath::RoundToInt(U * 2.f), FLinearColor::White);

	const float BoxW = L.W * 0.44f;
	const float BoxH = L.H * 0.7f;
	const float Top = L.H * 0.17f;
	const int32 Font = FMath::RoundToInt(U * 1.25f);
	const float Row = U * 2.3f;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const ECampusTeam Team = Side == 0 ? ECampusTeam::Red : ECampusTeam::Blue;
		const FLinearColor TeamColor = Side == 0 ? TouchStyle::Red : TouchStyle::Blue;
		const float X0 = Side == 0 ? L.W * 0.04f : L.W * 0.52f;
		DrawNotchedBox(Out, Layer + 1, G, FVector2D(X0, Top), FVector2D(BoxW, BoxH), U * 1.8f, TouchStyle::ChipStrong, TouchStyle::Hairline, 1.f);
		DrawRect(Out, Layer + 2, G, FVector2D(X0, Top), FVector2D(BoxW, U * 0.4f), TeamColor);
		DrawText(Out, Layer + 2, G, FString::Printf(TEXT("%s  ·  %d rounds won"), Side == 0 ? TEXT("RED") : TEXT("BLUE"), Side == 0 ? State->RedScore : State->BlueScore),
			FVector2D(X0 + U, Top + Row * 0.8f), FMath::RoundToInt(U * 1.6f), TeamColor, 0.f, 0.5f);
		// columns: name | K | D | damage dealt | damage taken (this room, all rounds so far)
		const float CK = X0 + BoxW * 0.52f;
		const float CD = X0 + BoxW * 0.62f;
		const float CDealt = X0 + BoxW * 0.77f;
		const float CTaken = X0 + BoxW * 0.92f;
		float Y = Top + Row * 1.9f;
		const FLinearColor Head(1.f, 1.f, 1.f, 0.6f);
		DrawText(Out, Layer + 2, G, TEXT("PLAYER"), FVector2D(X0 + U, Y), Font, Head, 0.f, 0.5f);
		DrawText(Out, Layer + 2, G, TEXT("K"), FVector2D(CK, Y), Font, Head);
		DrawText(Out, Layer + 2, G, TEXT("D"), FVector2D(CD, Y), Font, Head);
		DrawText(Out, Layer + 2, G, TEXT("DMG"), FVector2D(CDealt, Y), Font, Head);
		DrawText(Out, Layer + 2, G, TEXT("TAKEN"), FVector2D(CTaken, Y), Font, Head);
		TArray<const ACampusPlayerState*> Players;
		for (const APlayerState* Player : State->PlayerArray)
		{
			if (const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player); Campus && Campus->Team == Team)
			{
				Players.Add(Campus);
			}
		}
		Players.Sort([](const ACampusPlayerState& A, const ACampusPlayerState& B) { return A.Kills != B.Kills ? A.Kills > B.Kills : A.DamageDealt > B.DamageDealt; });
		for (const ACampusPlayerState* Campus : Players)
		{
			Y += Row;
			if (Campus == Mine)
			{
				DrawRect(Out, Layer + 2, G, FVector2D(X0 + U * 0.4f, Y - Row * 0.45f), FVector2D(BoxW - U * 0.8f, Row * 0.9f), FLinearColor(1.f, 0.82f, 0.12f, 0.18f));
			}
			const FLinearColor Text = Campus->GetPawn() ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.45f); // dimmed = out this round
			DrawText(Out, Layer + 3, G, Campus->GetPlayerName() + (Campus->bIsHost ? TEXT("  (host)") : TEXT("")), FVector2D(X0 + U, Y), Font, Text, 0.f, 0.5f);
			DrawText(Out, Layer + 3, G, FString::FromInt(Campus->Kills), FVector2D(CK, Y), Font, Text, 0.5f, 0.5f, true);
			DrawText(Out, Layer + 3, G, FString::FromInt(Campus->Deaths), FVector2D(CD, Y), Font, Text, 0.5f, 0.5f, true);
			DrawText(Out, Layer + 3, G, FString::FromInt(FMath::RoundToInt(Campus->DamageDealt)), FVector2D(CDealt, Y), Font, Text, 0.5f, 0.5f, true);
			DrawText(Out, Layer + 3, G, FString::FromInt(FMath::RoundToInt(Campus->DamageTaken)), FVector2D(CTaken, Y), Font, Text, 0.5f, 0.5f, true);
		}
	}
	DrawText(Out, Layer + 2, G, TEXT("tap anywhere to close"), FVector2D(L.W * 0.5f, Top + BoxH + U * 2.f), FMath::RoundToInt(U * 1.2f), FLinearColor(1.f, 1.f, 1.f, 0.7f));
}

void SCampusTouchControls::DrawSquadBoard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L,
	const ACampusBRGameState* State, const ACampusPlayerState* Mine) const
{
	const float U = L.U;
	DrawRect(Out, Layer, G, FVector2D::ZeroVector, FVector2D(L.W, L.H), FLinearColor(0.f, 0.f, 0.f, 0.55f));
	DrawText(Out, Layer + 2, G, FString::Printf(TEXT("MATCH INFO  ·  ALIVE %d / %d  ·  SQUADS %d"),
		State->AliveCombatants, State->TotalCombatants, State->AliveSquads),
		FVector2D(L.W * 0.5f, L.H * 0.08f), FMath::RoundToInt(U * 2.f), FLinearColor::White);

	// THE SQUADS THIS CLIENT CAN SEE: the humans and the physical bots, which is every combatant that
	// owns a player state. The virtual rows that make up the rest of the fifty have none by design,
	// which is exactly why the heading above carries the session's own counts and this list does not
	// pretend to be the whole roster.
	TArray<int32> Squads;
	TMap<int32, TArray<const ACampusPlayerState*>> Members;
	for (const APlayerState* Player : State->PlayerArray)
	{
		const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player);
		const int32 Squad = CampusBR::GetSquadId(Campus);
		if (!Campus || Squad == 0)
		{
			continue;
		}
		Members.FindOrAdd(Squad).Add(Campus);
		Squads.AddUnique(Squad);
	}
	const int32 MySquad = CampusBR::GetSquadId(Mine);
	// Your own squad first, then by number: that is the only row anybody looks for in a hurry.
	Squads.Sort([MySquad](int32 A, int32 B)
	{
		return (A == MySquad) != (B == MySquad) ? A == MySquad : A < B;
	});

	const int32 Font = FMath::RoundToInt(U * 1.25f);
	const float Row = U * 2.f;
	const float Top = L.H * 0.17f;
	const float ColumnWidth = L.W * 0.3f;
	const FLinearColor Dim(1.f, 1.f, 1.f, 0.45f);
	float X = L.W * 0.05f;
	float Y = Top;
	for (const int32 Squad : Squads)
	{
		const TArray<const ACampusPlayerState*>& Line = Members[Squad];
		int32 Kills = 0;
		bool bAlive = false;
		for (const ACampusPlayerState* Campus : Line)
		{
			Kills += Campus->Kills;
			// Placement is written the moment a combatant is eliminated, so zero IS still standing.
			bAlive |= Campus->Placement == 0;
		}
		const bool bMine = Squad == MySquad;
		DrawText(Out, Layer + 2, G, FString::Printf(TEXT("SQUAD %d%s  ·  %s  ·  %d KILLS"), Squad,
			bMine ? TEXT("  (yours)") : TEXT(""), bAlive ? TEXT("ALIVE") : TEXT("OUT"), Kills),
			FVector2D(X, Y), FMath::RoundToInt(U * 1.4f),
			!bAlive ? Dim : (bMine ? TouchStyle::Mate : FLinearColor::White), 0.f, 0.5f);
		Y += Row;
		for (const ACampusPlayerState* Campus : Line)
		{
			const bool bOut = Campus->Placement != 0;
			const FLinearColor Text = bOut ? Dim : (Campus == Mine ? TouchStyle::Accent : FLinearColor::White);
			DrawText(Out, Layer + 3, G, Campus->GetPlayerName(), FVector2D(X + U, Y), Font, Text, 0.f, 0.5f);
			DrawText(Out, Layer + 3, G, bOut ? FString::Printf(TEXT("#%d"), Campus->Placement)
				: FString::Printf(TEXT("%d K"), Campus->Kills),
				FVector2D(X + ColumnWidth - 2.f * U, Y), Font, Text, 1.f, 0.5f);
			Y += Row * 0.85f;
		}
		Y += Row * 0.5f;
		// Columns, because a squad match is twelve blocks and a solo match is fifty.
		if (Y > L.H * 0.86f)
		{
			Y = Top;
			X += ColumnWidth;
			if (X + ColumnWidth > L.W)
			{
				break;
			}
		}
	}
	DrawText(Out, Layer + 2, G, TEXT("tap anywhere to close"), FVector2D(L.W * 0.5f, L.H * 0.94f),
		FMath::RoundToInt(U * 1.2f), FLinearColor(1.f, 1.f, 1.f, 0.7f));
}

// ---------------------------------------------------------------------------------------------
// Battle royale overlay

void SCampusTouchControls::DrawSquadMarkers(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const
{
	const APlayerController* PC = Controller.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const ACampusPlayerState* Mine = PC ? PC->GetPlayerState<ACampusPlayerState>() : nullptr;
	const int32 MySquad = CampusBR::GetSquadId(Mine);
	if (!State || MySquad == 0)
	{
		// SOLO has no squadmates, so it has no markers. That is the mode working, not a gap.
		return;
	}
	const float U = L.U;
	// Projection answers in viewport pixels; this widget fills the viewport, so the widget's own
	// scale is the whole conversion to the local space every other draw here uses.
	const float Scale = FMath::Max(G.Scale, 0.01f);
	for (const APlayerState* Player : State->PlayerArray)
	{
		const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player);
		if (!Campus || Campus == Mine || CampusBR::GetSquadId(Campus) != MySquad)
		{
			continue;
		}
		const APawn* Pawn = Campus->GetPawn();
		FVector2D Screen = FVector2D::ZeroVector;
		if (!Pawn || !PC->ProjectWorldLocationToScreen(Pawn->GetActorLocation() + FVector(0.f, 0.f, 140.f), Screen, false))
		{
			continue; // no pawn, or behind the camera
		}
		const FVector2D At = Screen / Scale;
		if (At.X < 0.f || At.Y < 0.f || At.X > L.W || At.Y > L.H)
		{
			continue;
		}
		const float Size = 0.9f * U;
		DrawPolyline(Out, Layer, G, {
			FVector2f(float(At.X) - Size, float(At.Y) - Size),
			FVector2f(float(At.X), float(At.Y)),
			FVector2f(float(At.X) + Size, float(At.Y) - Size) },
			TouchStyle::Mate, FMath::Max(2.f, U * 0.22f));
		DrawText(Out, Layer, G, Campus->GetPlayerName().ToUpper(),
			FVector2D(At.X, At.Y - Size * 2.4f), FMath::Max(9, FMath::RoundToInt(U * 1.1f)), TouchStyle::Mate);
	}
}

void SCampusTouchControls::DrawBattleRoyaleHud(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const
{
	const APlayerController* PC = Controller.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	const ACampusBRGameState* State = World ? World->GetGameState<ACampusBRGameState>() : nullptr;
	if (!State)
	{
		return; // TDM: there is nothing battle-royale about this match
	}
	const float U = L.U;
	const int32 Big = FMath::RoundToInt(U * 2.6f);
	const int32 Small = FMath::RoundToInt(U * 1.3f);
	auto Clock = [](float Seconds)
	{
		const int32 Whole = FMath::CeilToInt(FMath::Max(0.f, Seconds));
		return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
	};

	// ALIVE and SQUADS are the top-centre plate's (DrawHud), where the room's score used to be.

	// The only marked players in a battle session: your own squad.
	DrawSquadMarkers(Out, Layer + 4, G, L);

	// Phase banner, screen centre-top: the only thing worth reading before the drop.
	FString Banner;
	FLinearColor BannerTint = FLinearColor::White;
	switch (State->BRPhase)
	{
	case ECampusBRPhase::Waiting:
		Banner = FString::Printf(TEXT("WAITING FOR PLAYERS  %d / %d  \u00b7  %s"),
			State->HumansJoined, State->HumanCap, *Clock(State->BRPhaseEndTime - float(State->GetServerWorldTimeSeconds())));
		break;
	case ECampusBRPhase::Countdown:
		Banner = FString::Printf(TEXT("DROPPING IN %s"), *Clock(State->BRPhaseEndTime - float(State->GetServerWorldTimeSeconds())));
		BannerTint = TouchStyle::Accent;
		break;
	default:
		break;
	}
	if (const ACampusBRDrop* Drop = ACampusBRPlane::FindDrop(GetCharacter()))
	{
		if (Drop->GetState() == ECampusBRDive::InPlane)
		{
			// The press is only honoured once the plane is over the map square (ACampusBRDrop::
			// CanDropNow): say so, rather than letting the player tap a button that does nothing.
			const bool bOpen = Drop->CanDropNow();
			Banner = bOpen ? TEXT("TAP JUMP TO DROP") : TEXT("THE PLANE IS NOT OVER THE MAP YET");
			BannerTint = bOpen ? TouchStyle::Accent : FLinearColor::White;
		}
		else if (Drop->GetState() == ECampusBRDive::Freefall)
		{
			Banner = FString::Printf(TEXT("%d m  \u00b7  JUMP TO OPEN CHUTE"), FMath::RoundToInt(Drop->HeightAboveGround() * 0.01f));
		}
		else if (Drop->GetState() == ECampusBRDive::Parachute)
		{
			Banner = FString::Printf(TEXT("%d m  \u00b7  CHUTE OPEN"), FMath::RoundToInt(Drop->HeightAboveGround() * 0.01f));
		}
	}
	if (State->BRPhase == ECampusBRPhase::Ended)
	{
		Banner = State->WinningSquad >= 0 ? FString(TEXT("BOOYAH!")) : FString(TEXT("MATCH OVER"));
		BannerTint = TouchStyle::Accent;
	}
	if (!Banner.IsEmpty())
	{
		DrawText(Out, Layer + 2, G, Banner, FVector2D(L.W * 0.5f, L.H * 0.17f),
			State->BRPhase == ECampusBRPhase::Ended ? FMath::RoundToInt(U * 5.f) : Big, BannerTint);
	}

	const ACampusBRZone* Zone = State->Zone;
	if (!Zone || Zone->GetPhase() < 0)
	{
		return;
	}

	// The zone clock. Shrinking reads red, because that is when standing still costs health.
	const FString ZoneLine = Zone->IsShrinking()
		? FString::Printf(TEXT("ZONE CLOSING  \u00b7  %s"), *Clock(Zone->SecondsToEvent()))
		: FString::Printf(TEXT("ZONE SHRINKING IN %s"), *Clock(Zone->SecondsToEvent()));
	// Directly under the ALIVE / SQUADS plate, where the room's round line sits.
	DrawText(Out, Layer + 2, G, ZoneLine, FVector2D(L.W * 0.5f, 32.f + U * 4.4f), Small,
		Zone->IsShrinking() ? TouchStyle::Red : FLinearColor::White);

	// Outside the circle: the one warning that has to be impossible to miss.
	const ACampusCharacter* Me = GetCharacter();
	const float Outside = Me ? Zone->MetresOutside(Me->GetActorLocation()) : 0.f;
	if (Outside > 0.f)
	{
		DrawText(Out, Layer + 2, G, FString::Printf(TEXT("OUTSIDE THE PLAY AREA  \u00b7  %d m  \u00b7  MOVE"), FMath::RoundToInt(Outside)),
			FVector2D(L.W * 0.5f, L.H * 0.26f), Big, TouchStyle::Red);
	}

	// The circle on the minimap: where it is now, and (dashed-bright) where it is closing to.
	if (!bShowMinimap || L.MapSize <= 0.f)
	{
		return;
	}
	auto ToMap = [&L](const FVector2D& Flat)
	{
		const FVector2D UV = CampusBRMap::ToUV(FVector(Flat.X, Flat.Y, 0.f));
		return L.MapMin + UV * L.MapSize;
	};
	const float PerCm = L.MapSize / CampusBRMap::Span;
	DrawRing(Out, Layer + 6, G, ToMap(Zone->GetCenter()), Zone->GetRadius() * PerCm, FLinearColor(1.f, 1.f, 1.f, 0.85f));
	if (!Zone->IsShrinking())
	{
		DrawRing(Out, Layer + 6, G, ToMap(Zone->GetNextCenter()), Zone->GetNextRadius() * PerCm, TouchStyle::Accent);
	}
}
