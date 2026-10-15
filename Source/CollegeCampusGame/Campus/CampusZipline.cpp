#include "Campus/CampusZipline.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

/** THE ART, loaded with FObjectFinder from the constructor so the cooker sees it in the class default
 *  object. Both basic shapes are engine content the levels place anyway, and every colour is one of
 *  the flat unlit campus instances Config/DefaultGame.ini already cooks whole
 *  (+DirectoriesToAlwaysCook=/Game/Campus/Materials) - so there is no asset to generate here.
 *
 *  A NAMED namespace: unity builds put several of these files in one translation unit (see the note
 *  in CampusBR.cpp). The hex in each name IS the three.js colour from zipline.js tower(). */
namespace CampusZiplineArt
{
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
	/** steel #45484d: posts, top beams, the pulley bar. */
	UMaterialInterface* Steel()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_45484d_100.MI_Campus_45484d_100"));
		return Found.Object;
	}
	/** steelLight #5d6168: rungs, railings, cross members, the control box. */
	UMaterialInterface* SteelLight()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_5d6168_100.MI_Campus_5d6168_100"));
		return Found.Object;
	}
	/** concrete #8d877b: the slab and its step. */
	UMaterialInterface* Concrete()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_8d877b_100.MI_Campus_8d877b_100"));
		return Found.Object;
	}
	/** dark #2b2d31: foot plates, the box's door, the pulley wheel. */
	UMaterialInterface* Dark()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_2b2d31_100.MI_Campus_2b2d31_100"));
		return Found.Object;
	}
	/** cable #1b1c1f. */
	UMaterialInterface* Cable()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_1b1c1f_100.MI_Campus_1b1c1f_100"));
		return Found.Object;
	}

	// zipline.js tower(): half width across the cable, half depth of each side frame along it, and the
	// post section. Every number below is the web builder's metres x 100.
	constexpr float HalfWidth = 200.f;
	constexpr float HalfDepth = 75.f;
	constexpr float Post = 34.f;
	/** The engine cube and cylinder are both 100 cm and centred on their own origin, so a size in cm
	 *  divided by this reads straight off as a scale. */
	constexpr float Unit = 100.f;
	/** Cable radius, and how many cylinders the Bezier is drawn with (the web uses a 48-segment tube;
	 *  20 is smooth enough for an 8 cm rope on a phone). */
	constexpr float CableRadius = 3.5f;
	constexpr int32 CableSegments = 20;
	/** Arc-length samples for the Bezier. three.js Curve.getLength uses 200 by default; the cable is a
	 *  shallow parabola, so 64 agrees with it to well under a centimetre. */
	constexpr int32 LengthSamples = 64;
}

/** THE LINES EACH LEVEL CARRIES, resolved from the level's name exactly the way CampusClashSpawn::For
 *  resolves its team pads - a new line is a row here and nothing else.
 *
 *  L_MATCH is the web game's own pair, straight out of zipline.js ZIPLINES through the transform the
 *  college room uses everywhere: Unreal = (x, z, y) * 100 with the campus origin (15710, 8222.6) on
 *  X and Y. Both run from the ground up to the roof of the front block (the web rides them both
 *  ways), and both are drawn by the imported mesh already, so bBuildArt is false for them.
 *
 *  L_CAMPUSMAP and L_VARSITYVALLEY have no ziplines in the web game - their worlds have no walkable
 *  roof to hang one from (worlds.js `scenery: false`), so there is nothing to copy and these are
 *  placed here. Both gantries of a placed line stand on the same ground, which makes the cable level
 *  and the ride a flat run with the web's own sag and dismount drop; the alternative - a tall far
 *  gantry - would end the ride six metres above its own landing slab. */
/** Every line alive, for ACampusZipline::All. A NAMED namespace and not an anonymous one: unity
 *  builds put several of these files in one translation unit (see the note in CampusBR.cpp). */
namespace CampusZiplineRegistry
{
	TArray<TWeakObjectPtr<ACampusZipline>> Lines;
}

namespace CampusZiplinePlacement
{
	struct FPlacement
	{
		const TCHAR* Level;
		/** Both gantry floors in world cm, and the gantry height over each. */
		FVector FloorA;
		float TowerA;
		FVector FloorB;
		float TowerB;
		bool bBuildArt;
	};

