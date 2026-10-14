#include "Campus/CampusBRHud.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBRPlane.h"
#include "Campus/CampusBRZone.h"
#include "Campus/CampusCharacter.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusUI.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusBRHud"

/** THE ZONE'S COLOUR, one value for this whole file: #3aa0ff, the same blue as the wall in the world
 *  (CampusBRWall::Colour) and the circle on the map (TouchStyle::ZoneBlue). The out-of-zone warning
 *  used to be Danger red, which read as "you are being shot at" rather than "you are in the gas". */
namespace CampusBRHudZone
{
	const FLinearColor Ink = FLinearColor(FColor(0x3A, 0xA0, 0xFF));
}

using namespace CampusStyle;

namespace
{
	// ---- reflection probes. Unity-safe names (BrHud*) so nothing here can collide with another
	// translation unit's helpers.

	const FProperty* BrHudFind(const UObject* Object, const TCHAR* A, const TCHAR* B = nullptr,
		const TCHAR* C = nullptr, const TCHAR* D = nullptr)
	{
		if (!Object)
		{
			return nullptr;
		}
		const UClass* Class = Object->GetClass();
		const TCHAR* Names[4] = { A, B, C, D };
		for (const TCHAR* Name : Names)
		{
			if (!Name)
			{
				continue;
			}
			if (const FProperty* Found = Class->FindPropertyByName(FName(Name)))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** Any numeric / bool / byte property as a double. */
	bool BrHudNumber(const UObject* Object, const FProperty* Property, double& Out)
	{
		if (!Object || !Property)
		{
			return false;
		}
		const void* Value = Property->ContainerPtrToValuePtr<void>(Object);
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			Out = Numeric->IsFloatingPoint() ? Numeric->GetFloatingPointPropertyValue(Value)
				: double(Numeric->GetSignedIntPropertyValue(Value));
			return true;
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			Out = Bool->GetPropertyValue(Value) ? 1.0 : 0.0;
			return true;
		}
		return false;
	}

	bool BrHudReadNumber(const UObject* Object, double& Out, const TCHAR* A, const TCHAR* B = nullptr,
		const TCHAR* C = nullptr, const TCHAR* D = nullptr)
	{
		return BrHudNumber(Object, BrHudFind(Object, A, B, C, D), Out);
	}

	/** A string (or name) property, for the session id. */
	bool BrHudReadString(const UObject* Object, FString& Out, const TCHAR* A, const TCHAR* B = nullptr)
	{
		const FProperty* Property = BrHudFind(Object, A, B);
		if (!Object || !Property)
		{
			return false;
		}
		const void* Value = Property->ContainerPtrToValuePtr<void>(Object);
		if (const FStrProperty* AsString = CastField<FStrProperty>(Property))
		{
			Out = AsString->GetPropertyValue(Value);
			return true;
		}
		if (const FNameProperty* AsName = CastField<FNameProperty>(Property))
		{
			Out = AsName->GetPropertyValue(Value).ToString();
			return true;
		}
		return false;
	}

	bool BrHudReadVector(const UObject* Object, FVector& Out, const TCHAR* A, const TCHAR* B = nullptr,
		const TCHAR* C = nullptr)
	{
		const FStructProperty* Property = CastField<FStructProperty>(BrHudFind(Object, A, B, C));
		if (!Object || !Property || !Property->Struct)
		{
			return false;
		}
		// FVector and every FVector_NetQuantize* derive from FVector and share its layout, which is
		// what the zone actually replicates - so match on the family, not on one exact struct.
		const FString Name = Property->Struct->GetName();
		if (Name != TEXT("Vector") && !Name.StartsWith(TEXT("Vector_NetQuantize")))
		{
			return false;
		}
		Out = *Property->ContainerPtrToValuePtr<FVector>(Object);
		return true;
	}

	/** The phase as the ENUMERATOR'S NAME, lowercased. Reading the name and not the integer is what
	 *  makes this survive the BR pass re-ordering its own enum. */
	FString BrHudReadPhase(const UObject* Object)
	{
		if (!Object)
		{
			return FString();
		}
		// BRPhase BEFORE Phase. ACampusBRGameState carries both: the battle-royale phase (waiting /
		// countdown / flight / playing / ended) and the plain match phase it inherits, which reads
		// "waiting" for the whole lobby AND the countdown and "playing" from the plane onward. Reading
		// the coarse one first is what stopped the drop countdown ever appearing.
		const FProperty* Property = BrHudFind(Object, TEXT("BRPhase"), TEXT("BrPhase"), TEXT("Phase"), TEXT("State"));
		if (!Property)
		{
			return FString();
		}
		const void* Value = Property->ContainerPtrToValuePtr<void>(Object);
		if (const FEnumProperty* AsEnum = CastField<FEnumProperty>(Property))
		{
			const int64 Raw = AsEnum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
			return AsEnum->GetEnum() ? AsEnum->GetEnum()->GetNameStringByValue(Raw).ToLower() : FString();
		}
		if (const FByteProperty* AsByte = CastField<FByteProperty>(Property))
		{
			const int64 Raw = AsByte->GetSignedIntPropertyValue(Value);
			return AsByte->Enum ? AsByte->Enum->GetNameStringByValue(Raw).ToLower() : FString();
		}
		if (const FNameProperty* AsName = CastField<FNameProperty>(Property))
		{
			return AsName->GetPropertyValue(Value).ToString().ToLower();
		}
		if (const FStrProperty* AsString = CastField<FStrProperty>(Property))
		{
			return AsString->GetPropertyValue(Value).ToLower();
		}
		return FString();
	}

	/** The first actor whose class name contains Needle (e.g. "BRZone"), or null. */
	AActor* BrHudFindActor(const UWorld* World, const TCHAR* Needle)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && !Actor->IsPendingKillPending() && Actor->GetClass()->GetName().Contains(Needle))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	/** THE SERVER'S CLOCK, which is the only one a replicated deadline can be measured against: a
	 *  client's own World->GetTimeSeconds() starts at zero when ITS level loads, so on a phone that
	 *  joined a host which had been up two minutes every countdown here read two minutes long. The
	 *  same call the zone and the touch HUD already use. */
	double BrHudNow(const UWorld* World)
	{
		const AGameStateBase* State = World ? World->GetGameState() : nullptr;
		return State ? State->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
	}

