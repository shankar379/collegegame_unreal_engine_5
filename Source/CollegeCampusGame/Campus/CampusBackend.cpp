#include "Campus/CampusBackend.h"

#include "Campus/CampusGameInstance.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusRazorpay.h"
#include "Campus/CampusSave.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr float HeartbeatSeconds = 30.f;

	FString PlatformName()
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
	FString NewDeviceSecret()
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

	FString NewKey()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	FString Str(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Value;
		if (Json.IsValid())
		{
			Json->TryGetStringField(Field, Value);
		}
		return Value;
	}

	double Num(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		double Value = 0.0;
		if (Json.IsValid())
		{
			Json->TryGetNumberField(Field, Value);
		}
		return Value;
	}

	bool Bool(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		bool Value = false;
		if (Json.IsValid())
		{
			Json->TryGetBoolField(Field, Value);
		}
		return Value;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Json.IsValid() && Json->TryGetObjectField(Field, Out) ? *Out : nullptr;
	}

	TArray<FString> Strings(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
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

	FCampusRoom ReadRoom(const TSharedPtr<FJsonObject>& Room)
	{
		FCampusRoom Out;
		Out.Code = Str(Room, TEXT("code"));
		Out.Address = Str(Room, TEXT("address"));
		Out.Port = int32(Num(Room, TEXT("port")));
		Out.Map = Str(Room, TEXT("map"));
		Out.TeamSize = int32(Num(Room, TEXT("team_size")));
		Out.MaxPlayers = int32(Num(Room, TEXT("max_players")));
		return Out;
	}
}

void UCampusBackend::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	HeartbeatHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCampusBackend::HeartbeatTick), HeartbeatSeconds);
}

void UCampusBackend::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatHandle);
	Super::Deinitialize();
}

UCampusSave* UCampusBackend::Save() const
{
	UCampusGameInstance* Instance = Cast<UCampusGameInstance>(GetGameInstance());
	return Instance ? Instance->GetProfile() : nullptr;
}

FString UCampusBackend::ApiBase() const
{
	return GetDefault<UCampusNetSettings>()->ApiBaseUrl();
}

bool UCampusBackend::HasSavedSession() const
{
	const UCampusSave* Profile = Save();
	return Profile && !Profile->RefreshToken.IsEmpty();
}

FString UCampusBackend::ErrorText(int32 Status, TSharedPtr<FJsonObject> Json)
{
	if (Status == 0)
	{
		return TEXT("Could not reach the server. Same Wi-Fi as the PC? Is the API running?");
	}
	const FString Message = Str(Obj(Json, TEXT("error")), TEXT("message"));
	return Message.IsEmpty() ? FString::Printf(TEXT("Server error (%d)."), Status) : Message;
}

// ---------------------------------------------------------------------------------------------
// HTTP

void UCampusBackend::Request(const FString& Verb, const FString& Path, TSharedPtr<FJsonObject> Body, bool bAuth, bool bIdempotent, FResponse OnResponse, bool bRetried)
{
	FString BodyText;
	if (Body.IsValid())
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyText);
		FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
	}
	// one key per user action: a retry of this call (after a token refresh) reuses it
	Send(Verb, Path, BodyText, bAuth, bIdempotent ? NewKey() : FString(), MoveTemp(OnResponse), bRetried);
}