	const FPlacement Placements[] = {
		// L_Match, RED: the courtyard on the left side up to the roof of the front block.
		{ TEXT("L_Match"),
			FVector(13710.f, 872.6f, 9.f), 460.f,
			FVector(13254.f, -2730.4f, 2838.f), 560.f, false },
		// L_Match, BLUE: the road outside the main gate up to the front edge of the same roof.
		{ TEXT("L_Match"),
			FVector(18210.f, -6477.4f, 4.f), 460.f,
			FVector(19553.f, -3566.4f, 2838.f), 600.f, false },

		// L_CampusMap. Image pixels through the transform campusmap_facts.md §2 states -
		// (pixel - CENTER) * PX * 100, CENTER = [700, 560], PX = 0.15 m - and the ground is at Z = 7
		// (src/world/campusmap/index.js GROUND_Y = 0.07).
		// MAIN ROAD, 54 m: the courtyard's south exit (662, 700) out through the main gate (662, 1060).
		// Dead centre of the carriageway, so it passes between the gate's two guard houses and UNDER
		// its arch beam (that beam's underside is at 5.2 m; the cable never rises past 4.0).
		{ TEXT("L_CampusMap"),
			FVector(-570.f, 2100.f, 7.f), 460.f,
			FVector(-570.f, 7500.f, 7.f), 460.f, true },
		// PARKING -> SOUTH LOT, 60 m: (250, 640) to (250, 1040), straight through the inner wall's
		// parking gap (layout.js WALL_GAPS spans pixels 125..270 there), ending clear of Team A's pad.
		{ TEXT("L_CampusMap"),
			FVector(-6750.f, 1200.f, 7.f), 460.f,
			FVector(-6750.f, 7200.f, 7.f), 460.f, true },

		// L_VarsityValley (Scripts/varsity_layout.py): the ground's top face is Z = 0 everywhere but
		// the hills, which are all out past +-31000. Both lines sit on the VERGE of the central
		// east-west road at |y| = 1200: outside the 14 m carriageway (ROAD_W / 2 = 700), so a gantry
		// is never a road block, and inside the strip the scatter pass skips (on_road(x, y, 700)), so
		// no tree, rock or crate can be standing where one is built. Clear of every ZONE_KEEPOUT.
		// WEST, 70 m: between the loop road and the middle, on the College Block / Sports side.
		{ TEXT("L_VarsityValley"),
			FVector(-13000.f, -1200.f, 0.f), 460.f,
			FVector(-6000.f, -1200.f, 0.f), 460.f, true },
		// EAST, 70 m: the same on the Hostels / Market side.
		{ TEXT("L_VarsityValley"),
			FVector(6000.f, 1200.f, 0.f), 460.f,
			FVector(13000.f, 1200.f, 0.f), 460.f, true },
	};
}

ACampusZipline::ACampusZipline()
{
	PrimaryActorTick.bCanEverTick = false;
	// Six numbers, replicated once and never again: cheap enough to keep every line relevant to
	// everyone, which it must be - the ride RPCs pass this actor by reference.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	auto MakePart = [this](const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, bool bSolid)
	{
		UInstancedStaticMeshComponent* Part = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Part->SetupAttachment(RootComponent);
		Part->SetMobility(EComponentMobility::Movable);
		if (Mesh)
		{
			Part->SetStaticMesh(Mesh);
		}
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
		// The posts, braces, beams and slab are solid: players, cameras and bullets all stop on them,
		// exactly as they do on the web gate. The cable is not (zipline.js: userData.noCollide).
		Part->SetCollisionProfileName(bSolid ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		// A line is placed after the navmesh is built, and a gantry is a thing to walk around, not a
		// reason to rebuild navigation on a phone.
		Part->SetCanEverAffectNavigation(false);
		Part->CastShadow = false;
		return Part;
	};
	Steel = MakePart(TEXT("Steel"), CampusZiplineArt::Cube(), CampusZiplineArt::Steel(), true);
	SteelLight = MakePart(TEXT("SteelLight"), CampusZiplineArt::Cube(), CampusZiplineArt::SteelLight(), true);
	Concrete = MakePart(TEXT("Concrete"), CampusZiplineArt::Cube(), CampusZiplineArt::Concrete(), true);
	Dark = MakePart(TEXT("Dark"), CampusZiplineArt::Cube(), CampusZiplineArt::Dark(), true);
	CableLine = MakePart(TEXT("Cable"), CampusZiplineArt::Cylinder(), CampusZiplineArt::Cable(), false);
}

void ACampusZipline::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusZipline, Span);
}

