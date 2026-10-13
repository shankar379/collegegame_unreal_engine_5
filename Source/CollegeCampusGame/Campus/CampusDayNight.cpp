#include "Campus/CampusDayNight.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// The build script tags the level's lighting with these (Scripts/build_varsity_valley.py).
	const FName TagSun(TEXT("VVSun"));
	const FName TagSky(TEXT("VVSky"));
	const FName TagFog(TEXT("VVFog"));
	const FName TagLamp(TEXT("VVLamp"));
	const TCHAR* CollectionPath = TEXT("/Game/VarsityValley/MPC_VarsityTime");
	const FName TintParameter(TEXT("DayNightTint"));
	/** The old baked sky dome's mesh asset. Matched by ASSET name and not by actor label, because a
	 *  label is editor-only data and a cooked phone build has none. */
	const FName DomeMesh(TEXT("SM_VV_Sky"));
}

ACampusDayNight::ACampusDayNight()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	// Noon on the quad: the art was authored at full brightness, so day is the identity tint.
	Day.WorldTint = FLinearColor::White;
	Day.SunIntensity = 3.2f;
	Day.SunColor = FLinearColor(1.f, 0.96f, 0.88f);
	Day.SunRotation = FRotator(-48.f, -35.f, 0.f);
	Day.SkyIntensity = 1.6f;
	Day.SkyColor = FLinearColor(0.85f, 0.81f, 0.71f);
	Day.FogDensity = 0.012f;
	Day.FogColor = FLinearColor(0.62f, 0.74f, 0.88f);
	Day.bLampsOn = false;

	// Night: a cold moon, a third of the brightness with a blue bias, denser dark-blue fog so sight
	// lines close in, and the street lamps carrying the roads.
	Night.WorldTint = FLinearColor(0.20f, 0.24f, 0.38f);
	Night.SunIntensity = 0.45f;
	Night.SunColor = FLinearColor(0.55f, 0.66f, 1.f);
	Night.SunRotation = FRotator(-38.f, 120.f, 0.f);
	Night.SkyIntensity = 0.30f;
	Night.SkyColor = FLinearColor(0.22f, 0.29f, 0.52f);
	Night.FogDensity = 0.045f;
	Night.FogColor = FLinearColor(0.05f, 0.07f, 0.16f);
	Night.bLampsOn = true;
}

void ACampusDayNight::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusDayNight, bNight);
}

void ACampusDayNight::BeginPlay()
{
	Super::BeginPlay();

	TimeCollection = LoadObject<UMaterialParameterCollection>(nullptr, CollectionPath);
	CollectLights();
	RetireSkyDome();

	// The host picks the preset from the travel URL the lobby already builds ("?Night=1"); clients get
	// it through bNight. Nothing in the GameMode has to know this actor exists.
	if (HasAuthority())
	{
		const AGameModeBase* Mode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
		const FString Options = Mode ? Mode->OptionsString : FString();
		bNight = UGameplayStatics::HasOption(Options, TEXT("Night"))
			&& UGameplayStatics::ParseOption(Options, TEXT("Night")) != TEXT("0");
	}
	Apply();
}

void ACampusDayNight::EnsureFor(UWorld* World)
{
	if (!World)
	{
		return;
	}
	if (TActorIterator<ACampusDayNight> It(World); It)
	{
		return; // the level already has one
	}
	World->SpawnActor<ACampusDayNight>(ACampusDayNight::StaticClass(), FTransform::Identity);
}

void ACampusDayNight::SetNight(bool bInNight)
{
	if (!HasAuthority() || bNight == bInNight)
	{
		return;
	}
	bNight = bInNight;
	Apply();
}

void ACampusDayNight::OnRep_Night()
{
	Apply();
}

