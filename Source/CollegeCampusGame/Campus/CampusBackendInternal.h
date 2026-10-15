// FILE-LOCAL plumbing shared by the UCampusBackend implementation files, and nothing else.
//
// ONE SUBSYSTEM, MANY FILES. UCampusBackend is the whole API client and was far too large for one
// translation unit, so it is split by ROUTE GROUP:
//
//   CampusBackend.cpp             the transport - Initialize/Deinitialize, Request/Send/OpenRequest,
//                                 the token refresh, the 18+ replay, the 503 and 426 latches, and
//                                 the one place an error becomes a sentence (ErrorText)
//   CampusBackendAuth.cpp         sessions and identity: guest / register / login / refresh / logout,
//                                 the 18+ tick, the college list, GET /me and PATCH /me
//   CampusBackendEngagement.cpp   streak, missions, weekend board, and the claims for all three
//   CampusBackendLiveConfig.cpp   maintenance, minimum build, announcements
//   CampusBackendContent.cpp      blob key `content`: which version of each uploaded picture is live
//   CampusBackendEvents.cpp       blob key `events`, plus /events/:id/progress and /claim
//   CampusBackendModes.cpp        blob key `modes`: which modes and maps are on, and the BR schedule
//   CampusBackendReports.cpp      POST /players/:id/report, the one moderation call the game makes
//   CampusBackendStore.cpp        the gun store and the Play Console product map
//   CampusBackendCosmetics.cpp    the wardrobe's outfits and the lobby's eight decor anchors
//   CampusBackendPayments.cpp     Razorpay and Google Play, the pending order and its polling
//   CampusBackendRooms.cpp        create / join / admit, the match report and the host heartbeat
//
// WHAT GOES IN HERE: the JSON readers and the small helpers every one of those files needs. They
// used to sit in an anonymous namespace at the top of CampusBackend.cpp; this header IS that
// namespace, shared, so there is one copy of each and the readers cannot drift apart.
//
// WHY A NAMED NAMESPACE AND A using-directive, rather than these names at global scope: `Str`, `Num`,
// `Bool`, `Obj` and `Id` are exactly the names a header has no business putting in the global
// namespace. CampusApi keeps them out of it while every call site below reads the way it always did.
//
// WHY EVERYTHING IS `inline`: it is a header. Without it each including file would emit its own copy
// and the linker would reject the set; with it there is also no "unused" warning in whichever file
// happens not to call one, which matters because clang builds this project with -Werror.
//
// WHAT DOES NOT GO IN HERE: anything a file outside this set needs. That belongs in
// Campus/CampusBackend.h, which is the subsystem's actual public header.

#pragma once

#include "CoreMinimal.h"

#include "Campus/CampusBackend.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"

namespace CampusApi
{
	inline constexpr float HeartbeatSeconds = 30.f;

	inline FString PlatformName()
	{
#if PLATFORM_ANDROID
		return TEXT("android");
#elif PLATFORM_IOS
		return TEXT("ios");
#else
		return TEXT("windows");
#endif
	}

	/** 32 random bytes as base64url (no padding): the guest device secret. */
	inline FString NewDeviceSecret()
	{
		TArray<uint8> Bytes;
		Bytes.Reserve(32);
		for (int32 Part = 0; Part < 2; ++Part)
		{
			const FGuid Guid = FGuid::NewGuid();
			for (uint32 Word : { Guid.A, Guid.B, Guid.C, Guid.D })
			{
				for (int32 Byte = 0; Byte < 4; ++Byte)
				{
					Bytes.Add(uint8(Word >> (Byte * 8)));
				}
			}
		}
		FString Text = FBase64::Encode(Bytes, EBase64Mode::UrlSafe);
		Text.RemoveFromEnd(TEXT("="));
		Text.RemoveFromEnd(TEXT("="));
		return Text;
	}

	inline FString NewKey()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	inline FString Str(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Value;
		if (Json.IsValid())
		{
			Json->TryGetStringField(Field, Value);
		}
		return Value;
	}

	inline double Num(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		double Value = 0.0;
		if (Json.IsValid())
		{
			Json->TryGetNumberField(Field, Value);
		}
		return Value;
	}

	inline bool Bool(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		bool Value = false;
		if (Json.IsValid())
		{
			Json->TryGetBoolField(Field, Value);
		}
		return Value;
	}

	inline TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Json.IsValid() && Json->TryGetObjectField(Field, Out) ? *Out : nullptr;
	}

	/** An ISO-8601 instant from the API. Returns a zero date when the field is missing or unparseable. */
	inline FDateTime Stamp(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FDateTime Out;
		const FString Text = Str(Json, Field);
		return !Text.IsEmpty() && FDateTime::ParseIso8601(*Text, Out) ? Out : FDateTime();
	}

	/** An id that may arrive as a JSON number (announcements) or as a string, always read as text:
	 *  the client only ever compares it and remembers it, so it has no business being arithmetic. */
	inline FString Id(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		if (const FString Text = Str(Json, Field); !Text.IsEmpty())
		{
			return Text;
		}
		double Value = 0.0;
		return Json.IsValid() && Json->TryGetNumberField(Field, Value) ? LexToString(int64(Value)) : FString();
	}

	/** Every reward in the engagement API is the same open shape: {diamonds?, room_cards?, title?}. */
	inline FCampusReward ReadReward(const TSharedPtr<FJsonObject>& Json)
	{
		FCampusReward Out;
		Out.Diamonds = int32(Num(Json, TEXT("diamonds")));
		Out.RoomCards = int32(Num(Json, TEXT("room_cards")));
		Out.bTitle = Bool(Json, TEXT("title"));
		return Out;
	}

	inline TArray<FString> Strings(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value->AsString());
			}
		}
		return Out;
	}

	inline FCampusRoom ReadRoom(const TSharedPtr<FJsonObject>& Room)
	{
		FCampusRoom Out;
		Out.Code = Str(Room, TEXT("code"));
		Out.Address = Str(Room, TEXT("address"));
		// Keep the struct's 7777 when the API omits the port (an EOS room has no port at all).
		if (const int32 ReadPort = int32(Num(Room, TEXT("port"))); ReadPort > 0)
		{
			Out.Port = ReadPort;
		}
		// Newer API: one "connect" object says how to reach the host. type "eos" = P2P over the
		// internet (puid), anything else = the LAN form, which may repeat address/port in here.
		if (const TSharedPtr<FJsonObject> Connect = Obj(Room, TEXT("connect")))
		{
			Out.ConnectType = Str(Connect, TEXT("type")).ToLower();
			Out.HostPuid = Str(Connect, TEXT("puid"));
			if (const FString ConnectAddress = Str(Connect, TEXT("address")); !ConnectAddress.IsEmpty())
			{
				Out.Address = ConnectAddress;
			}
			if (const int32 ConnectPort = int32(Num(Connect, TEXT("port"))); ConnectPort > 0)
			{
				Out.Port = ConnectPort;
			}
		}
		Out.Map = Str(Room, TEXT("map"));
		Out.TeamSize = int32(Num(Room, TEXT("team_size")));
		Out.MaxPlayers = int32(Num(Room, TEXT("max_players")));
		return Out;
	}
}
