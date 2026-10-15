// THE GUN STORE (UCampusBackend): GET /store, then buy / upgrade / sell / equip.
//
// THE SERVER OWNS EVERY PRICE, every owned flag and every upgrade level; this file only moves ids
// and reads the wallet back out of each response. Nothing here adds up a cost locally, which is
// what stops the counters on two devices sharing one account from ever disagreeing.
//
// GooglePlayProductFor lives here because GET /store is what fills the map it reads: the Play
// Console product id for a catalogue item is published by the server, never compiled in.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"

using namespace CampusApi;

// ---------------------------------------------------------------------------------------------
// Store

const FCampusGun* UCampusBackend::FindGun(const FString& Id) const
{
	return Guns.FindByPredicate([&Id](const FCampusGun& Gun) { return Gun.Id == Id; });
}

void UCampusBackend::ReadStore(TSharedPtr<FJsonObject> Json)
{
	ReadWallet(Obj(Json, TEXT("wallet")));
	EquippedGun = Str(Json, TEXT("equipped_gun"));
	bPaymentsEnabled = Bool(Json, TEXT("payments_enabled"));
	Guns.Reset();
	Offers.Reset();
	Packs.Reset();
	GoogleProducts.Reset();
	// Play Console id per catalogue item; the store uses it to price and buy through Google Play.
	auto NoteGoogleId = [this](const TSharedPtr<FJsonObject>& Item, const FString& Id)
	{
		FString Product = Str(Item, TEXT("google_product_id"));
		if (Product.IsEmpty())
		{
			Product = Str(Item, TEXT("googleProductId"));
		}
		if (!Product.IsEmpty() && !Id.IsEmpty())
		{
			GoogleProducts.Add(Id, Product);
		}
	};
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (Json->TryGetArrayField(TEXT("guns"), List))
	{
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> Item = Value->AsObject();
			const TSharedPtr<FJsonObject> Stats = Obj(Item, TEXT("stats"));
			FCampusGun& Gun = Guns.AddDefaulted_GetRef();
			Gun.Id = Str(Item, TEXT("id"));
			Gun.Name = Str(Item, TEXT("name"));
			Gun.Description = Str(Item, TEXT("description"));
			Gun.Badge = Str(Item, TEXT("badge"));
			Gun.Perks = Strings(Item, TEXT("perks"));
			// The console's uploaded tile art (migration 026). Absent on a server that predates it,
			// and the drawn silhouette is the fallback either way.
			Gun.ImageKey = Str(Item, TEXT("image_key"));
			Gun.PriceDiamonds = int32(Num(Item, TEXT("price_diamonds")));
			Gun.PriceInr = float(Num(Item, TEXT("price_inr")));
			Gun.bOwned = Bool(Item, TEXT("owned"));
			Gun.bStarter = Bool(Item, TEXT("starter"));
			Gun.Level = int32(Num(Item, TEXT("level")));
			Gun.MaxLevel = int32(Num(Item, TEXT("max_level")));
			Gun.NextUpgradeDiamonds = int32(Num(Item, TEXT("next_upgrade_diamonds")));
			Gun.SellDiamonds = int32(Num(Item, TEXT("sell_diamonds")));
			Gun.Damage = float(Num(Stats, TEXT("damage")));
			Gun.FireRate = float(Num(Stats, TEXT("fire_rate")));
			Gun.Accuracy = float(Num(Stats, TEXT("accuracy")));
			Gun.Magazine = int32(Num(Stats, TEXT("mag")));
			NoteGoogleId(Item, Gun.Id);
		}
	}
	for (const TCHAR* Field : { TEXT("offers"), TEXT("packs") })
	{
		if (Json->TryGetArrayField(Field, List))
		{
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusOffer& Offer = (FCString::Strcmp(Field, TEXT("offers")) == 0 ? Offers : Packs).AddDefaulted_GetRef();
				Offer.Id = Str(Item, TEXT("id"));
				Offer.Name = Str(Item, TEXT("name"));
				Offer.Description = Str(Item, TEXT("description"));
				Offer.Badge = Str(Item, TEXT("badge"));
				Offer.Perks = Strings(Item, TEXT("perks"));
				Offer.ImageKey = Str(Item, TEXT("image_key"));
				Offer.PriceInr = float(Num(Item, TEXT("price_inr")));
				NoteGoogleId(Item, Offer.Id);
			}
		}
	}
}

void UCampusBackend::LoadStore(FDone Done)
{
	Request(TEXT("GET"), TEXT("/store"), nullptr, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadStore(Json);
		Done(true, FString());
	});
}

void UCampusBackend::BuyGun(const FString& Id, FDone Done)
{
	Request(TEXT("POST"), FString::Printf(TEXT("/store/items/%s/buy"), *Id), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		LoadStore(Done); // refresh owned / levels / wallet in one go
	});
}

void UCampusBackend::UpgradeGun(const FString& Id, FDone Done)
{
	Request(TEXT("POST"), FString::Printf(TEXT("/store/items/%s/upgrade"), *Id), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		LoadStore(Done);
	});
}

void UCampusBackend::SellGun(const FString& Id, FDone Done)
{
	Request(TEXT("POST"), FString::Printf(TEXT("/store/items/%s/sell"), *Id), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		LoadStore(Done);
	});
}

void UCampusBackend::EquipGun(const FString& Id, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("gun_id"), Id);
	Request(TEXT("PUT"), TEXT("/me/loadout"), Body, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		EquippedGun = Str(Json, TEXT("equipped_gun"));
		Done(true, FString());
	});
}

FString UCampusBackend::GooglePlayProductFor(const FString& ItemId) const
{
	const FString* Found = GoogleProducts.Find(ItemId);
	return Found ? *Found : FString();
}