void ACampusDayNight::CollectLights()
{
	Suns.Reset();
	Skies.Reset();
	Fogs.Reset();
	Lamps.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->ActorHasTag(TagLamp))
		{
			Lamps.Add(Actor);
		}
		else if (Actor->ActorHasTag(TagSun))
		{
			Suns.Add(Actor);
		}
		else if (Actor->ActorHasTag(TagSky))
		{
			Skies.Add(Actor);
		}
		else if (Actor->ActorHasTag(TagFog))
		{
			Fogs.Add(Actor);
		}
	}
}

void ACampusDayNight::RetireSkyDome()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	bool bHasAtmosphere = false;
	int32 Retired = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->IsA<ASkyAtmosphere>())
		{
			bHasAtmosphere = true;
			continue;
		}
		AStaticMeshActor* Placed = Cast<AStaticMeshActor>(Actor);
		UStaticMeshComponent* Component = Placed ? Placed->GetStaticMeshComponent() : nullptr;
		const UStaticMesh* Asset = Component ? Component->GetStaticMesh() : nullptr;
		if (Asset && Asset->GetFName() == DomeMesh)
		{
			// Hidden, not destroyed: it is a level actor, so every machine has its own copy and this
			// stays a purely local decision about what is drawn. The dome never had collision.
			Component->SetVisibility(false, true);
			Placed->SetActorHiddenInGame(true);
			++Retired;
		}
	}
	if (!bHasAtmosphere)
	{
		// Purely visual, never replicated: each machine spawns its own, which is also what makes this
		// work on a client whose level predates the atmosphere.
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		World->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity, Params);
	}
	if (Retired > 0 || !bHasAtmosphere)
	{
		UE_LOG(LogTemp, Log, TEXT("CampusDayNight: hid %d baked sky dome actor(s), atmosphere %s"),
			Retired, bHasAtmosphere ? TEXT("already in the level") : TEXT("spawned"));
	}
}

void ACampusDayNight::Apply()
{
	const FCampusTimePreset& Preset = bNight ? Night : Day;

	// The unlit world: one parameter, every material instance.
	if (TimeCollection)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(this, TimeCollection, TintParameter, Preset.WorldTint);
	}

	for (AActor* Actor : Suns)
	{
		ADirectionalLight* Sun = Cast<ADirectionalLight>(Actor);
		UDirectionalLightComponent* Component = Sun ? Cast<UDirectionalLightComponent>(Sun->GetLightComponent()) : nullptr;
		if (!Component)
		{
			continue;
		}
		Component->SetIntensity(Preset.SunIntensity);
		Component->SetLightColor(Preset.SunColor);
		Sun->SetActorRotation(Preset.SunRotation);
		// The atmosphere needs a sun to scatter, and a level built before one was placed has a
		// directional light that is not marked as its sun - without this the sky renders black by day.
		// At NIGHT it is deliberately not the atmosphere's sun: a moon above the horizon scattering a
		// full daylight sky is a brighter blue at midnight than at noon. Night is the dark sky plus the
		// preset's dark-blue height fog, which is what the horizon is carried by.
		Component->SetAtmosphereSunLight(!bNight);
	}

	for (AActor* Actor : Skies)
	{
		if (ASkyLight* Sky = Cast<ASkyLight>(Actor))
		{
			Sky->GetLightComponent()->SetIntensity(Preset.SkyIntensity);
			Sky->GetLightComponent()->SetLightColor(Preset.SkyColor);
		}
	}

	for (AActor* Actor : Fogs)
	{
		if (AExponentialHeightFog* Fog = Cast<AExponentialHeightFog>(Actor))
		{
			UExponentialHeightFogComponent* Component = Fog->GetComponent();
			Component->SetFogDensity(Preset.FogDensity);
			Component->SetFogInscatteringColor(Preset.FogColor);
		}
	}

	for (AActor* Actor : Lamps)
	{
		if (APointLight* Lamp = Cast<APointLight>(Actor))
		{
			Lamp->GetLightComponent()->SetVisibility(Preset.bLampsOn);
		}
	}
}
