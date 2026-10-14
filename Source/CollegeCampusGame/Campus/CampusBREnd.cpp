// BATTLE ROYALE result screen - "CAMPUS KING". See Campus/CampusBREnd.h for why there is exactly one
// of these and how the 3D line-up behind it is made.

#include "Campus/CampusBREnd.h"

#include "Campus/CampusBR.h"
#include "Campus/CampusBRGameMode.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusUI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Math/RandomStream.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusBREnd"

namespace
{
	// ---- the screen's own tokens. Gold, one deep gold, ink and a shadow: nothing else. ----------
	const FLinearColor EndGold(1.f, 0.78f, 0.24f, 1.f);
	const FLinearColor EndGoldDeep(0.80f, 0.50f, 0.07f, 1.f);
	const FLinearColor EndInk(0.015f, 0.02f, 0.028f, 0.88f);
	const FLinearColor EndInkSoft(0.02f, 0.025f, 0.035f, 0.72f);
	const FLinearColor EndHair(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor EndShadow(0.f, 0.f, 0.f, 0.85f);
	const FLinearColor EndText(1.f, 1.f, 1.f, 0.95f);
	const FLinearColor EndMuted(1.f, 1.f, 1.f, 0.55f);

	/** The rank diamonds, 2 / 3 / 4. The NUMBER is the signal; the colour only reinforces it. */
	const FLinearColor EndRankTone[3] = {
		FLinearColor(0.33f, 0.58f, 0.92f, 1.f),
		FLinearColor(0.95f, 0.60f, 0.16f, 1.f),
		FLinearColor(0.88f, 0.38f, 0.14f, 1.f),
	};

	/** The title block's design size. The punch animation scales around its own centre by hand (see
	 *  SCampusBREnd::Tick), so it has to know how big it is - which it only does if it is pinned. */
	constexpr float TitleW = 680.f;
	constexpr float TitleH = 98.f;

	/** "18 : 26" - the duration face on the top-right chip and in YOUR STATS. */
	FText EndClock(float Seconds)
	{
		const int32 Whole = FMath::Max(0, FMath::RoundToInt(Seconds));
		return FText::FromString(FString::Printf(TEXT("%02d : %02d"), Whole / 60, Whole % 60));
	}

	/** 2148, 4598 - grouped, so a four-digit damage figure is read and not counted. */
	FText EndGrouped(int32 Value)
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(true);
		return FText::AsNumber(Value, &Options);
	}

	/** "6.4 km" from centimetres. One decimal: two is noise, none loses every short match. */
	FText EndKilometres(float Centimetres)
	{
		FNumberFormattingOptions Options;
		Options.SetMinimumFractionalDigits(1);
		Options.SetMaximumFractionalDigits(1);
		return FText::Format(LOCTEXT("EndKm", "{0} km"), FText::AsNumber(Centimetres / 100000.f, &Options));
	}

	/** Ease-out cubic: fast in, slow stop. Everything that ENTERS this screen uses it. */
	float EndEaseOut(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		return 1.f - FMath::Pow(1.f - Clamped, 3.f);
	}

	/** 0..1 of a beat that starts Delay seconds in and runs for Duration. */
	float EndBeat(float Life, float Delay, float Duration)
	{
		return FMath::Clamp((Life - Delay) / FMath::Max(Duration, UE_KINDA_SMALL_NUMBER), 0.f, 1.f);
	}

	/** The title's punch: in from 0.72, past 1 by a few per cent, settling on 1. */
	float EndPunch(float Alpha)
	{
		const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
		const float Base = 0.72f + 0.28f * EndEaseOut(Clamped);
		// One half-sine of overshoot, faded out by the end so the settle is exact and not "nearly".
		return Base + FMath::Sin(Clamped * UE_PI) * 0.09f * (1.f - Clamped);
	}

	/** A heading with the screen's one shadow, used by every panel title. */
	TSharedRef<SWidget> EndHeading(const FText& Label, const FLinearColor& Tint, int32 Size = 20)
	{
		return SNew(STextBlock)
			.Text(Label)
			.Font(CampusAngular::Display(Size))
			.ColorAndOpacity(Tint)
			.ShadowOffset(FVector2D(2.f, 2.f))
			.ShadowColorAndOpacity(EndShadow);
	}

	/** The dark notched plate every panel on this screen sits in. */
	TSharedRef<SWidget> EndPanel(TSharedRef<SWidget> Content, FMargin Padding = FMargin(18.f, 14.f))
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(14.f))
			.Fill(EndInk)
			.Border(EndHair)
			.BorderWidth(1.f)
			.ContentPadding(Padding)
			[
				Content
			];
	}
}

// -------------------------------------------------------------------------------------------------
// The stage

namespace CampusVictoryStage
{
	const FStagePose& Pose(int32 Rank)
	{
		// LEFT TO RIGHT: #2, MVP, #3, #4 - the reference photo's order, and the one a player reads as
		// "the best one is the one in the middle". The MVP stands a step forward and a tenth larger;
		// everybody else leans in towards them so the four read as a group and not as a rail.
		//
		// The laterals land the four at roughly 32 / 45 / 59 / 71 per cent across a 16:9 screen, which
		// is the gap the reference photo has: wide enough that nobody overlaps, tight enough that the
		// YOUR STATS panel on the left and SQUAD RESULTS on the right both stay clear of a shoulder.
		static const FStagePose Poses[MaxFigures] = {
			{  -35.f,  50.f,  0.f, 1.10f },  // 0: MVP
			{ -158.f, -15.f, -5.f, 1.00f },  // 1: to the MVP's left on screen
			{   82.f, -10.f,  5.f, 1.00f },  // 2
			{  184.f, -30.f,  8.f, 1.00f },  // 3, furthest back
		};
		return Poses[FMath::Clamp(Rank, 0, MaxFigures - 1)];
	}