	/** A field that IS a server world-time stamp (BRPhaseEndTime, EventEndTime): seconds left. */
	bool BrHudDeadline(const UObject* Object, const UWorld* World, float& Out, const TCHAR* A,
		const TCHAR* B = nullptr, const TCHAR* C = nullptr)
	{
		double Value = 0.0;
		if (!World || !BrHudReadNumber(Object, Value, A, B, C) || Value <= 0.0)
		{
			return false;
		}
		Out = FMath::Max(0.f, float(Value - BrHudNow(World)));
		return true;
	}

	/** A world-time deadline turned into seconds left, or a seconds-left field taken as it is. */
	bool BrHudSecondsLeft(const UObject* Object, const UWorld* World, float& Out, const TCHAR* A,
		const TCHAR* B = nullptr, const TCHAR* C = nullptr, const TCHAR* D = nullptr)
	{
		double Value = 0.0;
		if (!BrHudReadNumber(Object, Value, A, B, C, D) || !World)
		{
			return false;
		}
		// Anything larger than ten minutes is a world-time stamp, not a duration: BR phases are short
		// and a server that has been up an hour reports stamps in the thousands.
		const double Now = BrHudNow(World);
		Out = Value > 600.0 ? float(Value - Now) : float(Value);
		Out = FMath::Max(0.f, Out);
		return true;
	}

	const FLinearColor BrInk(0.f, 0.f, 0.f, 0.62f);
	const FLinearColor BrShadow(0.f, 0.f, 0.f, 0.85f);

	FString BrMmSs(float Seconds)
	{
		const int32 Whole = FMath::Max(0, int32(Seconds + 0.5f));
		return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
	}

	/** A dark angular chip with one glyph and one line - the whole HUD vocabulary. */
	TSharedRef<SWidget> BrChip(CampusAngular::EGlyph Glyph, TAttribute<FText> Text, const FLinearColor& Color, int32 Size = 24)
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(9.f))
			.Fill(BrInk)
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(16.f, 7.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(Glyph).Size(float(Size) * 0.8f).Color(Color)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Text).Font(CampusAngular::Numeric(Size)).ColorAndOpacity(FLinearColor::White)
				]
			];
	}
}

// -------------------------------------------------------------------------------------------------
// Reading the world

