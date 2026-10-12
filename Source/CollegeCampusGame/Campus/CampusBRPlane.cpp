#include "Campus/CampusBRPlane.h"

#include "Campus/CampusBRZone.h"
#include "Campus/CampusCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

namespace CampusBRDive
{
	/** Plane height (design: ~100 m) and how long one crossing takes. */
	constexpr float Altitude = 10000.f;
	constexpr float FlightSeconds = 45.f;
	/** The line starts and ends outside the map square so the plane is never seen turning. */
	constexpr float LineOvershoot = 1.3f;

	// Freefall: 40 m/s down with real lateral control, the same numbers on the server and the phone.
	constexpr float FreefallGravity = 3.5f;
	constexpr float FreefallFallSpeed = 4000.f;
	constexpr float FreefallLateralSpeed = 1300.f;
	constexpr float FreefallAirControl = 0.9f;
	constexpr float DiveAcceleration = 3000.f;

	// Chute: a slow, steerable descent that always survives the landing.
	constexpr float ChuteFallSpeed = 450.f;
	constexpr float ChuteLateralSpeed = 700.f;

	/** The chute opens by itself at 25 m above whatever is below. */
	constexpr float AutoChuteHeight = 2500.f;
	/** How far down to look for the ground (the whole drop, so the number is always real). */
	constexpr float GroundTraceLength = 30000.f;
	/** A landed drop lingers this long: the landing frame must never be charged as fall damage. */
	constexpr float DoneGraceSeconds = 2.f;

	TArray<TWeakObjectPtr<ACampusBRPlane>> LivePlanes;
	TArray<TWeakObjectPtr<ACampusBRDrop>> LiveDrops;

	UStaticMesh* BasicShape(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}
}

// ---------------------------------------------------------------------------------------------
// One diver's drop

ACampusBRDrop::ACampusBRDrop()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bOnlyRelevantToOwner = true; // nobody else needs it: the pawn itself carries what others can see
	SetReplicateMovement(false);
	SetCanBeDamaged(false);
}

void ACampusBRDrop::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusBRDrop, State);
	DOREPLIFETIME(ACampusBRDrop, Diver);
	DOREPLIFETIME(ACampusBRDrop, Plane);
}

void ACampusBRDrop::Board(ACampusCharacter* InDiver, ACampusBRPlane* InPlane)
{
	if (!HasAuthority() || !InDiver || !InPlane)
	{
		return;
	}
	Diver = InDiver;
	Plane = InPlane;
	State = ECampusBRDive::InPlane;
	CampusBRDive::LiveDrops.AddUnique(this);

	UCharacterMovementComponent* Movement = InDiver->GetCharacterMovement();
	if (Movement && !bSaved)
	{
		SavedGravityScale = Movement->GravityScale;
		SavedAirControl = Movement->AirControl;
		SavedMaxAcceleration = Movement->MaxAcceleration;
		SavedLateralFriction = Movement->FallingLateralFriction;
		bSaved = true;
	}
	if (Movement)
	{
		Movement->DisableMovement();
	}
	// Riding by attachment, not by teleporting every frame: the attachment replicates, so the owning
	// phone stops predicting a fall and the pawn follows the plane with no net traffic of its own.
	InDiver->AttachToActor(InPlane, FAttachmentTransformRules::KeepWorldTransform);
	InDiver->SetActorRelativeLocation(FVector::ZeroVector);
	InDiver->SetActorHiddenInGame(true);
	InDiver->SetActorEnableCollision(false);
}

void ACampusBRDrop::Drop()
{
	if (!HasAuthority() || State != ECampusBRDive::InPlane || !Diver)
	{
		return;
	}
	State = ECampusBRDive::Freefall;
	const FVector Exit = Plane ? Plane->SeatLocation() : Diver->GetActorLocation();
	Diver->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Diver->SetActorHiddenInGame(false);
	Diver->SetActorEnableCollision(true);
	Diver->SetActorLocation(Exit, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Movement = Diver->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Falling);
		Movement->Velocity = FVector(0.f, 0.f, -200.f);
	}
}

void ACampusBRDrop::OpenChute()
{
	if (!HasAuthority() || State != ECampusBRDive::Freefall)
	{
		return;
	}
	State = ECampusBRDive::Parachute;
}

