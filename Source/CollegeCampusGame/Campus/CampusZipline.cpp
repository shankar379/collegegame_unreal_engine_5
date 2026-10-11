#include "Campus/CampusZipline.h"

ACampusZipline::ACampusZipline()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
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
	const FVector Ends[2] = { Bottom, Top };
	for (int32 End = 0; End < 2; ++End)
	{
		const FVector Delta = Feet - Ends[End];
		if (Delta.Size2D() < UseDistance && FMath::Abs(Delta.Z) < 250.f)
		{
			return End;
		}
	}
	return INDEX_NONE;
}

FVector ACampusZipline::Landing(int32 End) const
{
	const FVector Here = End == 0 ? Bottom : Top;
	const FVector There = End == 0 ? Top : Bottom;
	const FVector Toward = (There - Here).GetSafeNormal2D();
	return Here + Toward * 20.f + FVector(0.f, 0.f, 45.f);
}
