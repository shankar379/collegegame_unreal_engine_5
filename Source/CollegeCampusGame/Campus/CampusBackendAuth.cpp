// WHO THE PLAYER IS (UCampusBackend). Sessions, identity, and the two gates every protected
// route is behind:
//
//   POST /auth/device    guest sign-in. The device id and secret are generated once, on this
//                        phone, and kept in the save - which is why a guest who logs out or
//                        reinstalls without registering first cannot get that account back.
//   POST /auth/register  email + password, and the one route that can bind a guest's progress.
//   POST /auth/login     email + password.
//   POST /auth/refresh   in Campus/CampusBackend.cpp, because the transport owns the retry.
//   POST /me/age         the 18+ tick. We store the TICK, never a date of birth or an age band.
//   GET  /me, PATCH /me  the profile, the college list and the board-privacy toggle.
//
// AcceptSession IS THE FUNNEL. Guest, register, login and refresh all land there, which is what
// makes it the right place - and the only place - to clear the 426 CLIENT_TOO_OLD latch: a 2xx
// arriving there is the server having just compared this build's `client_build` against its floor
// and accepted it.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Campus/CampusSave.h"

using namespace CampusApi;

// ---------------------------------------------------------------------------------------------
// Session

void UCampusBackend::AcceptSession(TSharedPtr<FJsonObject> Json)
{
	// THE 426 LATCH LETS GO HERE, and this is the only place it can. Every sign-in and every token
	// refresh funnels through this function and all of them send client_build, so a 2xx arriving here
	// is the server having just compared this build against its floor AND ACCEPTED IT. Without this
	// the latch was set for the life of the app run: a player who updated, or an owner who lowered
	// MIN_CLIENT_BUILD back down after a bad release, stared at UPDATE REQUIRED until a cold restart.
	// The floor read off the blob is untouched - IsUpdateRequired still honours a live one.
	bClientTooOld = false;
	AccessToken = Str(Json, TEXT("access_token"));
	if (UCampusSave* Profile = Save())
	{
		Profile->RefreshToken = Str(Json, TEXT("refresh_token"));
		Profile->Persist();
	}
	ReadPlayer(Obj(Json, TEXT("player")));
}

void UCampusBackend::ReadPlayer(TSharedPtr<FJsonObject> Player)
{
	PublicId = Str(Player, TEXT("public_id"));
	if (!Player.IsValid())
	{
		return;
	}
	PlayerId = Str(Player, TEXT("id"));
	PlayerName = Str(Player, TEXT("display_name"));
	Email = Str(Player, TEXT("email"));
	RollNo = Str(Player, TEXT("roll_no"));
	Branch = Str(Player, TEXT("branch"));
	College = Str(Player, TEXT("college"));
	CollegeId = Str(Player, TEXT("college_id"));
	// Additive field: an older server simply sends nothing and the badge keeps its level number.
	// Relative or absolute, resolved by CampusAvatar::Resolve at the point of use - never here.
	AvatarUrl = Str(Player, TEXT("avatar_url"));
	bShowCollege = Bool(Player, TEXT("show_college"));
	bGuest = Bool(Player, TEXT("guest"));
	bAdultConfirmed = Bool(Player, TEXT("adult_confirmed"));
	if (bAdultConfirmed)
	{
		// the server agrees this account is confirmed: no dialog is owed
		bAgeConfirmRequired = false;
	}
	if (UCampusSave* Profile = Save())
	{
		Profile->PlayerName = PlayerName;
		Profile->AvatarUrl = AvatarUrl;
		Profile->Persist();
	}
}

void UCampusBackend::ReadWallet(TSharedPtr<FJsonObject> Wallet)
{
	if (Wallet.IsValid())
	{
		RoomCards = int32(Num(Wallet, TEXT("room_cards")));
		Diamonds = int32(Num(Wallet, TEXT("diamonds")));
	}
}

void UCampusBackend::ResumeSession(FDone Done)
{
	if (!HasSavedSession())
	{
		Done(false, TEXT("No saved session."));
		return;
	}
	RefreshAccess([this, Done](bool bOk)
	{
		if (!bOk)
		{
			Done(false, LastError.IsEmpty() ? TEXT("Please sign in again.") : LastError);
			return;
		}
		LoadMe(Done);
	});
}

void UCampusBackend::SignInGuest(FDone Done)
{
	UCampusSave* Profile = Save();
	if (!Profile)
	{
		Done(false, TEXT("No profile."));
		return;
	}
	if (Profile->DeviceId.IsEmpty())
	{
		Profile->DeviceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		Profile->DeviceSecret = NewDeviceSecret();
		Profile->Persist();
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("device_id"), Profile->DeviceId);
	Body->SetStringField(TEXT("device_secret"), Profile->DeviceSecret);
	Body->SetStringField(TEXT("platform"), PlatformName());
	AddAdultTick(Body); // guest sign-in creates an account too: it needs the 18+ tick like register/login
	AddClientBuild(Body);
	Request(TEXT("POST"), TEXT("/auth/device"), Body, false, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200 && Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		AcceptSession(Json);
		LoadMe(Done);
	});
}

void UCampusBackend::Register(const FString& InEmail, const FString& Password, const FString& Name, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("email"), InEmail.TrimStartAndEnd());
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("display_name"), Name.TrimStartAndEnd());
	Body->SetStringField(TEXT("platform"), PlatformName());
	AddAdultTick(Body);
	AddClientBuild(Body);
	// signed in as a guest: the bearer makes the server upgrade that guest instead of starting over
	Request(TEXT("POST"), TEXT("/auth/register"), Body, IsSignedIn(), false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200 && Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		AcceptSession(Json);
		LoadMe(Done);
	});
}