void FCampusBRView::Read(const APlayerController* PC)
{
	*this = FCampusBRView();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const AGameStateBase* GameState = World->GetGameState();
	AActor* Zone = BrHudFindActor(World, TEXT("BRZone"));
	AActor* Plane = BrHudFindActor(World, TEXT("BRPlane"));
	const APawn* Pawn = PC->GetPawn();
	const APlayerState* PlayerState = PC->PlayerState;

	// Is this a BR match at all? Any of: a BR game state / game mode class, a zone, or a plane.
	const FString StateClass = GameState ? GameState->GetClass()->GetName() : FString();
	bFound = Zone != nullptr || Plane != nullptr || StateClass.Contains(TEXT("BR"));
	if (!bFound)
	{
		return;
	}

	Phase = BrHudReadPhase(GameState);
	if (Phase.IsEmpty() && Zone)
	{
		Phase = BrHudReadPhase(Zone);
	}

	double Number = 0.0;
	// ACampusBRGameState calls it AliveCombatants (humans and bots counted the same way, which is
	// exactly what "ALIVE 50" means to a player); the others are kept as fallbacks.
	if (BrHudReadNumber(GameState, Number, TEXT("AliveCombatants"), TEXT("AliveCount"), TEXT("PlayersAlive"), TEXT("NumAlive")))
	{
		bHasAlive = true;
		Alive = int32(Number);
	}
	// Kills are concrete: ACampusPlayerState replicates them already.
	if (const ACampusPlayerState* Mine = Cast<ACampusPlayerState>(PlayerState))
	{
		Kills = Mine->Kills;
	}
	else if (BrHudReadNumber(PlayerState, Number, TEXT("Kills")))
	{
		Kills = int32(Number);
	}

	int32 Joined = 0;
	if (BrHudReadNumber(GameState, Number, TEXT("Humans"), TEXT("HumansJoined"), TEXT("JoinedHumans"), TEXT("HumanCount")))
	{
		Joined = int32(Number);
	}
	else if (GameState)
	{
		Joined = GameState->PlayerArray.Num();
	}
	// PLAYERS x / 50: the banner counts towards the SESSION's fifty slots, which is what a battle
	// session is, and not towards this host's connection cap - the empty slots become bots.
	if (BrHudReadNumber(GameState, Number, TEXT("TotalCombatants"), TEXT("TotalSlots"), TEXT("MaxAlive")))
	{
		TotalSlots = FMath::Max(1, int32(Number));
	}
	bWaiting = Phase.Contains(TEXT("wait")) || Phase.Contains(TEXT("fill")) || Phase.Contains(TEXT("lobby"));
	// SEALED is the countdown phase: the backend has closed joins and the drop is coming.
	bSealed = Phase.Contains(TEXT("count"));
	bHasFill = Joined > 0;
	Humans = Joined;
	BrHudReadString(GameState, SessionId, TEXT("SessionId"), TEXT("BattleSessionId"));

	float Seconds = 0.f;
	// BRPhaseEndTime is a server world-time stamp: the waiting timeout, then the countdown.
	if (BrHudDeadline(GameState, World, Seconds, TEXT("BRPhaseEndTime"), TEXT("CountdownEndTime"))
		|| BrHudSecondsLeft(GameState, World, Seconds, TEXT("CountdownSeconds"), TEXT("SecondsToStart")))
	{
		// Only a COUNTDOWN gets the big numbers. The waiting phase has its own banner, and a 25-second
		// "25, 24, 23" over an empty lobby is pressure with nothing behind it.
		bHasCountdown = Seconds > 0.f && Phase.Contains(TEXT("count"));
		Countdown = Seconds;
	}

	// The zone: the actor first (it owns the circle), then the game state as a fallback.
	UObject* ZoneSource = Zone ? static_cast<UObject*>(Zone) : const_cast<AGameStateBase*>(GameState);
	// THE CIRCLE YOU ARE JUDGED AGAINST IS THE ONE YOU ARE STANDING IN.
	//
	// This used to read CenterTo / RadiusTo - the NEXT circle - on the grounds that the arrow should
	// point where you have to go. The arrow should; bOutside must not. The next circle is 58% of this
	// one and sits at a random offset inside it, so for the whole of every wait phase any player not
	// already standing in the next circle was told "OUTSIDE THE ZONE" in danger red while they were
	// perfectly safe. That is the HUD half of the phone bug report, and it was true on a correct map
	// as well as on the wrong one.
	//
	// So: the CURRENT circle through the actor's own accessors, which interpolate while it closes and
	// are the same numbers ACampusBRZone::BurnOutside uses on the host. The reflection path below is
	// kept only for a zone actor this HUD does not know (it is written to survive the BR module being
	// replaced), and it now reads CenterFrom / RadiusFrom - the circle that is closing, not its target.
	if (const ACampusBRZone* Circle = Cast<ACampusBRZone>(Zone))
	{
		if (Circle->GetPhase() >= 0 && Circle->GetRadius() > 0.f)
		{
			const FVector2D Middle = Circle->GetCenter();
			ZoneCenter = FVector(Middle.X, Middle.Y, 0.f);
			ZoneRadius = Circle->GetRadius();
			const FVector2D Target = Circle->GetNextCenter();
			NextCenter = FVector(Target.X, Target.Y, 0.f);
			NextRadius = Circle->GetNextRadius();
			bHasZone = true;
		}
	}
	else if (BrHudReadNumber(ZoneSource, Number, TEXT("RadiusFrom"), TEXT("ZoneRadius"), TEXT("CurrentRadius"), TEXT("SafeRadius"))
		&& Number > 0.0)
	{
		ZoneRadius = float(Number);
		if (!BrHudReadVector(ZoneSource, ZoneCenter, TEXT("CenterFrom"), TEXT("ZoneCenter"), TEXT("CurrentCenter")) && Zone)
		{
			// No centre field: the zone actor's own location is the obvious answer.
			ZoneCenter = Zone->GetActorLocation();
		}
		bHasZone = true;
		NextCenter = ZoneCenter;
		NextRadius = ZoneRadius;
		if (BrHudReadNumber(ZoneSource, Number, TEXT("RadiusTo")) && Number > 0.0)
		{
			NextRadius = float(Number);
			// No CenterTo on this zone: the next circle is concentric, which NextCenter already is.
			(void)BrHudReadVector(ZoneSource, NextCenter, TEXT("CenterTo"));
		}
	}
	if (BrHudDeadline(ZoneSource, World, Seconds, TEXT("EventEndTime"), TEXT("NextShrinkTime"), TEXT("ZoneCloseTime"))
		|| BrHudSecondsLeft(ZoneSource, World, Seconds, TEXT("ZoneSecondsLeft"), TEXT("ShrinkIn")))
	{
		bHasZoneClock = Seconds > 0.f;
		ZoneClock = Seconds;
	}
	if (BrHudReadNumber(ZoneSource, Number, TEXT("bShrinking")))
	{
		bZoneClosing = Number > 0.0;
	}

	if (Pawn)
	{
		Me = Pawn->GetActorLocation();
		MeYaw = PC->GetControlRotation().Yaw;
		if (bHasZone)
		{
			// 2D, deliberately: the circle is a column, so the 40 m waiting slab and the 500 m plane
			// are inside it exactly like the ground beneath them. Height must never count - reading
			// this as a 3D distance is one of the ways a player standing in the zone "drains".
			const float Flat = FVector::Dist2D(Me, ZoneCenter);
			DistanceToEdge = Flat - ZoneRadius;
			// AND NOT WHILE BEING DEPLOYED. The flight line starts outside the map square, so in the
			// plane and under the chute everybody is outside a circle that cannot reach them (which is
			// the same test ACampusBRZone::BurnOutside makes before it burns anyone).
			const ACampusCharacter* Body = Cast<ACampusCharacter>(Pawn);
			bOutside = DistanceToEdge > 0.f && !(Body && ACampusBRPlane::IsDeploying(Body));
		}
		if (BrHudReadNumber(Pawn, Number, TEXT("bInPlane"), TEXT("bOnPlane"), TEXT("bAboard"), TEXT("bInAircraft")))
		{
			bInPlane = Number > 0.0;
		}
		if (BrHudReadNumber(Pawn, Number, TEXT("bParachuting"), TEXT("bInParachute"), TEXT("bChuteOpen"), TEXT("bGliding")))
		{
			bParachuting = Number > 0.0;
		}
		// The drop is its own replicated actor (ACampusBRDrop: State = Done | InPlane | Freefall |
		// Parachute, with a Diver pointer back to the character). Find MINE and read its state name -
		// which is also the object the JUMP button calls ServerJump on.
		if (const UWorld* Each = World)
		{
			for (TActorIterator<AActor> It(Each); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor || !Actor->GetClass()->GetName().Contains(TEXT("BRDrop")))
				{
					continue;
				}
				const FObjectPropertyBase* DiverProp = CastField<FObjectPropertyBase>(
					Actor->GetClass()->FindPropertyByName(FName(TEXT("Diver"))));
				const UObject* Who = DiverProp ? DiverProp->GetObjectPropertyValue_InContainer(Actor) : nullptr;
				if (Who != Pawn)
				{
					continue;
				}
				Drop = Actor;
				const FString DiveState = BrHudReadPhase(Actor);
				bInPlane = DiveState.Contains(TEXT("inplane"));
				bFreefall = DiveState.Contains(TEXT("freefall"));
				bParachuting = DiveState.Contains(TEXT("parachute"));
				break;
			}
		}
	}
	// The phase name is the authority when it says so; the pawn flags are the fallback.
	if (Phase.Contains(TEXT("plane")) || Phase.Contains(TEXT("drop")) || Phase.Contains(TEXT("board")))
	{
		bInPlane = true;
	}
	if (Phase.Contains(TEXT("para")) || Phase.Contains(TEXT("chute")) || Phase.Contains(TEXT("glide")))
	{
		bParachuting = true;
	}

	if (BrHudReadNumber(PlayerState, Number, TEXT("Placement"), TEXT("MyPlace"), TEXT("Place"), TEXT("FinalPlace"))
		&& Number > 0.0)
	{
		bHasPlacement = true;
		Placement = int32(Number);
	}
	else if (BrHudReadNumber(GameState, Number, TEXT("Placement"), TEXT("MyPlace"), TEXT("FinalPlace")) && Number > 0.0)
	{
		bHasPlacement = true;
		Placement = int32(Number);
	}
	// The squad that won, and whether it is mine. Placement is server-side bookkeeping today, so
	// BOOYAH comes off WinningSquad - and when neither is readable the screen says MATCH OVER, which
	// is the one thing we always know.
	double Winner = -1.0;
	if (BrHudReadNumber(GameState, Winner, TEXT("WinningSquad")) && Winner >= 0.0)
	{
		bEnded = true;
		double Mine = -2.0;
		if (BrHudReadNumber(PlayerState, Mine, TEXT("Squad"), TEXT("SquadId"), TEXT("BRSquad"), TEXT("TeamId")))
		{
			bWon = int32(Mine) == int32(Winner);
		}
	}
	bEnded = bEnded || Phase.Contains(TEXT("end")) || Phase.Contains(TEXT("over")) || Phase.Contains(TEXT("result"))
		|| bHasPlacement;
}

