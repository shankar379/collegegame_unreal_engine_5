// THE WARDROBE AND THE LOBBY ROOM (UCampusBackend). The player's side is
// Campus/CampusWardrobeUI.cpp; what anything LOOKS like is Campus/CampusCosmetics.h. This file
// only moves ids.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Campus/CampusCosmetics.h"
#include "Campus/CampusSave.h"

using namespace CampusApi;

// -------------------------------------------------------------------------------------------------
// Cosmetics: the wardrobe's outfits and the lobby's eight decor anchors.
//
// THE SHAPE OF THE CONTRACT (one object, so one round trip dresses the whole lobby):
//   GET  /me/cosmetics          -> { equipped: {outfit: id}, presets: {"1": {outfit: id}, ...},
//                                    decor: {anchor: id}, owned: [{id, kind, slot, rarity, ...}] }
//   PUT  /me/cosmetics            { equipped: {outfit: id} }        full replace, idempotency key
//   PUT  /me/cosmetics/presets/1  { slots: {outfit: id} }           full replace, idempotency key
//   PUT  /me/lobby                { decor: {<all eight>: id|null} } full replace, idempotency key
//
// BOTH PUTs ARE A FULL REPLACE, which is why SaveLobbyDecor below sends all eight anchors every time
// and the bare ones as null: a patch of only what moved would leave the server holding a room nobody
// can see, and "take the trophy off the pedestal" would have no way to be said at all.
//
// ANY FAILURE KEEPS THE LOCAL CHOICE (409 NOT_OWNED, 422 SLOT_MISMATCH / UNKNOWN_ANCHOR / NOT_DECOR
// included). The device has already written and drawn it; the next GET /me/cosmetics is where the
// server wins, because the server is truth ON LOAD and nowhere else.
//
// Everything here goes through CallApi, like UCampusSocial does, so the token refresh, the 18+ replay,
// the Idempotency-Key and the error text are not re-implemented for one more feature.

bool UCampusBackend::OwnsCosmetic(const FString& Id) const
{
	if (Id.IsEmpty())
	{
		return false;
	}
	// The starter outfit is free and always owned, with or without a server - it is what a signed-out
	// player is wearing while they read the wardrobe.
	const int32 Outfit = CampusCosmetics::FindOutfit(FName(*Id));
	if (Outfit != INDEX_NONE && CampusCosmetics::Outfit(Outfit).bFree)
	{
		return true;
	}
	return OwnedCosmetics.ContainsByPredicate([&Id](const FCampusCosmetic& Item) { return Item.Id == Id; });
}