void UCampusBackend::Login(const FString& InEmail, const FString& Password, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("email"), InEmail.TrimStartAndEnd());
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("platform"), PlatformName());
	AddAdultTick(Body);
	AddClientBuild(Body);
	Request(TEXT("POST"), TEXT("/auth/login"), Body, false, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		AcceptSession(Json);
		LoadMe(Done);
	});
}

void UCampusBackend::Logout()
{
	UCampusSave* Profile = Save();
	if (Profile && !Profile->RefreshToken.IsEmpty())
	{
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("refresh_token"), Profile->RefreshToken);
		Request(TEXT("POST"), TEXT("/auth/logout"), Body, false, false, [](int32, TSharedPtr<FJsonObject>) {});
		Profile->RefreshToken.Reset();
		Profile->Persist();
	}
	AccessToken.Reset();
	PlayerId.Reset();
	Guns.Reset();
	Offers.Reset();
	Packs.Reset();
}

// -------------------------------------------------------------------------------------------------
// 18+ gate. We store the tick, never a date of birth or an age band.

bool UCampusBackend::HasAdultTick() const
{
	const UCampusSave* Profile = Save();
	return Profile && Profile->bAdultConfirmed;
}

void UCampusBackend::SetAdultTick(bool bTicked)
{
	if (UCampusSave* Profile = Save(); Profile && Profile->bAdultConfirmed != bTicked)
	{
		Profile->bAdultConfirmed = bTicked;
		Profile->Persist();
	}
}

void UCampusBackend::AddAdultTick(TSharedPtr<FJsonObject> Body) const
{
	// Sent as camelCase, which is the spelling the API documents for the game client.
	if (Body.IsValid() && HasAdultTick())
	{
		Body->SetBoolField(TEXT("adultConfirmed"), true);
	}
}

void UCampusBackend::AddClientBuild(TSharedPtr<FJsonObject> Body) const
{
	// A NUMBER, never a version string: routes/auth.js refuses "1.4.2" outright, because comparing
	// build strings puts '9' above '10' and waves through exactly the build being retired.
	if (Body.IsValid())
	{
		Body->SetNumberField(TEXT("client_build"), ClientBuild());
	}
}

void UCampusBackend::ConfirmAdult(FDone Done)
{
	SetAdultTick(true);
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetBoolField(TEXT("adultConfirmed"), true);
	Request(TEXT("POST"), TEXT("/me/confirm-adult"), Body, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		bAgeConfirmRequired = false;
		bAdultConfirmed = true;
		ReadPlayer(Obj(Json, TEXT("player")));
		Done(true, FString());
	});
}

// -------------------------------------------------------------------------------------------------
// Colleges (one college = one leaderboard) and the board-privacy toggle

void UCampusBackend::LoadColleges(FDone Done)
{
	Request(TEXT("GET"), TEXT("/colleges"), nullptr, false, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		Colleges.Reset();
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(TEXT("colleges"), List))
		{
			Colleges.Reserve(List->Num());
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusCollege& College = Colleges.AddDefaulted_GetRef();
				College.Id = Str(Item, TEXT("id"));
				College.Name = Str(Item, TEXT("name"));
				College.City = Str(Item, TEXT("city"));
			}
		}
		Done(true, FString());
	});
}

void UCampusBackend::SetCollege(const FString& InCollegeId, bool bShow, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	if (InCollegeId.IsEmpty())
	{
		Body->SetField(TEXT("college_id"), MakeShared<FJsonValueNull>());
	}
	else
	{
		Body->SetStringField(TEXT("college_id"), InCollegeId);
	}
	Body->SetBoolField(TEXT("show_college"), bShow);
	Request(TEXT("PATCH"), TEXT("/me"), Body, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadPlayer(Obj(Json, TEXT("player")));
		ReadWallet(Obj(Json, TEXT("wallet")));
		Done(true, FString());
	});
}

// ---------------------------------------------------------------------------------------------
// Profile, wallet

void UCampusBackend::LoadMe(FDone Done)
{
	Request(TEXT("GET"), TEXT("/me"), nullptr, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadPlayer(Obj(Json, TEXT("player")));
		ReadWallet(Obj(Json, TEXT("wallet")));
		bDailyCardAvailable = Bool(Json, TEXT("daily_card_available"));
		Done(true, FString());
	});
}

void UCampusBackend::UpdateProfile(const FString& Name, const FString& InRollNo, const FString& InBranch, const FString& InCollege, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("display_name"), Name.TrimStartAndEnd());
	auto Optional = [&Body](const TCHAR* Field, const FString& Value)
	{
		const FString Trimmed = Value.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			Body->SetField(Field, MakeShared<FJsonValueNull>());
		}
		else
		{
			Body->SetStringField(Field, Trimmed);
		}
	};
	Optional(TEXT("roll_no"), InRollNo);
	Optional(TEXT("branch"), InBranch);
	Optional(TEXT("college"), InCollege);
	Request(TEXT("PATCH"), TEXT("/me"), Body, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadPlayer(Obj(Json, TEXT("player")));
		ReadWallet(Obj(Json, TEXT("wallet")));
		Done(true, FString());
	});
}

void UCampusBackend::ClaimDailyCard(FDone Done)
{
	Request(TEXT("POST"), TEXT("/me/daily-card"), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			if (Str(Obj(Json, TEXT("error")), TEXT("code")) == TEXT("ALREADY_CLAIMED"))
			{
				bDailyCardAvailable = false;
			}
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadWallet(Obj(Json, TEXT("wallet")));
		bDailyCardAvailable = false;
		Done(true, FString());
	});
}