bool ACampusBRDrop::HandleJumpPressed()
{
	if (State == ECampusBRDive::Done)
	{
		return false;
	}
	if (HasAuthority())
	{
		if (State == ECampusBRDive::InPlane)
		{
			Drop();
		}
		else
		{
			OpenChute();
		}
	}
	else
	{
		ServerJump();
	}
	return true;
}

void ACampusBRDrop::ServerJump_Implementation()
{
	if (State == ECampusBRDive::InPlane)
	{
		Drop();
	}
	else if (State == ECampusBRDive::Freefall)
	{
		OpenChute();
	}
}

float ACampusBRDrop::HeightAboveGround() const
{
	if (!Diver)
	{
		return 0.f;
	}
	const FVector Feet = Diver->GetActorLocation() - FVector(0.f, 0.f, Diver->GetCapsuleComponent() ? Diver->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f);
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("CampusBRGround"), false, Diver);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Feet,
		Feet - FVector(0.f, 0.f, CampusBRDive::GroundTraceLength), ECC_Visibility, Params);
	return bHit ? Hit.Distance : CampusBRDive::GroundTraceLength;
}

void ACampusBRDrop::ApplyDiveMovement()
{
	UCharacterMovementComponent* Movement = Diver ? Diver->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	const bool bChute = State == ECampusBRDive::Parachute;
	Movement->GravityScale = bChute ? 0.f : CampusBRDive::FreefallGravity;
	Movement->AirControl = bChute ? 1.f : CampusBRDive::FreefallAirControl;
	Movement->MaxAcceleration = CampusBRDive::DiveAcceleration;
	Movement->FallingLateralFriction = bChute ? 2.f : 0.6f;

	// The clamps are the dive's physics, and they run on BOTH machines: identical input to identical
	// clamps means the server's replay of a predicted move lands on the same place, so a diver is
	// never corrected mid-air (a correction at 40 m/s throws you half a building sideways).
	FVector Velocity = Movement->Velocity;
	const float FallSpeed = bChute ? CampusBRDive::ChuteFallSpeed : CampusBRDive::FreefallFallSpeed;
	Velocity.Z = bChute ? -FallSpeed : FMath::Max(Velocity.Z, -FallSpeed);
	const float LateralCap = bChute ? CampusBRDive::ChuteLateralSpeed : CampusBRDive::FreefallLateralSpeed;
	FVector2D Lateral(Velocity.X, Velocity.Y);
	if (Lateral.SizeSquared() > FMath::Square(LateralCap))
	{
		Lateral = Lateral.GetSafeNormal() * LateralCap;
		Velocity.X = Lateral.X;
		Velocity.Y = Lateral.Y;
	}
	Movement->Velocity = Velocity;
}

void ACampusBRDrop::RestoreMovement()
{
	UCharacterMovementComponent* Movement = Diver ? Diver->GetCharacterMovement() : nullptr;
	if (!Movement || !bSaved)
	{
		return;
	}
	Movement->GravityScale = SavedGravityScale;
	Movement->AirControl = SavedAirControl;
	Movement->MaxAcceleration = SavedMaxAcceleration;
	Movement->FallingLateralFriction = SavedLateralFriction;
	bSaved = false;
}

void ACampusBRDrop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Diver)
	{
		if (HasAuthority())
		{
			Destroy();
		}
		return;
	}
	if (State == ECampusBRDive::InPlane || State == ECampusBRDive::Done)
	{
		// Done: the actor is only still here so IsFallDamageSuppressed covers the landing frame.
		if (HasAuthority() && State == ECampusBRDive::Done && DoneTime >= 0.f
			&& GetWorld()->GetTimeSeconds() - DoneTime > CampusBRDive::DoneGraceSeconds)
		{
			Destroy();
		}
		return;
	}

	ApplyDiveMovement();

	if (!HasAuthority())
	{
		return;
	}
	if (State == ECampusBRDive::Freefall && HeightAboveGround() < CampusBRDive::AutoChuteHeight)
	{
		OpenChute();
	}
	const UCharacterMovementComponent* Movement = Diver->GetCharacterMovement();
	if (Movement && Movement->IsMovingOnGround())
	{
		RestoreMovement();
		State = ECampusBRDive::Done;
		DoneTime = float(GetWorld()->GetTimeSeconds());
	}
}

