// CUSTOM ROOMS AND THE MATCH REPORT (UCampusBackend): create (costs one Room Card), join by
// code, admit a joining player, report the result, and the host's heartbeat.
//
// THE HOST IS A CLIENT, NOT A SERVER, and the heartbeat is what admits that: it tells the API the
// room is still alive and is also how the host learns the room was closed from somewhere else
// (the owner's console, or a stale-room sweep), at which point HostedRoom is dropped.
//
// ADMIT IS THE SEAM A CHEAT WOULD WALK THROUGH, so it is the server that says yes: the joining
// player hands over a ticket, the API answers with the player id or a rejection, and the host
// acts on the answer rather than on anything the joiner claimed about itself.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"

using namespace CampusApi;

// ---------------------------------------------------------------------------------------------
// Rooms

void UCampusBackend::CreateRoom(const FString& Map, int32 TeamSize, int32 Rounds, int32 Minutes, const FString& EosPuid, FRoomDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("rounds"), Rounds);
	Body->SetNumberField(TEXT("round_minutes"), Minutes);
	// THE API'S OWN NAME FOR THE MAP, which the caller has already resolved (CampusBR::ApiMapFor, and
	// routes/rooms.js MAPS is the enum): "classic", "campus" or "real". It used to be folded here, from
	// the catalogue id, into one of two values - which recorded a Campus Grounds room as the college.
	// Nothing keys off the field: a joiner travels to the host's address and loads whatever level the
	// host opened, so it is a label, but a label may as well be true.
	Body->SetStringField(TEXT("map"), Map);
	Body->SetNumberField(TEXT("team_size"), TeamSize == 1 || TeamSize == 2 ? TeamSize : 4);
	// Sent only when this phone really is EOS-logged-in: the API hands it to joiners as
	// connect:{type:"eos",puid}. Leaving it out keeps the room on the LAN form.
	if (!EosPuid.IsEmpty())
	{
		Body->SetStringField(TEXT("eos_puid"), EosPuid);
	}
	Request(TEXT("POST"), TEXT("/rooms"), Body, true, true, [this, Done, EosPuid](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 201)
		{
			Done(false, ErrorText(Status, Json), FCampusRoom());
			return;
		}
		ReadWallet(Obj(Json, TEXT("wallet")));
		HostedRoom = ReadRoom(Obj(Json, TEXT("room")));
		bHostedPlaying = false;
		HostedPlayers = 1;
		// Remembered so every heartbeat re-states it: if the room was created before this phone
		// finished its EOS login, the first heartbeat registers the puid and joiners get the EOS form.
		HostedEosPuid = EosPuid;
		Done(true, FString(), HostedRoom);
	});
}

void UCampusBackend::JoinRoom(const FString& Code, FRoomDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("code"), Code.TrimStartAndEnd().ToUpper());
	Request(TEXT("POST"), TEXT("/rooms/join"), Body, true, false, [Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json), FCampusRoom());
			return;
		}
		FCampusRoom Room = ReadRoom(Obj(Json, TEXT("room")));
		Room.Ticket = Str(Json, TEXT("ticket"));
		Done(true, FString(), Room);
	});
}

void UCampusBackend::AdmitPlayer(const FString& Ticket, TFunction<void(bool bRejected, const FString& PlayerId)> Done)
{
	if (!HostedRoom.IsValid() || Ticket.IsEmpty())
	{
		Done(HostedRoom.IsValid(), FString());
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("ticket"), Ticket);
	Request(TEXT("POST"), FString::Printf(TEXT("/rooms/%s/admit"), *HostedRoom.Code), Body, true, false, [Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		Done(Status >= 400 && Status < 500, Status == 200 ? Str(Json, TEXT("player_id")) : FString());
	});
}

void UCampusBackend::ReportMatchResult(const TArray<FCampusMatchResult>& Players)
{
	if (!HostedRoom.IsValid() || Players.IsEmpty())
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> Lines;
	for (const FCampusMatchResult& Player : Players)
	{
		TSharedPtr<FJsonObject> Line = MakeShared<FJsonObject>();
		Line->SetStringField(TEXT("player_id"), Player.PlayerId);
		Line->SetNumberField(TEXT("team"), Player.Team);
		Line->SetNumberField(TEXT("kills"), Player.Kills);
		Line->SetNumberField(TEXT("deaths"), Player.Deaths);
		Line->SetNumberField(TEXT("damage_dealt"), Player.DamageDealt);
		Line->SetNumberField(TEXT("damage_taken"), Player.DamageTaken);
		Line->SetBoolField(TEXT("won"), Player.bWon);
		Lines.Add(MakeShared<FJsonValueObject>(Line));
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("players"), Lines);
	// The API requires an Idempotency-Key on the result: one key per match, reused by Send() on every
	// retry (token refresh, 18+ confirm), so a replayed report can never pay the match out twice.
	// This runs once per match - the hosted room is cleared below - so the key is minted once.
	Request(TEXT("POST"), FString::Printf(TEXT("/rooms/%s/result"), *HostedRoom.Code), Body, true, true, [](int32 Status, TSharedPtr<FJsonObject>)
	{
		UE_LOG(LogTemp, Log, TEXT("Campus: match result reported (%d)"), Status);
	});
	HostedRoom = FCampusRoom(); // the server closed it; no more heartbeats
	HostedEosPuid.Reset();
}

void UCampusBackend::SetHostedRoomState(bool bPlaying, int32 PlayerCount)
{
	const bool bChanged = bPlaying != bHostedPlaying;
	bHostedPlaying = bPlaying;
	HostedPlayers = PlayerCount;
	if (bChanged)
	{
		HeartbeatTick(0.f); // 'playing' is what keeps the Room Card spent: tell the server right away
	}
}

void UCampusBackend::CloseHostedRoom()
{
	if (!HostedRoom.IsValid())
	{
		return;
	}
	Request(TEXT("DELETE"), FString::Printf(TEXT("/rooms/%s"), *HostedRoom.Code), nullptr, true, false, [this](int32, TSharedPtr<FJsonObject>)
	{
		LoadMe([](bool, const FString&) {}); // an unplayed room gives its card back
	});
	HostedRoom = FCampusRoom();
	HostedEosPuid.Reset();
}

bool UCampusBackend::HeartbeatTick(float)
{
	if (HostedRoom.IsValid() && IsSignedIn())
	{
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("status"), bHostedPlaying ? TEXT("playing") : TEXT("open"));
		Body->SetNumberField(TEXT("player_count"), HostedPlayers);
		// Re-states (or registers) how to reach this host over EOS. Harmless when it never changes.
		if (!HostedEosPuid.IsEmpty())
		{
			Body->SetStringField(TEXT("eos_puid"), HostedEosPuid);
		}
		Request(TEXT("POST"), FString::Printf(TEXT("/rooms/%s/heartbeat"), *HostedRoom.Code), Body, true, false, [this](int32 Status, TSharedPtr<FJsonObject>)
		{
			if (Status == 404)
			{
				HostedRoom = FCampusRoom(); // closed by the server (stale) or elsewhere
			}
		});
	}
	return true; // keep ticking
}