void ACampusZipline::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ACampusZipline::BeginPlay()
{
	Super::BeginPlay();
	CampusZiplineRegistry::Lines.Add(this);
	// Spawned from the table, so the span never went through a construction script.
	Rebuild();
}

void ACampusZipline::EndPlay(const EEndPlayReason::Type Reason)
{
	CampusZiplineRegistry::Lines.RemoveAllSwap([this](const TWeakObjectPtr<ACampusZipline>& Other) { return Other.Get() == this; });
	Super::EndPlay(Reason);
}

const TArray<TWeakObjectPtr<ACampusZipline>>& ACampusZipline::All()
{
	return CampusZiplineRegistry::Lines;
}

void ACampusZipline::OnRep_Span()
{
	Rebuild();
}

void ACampusZipline::SpawnForLevel(UWorld* World)
{
	if (!World)
	{
		return;
	}
	if (TActorIterator<ACampusZipline> It(World); It)
	{
		return; // the level already carries its lines
	}
	const FString Level = World->GetMapName();
	for (const CampusZiplinePlacement::FPlacement& Each : CampusZiplinePlacement::Placements)
	{
		if (!Level.Contains(Each.Level))
		{
			continue;
		}
		// Spawned at the middle of the span: the art and the path are stated in world cm, so the
		// transform only decides where the actor's bounds sit for relevancy and culling.
		const FTransform Middle((Each.FloorA + Each.FloorB) * 0.5f);
		ACampusZipline* Line = World->SpawnActorDeferred<ACampusZipline>(ACampusZipline::StaticClass(), Middle);
		if (!Line)
		{
			continue;
		}
		Line->Span.EndA = Each.FloorA;
		Line->Span.EndB = Each.FloorB;
		Line->Span.TowerA = Each.TowerA;
		Line->Span.TowerB = Each.TowerB;
		Line->Span.bBuildArt = Each.bBuildArt;
		Line->FinishSpawning(Middle);
	}
}

void ACampusZipline::Rebuild()
{
	Floors[0] = Span.EndA;
	Floors[1] = Span.EndB;
	// The pulley each end of the cable is tied to: zipline.js `e.y + 0.3 + e.tower - 0.95`.
	P0 = Floors[0] + FVector(0.f, 0.f, SlabTop + Span.TowerA - PulleyDrop);
	P2 = Floors[1] + FVector(0.f, 0.f, SlabTop + Span.TowerB - PulleyDrop);
	// The sag: the control point is the midpoint dropped by SAG, so the cable's own middle dips by
	// half of it - the same quadratic the web builds.
	P1 = (P0 + P2) * 0.5f - FVector(0.f, 0.f, Sag);

	float Walked = 0.f;
	FVector Previous = P0;
	for (int32 Step = 1; Step <= CampusZiplineArt::LengthSamples; ++Step)
	{
		const FVector Here = PointAt(float(Step) / float(CampusZiplineArt::LengthSamples));
		Walked += FVector::Dist(Previous, Here);
		Previous = Here;
	}
	Length = FMath::Max(Walked, 1.f);

	Steel->ClearInstances();
	SteelLight->ClearInstances();
	Concrete->ClearInstances();
	Dark->ClearInstances();
	CableLine->ClearInstances();
	if (!Span.bBuildArt)
	{
		return;
	}
	const FVector Toward = (Floors[1] - Floors[0]).GetSafeNormal2D();
	if (Toward.IsNearlyZero())
	{
		return; // a span with no length has no gate to point
	}
	BuildGantry(Floors[0], Span.TowerA, Toward);
	BuildGantry(Floors[1], Span.TowerB, -Toward);
	BuildCable();
}

