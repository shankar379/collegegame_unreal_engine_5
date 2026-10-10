#include "Campus/CampusSave.h"

#include "Kismet/GameplayStatics.h"

namespace
{
	const TCHAR* SlotName = TEXT("CampusProfile");

	FString Today()
	{
		return FDateTime::Now().ToString(TEXT("%Y-%m-%d"));
	}
}

UCampusSave* UCampusSave::LoadOrCreate()
{
	if (UCampusSave* Existing = Cast<UCampusSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
	{
		return Existing;
	}
	UCampusSave* Fresh = Cast<UCampusSave>(UGameplayStatics::CreateSaveGameObject(UCampusSave::StaticClass()));
	Fresh->PlayerName = FString::Printf(TEXT("Guest%04d"), FMath::RandRange(1000, 9999));
	Fresh->OwnedItems = { TEXT("Default"), TEXT("Rifle") };
	Fresh->Persist();
	return Fresh;
}

void UCampusSave::Persist()
{
	UGameplayStatics::SaveGameToSlot(this, SlotName, 0);
}

bool UCampusSave::TrySpend(int32 Amount)
{
	if (Coins < Amount)
	{
		return false;
	}
	Coins -= Amount;
	Persist();
	return true;
}

bool UCampusSave::CanClaimDaily() const
{
	return LastDailyClaim != Today();
}

void UCampusSave::ClaimDaily(int32 Amount)
{
	if (CanClaimDaily())
	{
		Coins += Amount;
		LastDailyClaim = Today();
		Persist();
	}
}
