// Day and night on one level (Varsity Valley, /Game/Maps/L_VarsityValley).
//
// The map's world art is UNLIT - the same flat shader the campus uses, so it reads identically on every
// phone and costs nothing. An unlit world cannot be darkened by turning a light down, so the preset moves
// two things at once:
//
//   * the world's colour, through one vector parameter (DayNightTint) in MPC_VarsityTime. Every material
//     instance Scripts/build_varsity_valley.py made multiplies by it, so one parameter set takes the whole
//     map to night in a single frame, with no per-material work and no extra draw calls.
//   * the few real lights: the directional light, the sky light, the height fog, and the street lamps -
//     off by day, on at night, and only the ones near roads exist at all (phone budget).
//
// Which actor is which is read from tags (VVSun / VVSky / VVFog / VVLamp) that the build script sets, so
// the level can be rebuilt from Python without anybody re-pointing editor references.
//
// bNight is replicated: the server decides once (travel option "?Night=1", which the lobby already appends
// to the URL it opens the match with) and every client applies the same preset on the replication callback,
// including late joiners.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusDayNight.generated.h"

class AActor;
class UMaterialParameterCollection;

/** One lighting preset: everything that differs between noon and midnight on this map. */
USTRUCT()
struct FCampusTimePreset
{
	GENERATED_BODY()

	/** Multiplies every unlit world material (MPC_VarsityTime.DayNightTint). */
	UPROPERTY(EditAnywhere) FLinearColor WorldTint = FLinearColor::White;
	UPROPERTY(EditAnywhere) float SunIntensity = 3.2f;
	UPROPERTY(EditAnywhere) FLinearColor SunColor = FLinearColor::White;
	/** Pitch / yaw of the sun or moon, degrees. */
	UPROPERTY(EditAnywhere) FRotator SunRotation = FRotator(-48.f, -35.f, 0.f);
	UPROPERTY(EditAnywhere) float SkyIntensity = 1.6f;
	UPROPERTY(EditAnywhere) FLinearColor SkyColor = FLinearColor::White;
	UPROPERTY(EditAnywhere) float FogDensity = 0.012f;
	UPROPERTY(EditAnywhere) FLinearColor FogColor = FLinearColor(0.62f, 0.74f, 0.88f, 1.f);
	UPROPERTY(EditAnywhere) bool bLampsOn = false;
};

UCLASS()
class ACampusDayNight : public AActor
{
	GENERATED_BODY()

public:
	ACampusDayNight();

	/** Server only. Replicates to every client, which re-applies on OnRep_Night. */
	void SetNight(bool bInNight);
	bool IsNight() const { return bNight; }

	/** The two presets, editable per level if a designer ever wants a different dusk. */
	UPROPERTY(EditAnywhere, Category = "Campus|Time")
	FCampusTimePreset Day;

	UPROPERTY(EditAnywhere, Category = "Campus|Time")
	FCampusTimePreset Night;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Night)
	bool bNight = false;

	UFUNCTION()
	void OnRep_Night();

	/** Fills the actor lists from the level's tags. Cheap, and done once. */
	void CollectLights();
	void Apply();

	UPROPERTY(Transient) TArray<AActor*> Suns;
	UPROPERTY(Transient) TArray<AActor*> Skies;
	UPROPERTY(Transient) TArray<AActor*> Fogs;
	UPROPERTY(Transient) TArray<AActor*> Lamps;

	UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> TimeCollection;
};
