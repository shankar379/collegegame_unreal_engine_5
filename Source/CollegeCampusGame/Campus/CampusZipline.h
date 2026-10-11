// Zipline (three.js src/game/zipline.js): a sagging cable between two towers, ridden both ways.
// The towers and cable are part of the imported college mesh; this actor only holds the ride path.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusZipline.generated.h"

UCLASS()
class ACampusZipline : public AActor
{
	GENERATED_BODY()

public:
	ACampusZipline();

	/** Quadratic Bezier of the cable (world, cm): bottom pulley, sag control, top pulley. */
	UPROPERTY(EditAnywhere, Category="Zipline")
	FVector P0 = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Zipline")
	FVector P1 = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Zipline")
	FVector P2 = FVector::ZeroVector;

	/** Tower floors (world, cm): where you grab the line and where you step off. */
	UPROPERTY(EditAnywhere, Category="Zipline")
	FVector Bottom = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Zipline")
	FVector Top = FVector::ZeroVector;

	/** Cable length (cm). */
	UPROPERTY(EditAnywhere, Category="Zipline")
	float Length = 1.f;

	FVector PointAt(float Alpha) const;
	FVector TangentAt(float Alpha) const;

	/** Tower 0 (bottom) or 1 (top) whose grab spot is near these feet, or INDEX_NONE. */
	int32 NearEnd(const FVector& Feet) const;

	/** Where to step off at tower End: on the slab, a little toward the other tower. */
	FVector Landing(int32 End) const;

	static constexpr float Speed = 1300.f;
	static constexpr float Accel = 900.f;
	static constexpr float Hang = 160.f;
	static constexpr float UseDistance = 260.f;
};