	float HorizontalFovDeg(float InAspect)
	{
		// THE VERTICAL FRAMING IS THE FIXED ONE. A phone that is 20:9 instead of 16:9 would otherwise
		// push the figures' feet off the bottom of the screen, taking the name plates with them.
		const float VerticalHalfRad = FMath::DegreesToRadians(VerticalHalfDeg);
		const float HorizontalHalfRad = FMath::Atan(FMath::Tan(VerticalHalfRad) * FMath::Max(InAspect, 0.5f));
		return FMath::RadiansToDegrees(HorizontalHalfRad) * 2.f;
	}

	float ViewportAspect()
	{
		if (GEngine && GEngine->GameViewport)
		{
			FVector2D Size = FVector2D::ZeroVector;
			GEngine->GameViewport->GetViewportSize(Size);
			if (Size.X > 0.0 && Size.Y > 0.0)
			{
				return float(Size.X / Size.Y);
			}
		}
		// No viewport to ask (a dedicated server, an editor pass before the window exists): 16:9 is
		// the landscape phone this game is laid out for, and the fallback never has to be exact.
		return 16.f / 9.f;
	}

	float ScreenFraction(int32 Rank, float InAspect)
	{
		const FStagePose& Spot = Pose(Rank);
		// How far the camera is from THIS figure, not from the centre of the row: one standing further
		// back is further away, so the same lateral offset covers fewer pixels.
		const float Distance = FMath::Max(50.f, CameraDistanceCm - Spot.DepthCm);
		const float HalfWidth = Distance * FMath::Tan(FMath::DegreesToRadians(HorizontalFovDeg(InAspect) * 0.5f));
		return 0.5f + Spot.LateralCm / FMath::Max(2.f * HalfWidth, 1.f);
	}

	float ScreenFootFraction(int32 Rank)
	{
		const FStagePose& Spot = Pose(Rank);
		const float Distance = FMath::Max(50.f, CameraDistanceCm - Spot.DepthCm);
		// The feet sit on the stage floor, CameraHeightCm below the lens. Negative = below the axis.
		const float FeetDeg = FMath::RadiansToDegrees(FMath::Atan2(-CameraHeightCm, Distance));
		const float Relative = FeetDeg - CameraPitchDeg;
		return FMath::Clamp(0.5f - Relative / (2.f * VerticalHalfDeg), 0.f, 1.f);
	}

	float FacingYaw(int32 Rank)
	{
		const FStagePose& Spot = Pose(Rank);
		// At yaw 0 a figure faces +X, straight down the stage axis, which only points AT the camera for
		// something standing dead centre. Turn it towards the lens first, then lean it into the group.
		const float Distance = FMath::Max(50.f, CameraDistanceCm - Spot.DepthCm);
		const float TowardsCamera = FMath::RadiansToDegrees(FMath::Atan2(Spot.LateralCm, Distance));
		return TowardsCamera + Spot.YawDeg;
	}
}

// -------------------------------------------------------------------------------------------------
// The view

void FCampusBREndView::Read(const APlayerController* InController)
{
	*this = FCampusBREndView();
	EndedAt = FDateTime::Now();

	const UWorld* World = InController ? InController->GetWorld() : nullptr;
	const ACampusBRGameState* State = World ? World->GetGameState<ACampusBRGameState>() : nullptr;
	if (!State)
	{
		return;
	}
	bFound = true;
	SquadSize = FMath::Max(1, State->TeamSize);
	TotalSquads = FMath::Max(1, State->TotalSquads);
	MatchSeconds = FMath::Max(0, State->MatchSeconds);

	// THE LEVEL THIS SCREEN IS STANDING IN is the map, and it needs no replication to be sure of it.
	// The replicated id is only a label and it has three spellings for the valley alone
	// (CampusBR::LevelForMap spells this out): the API calls it "real", the client catalogue
	// "VarsityValley", and the game state "RealMap". That folding is the fallback now, for a level the
	// catalogue does not know, and anything it still cannot place is the valley - which is where a
	// battle session runs unless the server named another map.
	FName MapId = CampusCatalog::MapIdForLevel(World->GetMapName());
	if (MapId.IsNone())
	{
		MapId = CampusBR::CatalogMapFor(State->MatchMap.ToString());
	}
	if (MapId.IsNone())
	{
		MapId = FName(TEXT("VarsityValley"));
	}
	const int32 MapIndex = CampusCatalog::FindMap(MapId);
	MapName = MapIndex == INDEX_NONE ? MapId.ToString() : FString(CampusCatalog::Map(MapIndex).Name);
	MapArt = MapIndex == INDEX_NONE ? nullptr : CampusCatalog::Map(MapIndex).Art;

	const ACampusPlayerState* Mine = InController->GetPlayerState<ACampusPlayerState>();
	const int32 MySquad = CampusBR::GetSquadId(Mine);
	bWon = MySquad != 0 && State->WinningSquad == MySquad;
	if (Mine)
	{
		Kills = Mine->Kills;
		Headshots = Mine->Headshots;
		Damage = Mine->DamageDealt;
		SurvivalSeconds = Mine->SurvivalSeconds;
		DistanceCm = Mine->DistanceCm;
		// The SQUAD's place is the one the screen shows. Falling back to the combatant placement is
		// wrong by a little; showing nothing at all is wrong by a lot.
		const int32 Raw = Mine->SquadPlacement > 0 ? Mine->SquadPlacement
			: (bWon ? 1 : Mine->Placement);
		Placement = FMath::Clamp(Raw, bWon ? 1 : 0, TotalSquads);
	}
	if (bWon)
	{
		Placement = 1;
	}

	for (const APlayerState* Each : State->PlayerArray)
	{
		const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Each);
		if (!Campus)
		{
			continue;
		}
		// Placement is written the moment a combatant is eliminated, so zero IS still standing - and
		// so is 1, which is what the final whistle writes on everybody left.
		const bool bStanding = Campus->Placement == 0 || Campus->Placement == 1;

		FCampusBREndRow& Row = Summary.AddDefaulted_GetRef();
		Row.Name = Campus->GetPlayerName();
		Row.Kills = Campus->Kills;
		Row.Placement = Campus->Placement > 0 ? Campus->Placement : 1;
		Row.bLocal = Campus == Mine;

		const bool bMySquad = MySquad != 0 ? Campus->Squad == MySquad : Campus == Mine;
		if (!bMySquad)
		{
			continue;
		}
		FCampusBREndMember& Member = Squad.AddDefaulted_GetRef();
		Member.Name = Campus->GetPlayerName();
		Member.Kills = Campus->Kills;
		Member.Damage = Campus->DamageDealt;
		Member.bAlive = bStanding;
		Member.bLocal = Campus == Mine;
		SquadKills += Campus->Kills;
		SquadDamage += Campus->DamageDealt;
		SquadAlive += bStanding ? 1 : 0;
	}

	// MOST KILLS IS THE MVP, and after that the most damage - two squadmates on four kills each are
	// not the same player, and a coin toss between them would reshuffle the line-up every rebuild.
	Squad.Sort([](const FCampusBREndMember& A, const FCampusBREndMember& B)
	{
		if (A.Kills != B.Kills)
		{
			return A.Kills > B.Kills;
		}
		if (!FMath::IsNearlyEqual(A.Damage, B.Damage))
		{
			return A.Damage > B.Damage;
		}
		if (A.bLocal != B.bLocal)
		{
			return A.bLocal;
		}
		return A.Name < B.Name;
	});
	if (Squad.Num() > CampusVictoryStage::MaxFigures)
	{
		Squad.SetNum(CampusVictoryStage::MaxFigures);
	}
	Summary.Sort([](const FCampusBREndRow& A, const FCampusBREndRow& B)
	{
		if (A.Placement != B.Placement)
		{
			return A.Placement < B.Placement;
		}
		return A.Kills > B.Kills;
	});
}