// -------------------------------------------------------------------------------------------------
// The zone dial: the circle, where I am in it, and the arrow to the middle

void SCampusBRZoneDial::Construct(const FArguments& InArgs)
{
	View = InArgs._View;
	SetCanTick(false);
}

int32 SCampusBRZoneDial::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!View || !View->bHasZone)
	{
		return LayerId;
	}
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FVector2f Middle = Size * 0.5f;
	const float Dial = FMath::Min(Size.X, Size.Y) * 0.5f - 6.f;
	if (Dial <= 4.f)
	{
		return LayerId;
	}
	const FPaintGeometry Paint = AllottedGeometry.ToPaintGeometry();
	// THE ZONE IS BLUE, the same #3aa0ff as the wall standing on it in the world and the circle on the
	// map (TouchStyle::ZoneBlue / CampusBRWall::Colour). Being outside it does not change the colour -
	// it brightens it - because "blue" has to keep meaning "the zone" and nothing else.
	const FLinearColor ZoneInk(FColor(0x3A, 0xA0, 0xFF));
	const FLinearColor Ring = View->bOutside ? ZoneInk : ZoneInk.CopyWithNewOpacity(0.75f);

	// The circle, 28 segments: one draw element, no texture, no per-frame allocation beyond the array.
	TArray<FVector2f> Circle;
	Circle.Reserve(29);
	for (int32 Step = 0; Step <= 28; ++Step)
	{
		const float Angle = (2.f * PI * float(Step)) / 28.f;
		Circle.Add(Middle + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Dial);
	}
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Paint, Circle, ESlateDrawEffect::None,
		Ring.CopyWithNewOpacity(0.85f), true, 2.f);

	// Where I am inside it, clamped to the rim so an out-of-circle player still reads as "that way".
	const float Flat = FVector::Dist2D(View->Me, View->ZoneCenter);
	const float Fraction = View->ZoneRadius > 0.f ? FMath::Min(Flat / View->ZoneRadius, 1.f) : 0.f;
	FVector2f ToCentre(float(View->ZoneCenter.X - View->Me.X), float(View->ZoneCenter.Y - View->Me.Y));
	// Screen space: X right, Y down; world Y is "right" on a top-down map, world X is "up".
	FVector2f Direction(ToCentre.Y, -ToCentre.X);
	if (Direction.SizeSquared() > 1.f)
	{
		Direction.Normalize();
	}
	else
	{
		Direction = FVector2f(0.f, -1.f);
	}
	// Turn the arrow with the camera, so "up" on the dial is always "straight ahead".
	const float Yaw = FMath::DegreesToRadians(View->MeYaw);
	const FVector2f Turned(
		Direction.X * FMath::Cos(Yaw) + Direction.Y * FMath::Sin(Yaw),
		-Direction.X * FMath::Sin(Yaw) + Direction.Y * FMath::Cos(Yaw));

	const FVector2f Dot = Middle - Turned * (Dial * Fraction);
	TArray<FVector2f> Pip;
	Pip.Reserve(9);
	for (int32 Step = 0; Step <= 8; ++Step)
	{
		const float Angle = (2.f * PI * float(Step)) / 8.f;
		Pip.Add(Dot + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * 4.f);
	}
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Paint, Pip, ESlateDrawEffect::None,
		FLinearColor::White, true, 2.f);

	// THE NEXT CIRCLE, WHITE, inside the blue one: where this circle is closing to, drawn to scale and
	// at its real offset, so the dial answers "which way do I rotate" without opening the map.
	if (View->NextRadius > 0.f && View->ZoneRadius > 0.f && View->NextRadius < View->ZoneRadius - 1.f)
	{
		const float Shrunk = Dial * (View->NextRadius / View->ZoneRadius);
		FVector2f Offset(float(View->NextCenter.Y - View->ZoneCenter.Y), -float(View->NextCenter.X - View->ZoneCenter.X));
		Offset *= Dial / View->ZoneRadius;
		const FVector2f Shifted(
			Offset.X * FMath::Cos(Yaw) + Offset.Y * FMath::Sin(Yaw),
			-Offset.X * FMath::Sin(Yaw) + Offset.Y * FMath::Cos(Yaw));
		TArray<FVector2f> Next;
		Next.Reserve(21);
		for (int32 Step = 0; Step <= 20; ++Step)
		{
			const float Angle = (2.f * PI * float(Step)) / 20.f;
			Next.Add(Middle + Shifted + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Shrunk);
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Paint, Next, ESlateDrawEffect::None,
			FLinearColor(1.f, 1.f, 1.f, 0.8f), true, 1.5f);
	}

	// The arrow: a chevron at the rim pointing the way to the middle of the circle.
	const FVector2f Tip = Middle + Turned * Dial;
	const FVector2f Side(-Turned.Y, Turned.X);
	TArray<FVector2f> Arrow;
	Arrow.Add(Tip - Turned * 16.f + Side * 9.f);
	Arrow.Add(Tip);
	Arrow.Add(Tip - Turned * 16.f - Side * 9.f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, Paint, Arrow, ESlateDrawEffect::None, Ring, true, 3.f);
	return LayerId + 3;
}