void UCampusBackend::Send(const FString& Verb, const FString& Path, const FString& BodyText, bool bAuth, const FString& IdempotencyKey, FResponse OnResponse, bool bRetried)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetURL(ApiBase() + Path);
	Http->SetVerb(Verb);
	Http->SetTimeout(10.f);
	Http->SetHeader(TEXT("Accept"), TEXT("application/json"));
	// Mutations always carry a JSON body ("{}" when there is nothing to send): without one, Android's HTTP stack
	// labels the empty POST as a form and the API refuses it ("Unsupported Media Type").
	if (Verb != TEXT("GET"))
	{
		Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Http->SetContentAsString(BodyText.IsEmpty() ? FString(TEXT("{}")) : BodyText);
	}
	if (bAuth && !AccessToken.IsEmpty())
	{
		Http->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	}
	if (!IdempotencyKey.IsEmpty())
	{
		Http->SetHeader(TEXT("Idempotency-Key"), IdempotencyKey);
	}
	TWeakObjectPtr<UCampusBackend> WeakThis(this);
	Http->OnProcessRequestComplete().BindLambda(
		[WeakThis, Verb, Path, BodyText, bAuth, IdempotencyKey, OnResponse, bRetried](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			UCampusBackend* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			TSharedPtr<FJsonObject> Json;
			if (Response.IsValid())
			{
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
			}
			// an expired access token: refresh once, then repeat the same call with the same idempotency key
			const FString Code = Str(Obj(Json, TEXT("error")), TEXT("code"));
			if (Status == 401 && bAuth && !bRetried && (Code == TEXT("TOKEN_EXPIRED") || Code == TEXT("TOKEN_MISSING")) && Self->HasSavedSession())
			{
				Self->RefreshAccess([WeakThis, Verb, Path, BodyText, bAuth, IdempotencyKey, OnResponse](bool bOk)
				{
					if (UCampusBackend* Again = WeakThis.Get())
					{
						if (bOk)
						{
							Again->Send(Verb, Path, BodyText, bAuth, IdempotencyKey, OnResponse, true);
						}
						else
						{
							OnResponse(401, nullptr);
						}
					}
				});
				return;
			}
			if (Status == 0)
			{
				Self->LastError = ErrorText(0, nullptr);
			}
			OnResponse(Status, Json);
		});
	Http->ProcessRequest();
}

void UCampusBackend::RefreshAccess(TFunction<void(bool bOk)> Then)
{
	PendingRefresh.Add(MoveTemp(Then));
	if (bRefreshing)
	{
		return; // one refresh at a time: refresh tokens are single use
	}
	bRefreshing = true;
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("refresh_token"), Save() ? Save()->RefreshToken : FString());
	Request(TEXT("POST"), TEXT("/auth/refresh"), Body, false, false, [this](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		const bool bOk = Status == 200;
		if (bOk)
		{
			AcceptSession(Json);
		}
		else if (Status == 401 || Status == 403)
		{
			// the session is over (expired, revoked, banned): sign out locally
			Logout();
		}
		bRefreshing = false;
		TArray<TFunction<void(bool)>> Waiting = MoveTemp(PendingRefresh);
		for (TFunction<void(bool)>& Callback : Waiting)
		{
			Callback(bOk);
		}
	});
}

// ---------------------------------------------------------------------------------------------
// Session

void UCampusBackend::AcceptSession(TSharedPtr<FJsonObject> Json)
{
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
	bGuest = Bool(Player, TEXT("guest"));
	if (UCampusSave* Profile = Save())
	{
		Profile->PlayerName = PlayerName;
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
				Offer.PriceInr = float(Num(Item, TEXT("price_inr")));
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

void UCampusBackend::BuyWithMoney(const FString& ItemId, FDone Done)
{
	if (IsWaitingForPayment())
	{
		Done(false, TEXT("A payment is already in progress."));
		return;
	}
	// Android: Razorpay's in-app payment sheet. Elsewhere: the hosted payment page in the browser.
	const bool bNative = CampusRazorpay::IsAvailable();
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("item_id"), ItemId);
	Body->SetStringField(TEXT("method"), bNative ? TEXT("checkout") : TEXT("link"));
	Request(TEXT("POST"), TEXT("/payments/orders"), Body, true, true, [this, Done, bNative](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		PendingOrderId = Str(Json, TEXT("order_id"));
		PendingOrderPolls = 0;
		PendingOrderDone = Done;
		if (bNative)
		{
			const FString OrderId = PendingOrderId;
			CampusRazorpay::Open(Str(Json, TEXT("key_id")), Str(Json, TEXT("razorpay_order_id")), int32(Num(Json, TEXT("amount_paise"))),
				TEXT("Campus Combat"), Str(Json, TEXT("description")), Email.Contains(TEXT("*")) ? FString() : Email,
				[this, OrderId](int32 Code, const FString& RzpOrderId, const FString& PaymentId, const FString& Signature, const FString& Message)
				{
					OnNativePaymentResult(OrderId, Code, RzpOrderId, PaymentId, Signature, Message);
				});
			return;
		}
		const FString PayUrl = Str(Json, TEXT("pay_url"));
		if (PayUrl.IsEmpty() || PendingOrderId.IsEmpty())
		{
			FinishPendingOrder(false, TEXT("Payment page unavailable."));
			return;
		}
		FPlatformProcess::LaunchURL(*PayUrl, nullptr, nullptr);
		if (!PaymentPollHandle.IsValid())
		{
			PaymentPollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCampusBackend::PaymentPollTick), 4.f);
		}
	});
}