void ACampusZipline::AddBox(UInstancedStaticMeshComponent* Into, const FVector& Floor, const FVector& Across,
	const FVector& Along, const FVector& Local, const FVector& Size)
{
	const FVector Where = Floor + Across * Local.X + Along * Local.Y + FVector(0.f, 0.f, Local.Z);
	const FRotator Facing = FRotationMatrix::MakeFromXY(Across, Along).Rotator();
	Into->AddInstance(FTransform(Facing, Where, Size / CampusZiplineArt::Unit), true);
}

void ACampusZipline::AddBar(UInstancedStaticMeshComponent* Into, const FVector& Floor, const FVector& Across,
	const FVector& Along, const FVector& From, const FVector& To, float Thickness)
{
	const FVector A = Floor + Across * From.X + Along * From.Y + FVector(0.f, 0.f, From.Z);
	const FVector B = Floor + Across * To.X + Along * To.Y + FVector(0.f, 0.f, To.Z);
	const FVector Run = B - A;
	const float Reach = Run.Size();
	if (Reach < 1.f)
	{
		return;
	}
	// The cube's local +Z is stretched along the bar, so the rotation only has to point +Z down it.
	const FRotator Facing = FRotationMatrix::MakeFromZ(Run).Rotator();
	Into->AddInstance(FTransform(Facing, (A + B) * 0.5f,
		FVector(Thickness, Thickness, Reach) / CampusZiplineArt::Unit), true);
}

void ACampusZipline::BuildGantry(const FVector& Floor, float Height, const FVector& Along)
{
	// W, D and P are the web builder's own names for these three (zipline.js tower()): half width
	// across the cable, half depth of each side frame along it, and the post section.
	constexpr float W = CampusZiplineArt::HalfWidth;
	constexpr float D = CampusZiplineArt::HalfDepth;
	constexpr float P = CampusZiplineArt::Post;
	// Local axes of that builder: X across the cable, Y along it (its local +Z), Z up. `Along` points
	// at the other gantry, so the cable runs straight through the open middle of the gate.
	// Along x Up and NOT Up x Along: MakeFromXY takes the third axis from X cross Y, and the other
	// order gives a frame whose "up" points DOWN - which buries every post in the ground.
	const FVector Across = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal();
	const float Top = SlabTop + Height;

	// The concrete slab and its step.
	AddBox(Concrete, Floor, Across, Along, FVector(0.f, 0.f, 15.f), FVector(2.f * W + 100.f, 2.f * D + 140.f, 30.f));
	AddBox(Concrete, Floor, Across, Along, FVector(0.f, 0.f, 6.f), FVector(2.f * W + 130.f, 2.f * D + 170.f, 12.f));

	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float PostX = Side == 0 ? -W : W;
		// Twin posts with foot plates and caps.
		for (int32 Pair = 0; Pair < 2; ++Pair)
		{
			const float PostY = Pair == 0 ? -D : D;
			AddBox(Steel, Floor, Across, Along, FVector(PostX, PostY, SlabTop + Height * 0.5f), FVector(P, P, Height));
			AddBox(Dark, Floor, Across, Along, FVector(PostX, PostY, 36.f), FVector(P + 20.f, P + 20.f, 12.f));
			AddBox(SteelLight, Floor, Across, Along, FVector(PostX, PostY, Top + 8.f), FVector(P + 12.f, P + 12.f, 16.f));
		}
		// Braces between the twin posts: a rung at each bay, and an X across every bay.
		const float Bays[4] = { SlabTop, SlabTop + Height * 0.36f, SlabTop + Height * 0.68f, Top - 20.f };
		for (const float BayZ : Bays)
		{
			AddBox(SteelLight, Floor, Across, Along, FVector(PostX, 0.f, BayZ), FVector(20.f, 2.f * D, 18.f));
		}
		for (int32 Bay = 0; Bay < 3; ++Bay)
		{
			AddBar(Steel, Floor, Across, Along, FVector(PostX, -D, Bays[Bay]), FVector(PostX, D, Bays[Bay + 1]), 12.f);
			AddBar(Steel, Floor, Across, Along, FVector(PostX, D, Bays[Bay]), FVector(PostX, -D, Bays[Bay + 1]), 12.f);
		}
	}

	// The top: twin main beams across, a truss railing with studs over each of them.
	for (int32 Pair = 0; Pair < 2; ++Pair)
	{
		const float BeamY = Pair == 0 ? -D : D;
		AddBox(Steel, Floor, Across, Along, FVector(0.f, BeamY, Top - 25.f), FVector(2.f * W + P + 30.f, 34.f, 42.f));
		AddBox(SteelLight, Floor, Across, Along, FVector(0.f, BeamY, Top + 42.f), FVector(2.f * W + P + 30.f, 20.f, 14.f));
		for (int32 Stud = 0; Stud <= 8; ++Stud)
		{
			const float StudX = FMath::Lerp(-W, W, Stud / 8.f);
			AddBox(Steel, Floor, Across, Along, FVector(StudX, BeamY, Top + 21.f), FVector(10.f, 10.f, 42.f));
		}
	}
	// Cross members tying the two top beams together.
	const float Ties[4] = { -W, -W / 3.f, W / 3.f, W };
	for (const float TieX : Ties)
	{
		AddBox(SteelLight, Floor, Across, Along, FVector(TieX, 0.f, Top - 10.f), FVector(16.f, 2.f * D, 16.f));
	}

	// The control box on one post pair, door outward.
	AddBox(SteelLight, Floor, Across, Along, FVector(W + 45.f, 0.f, Top - 140.f), FVector(50.f, 75.f, 110.f));
	AddBox(Dark, Floor, Across, Along, FVector(W + 72.f, 0.f, Top - 140.f), FVector(6.f, 45.f, 60.f));

	// The pulley bar along the cable, its two angled hangers, and the wheel the cable is tied through -
	// which is exactly the height PointAt(0) and PointAt(1) return.
	const float Pulley = Top - PulleyDrop;
	AddBox(Steel, Floor, Across, Along, FVector(0.f, 0.f, Pulley + 20.f), FVector(22.f, 2.f * D + 120.f, 22.f));
	for (int32 Pair = 0; Pair < 2; ++Pair)
	{
		const float Sign = Pair == 0 ? -1.f : 1.f;
		AddBar(Steel, Floor, Across, Along, FVector(0.f, Sign * (D + 50.f), Pulley + 20.f),
			FVector(0.f, Sign * D, Top - 25.f), 12.f);
	}
	AddBox(Dark, Floor, Across, Along, FVector(0.f, 0.f, Pulley), FVector(60.f, 16.f, 60.f));
}

