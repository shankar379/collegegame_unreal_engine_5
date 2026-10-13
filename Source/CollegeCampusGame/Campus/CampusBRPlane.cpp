#include "Campus/CampusBRPlane.h"

#include "Campus/CampusBRDecorChutes.h"
#include "Campus/CampusBRZone.h"
#include "Campus/CampusCharacter.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace CampusBRDive
{
	/** Plane height and how long one crossing takes.
	 *  300 m, NOT the 100 m the first pass used: the chute opens by itself 40 m up (AutoChuteHeight),
	 *  so at 100 m the whole freefall was 1.5 s and "open your own chute" was a button nobody could
	 *  reach in time. 300 m gives ~6.5 s of steerable freefall and ~9 s under the canopy. */
	constexpr float Altitude = 30000.f;
	constexpr float FlightSeconds = 45.f;
	/** The line starts and ends outside the map square so the plane is never seen turning. */
	constexpr float LineOvershoot = 1.3f;
	/** How much further the plane flies after the last diver is out, as a share of the crossing, before
	 *  it deletes itself. Without this an aircraft hangs over the map edge for the whole match. */
	constexpr float ExitShare = 0.12f;

	// Freefall: 40 m/s down with real lateral control, the same numbers on the server and the phone.
	constexpr float FreefallGravity = 3.5f;
	constexpr float FreefallFallSpeed = 4000.f;
	constexpr float FreefallLateralSpeed = 1300.f;
	constexpr float FreefallAirControl = 0.9f;
	constexpr float DiveAcceleration = 3000.f;

	// Chute: a slow, steerable descent that always survives the landing.
	constexpr float ChuteFallSpeed = 450.f;
	constexpr float ChuteLateralSpeed = 700.f;

	/** The chute opens by itself 40 m above whatever is below.
	 *  At ChuteFallSpeed (4.5 m/s) that is ~9 s under the canopy: long enough for it to appear, be
	 *  read, and be steered off a roof. 25 m gave 5.5 s, which together with the instant 40 -> 4.5 m/s
	 *  snap reads as a jerk and a thud. The design doc's 130 m assumed a 10 m/s chute; at 4.5 m/s it
	 *  would be 29 s of hanging still, longer than the rest of the drop put together. */
	constexpr float AutoChuteHeight = 4000.f;
	/** How far down to look for the ground: more than the whole drop, so the number is always real. */
	constexpr float GroundTraceLength = 60000.f;
	/** A landed drop lingers this long: the landing frame must never be charged as fall damage. */
	constexpr float DoneGraceSeconds = 2.f;

	/** Where a diver rides, relative to the aircraft: below and behind the belly. The pawn is hidden
	 *  up there, so this is really the camera's seat - and the camera must be OUTSIDE the fuselage. */
	const FVector SeatOffset(-200.f, 0.f, -520.f);

	TArray<TWeakObjectPtr<ACampusBRPlane>> LivePlanes;
	TArray<TWeakObjectPtr<ACampusBRDrop>> LiveDrops;
}

namespace CampusBRWait
{
	/** The slab's grid: eight across, seven deep, 4 m apart. 56 cells for the fifty, so the last to
	 *  arrive still has somewhere of their own to stand. */
	constexpr int32 Columns = 8;
	constexpr int32 Rows = 7;
	constexpr float Spacing = 400.f;
	/** Scatter inside a cell. Deliberately under (Spacing - MinSeparation) / 2, so two players in
	 *  NEIGHBOURING cells can never be pushed closer than the 1.5 m rule allows. */
	constexpr float Jitter = 110.f;
	/** Nobody spawns within 1.5 m of another pawn (Docs/BATTLE_SESSION_DESIGN.md §4). */
	constexpr float MinSeparation = 150.f;
	/** Height band that counts as "on the slab": the match floor is 40 m below it. */
	constexpr float SameFloor = 400.f;
}

/** The art of the plane and the canopy: engine primitives and material instances the campus already
 *  ships (Scripts/build_campus.py's flat unlit master), so there is no asset to generate and nothing
 *  new to add to DirectoriesToAlwaysCook. Both are loaded with FObjectFinder from a constructor, which
 *  is what puts them in the class default object - and that is what the cooker can actually see. */
