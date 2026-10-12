#include "Campus/CampusDayNight.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
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