void UCampusBackend::VerifyGooglePurchase(const FString& ProductId, const FString& PurchaseToken, FJsonDone Done)
{
	if (PurchaseToken.IsEmpty())
	{
		Done(false, TEXT("Missing purchase token."), nullptr);
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("productId"), ProductId);
	Body->SetStringField(TEXT("purchaseToken"), PurchaseToken);
	// Not idempotency-keyed: the purchase token is the natural key, and a retry days later from a restore must hit
	// the same server-side record rather than a fresh one.
	Request(TEXT("POST"), TEXT("/store/google/verify"), Body, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status < 200 || Status > 299)
		{
			Done(false, ErrorText(Status, Json), Json);
			return;
		}
		// Read the wallet wherever it turns up: nested under "wallet", under "player", or flat on the response.
		if (const TSharedPtr<FJsonObject> Wallet = Obj(Json, TEXT("wallet")))
		{
			ReadWallet(Wallet);
		}
		else if (const TSharedPtr<FJsonObject> Player = Obj(Json, TEXT("player")))
		{
			const TSharedPtr<FJsonObject> Nested = Obj(Player, TEXT("wallet"));
			ReadWallet(Nested.IsValid() ? Nested : Player);
		}
		else if (Json.IsValid() && (Json->HasField(TEXT("diamonds")) || Json->HasField(TEXT("room_cards"))))
		{
			ReadWallet(Json);
		}
		Done(true, FString(), Json);
	});
}

void UCampusBackend::OnNativePaymentResult(const FString& OrderId, int32 Code, const FString& RzpOrderId, const FString& PaymentId,
	const FString& Signature, const FString& Message)
{
	if (OrderId != PendingOrderId)
	{
		return; // a stale callback for an order that is already settled
	}
	if (Code == CampusRazorpay::Success)
	{
		// the server checks the signature with our secret before it credits anything
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("razorpay_order_id"), RzpOrderId);
		Body->SetStringField(TEXT("razorpay_payment_id"), PaymentId);
		Body->SetStringField(TEXT("razorpay_signature"), Signature);
		Request(TEXT("POST"), TEXT("/payments/verify"), Body, true, false, [this, OrderId](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status == 200 && Str(Json, TEXT("status")) == TEXT("paid"))
			{
				ReadWallet(Obj(Json, TEXT("wallet")));
				LoadStore([this](bool, const FString&) { FinishPendingOrder(true, FString()); });
				return;
			}
			// couldn't confirm right now (network): the server settles it with Razorpay; keep asking for a while
			StartPaymentPolling();
		});
		return;
	}
	// Not a success: cancelled, failed, or the sheet couldn't open. Never trust the phone's view of the money:
	// the server asks Razorpay whether anything was captured, then closes the order.
	FString Reason = Message;
	TSharedPtr<FJsonObject> ErrorJson;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Message), ErrorJson) && ErrorJson.IsValid())
	{
		const TSharedPtr<FJsonObject> Error = Obj(ErrorJson, TEXT("error"));
		const FString Description = Str(Error, TEXT("description"));
		const FString Why = Str(Error, TEXT("reason"));
		Reason = Why.Contains(TEXT("cancel")) || Description.Contains(TEXT("cancel")) ? FString(TEXT("cancelled")) : Description;
	}
	const bool bCancelled = Reason == TEXT("cancelled") || Message.Contains(TEXT("cancel"));
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("reason"), Reason.Left(200));
	Request(TEXT("POST"), FString::Printf(TEXT("/payments/orders/%s/cancel"), *OrderId), Body, true, false,
		[this, bCancelled, Reason](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status == 200 && Str(Json, TEXT("status")) == TEXT("paid"))
			{
				// Razorpay had captured it after all: credited
				LoadStore([this](bool, const FString&) { FinishPendingOrder(true, FString()); });
				return;
			}
			if (Status == 0)
			{
				FinishPendingOrder(false, TEXT("No connection. If money was taken, it is added automatically next time you open the Store."));
				return;
			}
			FinishPendingOrder(false, bCancelled ? FString(TEXT("Payment cancelled. No money was taken."))
				: FString::Printf(TEXT("Payment failed%s. No money was taken."), Reason.IsEmpty() ? TEXT("") : *(TEXT(": ") + Reason)));
		});
}