namespace CampusBRArt
{
	/** Every engine primitive below is 100 cm on each axis and centred on its own origin, so the
	 *  relative scales further down read straight off as centimetres. The cylinder's long axis is its
	 *  local +Z, which is why anything built along the fuselage carries a -90 pitch. */
	UStaticMesh* Cube()
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Found(TEXT("/Engine/BasicShapes/Cube.Cube"));
		return Found.Object;
	}
	UStaticMesh* Cylinder()
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Found(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		return Found.Object;
	}
	UStaticMesh* Sphere()
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Found(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		return Found.Object;
	}
	UStaticMesh* Cone()
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Found(TEXT("/Engine/BasicShapes/Cone.Cone"));
		return Found.Object;
	}

	/** Hull white. */
	UMaterialInterface* Shell()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_d9d9d9_100.MI_Campus_d9d9d9_100"));
		return Found.Object;
	}
	/** Wings and tail: slate, so the silhouette reads against a bright sky. */
	UMaterialInterface* Trim()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_3d4650_100.MI_Campus_3d4650_100"));
		return Found.Object;
	}
	/** Engines, propellers and rigging: near-black. */
	UMaterialInterface* Metal()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_1b1c1f_100.MI_Campus_1b1c1f_100"));
		return Found.Object;
	}
	/** Canopy amber - the same accent the HUD uses, and the one colour nobody loses against grass. */
	UMaterialInterface* CanopyCloth()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_d4a017_100.MI_Campus_d4a017_100"));
		return Found.Object;
	}

	/** Fast enough to read as "running", slow enough not to strobe at 30 fps. */
	constexpr float PropDegreesPerSecond = 900.f;

	/** The canopy sits over the diver's shoulders (the capsule is 83 cm half-height, so this is the
	 *  top of the chest), and the dome rides this far above that.
	 *
	 *  3.3 m, NOT the 4.2 m of the first pass. THAT is why nobody ever saw a parachute: the camera's
	 *  own boom sits 1.2 m up and 3.2 m back, so a 7 m dome 4.2 m over the shoulders was directly
	 *  ABOVE THE CAMERA and 28 deg of the way to the top of a 62 deg vertical frustum - a sliver of
	 *  amber at the very top of the screen on a level view, and nothing at all the moment the diver
	 *  looked down at where they were landing, which is the whole descent. The drop camera goes out to
	 *  9 m (ACampusCharacter::ChuteArmLength) and the dome comes down to 3.3 m: now the far rim lands
	 *  ~12 deg up and the near rim ~26 deg, so the whole canopy is in frame on every phone. */
	constexpr float ShoulderHeight = 55.f;
	constexpr float CanopyRise = 330.f;
	/** 7 m across - a real canopy's span, and wide enough to read from 300 m up. */
	constexpr float CanopyWidth = 7.f;
	/** Where the rigging meets the dome, and where it meets the diver. */
	constexpr float RiggingRadius = 250.f;
	constexpr float ShoulderRadius = 18.f;
	/** The inflation: from a bundle at the shoulders to the full canopy. */
	constexpr float OpenSeconds = 0.6f;
	constexpr float PackedScale = 0.12f;

	/** One cosmetic primitive: no collision, no shadow, no decals. Used by the plane and the chute. */
	void Dress(UStaticMeshComponent* Part, UStaticMesh* Mesh, UMaterialInterface* Material,
		const FVector& Where, const FRotator& Turn, const FVector& Size)
	{
		if (!Part)
		{
			return;
		}
		if (Mesh)
		{
			Part->SetStaticMesh(Mesh);
		}
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
		Part->SetRelativeLocation(Where);
		Part->SetRelativeRotation(Turn);
		Part->SetRelativeScale3D(Size);
		Part->SetCollisionProfileName(TEXT("NoCollision"));
		Part->SetCastShadow(false);
		Part->bReceivesDecals = false;
	}
}

// ---------------------------------------------------------------------------------------------
// The canopy

