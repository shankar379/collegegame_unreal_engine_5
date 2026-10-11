// The game's client for the Campus Combat API (campuscombat-backend). One per game instance, so it survives level
// travel. Holds the session (access token in memory, refresh token in the save), caches the profile, wallet and
// store, and keeps a hosted room alive with heartbeats. No gameplay code talks HTTP directly; it calls this.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "CampusBackend.generated.h"

class FJsonObject;
class UCampusSave;

/** A gun in the store (text only until there are icons). */
struct FCampusGun
{
	FString Id;
	FString Name;
	FString Description;
	FString Badge;
	TArray<FString> Perks;
	int32 PriceDiamonds = 0;   // 0 = not sold for Diamonds
	float PriceInr = 0.f;      // 0 = not sold for money
	bool bOwned = false;
	bool bStarter = false;
	int32 Level = 0;
	int32 MaxLevel = 1;
	int32 NextUpgradeDiamonds = 0;   // 0 = maxed / not owned
	int32 SellDiamonds = 0;
	float Damage = 0.f;
	float FireRate = 0.f;
	float Accuracy = 0.f;
	int32 Magazine = 0;
};

/** A pack or offer bought with money. */
struct FCampusOffer
{
	FString Id;
	FString Name;
	FString Description;
	FString Badge;
	TArray<FString> Perks;
	float PriceInr = 0.f;
};

struct FCampusRoom
{
	FString Code;
	FString Address;
	int32 Port = 7777;
	FString Map;
	int32 TeamSize = 4;
	int32 MaxPlayers = 8;
	/** Join only: proof for the match server that the API let this player in (short-lived). */
	FString Ticket;
	bool IsValid() const { return !Code.IsEmpty(); }
};

/** One player's final line, reported by the match server (never by the player's own phone). */
struct FCampusMatchResult
{
	FString PlayerId;
	int32 Team = 0;
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 DamageDealt = 0;
	int32 DamageTaken = 0;
	bool bWon = false;
};

UCLASS()
class UCampusBackend : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FDone = TFunction<void(bool bOk, const FString& Error)>;
	using FRoomDone = TFunction<void(bool bOk, const FString& Error, const FCampusRoom& Room)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- session
	bool IsSignedIn() const { return !PlayerId.IsEmpty(); }
	bool IsWaitingForPayment() const { return !PendingOrderId.IsEmpty(); }
	/** The player gave up on an open payment: the server checks Razorpay (paid after all -> credited), else closes it. */
	void CancelPendingPayment();
	bool HasSavedSession() const;
	/** Signs back in with the saved refresh token (app restart). */
	void ResumeSession(FDone Done);
	/** "Play as Guest": this device's own id + secret, created on first use. */
	void SignInGuest(FDone Done);
	/** New email account; a signed-in guest is upgraded in place (keeps Room Cards, Diamonds, guns). */
	void Register(const FString& Email, const FString& Password, const FString& Name, FDone Done);
	void Login(const FString& Email, const FString& Password, FDone Done);
	void Logout();

	// ---- profile + wallet
	void LoadMe(FDone Done);
	void UpdateProfile(const FString& Name, const FString& RollNo, const FString& Branch, const FString& College, FDone Done);
	void ClaimDailyCard(FDone Done);

	// ---- store
	void LoadStore(FDone Done);
	void BuyGun(const FString& Id, FDone Done);
	void UpgradeGun(const FString& Id, FDone Done);
	void SellGun(const FString& Id, FDone Done);
	void EquipGun(const FString& Id, FDone Done);
	/** Real money (Razorpay): opens the payment page, then reports once the server confirms the payment. */
	void BuyWithMoney(const FString& ItemId, FDone Done);

	// ---- rooms
	/** Spends 1 Room Card; this phone becomes the host (listen server) of the returned room. */
	void CreateRoom(const FString& Map, int32 TeamSize, int32 Rounds, int32 Minutes, FRoomDone Done);
	void JoinRoom(const FString& Code, FRoomDone Done);
	/** Called by the host's match: keeps the room listed and marks it playing once the match starts. */
	void SetHostedRoomState(bool bPlaying, int32 PlayerCount);
	void CloseHostedRoom();
	/** Host: checks a connecting phone's ticket. bRejected = the API said no (a network failure is not a no). */
	void AdmitPlayer(const FString& Ticket, TFunction<void(bool bRejected, const FString& PlayerId)> Done);
	/** Host: final stats of every admitted player; ends the room on the server. */
	void ReportMatchResult(const TArray<FCampusMatchResult>& Players);
	const FCampusRoom& GetHostedRoom() const { return HostedRoom; }

	// ---- cached state (valid after the calls above)
	FString PlayerId;
	FString PlayerName;
	FString Email;
	FString RollNo;
	FString Branch;
	FString College;
	bool bGuest = true;
	int32 RoomCards = 0;
	int32 Diamonds = 0;
	bool bDailyCardAvailable = false;
	TArray<FCampusGun> Guns;
	TArray<FCampusOffer> Offers;
	TArray<FCampusOffer> Packs;
	FString EquippedGun;
	bool bPaymentsEnabled = false;
	/** Last error worth showing (e.g. "Could not reach the server"). */
	FString LastError;

	const FCampusGun* FindGun(const FString& Id) const;

private:
	using FResponse = TFunction<void(int32 Status, TSharedPtr<FJsonObject> Json)>;

	UCampusSave* Save() const;
	void Request(const FString& Verb, const FString& Path, TSharedPtr<FJsonObject> Body, bool bAuth, bool bIdempotent, FResponse OnResponse, bool bRetried = false);
	void Send(const FString& Verb, const FString& Path, const FString& BodyText, bool bAuth, const FString& IdempotencyKey, FResponse OnResponse, bool bRetried);
	void RefreshAccess(TFunction<void(bool bOk)> Then);
	void AcceptSession(TSharedPtr<FJsonObject> Json);
	void ReadPlayer(TSharedPtr<FJsonObject> Player);
	void ReadWallet(TSharedPtr<FJsonObject> Wallet);
	void ReadStore(TSharedPtr<FJsonObject> Json);
	static FString ErrorText(int32 Status, TSharedPtr<FJsonObject> Json);
	FString ApiBase() const;
	bool HeartbeatTick(float DeltaTime);
	bool PaymentPollTick(float DeltaTime);
	void FinishPendingOrder(bool bOk, const FString& Error);
	/** The in-app Razorpay sheet's outcome: success -> server verifies the signature; otherwise the server asks
	 *  Razorpay what really happened, then closes the order (cancelled / failed / paid after all). */
	void OnNativePaymentResult(const FString& OrderId, int32 Code, const FString& RzpOrderId, const FString& PaymentId,
		const FString& Signature, const FString& Message);
	void StartPaymentPolling();

	/** A real-money order waiting for Razorpay (the payment page is open in the browser). */
	FString PendingOrderId;
	int32 PendingOrderPolls = 0;
	FDone PendingOrderDone;
	FTSTicker::FDelegateHandle PaymentPollHandle;

	FString AccessToken;
	FCampusRoom HostedRoom;
	bool bHostedPlaying = false;
	int32 HostedPlayers = 1;
	FTSTicker::FDelegateHandle HeartbeatHandle;
	TArray<TFunction<void(bool)>> PendingRefresh;
	bool bRefreshing = false;
};