void UCampusBackend::LoadCosmetics(FDone Done)
{
	CallApi(TEXT("GET"), TEXT("/me/cosmetics"), nullptr, false,
		[this, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		if (!bOk)
		{
			if (Done)
			{
				Done(false, Error);
			}
			return;
		}
		// ABSENT IS NOT EMPTY, and this is the whole of the difference. `owned` and `equipped` are
		// unconditional parts of the contract, so a response without them IS an empty wardrobe. The
		// `presets` and `decor` objects are not: an older server, a partial projection or a route that
		// grows a 206 simply does not mention them. Clearing on the way in - which is what this used to
		// do - turned any such answer into "the player has no presets and an empty room", and then
		// mirrored that verdict into the save, where the next launch read it back as the truth.
		//
		// So each block below owns its own reset, inside the test that proves the key was really sent.
		OwnedCosmetics.Reset();
		EquippedOutfit = Str(Obj(Json, TEXT("equipped")), TEXT("outfit"));

		// presets: an OBJECT keyed "1".."3", because the API indexes them the way the player reads
		// them. Anything outside that range is somebody else's feature and is ignored.
		const TSharedPtr<FJsonObject>* Presets = nullptr;
		const bool bHasPresets = Json.IsValid() && Json->TryGetObjectField(TEXT("presets"), Presets) && Presets;
		if (bHasPresets)
		{
			CosmeticPresets.Init(FString(), CampusCosmetics::NumPresets);
			for (int32 Index = 0; Index < CampusCosmetics::NumPresets; ++Index)
			{
				const TSharedPtr<FJsonObject> Slots = Obj(*Presets, *FString::FromInt(Index + 1));
				CosmeticPresets[Index] = Str(Slots, TEXT("outfit"));
			}
		}
		else if (CosmeticPresets.Num() != CampusCosmetics::NumPresets)
		{
			// Never read one, and the server did not say: the array still has to be the right width
			// for the wardrobe to index it.
			CosmeticPresets.Init(FString(), CampusCosmetics::NumPresets);
		}

		const TSharedPtr<FJsonObject>* DecorField = nullptr;
		const bool bHasDecor = Json.IsValid() && Json->TryGetObjectField(TEXT("decor"), DecorField) && DecorField;
		if (bHasDecor)
		{
			const TSharedPtr<FJsonObject> Decor = *DecorField;
			LobbyDecor.Reset();
			for (const auto& Pair : Decor->Values)
			{
				FString Item;
				// A null (or anything that is not a string) is how the server says "this anchor is
				// bare", which is the same thing as the key being absent.
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(Item) && !Item.IsEmpty())
				{
					// FString(*Pair.Key) AND NOT Pair.Key: the key type of FJsonObject::Values is a
					// plain FString on MSVC and a TSharedString on the Android toolchain, and this is
					// the one spelling both accept (commit c147cb2). Do not "simplify" it.
					LobbyDecor.Add(FString(*Pair.Key), Item);
				}
			}
		}

		// gun_skins (migration 032): same ABSENT-IS-NOT-EMPTY rule as decor - an older server simply
		// does not send it, and that must not strip the skins the save already holds.
		const TSharedPtr<FJsonObject>* SkinsField = nullptr;
		const bool bHasSkins = Json.IsValid() && Json->TryGetObjectField(TEXT("gun_skins"), SkinsField) && SkinsField;
		if (bHasSkins)
		{
			GunSkins.Reset();
			for (const auto& Pair : (*SkinsField)->Values)
			{
				FString Item;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(Item) && !Item.IsEmpty())
				{
					GunSkins.Add(FString(*Pair.Key), Item); // FString(*Key): see the decor note above
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(TEXT("owned"), List))
		{
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				const FString Id = Str(Item, TEXT("id"));
				if (Id.IsEmpty())
				{
					continue;
				}
				FCampusCosmetic& Owned = OwnedCosmetics.AddDefaulted_GetRef();
				Owned.Id = Id;
				Owned.Name = Str(Item, TEXT("name"));
				Owned.Kind = Str(Item, TEXT("kind"));
				Owned.Slot = Str(Item, TEXT("slot"));
				Owned.Rarity = Str(Item, TEXT("rarity"));
				Owned.ForGun = Str(Item, TEXT("for_gun"));
			}
		}
		bCosmeticsLoaded = true;

		// MIRROR IT INTO THE SAVE. The server has just spoken, so this is the one moment the device's
		// copy is known to be wrong; from here the save is what paints the lobby on the next launch
		// before a single request has been made.
		if (UCampusSave* Profile = Save())
		{
			if (!EquippedOutfit.IsEmpty())
			{
				Profile->EquippedOutfit = FName(*EquippedOutfit);
			}
			// SAME RULE AS ABOVE, and it matters more here because the save is what the next launch
			// paints from: a response that never mentioned presets or decor must leave the device's
			// copy of them exactly where it was, not overwrite it with the nothing it was told.
			if (bHasPresets)
			{
				Profile->OutfitPresets.Init(NAME_None, CampusCosmetics::NumPresets);
				for (int32 Index = 0; Index < CampusCosmetics::NumPresets; ++Index)
				{
					Profile->OutfitPresets[Index] = CosmeticPresets[Index].IsEmpty()
						? NAME_None : FName(*CosmeticPresets[Index]);
				}
			}
			if (bHasSkins)
			{
				Profile->GunSkins.Reset();
				for (const TPair<FString, FString>& Pair : GunSkins)
				{
					Profile->GunSkins.Add(FName(*Pair.Key), FName(*Pair.Value));
				}
			}
			if (bHasDecor)
			{
				Profile->LobbyDecor.Reset();
				for (const TPair<FString, FString>& Pair : LobbyDecor)
				{
					Profile->LobbyDecor.Add(FName(*Pair.Key), FName(*Pair.Value));
				}
			}
			Profile->Persist();
		}
		if (Done)
		{
			Done(true, FString());
		}
	});
}