ACampusBRChute::ACampusBRChute()
{
	// It rides the diver's capsule and never moves by itself: the tick is the 0.6 s inflation and
	// nothing else, and it stops the frame that finishes (see Tick).
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false); // the attachment is rebuilt from Wearer, not streamed
	SetCanBeDamaged(false);
	// A canopy is a landmark - who is dropping where is half of the first minute - so it carries much
	// further than a body (474 m), and it never changes, so it costs two updates a second to say so.
	SetNetCullDistanceSquared(2.25e9f);
	SetNetUpdateFrequency(2.f);

	Harness = CreateDefaultSubobject<USceneComponent>(TEXT("Harness"));
	SetRootComponent(Harness);

	// The canopy is TWO primitives, because one is not a parachute:
	//   the dome - a sphere squashed hard on Z (7 m across, 70 cm deep) - is the underside, the part
	//   the wearer and anyone below actually looks at, and it is nearly flat so it reads as cloth
	//   under load instead of the egg a 2.4-deep ellipsoid gave;
	//   the ridge - a cone, apex up, over the dome's top - is the arch, which is the whole silhouette
	//   from the side and from the air.
	// Both are engine basic shapes (/Engine/BasicShapes is cooked: the levels place them too).
	Canopy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Canopy"));
	Canopy->SetupAttachment(Harness);
	CampusBRArt::Dress(Canopy, CampusBRArt::Sphere(), CampusBRArt::CanopyCloth(),
		FVector(0.f, 0.f, CampusBRArt::CanopyRise), FRotator::ZeroRotator,
		FVector(CampusBRArt::CanopyWidth, CampusBRArt::CanopyWidth, 1.4f));

	CanopyRidge = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CanopyRidge"));
	CanopyRidge->SetupAttachment(Harness);
	// The engine cone is 100 cm tall about its own centre, so lifting it by half its height puts its
	// base on the dome's waist and its apex 1.1 m over the diver's canopy.
	CampusBRArt::Dress(CanopyRidge, CampusBRArt::Cone(), CampusBRArt::CanopyCloth(),
		FVector(0.f, 0.f, CampusBRArt::CanopyRise + 50.f), FRotator::ZeroRotator,
		FVector(CampusBRArt::CanopyWidth * 0.92f, CampusBRArt::CanopyWidth * 0.92f, 1.1f));

	// One line per diagonal, each built from the two points it joins - the dome's rim and the
	// shoulder - so the geometry is stated once and the dome can move without the rigging drifting.
	auto MakeLine = [this](const TCHAR* Name, int32 Corner) -> UStaticMeshComponent*
	{
		const float Angle = FMath::DegreesToRadians(45.f + 90.f * Corner);
		const FVector2D Around(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector Top(Around.X * CampusBRArt::RiggingRadius, Around.Y * CampusBRArt::RiggingRadius,
			CampusBRArt::CanopyRise - 50.f); // the dome's underside at the rigging radius
		const FVector Bottom(Around.X * CampusBRArt::ShoulderRadius, Around.Y * CampusBRArt::ShoulderRadius, 0.f);
		const FVector Along = Top - Bottom;

		UStaticMeshComponent* Line = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Line->SetupAttachment(Harness);
		// 3 cm of cylinder stretched to the exact span, pointed along it: one component per line.
		CampusBRArt::Dress(Line, CampusBRArt::Cylinder(), CampusBRArt::Metal(),
			(Top + Bottom) * 0.5f, FRotationMatrix::MakeFromZ(Along).Rotator(),
			FVector(0.03f, 0.03f, Along.Size() * 0.01f));
		return Line;
	};
	LineFrontLeft = MakeLine(TEXT("LineFrontLeft"), 0);
	LineBackLeft = MakeLine(TEXT("LineBackLeft"), 1);
	LineBackRight = MakeLine(TEXT("LineBackRight"), 2);
	LineFrontRight = MakeLine(TEXT("LineFrontRight"), 3);
}

void ACampusBRChute::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusBRChute, Wearer);
}

void ACampusBRChute::BeginPlay()
{
	Super::BeginPlay();
	// Packed until the first inflation frame, so a canopy can never be seen at full size for one frame
	// (which on a phone is exactly the "it popped" flicker).
	Inflate(0.f);
	// A client can have Wearer applied before BeginPlay runs; hanging again here is free and covers
	// the other order too.
	HangOn(Wearer);
}

ACampusBRChute* ACampusBRChute::Deploy(ACampusCharacter* Diver)
{
	UWorld* World = Diver ? Diver->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = Diver; // the owner chain is what makes this relevant to the diver's own phone
	ACampusBRChute* Chute = World->SpawnActor<ACampusBRChute>(ACampusBRChute::StaticClass(),
		FTransform(Diver->GetActorLocation()), Params);
	if (Chute)
	{
		Chute->Wearer = Diver;
		Chute->HangOn(Diver);
	}
	return Chute;
}

void ACampusBRChute::OnRep_Wearer()
{
	HangOn(Wearer);
}