// -------------------------------------------------------------------------------------------------
// Backdrop and confetti

void SCampusBREndBackdrop::Construct(const FArguments& InArgs)
{
	bWarm = InArgs._Warm;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SCampusBREndBackdrop::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	if (Size.X <= 0.f || Size.Y <= 0.f)
	{
		return LayerId;
	}
	const FCampusShape Rect;
	// 1. The whole frame, warm at the top and sinking into the dark at the bottom, so the plates and
	//    the buttons along the bottom edge have something solid to sit on.
	const FLinearColor Top = bWarm ? FLinearColor(0.42f, 0.26f, 0.04f, 0.34f) : FLinearColor(0.05f, 0.07f, 0.10f, 0.42f);
	const FLinearColor Bottom = bWarm ? FLinearColor(0.03f, 0.02f, 0.f, 0.74f) : FLinearColor(0.01f, 0.015f, 0.02f, 0.76f);
	CampusAngular::DrawVerticalFade(OutDrawElements, LayerId, AllottedGeometry, FVector2f::ZeroVector, Size, Rect, Top, Bottom);

	// 2. A darker bar across the top third: the logo, the title and the clock all live up there and
	//    the sky behind them is the brightest thing on screen.
	CampusAngular::DrawVerticalFade(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2f::ZeroVector,
		FVector2f(Size.X, Size.Y * 0.34f), Rect, FLinearColor(0.f, 0.f, 0.f, 0.52f), FLinearColor(0.f, 0.f, 0.f, 0.f));

	// 3. The vignette. Both sides, a quarter of the width each, so the panels read as being ON something.
	const FLinearColor Edge(0.f, 0.f, 0.f, 0.58f);
	const FLinearColor Clear(0.f, 0.f, 0.f, 0.f);
	const float Band = Size.X * 0.26f;
	CampusAngular::DrawHorizontalFade(OutDrawElements, LayerId + 2, AllottedGeometry, FVector2f::ZeroVector,
		FVector2f(Band, Size.Y), Rect, Edge, Clear);
	CampusAngular::DrawHorizontalFade(OutDrawElements, LayerId + 2, AllottedGeometry, FVector2f(Size.X - Band, 0.f),
		FVector2f(Band, Size.Y), Rect, Clear, Edge);
	return LayerId + 3;
}

void SCampusBREndConfetti::Construct(const FArguments& InArgs)
{
	bCelebrate = InArgs._Celebrate;
	SetVisibility(EVisibility::HitTestInvisible);
}

void SCampusBREndConfetti::Seed(float Height)
{
	Flakes.Reset(NumFlakes);
	FRandomStream Stream(0x5EEDC0DE);
	for (int32 Index = 0; Index < NumFlakes; ++Index)
	{
		FFlake& Flake = Flakes.AddDefaulted_GetRef();
		Flake.X = Stream.FRand();
		// Spread the first shower over twice the screen so it is already falling when it appears.
		Flake.Y = -Stream.FRandRange(0.f, Height * 2.f);
		Flake.Fall = Stream.FRandRange(42.f, 118.f);
		Flake.Drift = Stream.FRandRange(-16.f, 16.f);
		Flake.Spin = Stream.FRandRange(1.4f, 3.6f);
		Flake.Phase = Stream.FRandRange(0.f, 6.283f);
		Flake.Size = Stream.FRandRange(5.f, 11.f);
		Flake.Tone = Stream.FRand();
	}
	SeededHeight = Height;
}