// -------------------------------------------------------------------------------------------------
// The HUD

void SCampusBRHud::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;

	// Itself, not just its child: AddViewportWidgetContent gives this widget the whole screen, and at
	// the default Visible it sits above the touch controls (Z 5) and eats every tap. Children stay hittable.
	SetVisibility(EVisibility::SelfHitTestInvisible);

	// Every piece is a visibility lambda over the cached view: built once, repainted per frame, and
	// NOTHING is rebuilt when the state changes. On a budget phone that is the whole game.
	ChildSlot
	[
		SNew(SOverlay)
		.Visibility_Lambda([this]()
		{
			// Not a BR match (or nothing readable yet): the widget is not there at all, so it cannot
			// eat a tap meant for the controls underneath it.
			//
			// AND NOTHING AT ALL ONCE THE MATCH IS OVER. The result screen (SCampusBREnd) is the only
			// end-of-match surface; this HUD used to paint its own BOOYAH! card and the two stacked.
			// Collapsing the whole widget - not just that card - also takes the ALIVE / KILLS chips,
			// the squad list and the kill feed off the victory photo, which is the point of it.
			return View.IsBattleRoyale() && !View.bEnded
				? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
		})
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Visibility(EVisibility::SelfHitTestInvisible).Padding(FMargin(18.f, 10.f))
			[
				SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
				// ---- top centre: ALIVE and KILLS. Top LEFT and RIGHT belong to the team list and
				// the gear (SCampusTouchControls), which this widget does not touch.
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top)
				[
					SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]() { return View.bHasAlive ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						[
							BrChip(CampusAngular::EGlyph::Friends, TAttribute<FText>::CreateLambda([this]()
							{
								return FText::Format(LOCTEXT("BrAlive", "ALIVE  {0}"), FText::AsNumber(View.Alive));
							}), CampusAngular::Accent)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						BrChip(CampusAngular::EGlyph::Target, TAttribute<FText>::CreateLambda([this]()
						{
							return FText::Format(LOCTEXT("BrKills", "KILLS  {0}"), FText::AsNumber(View.Kills));
						}), CampusAngular::Danger)
					]
				]
				// ---- the zone row, directly under the chips: the clock and, beside it, the dial.
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 54.f, 0.f, 0.f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						return (View.bHasZoneClock || View.bOutside) && !View.bEnded
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						SNew(SCampusAngularPanel)
						.Shape(FCampusShape::Notched(8.f))
						.Fill(BrInk)
						.Border_Lambda([this]() { return View.bOutside ? CampusBRHudZone::Ink : CampusAngular::Hairline; })
						.BorderWidth(1.f)
						.ContentPadding(FMargin(14.f, 6.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
							[
								SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Clock).Size(17.f)
								.Color_Lambda([this]() { return View.bOutside ? CampusBRHudZone::Ink : CampusAngular::Accent; })
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text_Lambda([this]()
								{
									// Colour is never the only signal: outside the circle the WORDS change too.
									if (View.bOutside)
									{
										return FText::Format(LOCTEXT("BrOutside", "MOVE TO SAFE ZONE  ·  {0} M"),
											FText::AsNumber(FMath::Max(1, int32(View.DistanceToEdge / 100.f))));
									}
									return View.bZoneClosing
										? FText::Format(LOCTEXT("BrZoneNow", "ZONE CLOSING  ·  {0}"),
											FText::FromString(BrMmSs(View.ZoneClock)))
										: FText::Format(LOCTEXT("BrZoneIn", "ZONE SHRINKING IN  {0}"),
											FText::FromString(BrMmSs(View.ZoneClock)));
								})
								.Font(CampusAngular::Display(19))
								.ColorAndOpacity_Lambda([this]()
								{
									return View.bOutside ? CampusBRHudZone::Ink : FLinearColor::White;
								})
								.ShadowOffset(FVector2D(2.f, 2.f))
								.ShadowColorAndOpacity(BrShadow)
							]
						]
					]
				]
				// ---- the dial, right edge at eye height: the circle and the way to it.
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(132.f).HeightOverride(132.f)
					.Visibility_Lambda([this]()
					{
						return View.bHasZone && !View.bEnded ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						SNew(SCampusBRZoneDial).View(&View)
					]
				]
				// ---- waiting area: the fill banner and, under it, the big countdown.
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SBox)
						.Visibility_Lambda([this]()
						{
							// The waiting AREA, not just "a player count is readable": the count is
							// readable all match long, so this banner used to sit over the middle of the
							// screen through the drop and the whole fight.
							return (View.bWaiting || View.bSealed) && View.bHasFill && !View.bEnded
								&& !View.bInPlane && !View.bFreefall && !View.bParachuting
								? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						})
						[
							SNew(SCampusAngularPanel)
							.Shape(FCampusShape::Wedge(18.f, 12.f))
							.Fill(FLinearColor(0.f, 0.f, 0.f, 0.72f))
							.Border(CampusAngular::Accent)
							.BorderWidth(2.f)
							.ContentPadding(FMargin(26.f, 12.f))
							[
								SNew(SVerticalBox)
								// PLAYERS 7 / 50, live off the replicated game state: it climbs as the
								// session fills, which is the whole point of walking in before it is full.
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
								[
									SNew(STextBlock)
									.Text_Lambda([this]()
									{
										return FText::Format(LOCTEXT("BrPlayers", "PLAYERS {0} / {1}"),
											FText::AsNumber(View.Humans), FText::AsNumber(View.TotalSlots));
									})
									.Font(CampusAngular::Numeric(28))
									.ColorAndOpacity(FLinearColor::White)
								]
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
								[
									SNew(STextBlock)
									.Text_Lambda([this]()
									{
										// Two states, one line: still filling, or sealed and dropping.
										if (!View.bSealed)
										{
											return LOCTEXT("BrWaiting", "WAITING FOR PLAYERS");
										}
										return FText::Format(LOCTEXT("BrStarting", "MATCH STARTING {0}..."),
											FText::AsNumber(FMath::Max(0, int32(View.Countdown + 0.5f))));
									})
									.Font(CampusAngular::Display(18))
									.ColorAndOpacity(CampusAngular::Accent)
								]
								// THE SESSION ID: what a player reads out to a friend, or to us when they
								// report a bug. Absent in editor play, where it simply does not draw.
								+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
								[
									SNew(STextBlock)
									.Visibility_Lambda([this]()
									{
										return View.SessionId.IsEmpty()
											? EVisibility::Collapsed : EVisibility::HitTestInvisible;
									})
									.Text_Lambda([this]() { return FText::FromString(View.SessionId); })
									.Font(CampusAngular::Body(13))
									.ColorAndOpacity(CampusAngular::TextMuted)
								]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Visibility_Lambda([this]()
						{
							return View.bHasCountdown && !View.bEnded ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						})
						.Text_Lambda([this]() { return FText::AsNumber(FMath::Max(0, int32(View.Countdown + 0.5f))); })
						.Font(CampusAngular::Numeric(110))
						.ColorAndOpacity(CampusAngular::Accent)
						.ShadowOffset(FVector2D(4.f, 4.f))
						.ShadowColorAndOpacity(BrShadow)
					]
				]
				// ---- the plane: one 200 x 200 JUMP, bottom centre, clear of both thumbs' controls.
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 24.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 12.f)
					[
						SNew(STextBlock)
						.Visibility_Lambda([this]()
						{
							return (View.bInPlane || View.bFreefall || View.bParachuting) && !View.bEnded
								? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						})
						.Text_Lambda([this]()
						{
							if (bJumpUnavailable)
							{
								return LOCTEXT("BrJumpSoon", "THE DROP IS NOT WIRED UP IN THIS BUILD YET");
							}
							if (View.bParachuting)
							{
								return LOCTEXT("BrChute", "HOLD FORWARD TO GLIDE FURTHER  ·  STEER TO YOUR LANDING");
							}
							if (View.bFreefall)
							{
								return LOCTEXT("BrFreefall", "OPEN IT YOURSELF  ·  OR IT OPENS AT 40 m");
							}
							return LOCTEXT("BrJumpHint", "TAP JUMP WHEN THE VALLEY IS UNDER YOU");
						})
						.Font(CampusAngular::Display(18))
						.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.82f))
						.ShadowOffset(FVector2D(2.f, 2.f))
						.ShadowColorAndOpacity(BrShadow)
						.Justification(ETextJustify::Center)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						// ONE button, two jobs - which is exactly what the drop does: the first press
						// leaves the plane, the second pulls the canopy. It must therefore survive into
						// freefall, where it used to collapse and leave the manual chute unreachable on
						// a phone. It goes away once the canopy is out, because there is nothing left
						// to press.
						SNew(SBox).WidthOverride(200.f).HeightOverride(200.f)
						.Visibility_Lambda([this]()
						{
							return (View.bInPlane || View.bFreefall) && !View.bParachuting && !View.bEnded
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SCampusAngularButton)
							.Shape(FCampusShape::Clipped(28.f))
							.Fill(FLinearColor(0.96f, 0.72f, 0.f, 0.92f))
							.Border(FLinearColor::White)
							.BorderWidth(3.f)
							.ContentPadding(FMargin(0.f))
							.OnClicked(FOnClicked::CreateLambda([this]() { BRJump(); return FReply::Handled(); }))
							[
								SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text_Lambda([this]()
									{
										return View.bFreefall ? LOCTEXT("BrOpenChute", "OPEN\nCHUTE")
											: LOCTEXT("BrJump", "JUMP");
									})
									.Font_Lambda([this]()
									{
										// "OPEN CHUTE" over two lines needs a smaller face to stay inside
										// the same 200 x 200 touch target.
										return CampusAngular::Display(View.bFreefall ? 30 : 46);
									})
									.Justification(ETextJustify::Center)
									.ColorAndOpacity(FLinearColor::Black)
								]
							]
						]
					]
				]
			]
		]
	];
}

void SCampusBRHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ReadIn -= InDeltaTime;
	if (ReadIn > 0.f)
	{
		return;
	}
	ReadIn = 0.25f;
	View.Read(Controller.Get());
}

void SCampusBRHud::BRJump()
{
	// THE HOOK. The BR pass owns the jump; this finds whatever no-argument UFUNCTION it exposed, on
	// the pawn first and then the controller. No match = the button says so instead of lying.
	static const TCHAR* Names[] = { TEXT("ServerJump"), TEXT("BRJump"), TEXT("BrJump"), TEXT("JumpFromPlane"),
		TEXT("ServerBRJump"), TEXT("Dive"), TEXT("LeavePlane") };
	APlayerController* PC = Controller.Get();
	// The drop actor first - ACampusBRDrop::ServerJump is the real hook - then the pawn, then the
	// controller, so this still works if the drop moves onto the character later.
	UObject* Targets[3] = { View.Drop.Get(), PC ? static_cast<UObject*>(PC->GetPawn()) : nullptr, PC };
	for (UObject* Target : Targets)
	{
		if (!Target)
		{
			continue;
		}
		for (const TCHAR* Name : Names)
		{
			UFunction* Function = Target->FindFunction(FName(Name));
			// Only a parameterless function: calling one that expects arguments with a null frame
			// would read uninitialised memory on the other side.
			if (Function && Function->NumParms == 0)
			{
				Target->ProcessEvent(Function, nullptr);
				bJumpUnavailable = false;
				return;
			}
		}
	}
	bJumpUnavailable = true;
}

#undef LOCTEXT_NAMESPACE