void ACampusBRChute::HangOn(ACampusCharacter* Who)
{
	if (!Who || !Who->GetRootComponent())
	{
		// No wearer yet (a replicated actor reference that has not resolved on this machine, because
		// that diver's pawn is out of relevancy range): draw NOTHING. Left visible, the canopy would
		// hang unattached at the height it opened for the rest of the match.
		OpenAge = -1.f;
		SetActorHiddenInGame(true);
		return;
	}
	// The capsule, not a bone: the canopy hangs off the body's centre line and must not inherit the
	// mesh's animation, and SnapToTargetNotIncludingScale keeps the dome's size its own.
	AttachToComponent(Who->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeLocation(FVector(0.f, 0.f, CampusBRArt::ShoulderHeight));
	SetActorRelativeRotation(FRotator::ZeroRotator);
	SetActorHiddenInGame(false);
	if (OpenAge < 0.f)
	{
		OpenAge = 0.f; // this machine's inflation starts the frame it first has somebody to hang on
	}
}

void ACampusBRChute::Inflate(float Alpha)
{
	if (!Harness)
	{
		return;
	}
	// One scale on the root: the dome, the ridge and the four lines all grow out of the diver's back,
	// which is what a canopy coming out of its pack looks like. Eased out (1 - (1-a)^3), so it snaps
	// open and settles instead of arriving at a constant rate.
	const float Eased = 1.f - FMath::Pow(1.f - FMath::Clamp(Alpha, 0.f, 1.f), 3.f);
	const float Scale = FMath::Lerp(CampusBRArt::PackedScale, 1.f, Eased);
	Harness->SetRelativeScale3D(FVector(Scale));
}

void ACampusBRChute::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (OpenAge < 0.f)
	{
		// Still waiting for a wearer. Keep asking: an unresolved actor reference can land late, and on
		// a dedicated server there is no rep notify at all.
		HangOn(Wearer);
		return;
	}
	if (OpenAge >= CampusBRArt::OpenSeconds)
	{
		Inflate(1.f);
		SetActorTickEnabled(false); // open; from here it is a child of the capsule and costs nothing
		return;
	}
	OpenAge += DeltaSeconds;
	Inflate(OpenAge / CampusBRArt::OpenSeconds);
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

void ACampusBRDrop::BeginPlay()
{
	Super::BeginPlay();
	// The register is per MACHINE, not per server: Board only ever runs on the host, so on a phone the
	// list stayed empty for the whole match and FindDrop - which is what the touch controls ask before
	// they will treat JUMP as "leave the plane" or "open the chute" - answered null every time.
	CampusBRDive::LiveDrops.AddUnique(this);
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
	InDiver->SetDivePose(ACampusCharacter::DiveNone); // aboard: the pawn is hidden up there anyway

	CaptureMovement();
	if (UCharacterMovementComponent* Movement = InDiver->GetCharacterMovement())
	{
		Movement->DisableMovement();
	}
	// Riding by attachment, not by teleporting every frame: the attachment replicates, so the owning
	// phone stops predicting a fall and the pawn follows the plane with no net traffic of its own.
	// The plane's root is deliberately an unscaled scene component, so nothing of the aircraft's own
	// 16 m worth of part scales can leak into the passenger.
	InDiver->AttachToActor(InPlane, FAttachmentTransformRules::KeepWorldTransform);
	InDiver->SetActorRelativeLocation(CampusBRDive::SeatOffset);
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
	// The pose is on the PAWN, not here: this actor is owner-relevant, and every other phone has to
	// see a skydiver rather than a man falling off a kerb.
	Diver->SetDivePose(ACampusCharacter::DiveFreefall);
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
	// Diver checked too: an RPC can arrive in the frame after that pawn was killed in the air.
	if (!HasAuthority() || State != ECampusBRDive::Freefall || !Diver)
	{
		return;
	}
	State = ECampusBRDive::Parachute;
	Diver->SetDivePose(ACampusCharacter::DiveChute);
	if (!Chute)
	{
		Chute = ACampusBRChute::Deploy(Diver);
	}
}

bool ACampusBRDrop::CanDropNow() const
{
	// Jumping on the run-in or the run-out puts you down outside the first circle with no cover and a
	// 30-second walk; the plane says when its door is useful and the button obeys.
	return State == ECampusBRDive::InPlane && Plane && Plane->IsOverMap();
}

bool ACampusBRDrop::HandleJumpPressed()
{
	if (State == ECampusBRDive::Done)
	{
		return false;
	}
	// Aboard but not over the map yet: the press is still CONSUMED. Jump means one thing in a plane,
	// and letting it fall through would vault the diver off a seat that is not there.
	if (State == ECampusBRDive::InPlane && !CanDropNow())
	{
		return true;
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
		// Re-checked here, not just on the phone: the gate is the server's rule, the UI only draws it.
		if (CanDropNow())
		{
			Drop();
		}
	}
	else if (State == ECampusBRDive::Freefall)
	{
		OpenChute();
	}
}

void ACampusBRDrop::OnRep_Dive()
{
	// One handler for State AND Diver, because either can land in the bunch first and the owning phone
	// needs both before it can do anything.
	UCharacterMovementComponent* Movement = Diver ? Diver->GetCharacterMovement() : nullptr;
	if (!Movement || !Diver->IsLocallyControlled() || HasAuthority())
	{
		return;
	}
	switch (State)
	{
	case ECampusBRDive::InPlane:
		// The server parked this pawn in MOVE_None; movement mode is not a replicated property, so
		// without this the phone predicts a 300 m fall and eats a correction every frame of the ride.
		Movement->DisableMovement();
		break;
	case ECampusBRDive::Freefall:
	case ECampusBRDive::Parachute:
		if (Movement->MovementMode == MOVE_None)
		{
			Movement->SetMovementMode(MOVE_Falling);
		}
		break;
	case ECampusBRDive::Done:
		// Landed: the phone has its OWN copy of GravityScale and friends to put back, and it must not
		// wait for the drop actor to be destroyed to do it - until then it predicts zero gravity.
		RestoreMovement();
		break;
	}
}

float ACampusBRDrop::HeightAboveGround() const
{
	UWorld* World = GetWorld();
	if (!Diver || !World)
	{
		return CampusBRDive::GroundTraceLength;
	}
	const float HalfHeight = Diver->GetCapsuleComponent()
		? Diver->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f;
	const FVector Feet = Diver->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("CampusBRGround"), false, Diver);
	// THE WORLD below, not whoever is below. Traced by object type rather than on ECC_Visibility so
	// another diver's capsule (ECC_Pawn) can never be read as ground and pop this chute 40 m over
	// somebody's head - which is exactly what a crowded hot drop would have done. Terrain, rooftops,
	// the water plane and parked vehicles are all WorldStatic or WorldDynamic, so all of them count.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const bool bHit = World->LineTraceSingleByObjectType(Hit, Feet,
		Feet - FVector(0.f, 0.f, CampusBRDive::GroundTraceLength), Objects, Params);
	return bHit ? Hit.Distance : CampusBRDive::GroundTraceLength;
}