void SCampusBREndConfetti::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bCelebrate)
	{
		return;
	}
	const float Height = FMath::Max(1.f, float(AllottedGeometry.GetLocalSize().Y));
	if (Flakes.Num() != NumFlakes || !FMath::IsNearlyEqual(SeededHeight, Height, 1.f))
	{
		Seed(Height);
	}
	Life += InDeltaTime;
	for (FFlake& Flake : Flakes)
	{
		Flake.Y += Flake.Fall * InDeltaTime;
		if (Flake.Y > Height + 40.f)
		{
			Flake.Y = -40.f;
		}
	}
}

int32 SCampusBREndConfetti::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!bCelebrate || Flakes.Num() == 0)
	{
		return LayerId;
	}
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	for (const FFlake& Flake : Flakes)
	{
		// A ribbon tumbling end over end: its WIDTH breathes with the spin, which is what rotation
		// looks like on a flat sliver and costs one quad instead of a transform per flake.
		const float Turn = FMath::Cos(Life * Flake.Spin + Flake.Phase);
		const float Width = FMath::Max(1.5f, Flake.Size * FMath::Abs(Turn));
		const float X = Flake.X * Size.X + FMath::Sin(Life * 0.8f + Flake.Phase) * Flake.Drift;
		const FLinearColor Tone = FMath::Lerp(EndGold, EndGoldDeep, Flake.Tone)
			.CopyWithNewOpacity(0.55f + 0.35f * FMath::Abs(Turn));
		CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId, AllottedGeometry,
			FVector2f(X, Flake.Y), FVector2f(Width, Flake.Size * 1.5f),
			FCampusShape::Slanted(Width * 0.6f), Tone);
	}
	return LayerId + 1;
}

// -------------------------------------------------------------------------------------------------
// The screen

void SCampusBREnd::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	Aspect = CampusVictoryStage::ViewportAspect();
	// Read now only for the backdrop's colour - the winning squad rides along with the phase, so that
	// one is reliable immediately. Everything else is read again in BuildScreen.
	View.Read(Controller.Get());
	// VISIBLE, not SelfHitTestInvisible: the match is over and nothing behind this may be tapped.
	SetVisibility(EVisibility::Visible);

	ChildSlot
	[
		SNew(SCampusBREndBackdrop).Warm(View.bWon)
	];
}

void SCampusBREnd::BuildScreen()
{
	Entrances.Reset();
	TitleBlock.Reset();
	TitleShine.Reset();
	CrownBlock.Reset();

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusBREndBackdrop).Warm(View.bWon)
		]
		+ SOverlay::Slot()
		[
			SNew(SCampusBREndConfetti).Celebrate(View.bWon && WantsMotion())
		]
		// The name plates hang off the figures, so they are anchored in screen fractions of their own
		// and not inside the safe zone: moving them in would walk them off their characters.
		+ SOverlay::Slot()
		[
			BuildPlates()
		]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(20.f, 12.f))
			[
				SNew(SConstraintCanvas)
				+ SConstraintCanvas::Slot().Anchors(FAnchors(0.f, 0.f)).Alignment(FVector2D(0.f, 0.f)).AutoSize(true)
				[
					Enter(BuildIdentity(), 0.05f)
				]
				+ SConstraintCanvas::Slot().Anchors(FAnchors(1.f, 0.f)).Alignment(FVector2D(1.f, 0.f)).AutoSize(true)
				[
					Enter(BuildClock(), 0.10f)
				]
				+ SConstraintCanvas::Slot().Anchors(FAnchors(0.f, 0.40f)).Alignment(FVector2D(0.f, 0.f)).AutoSize(true)
				[
					Enter(BuildStats(), 0.20f, 24.f)
				]
				+ SConstraintCanvas::Slot().Anchors(FAnchors(1.f, 0.44f)).Alignment(FVector2D(1.f, 0.f)).AutoSize(true)
				[
					Enter(BuildSquadResults(), 0.26f, 24.f)
				]
				+ SConstraintCanvas::Slot().Anchors(FAnchors(1.f, 1.f)).Alignment(FVector2D(1.f, 1.f)).AutoSize(true)
				[
					Enter(BuildButtons(), 0.34f, 20.f)
				]
				// The title is last so it paints over everything, and it is NOT given an entrance:
				// it has one of its own (the punch), applied in Tick.
				+ SConstraintCanvas::Slot().Anchors(FAnchors(0.5f, 0.f)).Alignment(FVector2D(0.5f, 0.f)).AutoSize(true)
				[
					BuildTitle()
				]
			]
		]
		+ SOverlay::Slot()
		[
			BuildSummary()
		]
		// The SHARE confirmation: one line that says what just happened, then gets out of the way.
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 110.f)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return ToastLeft > 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Notched(10.f))
				.Fill(FLinearColor(0.02f, 0.025f, 0.03f, 0.94f))
				.Border(EndGold)
				.BorderWidth(1.f)
				.ContentPadding(FMargin(22.f, 10.f))
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return Toast; })
					.Font(CampusAngular::Display(16))
					.ColorAndOpacity(EndGold)
				]
			]
		]
	];
}

bool SCampusBREnd::WantsMotion() const
{
	const ACampusMatchPlayerController* PC = Controller.Get();
	const UCampusSave* Profile = PC ? PC->GetProfile() : nullptr;
	// 0 is the "smooth" setting a player picks when the phone cannot keep up. Honour it everywhere,
	// including here - a victory screen is no reason to start spending frames they asked us not to.
	return !Profile || Profile->GraphicsQuality > 0;
}