void ACampusBRDrop::EndPlay(const EEndPlayReason::Type Reason)
{
	// Both machines put the movement component back: a client that only ever saw the replicated state
	// still has its own copy of GravityScale to undo.
	RestoreMovement();
	CampusBRDive::LiveDrops.Remove(this);
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------------
// The plane

ACampusBRPlane::ACampusBRPlane()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	// Deliberately NOT replicating movement: every machine runs the same straight line from the same
	// replicated endpoints and clock, which costs nothing on the wire and never stutters on Wi-Fi.
	SetReplicateMovement(false);
	SetCanBeDamaged(false);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	// Cosmetic only: a long box reads as an aircraft from 100 m below, and it costs one draw call.
	Body->SetWorldScale3D(FVector(16.f, 3.f, 2.5f));
}

void ACampusBRPlane::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusBRPlane, Start);
	DOREPLIFETIME(ACampusBRPlane, Finish);
	DOREPLIFETIME(ACampusBRPlane, LaunchTime);
	DOREPLIFETIME(ACampusBRPlane, FlightSeconds);
}

void ACampusBRPlane::BeginPlay()
{
	Super::BeginPlay();
	CampusBRDive::LivePlanes.AddUnique(this);
	if (Body && !Body->GetStaticMesh())
	{
		Body->SetStaticMesh(CampusBRDive::BasicShape(TEXT("/Engine/BasicShapes/Cube.Cube")));
	}
}

void ACampusBRPlane::EndPlay(const EEndPlayReason::Type Reason)
{
	CampusBRDive::LivePlanes.Remove(this);
	Super::EndPlay(Reason);
}

ACampusBRPlane* ACampusBRPlane::Get(const UWorld* World)
{
	for (int32 Index = CampusBRDive::LivePlanes.Num() - 1; Index >= 0; --Index)
	{
		ACampusBRPlane* Plane = CampusBRDive::LivePlanes[Index].Get();
		if (!Plane)
		{
			CampusBRDive::LivePlanes.RemoveAtSwap(Index);
		}
		else if (Plane->GetWorld() == World)
		{
			return Plane;
		}
	}
	return nullptr;
}

ACampusBRDrop* ACampusBRPlane::FindDrop(const ACampusCharacter* Character)
{
	if (!Character)
	{
		return nullptr;
	}
	for (int32 Index = CampusBRDive::LiveDrops.Num() - 1; Index >= 0; --Index)
	{
		ACampusBRDrop* Drop = CampusBRDive::LiveDrops[Index].Get();
		if (!Drop)
		{
			CampusBRDive::LiveDrops.RemoveAtSwap(Index);
		}
		else if (Drop->GetDiver() == Character)
		{
			return Drop;
		}
	}
	return nullptr;
}

bool ACampusBRPlane::IsDeploying(const ACampusCharacter* Character)
{
	const ACampusBRDrop* Drop = FindDrop(Character);
	return Drop && Drop->IsDeploying();
}

bool ACampusBRPlane::IsFallDamageSuppressed(const ACampusCharacter* Character)
{
	return FindDrop(Character) != nullptr;
}

bool ACampusBRPlane::HandleJumpPressed(ACampusCharacter* Character)
{
	ACampusBRDrop* Drop = FindDrop(Character);
	return Drop && Drop->HandleJumpPressed();
}

void ACampusBRPlane::Launch()
{
	if (!HasAuthority())
	{
		return;
	}
	const FVector2D Middle = CampusBRMap::Center();
	const float Half = CampusBRMap::HalfSpan();
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
	const FVector2D Side(-Direction.Y, Direction.X);
	// Offset from the centre so the line is not always straight through the middle of the campus.
	const FVector2D Through = Middle + Side * FMath::FRandRange(-0.35f, 0.35f) * Half;
	const float Reach = Half * CampusBRDive::LineOvershoot;
	Start = FVector_NetQuantize(Through.X - Direction.X * Reach, Through.Y - Direction.Y * Reach, CampusBRDive::Altitude);
	Finish = FVector_NetQuantize(Through.X + Direction.X * Reach, Through.Y + Direction.Y * Reach, CampusBRDive::Altitude);
	FlightSeconds = CampusBRDive::FlightSeconds;
	LaunchTime = float(GetWorld()->GetTimeSeconds());
	SetActorLocationAndRotation(FVector(Start), FRotationMatrix::MakeFromX(FVector(Finish) - FVector(Start)).Rotator());
}