void UCampusBackend::CancelPendingPayment()
{
	if (PendingOrderId.IsEmpty())
	{
		return;
	}
	OnNativePaymentResult(PendingOrderId, 0, FString(), FString(), FString(), TEXT("cancelled"));
}

void UCampusBackend::StartPaymentPolling()
{
	if (!PaymentPollHandle.IsValid())
	{
		PaymentPollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCampusBackend::PaymentPollTick), 4.f);
	}
}

bool UCampusBackend::PaymentPollTick(float)
{
	if (PendingOrderId.IsEmpty())
	{
		PaymentPollHandle.Reset();
		return false;
	}
	// ~10 minutes of checks (the payment page itself expires after 30)
	if (++PendingOrderPolls > 150)
	{
		FinishPendingOrder(false, TEXT("Payment not completed. If money was taken, it is credited automatically once Razorpay confirms."));
		return false;
	}
	Request(TEXT("GET"), FString::Printf(TEXT("/payments/orders/%s"), *PendingOrderId), nullptr, true, false, [this](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200 || PendingOrderId.IsEmpty())
		{
			return; // keep checking (Wi-Fi blips while the browser is in front)
		}
		const FString OrderStatus = Str(Json, TEXT("status"));
		if (OrderStatus == TEXT("paid"))
		{
			ReadWallet(Obj(Json, TEXT("wallet")));
			LoadStore([this](bool, const FString&) { FinishPendingOrder(true, FString()); });
		}
		else if (OrderStatus == TEXT("failed"))
		{
			FinishPendingOrder(false, TEXT("Payment failed or expired. No money was taken."));
		}
	});
	return true;
}

void UCampusBackend::FinishPendingOrder(bool bOk, const FString& Error)
{
	PendingOrderId.Reset();
	FTSTicker::GetCoreTicker().RemoveTicker(PaymentPollHandle);
	PaymentPollHandle.Reset();
	FDone Done = MoveTemp(PendingOrderDone);
	PendingOrderDone = nullptr;
	if (Done)
	{
		Done(bOk, Error);
	}
}

// ---------------------------------------------------------------------------------------------
// Rooms

void UCampusBackend::CreateRoom(const FString& Map, int32 TeamSize, int32 Rounds, int32 Minutes, FRoomDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("rounds"), Rounds);
	Body->SetNumberField(TEXT("round_minutes"), Minutes);
	Body->SetStringField(TEXT("map"), Map == TEXT("RealMap") ? TEXT("real") : TEXT("classic"));
	Body->SetNumberField(TEXT("team_size"), TeamSize == 1 || TeamSize == 2 ? TeamSize : 4);
	Request(TEXT("POST"), TEXT("/rooms"), Body, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
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
	Request(TEXT("POST"), FString::Printf(TEXT("/rooms/%s/result"), *HostedRoom.Code), Body, true, false, [](int32 Status, TSharedPtr<FJsonObject>)
	{
		UE_LOG(LogTemp, Log, TEXT("Campus: match result reported (%d)"), Status);
	});
	HostedRoom = FCampusRoom(); // the server closed it; no more heartbeats
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
}

bool UCampusBackend::HeartbeatTick(float)
{
	if (HostedRoom.IsValid() && IsSignedIn())
	{
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("status"), bHostedPlaying ? TEXT("playing") : TEXT("open"));
		Body->SetNumberField(TEXT("player_count"), HostedPlayers);
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