void ACampusBRDrop::ApplyDiveMovement()
{
	UCharacterMovementComponent* Movement = Diver ? Diver->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	CaptureMovement(); // the owning phone's first dive frame: remember what it is about to overwrite
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

void ACampusBRDrop::CaptureMovement()
{
	UCharacterMovementComponent* Movement = Diver ? Diver->GetCharacterMovement() : nullptr;
	if (!Movement || bSaved)
	{
		return;
	}
	SavedGravityScale = Movement->GravityScale;
	SavedAirControl = Movement->AirControl;
	SavedMaxAcceleration = Movement->MaxAcceleration;
	SavedLateralFriction = Movement->FallingLateralFriction;
	bSaved = true;
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

void ACampusBRDrop::CutChute()
{
	if (Chute)
	{
		Chute->Destroy();
		Chute = nullptr;
	}
	if (Diver && HasAuthority())
	{
		Diver->SetDivePose(ACampusCharacter::DiveNone);
	}
}

void ACampusBRDrop::Land()
{
	RestoreMovement();
	CutChute();
	State = ECampusBRDive::Done;
	DoneTime = float(GetWorld()->GetTimeSeconds());
}

void ACampusBRDrop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Diver)
	{
		// The pawn is gone (killed, or its player disconnected mid-air): nothing left to deploy.
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

	if (HasAuthority())
	{
		// Landing is checked BEFORE the clamps, so the touchdown frame is never handed a -4.5 m/s
		// velocity and a zero gravity scale it then has to be talked out of.
		const UCharacterMovementComponent* Movement = Diver->GetCharacterMovement();
		if (Movement && Movement->IsMovingOnGround())
		{
			Land();
			return;
		}
		if (State == ECampusBRDive::Freefall && HeightAboveGround() < CampusBRDive::AutoChuteHeight)
		{
			OpenChute();
		}
	}
	ApplyDiveMovement();
}

void ACampusBRDrop::EndPlay(const EEndPlayReason::Type Reason)
{
	// Both machines put the movement component back: a client that only ever saw the replicated state
	// still has its own copy of GravityScale to undo.
	RestoreMovement();
	CutChute(); // a canopy must never outlive the dive that opened it
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

	// A low-poly cargo plane out of three engine primitives, nose along +X because that is the axis
	// Launch points down the flight line. Fourteen components, no collision, no shadows, four flat
	// unlit materials - it is seen from 300 m below, on a phone, for forty-five seconds.
	//
	// The ROOT is a bare scene component on purpose. A passenger attaches to the root, and a root that
	// was itself a scaled mesh would hand its 16 x 2.6 x 2.6 scale to whoever boarded.
	Hull = CreateDefaultSubobject<USceneComponent>(TEXT("Hull"));
	SetRootComponent(Hull);

	UStaticMesh* const Cube = CampusBRArt::Cube();
	UStaticMesh* const Cylinder = CampusBRArt::Cylinder();
	UMaterialInterface* const Shell = CampusBRArt::Shell();
	UMaterialInterface* const Trim = CampusBRArt::Trim();
	UMaterialInterface* const Metal = CampusBRArt::Metal();

	auto MakePart = [this](const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material,
		const FVector& Where, const FRotator& Turn, const FVector& Size) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Hull);
		CampusBRArt::Dress(Part, Mesh, Material, Where, Turn, Size);
		return Part;
	};

	// The parts list, in centimetres, nose at +X. A cylinder's long axis is its local Z, which is what
	// the -90 pitch on the fuselage and the engines is for.
	//
	// 16 m fuselage, 2.6 m across - a cargo plane's 6:1.
	Fuselage = MakePart(TEXT("Fuselage"), Cylinder, Shell,
		FVector::ZeroVector, FRotator(-90.f, 0.f, 0.f), FVector(2.6f, 2.6f, 16.f));
	// A sphere stretched along the flight axis, capping the front.
	Nose = MakePart(TEXT("Nose"), CampusBRArt::Sphere(), Shell,
		FVector(790.f, 0.f, 0.f), FRotator::ZeroRotator, FVector(3.2f, 2.6f, 2.4f));
	// High wings, 18 m tip to tip, 3 degrees of dihedral so they are not a plank.
	WingLeft = MakePart(TEXT("WingLeft"), Cube, Trim,
		FVector(100.f, 555.f, 110.f), FRotator(0.f, 0.f, 3.f), FVector(3.2f, 8.5f, 0.35f));
	WingRight = MakePart(TEXT("WingRight"), Cube, Trim,
		FVector(100.f, -555.f, 110.f), FRotator(0.f, 0.f, -3.f), FVector(3.2f, 8.5f, 0.35f));
	// The tail: the one part that says "aircraft" from directly below.
	Fin = MakePart(TEXT("Fin"), Cube, Trim,
		FVector(-720.f, 0.f, 310.f), FRotator::ZeroRotator, FVector(2.6f, 0.3f, 3.6f));
	StabiliserLeft = MakePart(TEXT("StabiliserLeft"), Cube, Trim,
		FVector(-700.f, 195.f, 60.f), FRotator::ZeroRotator, FVector(1.8f, 3.2f, 0.3f));
	StabiliserRight = MakePart(TEXT("StabiliserRight"), Cube, Trim,
		FVector(-700.f, -195.f, 60.f), FRotator::ZeroRotator, FVector(1.8f, 3.2f, 0.3f));
	// Two engines, 2.8 m nacelles poking forward of the wing's leading edge.
	NacelleLeft = MakePart(TEXT("NacelleLeft"), Cylinder, Metal,
		FVector(230.f, 420.f, 110.f), FRotator(-90.f, 0.f, 0.f), FVector(1.f, 1.f, 2.8f));
	NacelleRight = MakePart(TEXT("NacelleRight"), Cylinder, Metal,
		FVector(230.f, -420.f, 110.f), FRotator(-90.f, 0.f, 0.f), FVector(1.f, 1.f, 2.8f));
	// 4.2 m propellers: two crossed slabs each, hung off the HULL and not off the nacelle, because the
	// nacelle is a scaled cylinder and a child of it would inherit that scale and shear. Each slab's
	// long axis is its local Y, so rolling it about local X sweeps a disc across the flight axis.
	BladeLeftA = MakePart(TEXT("BladeLeftA"), Cube, Metal,
		FVector(380.f, 420.f, 110.f), FRotator::ZeroRotator, FVector(0.3f, 4.2f, 0.08f));
	BladeLeftB = MakePart(TEXT("BladeLeftB"), Cube, Metal,
		FVector(380.f, 420.f, 110.f), FRotator(0.f, 0.f, 90.f), FVector(0.3f, 4.2f, 0.08f));
	BladeRightA = MakePart(TEXT("BladeRightA"), Cube, Metal,
		FVector(380.f, -420.f, 110.f), FRotator::ZeroRotator, FVector(0.3f, 4.2f, 0.08f));
	BladeRightB = MakePart(TEXT("BladeRightB"), Cube, Metal,
		FVector(380.f, -420.f, 110.f), FRotator(0.f, 0.f, 90.f), FVector(0.3f, 4.2f, 0.08f));
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
	// Nothing to see until Launch has handed it a line. Done HERE and not in the constructor because
	// SetupAttachment does not fill AttachChildren until the components register, so a propagating
	// SetVisibility in the constructor would hide the root and leave every part of the plane on screen.
	// On the host that is the frame between SpawnActor and Launch; on a phone it is the whole wait.
	if (Hull)
	{
		Hull->SetVisibility(LaunchTime > 0.f && FlightSeconds > 0.f, true);
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

FVector ACampusBRPlane::FlightLocation() const
{
	if (FlightSeconds <= 0.f)
	{
		return FVector(Finish);
	}
	// NOT clamped at the top end: past the end of the line the aircraft keeps going in a straight line
	// and leaves, which is the whole reason it can delete itself instead of parking over the map edge.
	const float Travel = FMath::Max((CampusBRServerTime(GetWorld()) - LaunchTime) / FlightSeconds, 0.f);
	return FMath::Lerp(FVector(Start), FVector(Finish), Travel);
}

FQuat ACampusBRPlane::Heading() const
{
	const FVector Along = FVector(Finish) - FVector(Start);
	return Along.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromX(Along).ToQuat();
}

bool ACampusBRPlane::IsOverMap() const
{
	const FVector Where = FlightLocation();
	const FVector2D Middle = CampusBRMap::Center();
	const float Half = CampusBRMap::HalfSpan();
	return FMath::Abs(Where.X - Middle.X) <= Half && FMath::Abs(Where.Y - Middle.Y) <= Half;
}

FVector ACampusBRPlane::SeatLocation() const
{
	return FlightLocation() + Heading().RotateVector(CampusBRDive::SeatOffset);
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
		// A drop whose diver has gone (killed on the slab, or disconnected during the ride) is not a
		// passenger. Without that test one lost pawn would keep the plane "occupied" for ever.
		if (Drop && Drop->GetDiver() && Drop->GetState() == ECampusBRDive::InPlane)
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

void ACampusBRPlane::SpinPropellers(float DeltaSeconds)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return; // nothing is drawing this, and nothing in the game reads the angle
	}
	PropAngle = FMath::Fmod(PropAngle + CampusBRArt::PropDegreesPerSecond * DeltaSeconds, 360.f);
	// Roll about the blade's own X, which is the flight axis. The B blade keeps the 90 degrees it was
	// built with, so each pair stays crossed and reads as one disc.
	const FQuat Turn(FVector::XAxisVector, FMath::DegreesToRadians(PropAngle));
	const FQuat Crossed(FVector::XAxisVector, FMath::DegreesToRadians(PropAngle + 90.f));
	UStaticMeshComponent* const Pairs[4] = { BladeLeftA, BladeRightA, BladeLeftB, BladeRightB };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Pairs); ++Index)
	{
		if (UStaticMeshComponent* Blade = Pairs[Index])
		{
			Blade->SetRelativeRotation(Index < 2 ? Turn : Crossed);
		}
	}
}