float ACampusBRPlane::FlightAlpha() const
{
	if (FlightSeconds <= 0.f)
	{
		return 1.f;
	}
	return FMath::Clamp((CampusBRServerTime(GetWorld()) - LaunchTime) / FlightSeconds, 0.f, 1.f);
}

FVector ACampusBRPlane::SeatLocation() const
{
	return FMath::Lerp(FVector(Start), FVector(Finish), FlightAlpha());
}

ACampusBRDrop* ACampusBRPlane::Board(ACampusCharacter* Diver)
{
	if (!HasAuthority() || !Diver)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	// Owned by the diver's controller: that is what lets the phone send ServerJump on this actor.
	Params.Owner = Diver->GetController();
	ACampusBRDrop* Drop = GetWorld()->SpawnActor<ACampusBRDrop>(ACampusBRDrop::StaticClass(), FTransform::Identity, Params);
	if (!Drop)
	{
		return nullptr;
	}
	Drop->Board(Diver, this);
	Drops.Add(Drop);
	return Drop;
}

bool ACampusBRPlane::IsEmpty() const
{
	for (const TObjectPtr<ACampusBRDrop>& Drop : Drops)
	{
		if (Drop && Drop->GetState() == ECampusBRDive::InPlane)
		{
			return false;
		}
	}
	return true;
}

void ACampusBRPlane::EjectAll()
{
	for (const TObjectPtr<ACampusBRDrop>& Drop : Drops)
	{
		if (Drop && Drop->GetState() == ECampusBRDive::InPlane)
		{
			Drop->Drop();
		}
	}
}

void ACampusBRPlane::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (LaunchTime <= 0.f)
	{
		return;
	}
	const float Alpha = FlightAlpha();
	SetActorLocation(FMath::Lerp(FVector(Start), FVector(Finish), Alpha));
	if (HasAuthority() && Alpha >= 1.f)
	{
		EjectAll(); // end of the line: everybody out, nobody rides past the map
	}
}

// ---------------------------------------------------------------------------------------------
// The waiting platform

ACampusBRWaitingArea::ACampusBRWaitingArea()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // the host and every client build the identical slab from code at the same place
	SetCanBeDamaged(false);

	Slab = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Slab"));
	SetRootComponent(Slab);
	Slab->SetWorldScale3D(FVector(40.f, 40.f, 0.5f));
	Slab->SetCollisionProfileName(TEXT("BlockAll"));
	Slab->SetCastShadow(false);
}

FVector ACampusBRWaitingArea::Place()
{
	const FVector2D Middle = CampusBRMap::Center();
	const float Half = CampusBRMap::HalfSpan();
	// A corner of the map square, 40 m up: out of every sightline, and clearly "not the match yet".
	return FVector(Middle.X - Half * 0.82f, Middle.Y - Half * 0.82f, 4000.f);
}

FTransform ACampusBRWaitingArea::SlotFor(int32 Index) const
{
	// 50 slots on the slab: eight across, 4 m apart, all looking toward the campus.
	constexpr int32 PerRow = 8;
	constexpr float Spacing = 400.f;
	const int32 Column = Index % PerRow;
	const int32 Row = Index / PerRow;
	const FVector Base = GetActorLocation();
	const FVector Where(
		Base.X + (Column - (PerRow - 1) * 0.5f) * Spacing,
		Base.Y + (Row - 3.f) * Spacing,
		Base.Z + 120.f);
	const FVector2D Middle = CampusBRMap::Center();
	const FRotator Facing = FRotationMatrix::MakeFromX(FVector(Middle.X, Middle.Y, Base.Z) - Where).Rotator();
	return FTransform(FRotator(0.f, Facing.Yaw, 0.f), Where);
}