TSharedRef<SWidget> SCampusBREnd::Enter(TSharedRef<SWidget> Panel, float Delay, float RisePx)
{
	FEntrance& Beat = Entrances.AddDefaulted_GetRef();
	Beat.Widget = Panel;
	Beat.Delay = Delay;
	Beat.RisePx = RisePx;
	// Start where the animation starts, so the first painted frame is already correct.
	Panel->SetRenderOpacity(0.f);
	Panel->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(0.f, RisePx))));
	return Panel;
}

void SCampusBREnd::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	Life += InDeltaTime;
	if (ToastLeft > 0.f)
	{
		ToastLeft = FMath::Max(0.f, ToastLeft - InDeltaTime);
	}

	// THE SETTLE. One beat of golden backdrop, then the results and the 3D line-up together - with
	// Life wound back so every animation below starts from the top instead of halfway through.
	if (!bSettled && Life >= SettleSeconds)
	{
		bSettled = true;
		Life = 0.f;
		View.Read(Controller.Get());
		BuildScreen();
		if (ACampusMatchPlayerController* PC = Controller.Get())
		{
			PC->ShowResultStage();
		}
	}
	if (!bSettled)
	{
		return;
	}

	// Panels: fade and slide up on a 60 ms stagger. Transform and opacity only.
	for (const FEntrance& Beat : Entrances)
	{
		if (!Beat.Widget.IsValid())
		{
			continue;
		}
		const float Alpha = EndEaseOut(EndBeat(Life, Beat.Delay, 0.26f));
		Beat.Widget->SetRenderOpacity(Alpha);
		Beat.Widget->SetRenderTransform(TOptional<FSlateRenderTransform>(
			FSlateRenderTransform(FVector2f(0.f, Beat.RisePx * (1.f - Alpha)))));
	}

	// The title punches in. The pivot is the block's own centre, which Slate would need a pivot
	// attribute for - so the translation that keeps the centre still is done here, from the design
	// size the block is pinned to.
	if (TitleBlock.IsValid())
	{
		const float Scale = EndPunch(EndBeat(Life, 0.02f, 0.42f));
		TitleBlock->SetRenderOpacity(FMath::Clamp(Life * 6.f, 0.f, 1.f));
		TitleBlock->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(
			Scale, FVector2f(-(Scale - 1.f) * TitleW * 0.5f, -(Scale - 1.f) * TitleH * 0.5f))));
	}
	// The crown drops onto it, a beat behind, and does not bounce: a crown that bounces is a hat.
	if (CrownBlock.IsValid())
	{
		const float Drop = EndBeat(Life, 0.22f, 0.34f);
		CrownBlock->SetRenderOpacity(Drop);
		CrownBlock->SetRenderTransform(TOptional<FSlateRenderTransform>(
			FSlateRenderTransform(FVector2f(0.f, -74.f * (1.f - EndEaseOut(Drop))))));
	}
	// The shine sweeps across once the title has settled, then every 3.4 s. Clipped to the title box.
	if (TitleShine.IsValid())
	{
		const float Period = 3.4f;
		const float Start = 0.80f;
		const float Sweep = 0.9f;
		const float Since = Life - Start;
		const float Phase = Since < 0.f ? -1.f : FMath::Fmod(Since, Period) / Sweep;
		const bool bSweeping = WantsMotion() && Phase >= 0.f && Phase <= 1.f;
		TitleShine->SetRenderOpacity(bSweeping ? 1.f : 0.f);
		if (bSweeping)
		{
			TitleShine->SetRenderTransform(TOptional<FSlateRenderTransform>(
				FSlateRenderTransform(FVector2f(FMath::Lerp(-190.f, TitleW + 60.f, Phase), 0.f))));
		}
	}
}

FText SCampusBREnd::TitleText() const
{
	if (View.bWon)
	{
		return LOCTEXT("EndKing", "CAMPUS KING");
	}
	// An honest ladder, and only when the rung means something: "TOP 5" out of four squads is a lie.
	if (View.Placement > 0 && View.TotalSquads > 5 && View.Placement <= 5)
	{
		return LOCTEXT("EndTop5", "TOP 5");
	}
	if (View.Placement > 0 && View.TotalSquads > 10 && View.Placement <= 10)
	{
		return LOCTEXT("EndTop10", "TOP 10");
	}
	// No place at all - the server settled the session without one. "MATCH OVER" is true; "ELIMINATED"
	// over a "#1" pill that has nothing behind it is not.
	return View.Placement > 0 ? LOCTEXT("EndOut", "ELIMINATED") : LOCTEXT("EndOver", "MATCH OVER");
}

// ---- top left: who we are, what we played, where ------------------------------------------------

TSharedRef<SWidget> SCampusBREnd::BuildIdentity()
{
	const FText Mode = View.SquadSize <= 1 ? LOCTEXT("EndSolo", "BATTLE ROYALE  ·  SOLO")
		: (View.SquadSize == 2 ? LOCTEXT("EndDuo", "BATTLE ROYALE  ·  DUO")
			: LOCTEXT("EndSquad", "BATTLE ROYALE  ·  SQUAD"));

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			CampusAngular::Logo(30)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Target).Size(18.f).Color(EndGold)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Mode).Font(CampusAngular::Display(16)).ColorAndOpacity(EndText)
				.ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(EndShadow)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("EndMapLine", "{0} MAP"), FText::FromString(View.MapName.ToUpper())))
			.Font(CampusAngular::Display(14)).ColorAndOpacity(EndMuted)
		];

	// The thumbnail is OPTIONAL art: a missing PNG paints the plate's own gradient, never an error.
	Column->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(178.f).HeightOverride(98.f)
			[
				SNew(SCampusArtPlate)
				.Shape(FCampusShape::Notched(10.f))
				.Art(View.MapArt)
				.Border(EndHair)
				.BorderWidth(1.f)
				.ScrimStrength(0.35f)
				.ContentPadding(FMargin(10.f, 8.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(View.MapName.ToUpper()))
						.Font(CampusAngular::Display(13)).ColorAndOpacity(EndText)
					]
				]
			]
		];
	return Column;
}