void ACampusBRPlane::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const bool bFlying = LaunchTime > 0.f && FlightSeconds > 0.f;
	if (Hull && Hull->GetVisibleFlag() != bFlying)
	{
		Hull->SetVisibility(bFlying, true);
	}
	if (!bFlying)
	{
		return;
	}
	// The ROTATION is set here, on every machine, from the replicated endpoints - not left to the one
	// SetActorLocationAndRotation that Launch does on the server. Movement is not replicated for this
	// actor, so a client that joined after the initial bunch would otherwise fly a plane-shaped object
	// sideways down the line. With a cube nobody could tell; with a plane everybody can.
	SetActorLocationAndRotation(FlightLocation(), Heading());
	SpinPropellers(DeltaSeconds);
	CampusBRDecor::SpawnForFlight(GetWorld(), this, int32(LaunchTime * 100.f));

	if (!HasAuthority())
	{
		return;
	}
	const bool bOver = IsOverMap();
	bEnteredMap |= bOver;
	const float Alpha = FlightAlpha();
	// Two ways out, and both are needed: leaving the far edge of the map square is the real deadline
	// (eject there and a diver can still glide back inside the first circle), and the end of the line
	// is the backstop for a route that somehow never crossed the square at all.
	if (Alpha >= 1.f || (bEnteredMap && !bOver))
	{
		EjectAll();
	}
	// Done its job: fly on out of sight and go. Leaving it alive meant an aeroplane hanging motionless
	// over the map edge for the remaining nine minutes of the match.
	if (Alpha >= 1.f && IsEmpty()
		&& CampusBRServerTime(GetWorld()) - LaunchTime > FlightSeconds * (1.f + CampusBRDive::ExitShare))
	{
		Destroy();
	}
}