void ACampusZipline::BuildCable()
{
	constexpr float Thickness = CampusZiplineArt::CableRadius * 2.f;
	FVector Previous = P0;
	for (int32 Segment = 1; Segment <= CampusZiplineArt::CableSegments; ++Segment)
	{
		const FVector Here = PointAt(float(Segment) / float(CampusZiplineArt::CableSegments));
		const FVector Run = Here - Previous;
		const float Reach = Run.Size();
		if (Reach >= 1.f)
		{
			// The engine cylinder is 100 cm tall about its own centre with its axis on local +Z.
			CableLine->AddInstance(FTransform(FRotationMatrix::MakeFromZ(Run).Rotator(), (Previous + Here) * 0.5f,
				FVector(Thickness, Thickness, Reach) / CampusZiplineArt::Unit), true);
		}
		Previous = Here;
	}
}

FVector ACampusZipline::PointAt(float Alpha) const
{
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	const float B = 1.f - A;
	return B * B * P0 + 2.f * B * A * P1 + A * A * P2;
}

FVector ACampusZipline::TangentAt(float Alpha) const
{
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	return (2.f * (1.f - A) * (P1 - P0) + 2.f * A * (P2 - P1)).GetSafeNormal();
}

int32 ACampusZipline::NearEnd(const FVector& Feet) const
{
	for (int32 End = 0; End < 2; ++End)
	{
		const FVector Delta = Feet - Floors[End];
		if (Delta.Size2D() < UseDistance && FMath::Abs(Delta.Z) < ReachHeight)
		{
			return End;
		}
	}
	return INDEX_NONE;
}

FVector ACampusZipline::Landing(int32 End) const
{
	const int32 Here = FMath::Clamp(End, 0, 1);
	const FVector Toward = (Floors[1 - Here] - Floors[Here]).GetSafeNormal2D();
	return Floors[Here] + Toward * 20.f + FVector(0.f, 0.f, 45.f);
}