// ---- top centre: the animated title --------------------------------------------------------------

TSharedRef<SWidget> SCampusBREnd::BuildTitle()
{
	const FLinearColor Tint = View.bWon ? EndGold : FLinearColor(0.92f, 0.94f, 0.97f, 1.f);

	TSharedRef<SWidget> Block =
		SNew(SBox).WidthOverride(TitleW).HeightOverride(TitleH)
		.Clipping(EWidgetClipping::ClipToBounds)
		[
			SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(TitleText())
				.Font(CampusAngular::Display(View.bWon ? 74 : 62))
				.ColorAndOpacity(Tint)
				.ShadowOffset(FVector2D(4.f, 4.f))
				.ShadowColorAndOpacity(EndShadow)
				.Justification(ETextJustify::Center)
			]
			// The shine: a slanted bar of light that crosses the block. Unclipped to the letterforms
			// on purpose - a mask would cost a render target, and a bar that crosses the whole lockup
			// is what the arcade version of this screen has always done.
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
			[
				SAssignNew(TitleShine, SBox).WidthOverride(120.f)
				.Visibility(EVisibility::HitTestInvisible)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Slanted(42.f))
					.Fill(FLinearColor(1.f, 0.95f, 0.75f, 0.16f))
					[
						SNew(SSpacer).Size(FVector2D(1.f, 1.f))
					]
				]
			]
		];
	TitleBlock = Block;

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	// The crown belongs to a WIN. On a placement screen there is no crown and no gap where one was.
	if (View.bWon)
	{
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, -14.f)
			[
				SAssignNew(CrownBlock, SBox).WidthOverride(104.f).HeightOverride(62.f)
				.Visibility(EVisibility::HitTestInvisible)
				[
					SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Crown).Size(62.f).Color(EndGold)
				]
			];
	}
	Column->AddSlot().AutoHeight().HAlign(HAlign_Center)[Block];
	// "#1 / 20" - the pill under the title. The same two numbers as SQUAD RESULTS, said once loudly.
	// No place, no pill: zero footprint beats a "#1" that means nothing.
	if (View.Placement > 0)
	{
		Column->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, -6.f, 0.f, 0.f)
			[
				Enter(SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Wedge(14.f, 10.f))
					.Fill(FLinearColor(0.02f, 0.025f, 0.03f, 0.92f))
					.Border(View.bWon ? EndGold : EndHair)
					.BorderWidth(View.bWon ? 2.f : 1.f)
					.ContentPadding(FMargin(28.f, 5.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("EndRankNum", "#{0}"),
								FText::AsNumber(FMath::Max(1, View.Placement))))
							.Font(CampusAngular::Display(34)).ColorAndOpacity(View.bWon ? EndGold : EndText)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("EndRankOf", "/ {0}"), FText::AsNumber(View.TotalSquads)))
							.Font(CampusAngular::Display(26)).ColorAndOpacity(EndMuted)
						]
					], 0.46f, 14.f)
			];
	}
	return Column;
}

// ---- top right: how long, when, where -----------------------------------------------------------

TSharedRef<SWidget> SCampusBREnd::BuildClock()
{
	auto Cell = [](const FText& Caption, const FText& Value, int32 ValueSize, const FLinearColor& Tint)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Caption).Font(CampusAngular::Display(12)).ColorAndOpacity(EndMuted)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(Value).Font(CampusAngular::Numeric(ValueSize)).ColorAndOpacity(Tint)
			];
	};

	return EndPanel(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 20.f, 0.f)
		[
			Cell(LOCTEXT("EndDurationCap", "MATCH DURATION"), EndClock(float(View.MatchSeconds)), 24, EndText)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(0.f, 2.f, 20.f, 2.f)
		[
			SNew(SBox).WidthOverride(1.f)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.16f))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Cell(FText::FromString(View.EndedAt.ToString(TEXT("%Y-%m-%d  %H:%M"))),
				FText::FromString(View.MapName.ToUpper()), 16, EndText)
		],
		FMargin(18.f, 10.f));
}

// ---- left: YOUR STATS ----------------------------------------------------------------------------

TSharedRef<SWidget> SCampusBREnd::StatRow(CampusAngular::EGlyph Glyph, const FText& Label, const FText& Value)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(SCampusGlyph).Glyph(Glyph).Size(17.f).Color(FLinearColor(1.f, 1.f, 1.f, 0.72f))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Label).Font(CampusAngular::Display(15)).ColorAndOpacity(EndText)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
		[
			SNew(STextBlock).Text(Value).Font(CampusAngular::Numeric(18)).ColorAndOpacity(EndGold)
			.Justification(ETextJustify::Right)
		];
}