// ---------------------------------------------------------------------------------------------
// The waiting platform

ACampusBRWaitingArea::ACampusBRWaitingArea()
{
	PrimaryActorTick.bCanEverTick = false;
	// Only the game mode spawns it, so it must replicate or clients have no floor (predict a fall,
	// get corrected every frame). Always relevant: everyone stands on it.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetCanBeDamaged(false);

	Slab = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Slab"));
	SetRootComponent(Slab);
	// It had collision but no mesh, so the lobby gathered on an invisible floor 40 m up. One cube and
	// the campus concrete instance, set in the constructor so the cooker sees both.
	Slab->SetStaticMesh(CampusBRArt::Cube());
	if (UMaterialInterface* Concrete = CampusBRArt::Shell())
	{
		Slab->SetMaterial(0, Concrete);
	}
	Slab->SetRelativeScale3D(FVector(40.f, 40.f, 0.5f));
	Slab->SetCollisionProfileName(TEXT("BlockAll"));
	Slab->SetCastShadow(false);
	Slab->bReceivesDecals = false;
}

FVector ACampusBRWaitingArea::Place()
{
	const FVector2D Middle = CampusBRMap::Center();
	const float Half = CampusBRMap::HalfSpan();
	// A corner of the map square, 40 m up: out of every sightline, and clearly "not the match yet".
	return FVector(Middle.X - Half * 0.82f, Middle.Y - Half * 0.82f, 4000.f);
}