void UCampusBackend::SaveCosmetics(const FString& OutfitId, FDone Done)
{
	TSharedPtr<FJsonObject> Equipped = MakeShared<FJsonObject>();
	Equipped->SetStringField(TEXT("outfit"), OutfitId);
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("equipped"), Equipped);
	CallApi(TEXT("PUT"), TEXT("/me/cosmetics"), Body, true,
		[this, OutfitId, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		if (bOk)
		{
			// Trust the response when it echoes the equip back, so a server that renamed or rejected
			// an id is not argued with; fall back to what we asked for when it says nothing.
			const FString Confirmed = Str(Obj(Json, TEXT("equipped")), TEXT("outfit"));
			EquippedOutfit = Confirmed.IsEmpty() ? OutfitId : Confirmed;
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusBackend::SaveGunSkins(const TMap<FString, FString>& Skins, FDone Done)
{
	TSharedPtr<FJsonObject> Map = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Pair : Skins)
	{
		if (Pair.Value.IsEmpty())
		{
			Map->SetField(Pair.Key, MakeShared<FJsonValueNull>());
		}
		else
		{
			Map->SetStringField(Pair.Key, Pair.Value);
		}
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("skins"), Map);
	CallApi(TEXT("PUT"), TEXT("/me/gunskins"), Body, true,
		[this, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		const TSharedPtr<FJsonObject> Confirmed = Obj(Json, TEXT("gun_skins"));
		if (bOk && Confirmed.IsValid())
		{
			GunSkins.Reset();
			for (const auto& Pair : Confirmed->Values)
			{
				FString Item;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(Item) && !Item.IsEmpty())
				{
					GunSkins.Add(FString(*Pair.Key), Item);
				}
			}
			if (UCampusSave* Profile = Save())
			{
				Profile->GunSkins.Reset();
				for (const TPair<FString, FString>& Pair : GunSkins)
				{
					Profile->GunSkins.Add(FName(*Pair.Key), FName(*Pair.Value));
				}
				Profile->Persist();
			}
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusBackend::SavePreset(int32 Index, const FString& OutfitId, FDone Done)
{
	if (Index < 0 || Index >= CampusCosmetics::NumPresets)
	{
		if (Done)
		{
			Done(false, TEXT("That preset does not exist."));
		}
		return;
	}
	TSharedPtr<FJsonObject> Slots = MakeShared<FJsonObject>();
	Slots->SetStringField(TEXT("outfit"), OutfitId);
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("slots"), Slots);
	CallApi(TEXT("PUT"), FString::Printf(TEXT("/me/cosmetics/presets/%d"), Index + 1), Body, true,
		[this, Index, OutfitId, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (bOk)
		{
			if (CosmeticPresets.Num() < CampusCosmetics::NumPresets)
			{
				CosmeticPresets.Init(FString(), CampusCosmetics::NumPresets);
			}
			CosmeticPresets[Index] = OutfitId;
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusBackend::SaveLobbyDecor(const TMap<FString, FString>& Decor, FDone Done)
{
	// EVERY anchor, every time: the ones with nothing on them go up as JSON null. A patch of only the
	// anchors that changed would leave the server holding a room nobody can see, and "clear this
	// anchor" would have no way to be said at all.
	TSharedPtr<FJsonObject> Anchors = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < CampusCosmetics::NumAnchors; ++Index)
	{
		const FString Key(CampusCosmetics::Anchor(Index).Id);
		const FString* Item = Decor.Find(Key);
		if (Item && !Item->IsEmpty())
		{
			Anchors->SetStringField(Key, *Item);
		}
		else
		{
			Anchors->SetField(Key, MakeShared<FJsonValueNull>());
		}
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("decor"), Anchors);
	CallApi(TEXT("PUT"), TEXT("/me/lobby"), Body, true,
		[this, Decor, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (bOk)
		{
			LobbyDecor = Decor;
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}