TSharedRef<SWidget> SCampusBREnd::BuildStats()
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 9.f, 0.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Crown).Size(20.f).Color(EndGold)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				EndHeading(LOCTEXT("EndYourStats", "YOUR STATS"), EndText)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 10.f)
		[
			SNew(SBox).HeightOverride(1.f)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.14f))
			]
		];

	struct FStatLine
	{
		CampusAngular::EGlyph Glyph;
		FText Label;
		FText Value;
	};
	// Every row carries its own icon AND its own word: the glyph is a landmark for the eye, never the
	// thing that says what the number is. Target is the crosshair the profile page already uses for
	// headshot accuracy, so the two pages agree about what that icon means.
	const FStatLine Lines[] = {
		{ CampusAngular::EGlyph::Skull, LOCTEXT("EndKills", "KILLS"), EndGrouped(View.Kills) },
		{ CampusAngular::EGlyph::Rank, LOCTEXT("EndDamage", "DAMAGE"), EndGrouped(FMath::RoundToInt(View.Damage)) },
		{ CampusAngular::EGlyph::Clock, LOCTEXT("EndSurvival", "SURVIVAL TIME"), EndClock(View.SurvivalSeconds) },
		{ CampusAngular::EGlyph::Chevron, LOCTEXT("EndDistance", "DISTANCE TRAVELED"), EndKilometres(View.DistanceCm) },
		{ CampusAngular::EGlyph::Target, LOCTEXT("EndHeadshots", "HEADSHOTS"), EndGrouped(View.Headshots) },
	};
	for (const FStatLine& Line : Lines)
	{
		Rows->AddSlot().AutoHeight().Padding(0.f, 5.f)
			[
				StatRow(Line.Glyph, Line.Label, Line.Value)
			];
	}
	return SNew(SBox).WidthOverride(322.f)[EndPanel(Rows, FMargin(20.f, 16.f))];
}

// ---- right: SQUAD RESULTS / SOLO RESULT ----------------------------------------------------------

TSharedRef<SWidget> SCampusBREnd::BuildSquadResults()
{
	auto Row = [](const FText& Label, const FText& Value)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label).Font(CampusAngular::Display(14)).ColorAndOpacity(EndMuted)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(Value).Font(CampusAngular::Numeric(18)).ColorAndOpacity(EndText)
			];
	};

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 9.f, 0.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Friends).Size(20.f).Color(EndGold)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				EndHeading(View.SquadSize <= 1 ? LOCTEXT("EndSoloResult", "SOLO RESULT")
					: LOCTEXT("EndSquadResults", "SQUAD RESULTS"), EndText)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 10.f)
		[
			SNew(SBox).HeightOverride(1.f)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.14f))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("EndSquadRank", "# {0}"), FText::AsNumber(FMath::Max(1, View.Placement))))
				.Font(CampusAngular::Display(34)).ColorAndOpacity(View.bWon ? EndGold : EndText)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(8.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("EndSquadOf", "/ {0}"), FText::AsNumber(View.TotalSquads)))
				.Font(CampusAngular::Display(20)).ColorAndOpacity(EndMuted)
			]
		];

	Rows->AddSlot().AutoHeight().Padding(0.f, 5.f)[Row(LOCTEXT("EndTotalKills", "TOTAL KILLS"), EndGrouped(View.SquadKills))];
	Rows->AddSlot().AutoHeight().Padding(0.f, 5.f)[Row(LOCTEXT("EndTotalDamage", "TOTAL DAMAGE"), EndGrouped(FMath::RoundToInt(View.SquadDamage)))];
	Rows->AddSlot().AutoHeight().Padding(0.f, 5.f)[Row(LOCTEXT("EndAlive", "PLAYERS ALIVE"), EndGrouped(View.SquadAlive))];
	return SNew(SBox).WidthOverride(286.f)[EndPanel(Rows, FMargin(20.f, 16.f))];
}

// ---- the plates under the figures ----------------------------------------------------------------

TSharedRef<SWidget> SCampusBREnd::Plate(int32 Rank)
{
	const FCampusBREndMember& Member = View.Squad[Rank];
	const bool bMvp = Rank == 0;
	const FLinearColor Tone = bMvp ? EndGold : EndRankTone[FMath::Clamp(Rank - 1, 0, 2)];

	TSharedRef<SWidget> Badge = bMvp
		? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Wedge(10.f, 6.f))
			.Fill(EndGold)
			.ContentPadding(FMargin(14.f, 1.f))
			[
				SNew(STextBlock).Text(LOCTEXT("EndMvp", "MVP")).Font(CampusAngular::Display(17))
				.ColorAndOpacity(FLinearColor::Black)
			])
		: StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(26.f).HeightOverride(26.f)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Clipped(9.f))
				.Fill(Tone)
				[
					SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::AsNumber(Rank + 1)).Font(CampusAngular::Numeric(15))
						.ColorAndOpacity(FLinearColor::Black)
					]
				]
			]);

	TSharedRef<SWidget> Body =
		SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Notched(bMvp ? 12.f : 9.f))
		.Fill(bMvp ? FLinearColor(0.05f, 0.035f, 0.005f, 0.92f) : EndInkSoft)
		.Border(bMvp ? EndGold : EndHair)
		.BorderWidth(bMvp ? 2.f : 1.f)
		.ContentPadding(FMargin(bMvp ? 20.f : 12.f, bMvp ? 7.f : 5.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Member.Name))
				.Font(CampusAngular::Display(bMvp ? 22 : 15))
				.ColorAndOpacity(bMvp ? EndGold : EndText)
				.ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(EndShadow)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("EndPlateLine", "KILLS  {0}   |   DAMAGE  {1}"),
					EndGrouped(Member.Kills), EndGrouped(FMath::RoundToInt(Member.Damage))))
				.Font(CampusAngular::Numeric(bMvp ? 15 : 12))
				.ColorAndOpacity(bMvp ? EndText : EndMuted)
			]
		];

	TSharedRef<SVerticalBox> Stack = SNew(SVerticalBox);
	if (bMvp)
	{
		Stack->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, -4.f)[Badge];
		Stack->AddSlot().AutoHeight().HAlign(HAlign_Center)[Body];
	}
	else
	{
		// The diamond beside the plate, not above it: the outer three have far less room than the MVP.
		Stack->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 5.f, 0.f)[Badge]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Body]
			];
	}
	return Stack;
}