int32 ACampusBRWaitingArea::SlotCount()
{
	return CampusBRWait::Columns * CampusBRWait::Rows;
}

FTransform ACampusBRWaitingArea::SlotFor(int32 Index) const
{
	const int32 Cell = FMath::Abs(Index) % SlotCount();
	const int32 Column = Cell % CampusBRWait::Columns;
	const int32 Row = Cell / CampusBRWait::Columns;
	// Scatter inside the cell, seeded from the cell: fifty players standing on an exact 4 m lattice read
	// as a parade square, and a cell that moved between the clearance test and the spawn would let two
	// arrivals land on the same square metre.
	const FRandomStream Scatter(Cell * 7919 + 13);
	const FVector Base = GetActorLocation();
	const FVector Where(
		Base.X + (Column - (CampusBRWait::Columns - 1) * 0.5f) * CampusBRWait::Spacing
			+ Scatter.FRandRange(-CampusBRWait::Jitter, CampusBRWait::Jitter),
		Base.Y + (Row - (CampusBRWait::Rows - 1) * 0.5f) * CampusBRWait::Spacing
			+ Scatter.FRandRange(-CampusBRWait::Jitter, CampusBRWait::Jitter),
		Base.Z + 120.f);
	const FVector2D Middle = CampusBRMap::Center();
	const FRotator Facing = FRotationMatrix::MakeFromX(FVector(Middle.X, Middle.Y, Base.Z) - Where).Rotator();
	return FTransform(FRotator(0.f, Facing.Yaw, 0.f), Where);
}

bool ACampusBRWaitingArea::IsSpotClear(const FVector& Where) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Pawn = *It;
		if (!Pawn || Pawn->IsPendingKillPending())
		{
			continue;
		}
		const FVector At = Pawn->GetActorLocation();
		// FLAT distance, and only against pawns on this floor: the whole campus is 40 m below, and a
		// player standing under the slab is not in the way of one spawning on top of it.
		if (FMath::Abs(At.Z - Where.Z) < CampusBRWait::SameFloor
			&& FVector::DistSquared2D(At, Where) < FMath::Square(CampusBRWait::MinSeparation))
		{
			return false;
		}
	}
	return true;
}

FTransform ACampusBRWaitingArea::FreeSlot() const
{
	const int32 Cells = SlotCount();
	const int32 First = FMath::RandHelper(Cells);
	for (int32 Step = 0; Step < Cells; ++Step)
	{
		const FTransform Candidate = SlotFor((First + Step) % Cells);
		if (IsSpotClear(Candidate.GetLocation()))
		{
			return Candidate;
		}
	}
	// Every cell taken (more pawns on the slab than cells, which fifty cannot reach): the random cell is
	// still a floor to stand on, and the spawn's collision handling nudges the pawn clear of the rest.
	return SlotFor(First);
}
