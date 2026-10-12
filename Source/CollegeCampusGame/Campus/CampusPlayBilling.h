// Google Play Billing for the real-money store. Play takes the money; our backend is the only thing that decides
// what the player actually gets: every paid token is POSTed to /store/google/verify, which checks it with Google,
// credits the wallet and consumes it server-side. The client never consumes, so an interrupted purchase is still
// sitting in Play's queryPurchases next time we look - that is the whole recovery story (see RestoreUnfinished).
//
// Lives beside UCampusBackend as a game instance subsystem so it survives level travel. Android only; everywhere
// else IsAvailable() is false and every call reports Unavailable, so the store UI needs no platform ifdefs.
//
// Config this needs (Config/DefaultEngine.ini):
//   [/Script/AndroidRuntimeSettings.AndroidRuntimeSettings] bEnableGooglePlaySupport=True, GooglePlayLicenseKey=...
//   [OnlineSubsystemGooglePlay.Store] bSupportsInAppPurchasing=True, bDisableLocalAcknowledgeAndConsume=True
// DefaultPlatformService stays Null (team voice depends on it); Google Play is reached explicitly by name.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "CampusPlayBilling.generated.h"

class UCampusBackend;

/** A Play product as Google described it, in the player's own currency. Never price these strings yourself. */
struct FCampusPlayProduct
{
	/** Play Console product id, e.g. "diamonds_500". */
	FString ProductId;
	/** Play Console title. Note Google appends "(App Name)" to it; the UI usually wants its own label instead. */
	FString Title;
	FString Description;
	/** Ready to draw, already localized and currency-correct: "₹79.00", "$0.99". */
	FString PriceText;
	/** ISO code Play charged in, e.g. "INR". */
	FString CurrencyCode;
	/** PriceText as a number, in minor units of CurrencyCode (79.00 INR -> 7900 paise). For sorting only -
	 *  the engine rounds Google's micro-price into minor units, so never show this instead of PriceText. */
	int64 PriceMinorUnits = 0;
};

/** What came of a Purchase() call. Only Credited means the player has their goods. */
enum class ECampusPurchaseResult : uint8
{
	/** Play charged the player and our backend credited the wallet. Re-read the wallet from UCampusBackend. */
	Credited,
	/** Play accepted the order but the money has not settled (UPI collect request, cash at a counter). Nothing is
	 *  credited yet; it lands on a later RestoreUnfinished(), typically when the app next comes to the foreground. */
	Pending,
	/** The player backed out of Play's sheet. */
	Cancelled,
	/** Billing is not usable here: not Android, Play Store missing, or the build was packaged without IAP. */
	Unavailable,
	/** Play charged nobody, or Play charged and our backend could not be reached. See the error text. */
	Failed,
};

const TCHAR* LexToString(ECampusPurchaseResult Result);

UCLASS()
class UCampusPlayBilling : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FProductsDone = TFunction<void(bool bOk, const FString& Error, const TArray<FCampusPlayProduct>& Products)>;
	using FPurchaseDone = TFunction<void(ECampusPurchaseResult Result, const FString& Error)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** True where Play Billing can actually be used. False -> hide the money store, or fall back to Razorpay. */
	bool IsAvailable() const;

	/** Asks Play for titles and localized prices. Products Play does not know about are simply absent from the
	 *  result, so always draw from what comes back, never from the ids you asked for. */
	void QueryProducts(const TArray<FString>& ProductIds, FProductsDone Done);

	/** Prices from the last successful QueryProducts, for redrawing without another round trip. */
	const TArray<FCampusPlayProduct>& GetProducts() const { return Products; }
	const FCampusPlayProduct* FindProduct(const FString& ProductId) const;

	/** Opens Play's purchase sheet, then verifies the token with our backend. Exactly one Done call follows.
	 *  The player must be signed in to the backend first: their player id goes to Play as the obfuscated account
	 *  id, which is how Google's fraud tooling ties a charge back to an account. */
	void Purchase(const FString& ProductId, FPurchaseDone Done);

	/** True between Purchase() and its Done. Keep the buy buttons disabled while it is set. */
	bool IsPurchasing() const { return bPurchasing; }

	/** Re-sends every paid-but-unconsumed Play purchase to the backend. Runs by itself after sign-in and on every
	 *  return to the foreground; call it directly when opening the store or after a failed verify. Harmless to
	 *  spam - tokens already accepted in this session are skipped, and the endpoint is idempotent. */
	void RestoreUnfinished();

	/** Purchases Play is holding as pending (UPI not yet collected). Worth showing as "payment in progress". */
	int32 GetPendingCount() const { return PendingTokens.Num(); }

private:
	UCampusBackend* Backend() const;

	/** Drives the post-sign-in restore until it has run once. */
	bool RestoreTick(float DeltaTime);
	void OnEnteredForeground();

	/** Walks the receipts Play just handed us and sends the paid ones onward. */
	void DrainReceipts();
	void SendToBackend(const FString& ProductId, const FString& PurchaseToken, const FString& ValidationInfo, FPurchaseDone Done);
	/** Hands the token back to the engine's purchase interface. A no-op while bDisableLocalAcknowledgeAndConsume
	 *  is True (our shipping config, because the backend consumes); it consumes if that key is ever flipped. */
	void Finalize(const FString& PurchaseToken, const FString& ValidationInfo);

	TArray<FCampusPlayProduct> Products;

	/** Tokens this session already handed to the backend: stops the restore from re-posting in a loop while the
	 *  server has accepted them but not yet consumed them (Play keeps listing a purchase until it is consumed). */
	TSet<FString> SentTokens;
	/** Tokens Play reports as pending, so the UI can say so. Not sent anywhere: there is nothing to credit. */
	TSet<FString> PendingTokens;

	bool bPurchasing = false;
	bool bRestoring = false;
	bool bRestoredOnce = false;
	int32 RestoreAttempts = 0;

	FTSTicker::FDelegateHandle RestoreHandle;
	FDelegateHandle ForegroundHandle;
};