TSharedRef<SWidget> SCampusBREnd::BuildPlates()
{
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas).Visibility(EVisibility::HitTestInvisible);
	for (int32 Rank = 0; Rank < View.Squad.Num() && Rank < CampusVictoryStage::MaxFigures; ++Rank)
	{
		// BOTH SIDES READ THE SAME TABLE. The figure stands where CampusVictoryStage::Pose puts it and
		// the plate hangs at that same spot's screen fraction, so a plate cannot drift off its player.
		Canvas->AddSlot()
			.Anchors(FAnchors(CampusVictoryStage::ScreenFraction(Rank, Aspect),
				CampusVictoryStage::ScreenFootFraction(Rank)))
			.Alignment(FVector2D(0.5f, 0.f))
			.AutoSize(true)
			[
				Enter(Plate(Rank), 0.40f + 0.06f * float(Rank), 16.f)
			];
	}
	return Canvas;
}

// ---- bottom right: SHARE, MATCH SUMMARY, BACK TO LOBBY -------------------------------------------

TSharedRef<SWidget> SCampusBREnd::BuildButtons()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			CampusStyle::Button(LOCTEXT("EndShare", "SHARE"), FOnClicked::CreateLambda([this]()
			{
				Share();
				return FReply::Handled();
			}), CampusStyle::EButton::Dark, 18, FVector2D(176.f, 68.f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
		[
			CampusStyle::Button(LOCTEXT("EndSummary", "MATCH SUMMARY"), FOnClicked::CreateLambda([this]()
			{
				bSummaryOpen = true;
				return FReply::Handled();
			}), CampusStyle::EButton::Dark, 18, FVector2D(236.f, 68.f))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
		[
			CampusStyle::StartButton(LOCTEXT("EndBack", "BACK TO LOBBY"), FOnClicked::CreateLambda([this]()
			{
				BackToLobby();
				return FReply::Handled();
			}), FVector2D(282.f, 72.f))
		];
}

// ---- the summary sheet ---------------------------------------------------------------------------

TSharedRef<SWidget> SCampusBREnd::BuildSummary()
{
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (View.Summary.Num() == 0)
	{
		// Only the bots were left and none of them carries a player state: say so, do not draw a
		// table of nothing.
		List->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 26.f)
			[
				SNew(STextBlock).Text(LOCTEXT("EndNoSummary", "NO PER-PLAYER RESULTS FOR THIS MATCH"))
				.Font(CampusAngular::Display(16)).ColorAndOpacity(EndMuted)
			];
	}
	for (const FCampusBREndRow& Row : View.Summary)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 3.f)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Notched(8.f))
				.Fill(Row.bLocal ? FLinearColor(0.08f, 0.06f, 0.01f, 0.9f) : EndInkSoft)
				.AccentEdge(Row.bLocal ? EndGold : FLinearColor::Transparent)
				.AccentEdgeWidth(Row.bLocal ? 3.f : 0.f)
				.ContentPadding(FMargin(14.f, 7.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(58.f)
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("EndSumPlace", "#{0}"), FText::AsNumber(Row.Placement)))
							.Font(CampusAngular::Numeric(16)).ColorAndOpacity(Row.Placement == 1 ? EndGold : EndMuted)
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Row.Name))
						.Font(CampusAngular::Display(16)).ColorAndOpacity(EndText)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("EndSumKills", "{0} KILLS"), FText::AsNumber(Row.Kills)))
						.Font(CampusAngular::Numeric(16)).ColorAndOpacity(EndText)
					]
				]
			];
	}

	return SNew(SBox)
		.Visibility_Lambda([this]() { return bSummaryOpen ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.80f))
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(620.f).MaxDesiredHeight(460.f)
				[
					EndPanel(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								EndHeading(LOCTEXT("EndSummaryTitle", "MATCH SUMMARY"), EndGold, 22)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								CampusStyle::IconButton(CampusAngular::EGlyph::Close,
									FOnClicked::CreateLambda([this]()
									{
										bSummaryOpen = false;
										return FReply::Handled();
									}), 48.f)
							]
						]
						+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 12.f, 0.f, 0.f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								List
							]
						],
						FMargin(20.f, 16.f))
				]
			]
		];
}

// ---- actions -------------------------------------------------------------------------------------

void SCampusBREnd::Share()
{
	// A REAL SHARE, not a dead button: the result goes on the clipboard, which every messaging app on
	// the phone can take - the same thing the profile page's COPY does. An Android ACTION_SEND sheet
	// is a JNI job and can replace this one call without touching anything else on the screen.
	const FString Line = FString::Printf(
		TEXT("Campus Combat - Battle Royale on %s: #%d/%d, %d kills, %d damage, %s survived."),
		*View.MapName, FMath::Max(1, View.Placement), View.TotalSquads, View.Kills,
		FMath::RoundToInt(View.Damage), *EndClock(View.SurvivalSeconds).ToString());
	FPlatformApplicationMisc::ClipboardCopy(*Line);
	Toast = LOCTEXT("EndShared", "RESULT COPIED - PASTE IT ANYWHERE");
	ToastLeft = 2.6f;
}

void SCampusBREnd::BackToLobby()
{
	if (ACampusMatchPlayerController* PC = Controller.Get())
	{
		// The existing path: bank the reward once (it is idempotent), then travel. The stage is torn
		// down by the controller's EndPlay, which the travel runs through.
		PC->ClaimAndLeave();
	}
}

#undef LOCTEXT_NAMESPACE
