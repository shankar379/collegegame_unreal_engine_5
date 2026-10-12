#include "Campus/CampusPlayBilling.h"

#include "Campus/CampusBackend.h"
#include "Engine/GameInstance.h"
#include "Misc/CoreDelegates.h"

#if PLATFORM_ANDROID
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineError.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePurchaseInterface.h"
#include "Interfaces/OnlineStoreInterfaceV2.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCampusBilling, Log, All);

namespace
{
	/** How often the startup restore retries while the player is not signed in to our backend yet. */
	constexpr float RestoreRetrySeconds = 5.f;
	/** Give up arming the automatic restore after this many tries (~2 minutes). Foreground still re-arms it. */
	constexpr int32 MaxRestoreAttempts = 24;
}

const TCHAR* LexToString(ECampusPurchaseResult Result)
{
	switch (Result)
	{
	case ECampusPurchaseResult::Credited:    return TEXT("Credited");
	case ECampusPurchaseResult::Pending:     return TEXT("Pending");
	case ECampusPurchaseResult::Cancelled:   return TEXT("Cancelled");
	case ECampusPurchaseResult::Unavailable: return TEXT("Unavailable");
	default:                                 return TEXT("Failed");
	}
}

#if PLATFORM_ANDROID
namespace
{
	/** The Google Play subsystem, by name. DefaultPlatformService stays Null because team voice runs on it, so
	 *  this is never the subsystem IOnlineSubsystem::Get() hands back - it has to be asked for explicitly. */
	IOnlineSubsystem* PlaySubsystem()
	{
		return IOnlineSubsystem::Get(GOOGLEPLAY_SUBSYSTEM);
	}

	/** Google Play Billing needs no Play Games sign-in; when nobody is signed in the identity interface returns a
	 *  valid empty Google Play id, which is what the purchase interface expects (it casts, so a foreign id from
	 *  another subsystem would be fatal - never pass one in). */
	FUniqueNetIdPtr PlayUserId()
	{
		IOnlineSubsystem* const Play = PlaySubsystem();
		const IOnlineIdentityPtr Identity = Play ? Play->GetIdentityInterface() : nullptr;
		return Identity.IsValid() ? Identity->GetUniquePlayerId(0) : nullptr;
	}
}
#endif

void UCampusPlayBilling::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!IsAvailable())
	{
		UE_LOG(LogCampusBilling, Log, TEXT("Google Play Billing unavailable on this platform/build."));
		return;
	}

	// Anything interrupted - app killed mid-purchase, verify call lost to a dead network, a UPI collect request the
	// player approved hours later - is still waiting in Play's purchase list. Drain it as soon as we have a session.
	RestoreHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UCampusPlayBilling::RestoreTick), RestoreRetrySeconds);

	// A pending UPI purchase almost always settles while the game is in the background. This is the hop that
	// catches it; without it the player pays and sees nothing until the next cold start.
	ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(
		this, &UCampusPlayBilling::OnEnteredForeground);
}

void UCampusPlayBilling::Deinitialize()
{
	if (RestoreHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RestoreHandle);
		RestoreHandle.Reset();
	}
	if (ForegroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
		ForegroundHandle.Reset();
	}
	Super::Deinitialize();
}

UCampusBackend* UCampusPlayBilling::Backend() const
{
	UGameInstance* const Instance = GetGameInstance();
	return Instance ? Instance->GetSubsystem<UCampusBackend>() : nullptr;
}

bool UCampusPlayBilling::IsAvailable() const
{
#if PLATFORM_ANDROID
	// Null unless bEnableGooglePlaySupport is set; the interfaces exist only if bSupportsInAppPurchasing is too.
	IOnlineSubsystem* const Play = PlaySubsystem();
	return Play != nullptr && Play->GetStoreV2Interface().IsValid() && Play->GetPurchaseInterface().IsValid();
#else
	return false;
#endif
}

const FCampusPlayProduct* UCampusPlayBilling::FindProduct(const FString& ProductId) const
{
	return Products.FindByPredicate([&ProductId](const FCampusPlayProduct& Product) { return Product.ProductId == ProductId; });
}

// ---------------------------------------------------------------------------------------------
// Prices

void UCampusPlayBilling::QueryProducts(const TArray<FString>& ProductIds, FProductsDone Done)
{
#if PLATFORM_ANDROID
	IOnlineSubsystem* const Play = PlaySubsystem();
	const IOnlineStoreV2Ptr Store = Play ? Play->GetStoreV2Interface() : nullptr;
	const FUniqueNetIdPtr UserId = PlayUserId();
	if (!Store.IsValid() || !UserId.IsValid())
	{
		Done(false, TEXT("In-app purchases are not available on this device."), Products);
		return;
	}
	if (ProductIds.IsEmpty())
	{
		Done(false, TEXT("No products to look up."), Products);
		return;
	}

	TWeakObjectPtr<UCampusPlayBilling> WeakThis(this);
	Store->QueryOffersById(*UserId, ProductIds,
		FOnQueryOnlineStoreOffersComplete::CreateLambda(
			[WeakThis, ProductIds, Done](bool bWasSuccessful, const TArray<FUniqueOfferId>& /*OfferIds*/, const FString& Error)
			{
				UCampusPlayBilling* const Self = WeakThis.Get();
				if (!Self)
				{
					return;
				}
				IOnlineSubsystem* const Subsystem = PlaySubsystem();
				const IOnlineStoreV2Ptr Offers = Subsystem ? Subsystem->GetStoreV2Interface() : nullptr;
				if (!bWasSuccessful || !Offers.IsValid())
				{
					UE_LOG(LogCampusBilling, Warning, TEXT("QueryProducts failed: %s"), *Error);
					Done(false, Error.IsEmpty() ? TEXT("Could not reach the Play Store.") : Error, Self->Products);
					return;
				}

				Self->Products.Reset();
				for (const FString& Id : ProductIds)
				{
					// Absent means the id is not live in the Play Console for this package and track. Skip it
					// rather than inventing a price: a wrong price here is a refund later.
					const TSharedPtr<FOnlineStoreOffer> Offer = Offers->GetOffer(Id);
					if (!Offer.IsValid())
					{
						UE_LOG(LogCampusBilling, Warning, TEXT("Play does not know product '%s'."), *Id);
						continue;
					}
					FCampusPlayProduct& Product = Self->Products.AddDefaulted_GetRef();
					Product.ProductId = Offer->OfferId;
					Product.Title = Offer->Title.ToString();
					Product.Description = Offer->Description.IsEmpty() ? Offer->LongDescription.ToString() : Offer->Description.ToString();
					Product.PriceText = Offer->GetDisplayPrice().ToString();
					Product.CurrencyCode = Offer->CurrencyCode;
					Product.PriceMinorUnits = Offer->NumericPrice;
				}
				UE_LOG(LogCampusBilling, Log, TEXT("QueryProducts: %d of %d products priced."), Self->Products.Num(), ProductIds.Num());
				Done(true, FString(), Self->Products);
			}));
#else
	Done(false, TEXT("In-app purchases are only available in the Android build."), Products);
#endif
}

// ---------------------------------------------------------------------------------------------
// Buying

void UCampusPlayBilling::Purchase(const FString& ProductId, FPurchaseDone Done)
{
#if PLATFORM_ANDROID
	if (bPurchasing)
	{
		Done(ECampusPurchaseResult::Failed, TEXT("A purchase is already in progress."));
		return;
	}
	IOnlineSubsystem* const Play = PlaySubsystem();
	const IOnlinePurchasePtr Purchases = Play ? Play->GetPurchaseInterface() : nullptr;
	const FUniqueNetIdPtr UserId = PlayUserId();
	if (!Purchases.IsValid() || !UserId.IsValid())
	{
		Done(ECampusPurchaseResult::Unavailable, TEXT("In-app purchases are not available on this device."));
		return;
	}
	UCampusBackend* const Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		// Without a player id there is nothing to credit and nothing to tie the charge to. Refuse before Play
		// takes money we cannot attribute.
		Done(ECampusPurchaseResult::Failed, TEXT("Sign in before buying."));
		return;
	}

	FPurchaseCheckoutRequest Request;
	Request.AddPurchaseOffer(TEXT(""), ProductId, 1, /*bInIsConsumable*/ true);
	// Goes to Play as obfuscatedAccountId. The engine's Java layer hashes it (SHA-256, lower-case hex) before
	// handing it to the Billing Library, so the backend sees the hash, not the raw player id.
	Request.AccountId = Api->PlayerId;

	bPurchasing = true;
	TWeakObjectPtr<UCampusPlayBilling> WeakThis(this);
	Purchases->Checkout(*UserId, Request,
		FOnPurchaseCheckoutComplete::CreateLambda(
			[WeakThis, ProductId, Done](const FOnlineError& Error, const TSharedRef<FPurchaseReceipt>& Receipt)
			{
				UCampusPlayBilling* const Self = WeakThis.Get();
				if (!Self)
				{
					return;
				}
				Self->bPurchasing = false;

				const FString Code = Error.GetErrorCode();
				UE_LOG(LogCampusBilling, Log, TEXT("Checkout '%s' state=%d code=%s"), *ProductId, int32(Receipt->TransactionState), *Code);

				switch (Receipt->TransactionState)
				{
				case EPurchaseTransactionState::Purchased:
					break;

				case EPurchaseTransactionState::Deferred:
					// UPI collect request or cash: Play has the order, the money has not moved. Nothing is owed to
					// the player yet, and the token carries no validation info, so there is nothing to verify.
					Self->PendingTokens.Add(Receipt->TransactionId);
					Done(ECampusPurchaseResult::Pending, TEXT("Payment started. Your purchase will arrive once it clears."));
					return;

				case EPurchaseTransactionState::Canceled:
					Done(ECampusPurchaseResult::Cancelled, FString());
					return;

				default:
					if (Code.Contains(TEXT("ItemAlreadyOwned")))
					{
						// We never consume on the device - the backend does, after it verifies. So an earlier
						// purchase of this same consumable is still owned until the server finishes with it.
						// Drain it now; that is usually the whole fix.
						UE_LOG(LogCampusBilling, Warning, TEXT("'%s' still owned: a previous purchase is not consumed yet. Draining."), *ProductId);
						Self->RestoreUnfinished();
						Done(ECampusPurchaseResult::Failed, TEXT("Your last purchase of this item is still being processed. Try again in a moment."));
						return;
					}
					Done(ECampusPurchaseResult::Failed, Error.ErrorMessage.IsEmpty()
						? TEXT("The Play Store could not complete the purchase.")
						: Error.ErrorMessage.ToString());
					return;
				}

				// Paid. The token is the only thing that matters from here; the backend decides what it is worth.
				FString Token = Receipt->TransactionId;
				FString ValidationInfo;
				FString PaidProductId = ProductId;
				if (Receipt->ReceiptOffers.Num() > 0)
				{
					const FPurchaseReceipt::FReceiptOfferEntry& Entry = Receipt->ReceiptOffers[0];
					if (!Entry.OfferId.IsEmpty())
					{
						PaidProductId = Entry.OfferId;
					}
					if (Entry.LineItems.Num() > 0)
					{
						ValidationInfo = Entry.LineItems[0].ValidationInfo;
						if (Token.IsEmpty())
						{
							Token = Entry.LineItems[0].UniqueId;
						}
					}
				}
				if (Token.IsEmpty())
				{
					// Should not happen: the engine rejects a receipt with no purchase token. Say so loudly, the
					// player has been charged and only the Play Console can settle it now.
					UE_LOG(LogCampusBilling, Error, TEXT("Paid receipt for '%s' carried no purchase token."), *ProductId);
					Done(ECampusPurchaseResult::Failed, TEXT("Payment went through but the receipt was unreadable. Contact support."));
					return;
				}

				Self->SendToBackend(PaidProductId, Token, ValidationInfo, Done);
			}));
#else
	Done(ECampusPurchaseResult::Unavailable, TEXT("In-app purchases are only available in the Android build."));
#endif
}

void UCampusPlayBilling::SendToBackend(const FString& ProductId, const FString& PurchaseToken, const FString& ValidationInfo, FPurchaseDone Done)
{
	UCampusBackend* const Api = Backend();
	if (!Api)
	{
		Done(ECampusPurchaseResult::Failed, TEXT("Not connected to the server."));
		return;
	}

	TWeakObjectPtr<UCampusPlayBilling> WeakThis(this);
	Api->VerifyGooglePurchase(ProductId, PurchaseToken,
		[WeakThis, ProductId, PurchaseToken, ValidationInfo, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
		{
			UCampusPlayBilling* const Self = WeakThis.Get();
			if (!bOk)
			{
				// Play has the money and the purchase is still unconsumed, so Play will keep handing us this token.
				// RestoreUnfinished() picks it up on the next foreground or store visit. Nothing is lost.
				UE_LOG(LogCampusBilling, Warning, TEXT("Verify failed for '%s': %s. Will retry on restore."), *ProductId, *Error);
				Done(ECampusPurchaseResult::Failed, Error.IsEmpty()
					? TEXT("Payment received. We could not reach the server to credit it - it will arrive shortly.")
					: Error);
				return;
			}
			// 202 {"status":"pending"}: Google has not settled it yet (UPI collect). Keep the token pending and do NOT
			// finalize - a later restore re-sends it, and RTDN completes it server-side meanwhile.
			FString VerifyStatus;
			if (Json.IsValid() && Json->TryGetStringField(TEXT("status"), VerifyStatus) && VerifyStatus == TEXT("pending"))
			{
				if (Self)
				{
					Self->PendingTokens.Add(PurchaseToken);
				}
				Done(ECampusPurchaseResult::Pending, TEXT("Google is still processing this payment."));
				return;
			}
			if (Self)
			{
				Self->SentTokens.Add(PurchaseToken);
				Self->PendingTokens.Remove(PurchaseToken);
				Self->Finalize(PurchaseToken, ValidationInfo);
			}
			Done(ECampusPurchaseResult::Credited, FString());
		});
}

void UCampusPlayBilling::Finalize(const FString& PurchaseToken, const FString& ValidationInfo)
{
#if PLATFORM_ANDROID
	IOnlineSubsystem* const Play = PlaySubsystem();
	const IOnlinePurchasePtr Purchases = Play ? Play->GetPurchaseInterface() : nullptr;
	const FUniqueNetIdPtr UserId = PlayUserId();
	if (Purchases.IsValid() && UserId.IsValid())
	{
		// With [OnlineSubsystemGooglePlay.Store] bDisableLocalAcknowledgeAndConsume=True this does nothing at all,
		// which is what we want: the backend acknowledges and consumes after it has verified, so a crash between
		// "Play charged" and "wallet credited" can never lose the purchase. Called anyway so that flipping that
		// key back to False gives correct client-side behaviour without touching this file.
		Purchases->FinalizePurchase(*UserId, PurchaseToken, ValidationInfo);
	}
#endif
}

// ---------------------------------------------------------------------------------------------
// Recovery: assume we were killed

bool UCampusPlayBilling::RestoreTick(float /*DeltaTime*/)
{
	if (bRestoredOnce)
	{
		RestoreHandle.Reset();
		return false;
	}
	const UCampusBackend* const Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		if (++RestoreAttempts >= MaxRestoreAttempts)
		{
			// Stop burning a timer; coming back to the foreground re-arms the drain anyway.
			RestoreHandle.Reset();
			return false;
		}
		return true;
	}
	RestoreUnfinished();
	RestoreHandle.Reset();
	return false;
}

void UCampusPlayBilling::OnEnteredForeground()
{
	// The player may have just finished a UPI collect request in another app.
	RestoreUnfinished();
}

void UCampusPlayBilling::RestoreUnfinished()
{
#if PLATFORM_ANDROID
	if (bRestoring)
	{
		return;
	}
	IOnlineSubsystem* const Play = PlaySubsystem();
	const IOnlinePurchasePtr Purchases = Play ? Play->GetPurchaseInterface() : nullptr;
	const FUniqueNetIdPtr UserId = PlayUserId();
	const UCampusBackend* const Api = Backend();
	if (!Purchases.IsValid() || !UserId.IsValid() || !Api || !Api->IsSignedIn())
	{
		return;
	}

	bRestoring = true;
	TWeakObjectPtr<UCampusPlayBilling> WeakThis(this);
	Purchases->QueryReceipts(*UserId, /*bRestoreReceipts*/ true,
		FOnQueryReceiptsComplete::CreateLambda([WeakThis](const FOnlineError& Error)
		{
			UCampusPlayBilling* const Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bRestoring = false;
			if (!Error.WasSuccessful())
			{
				UE_LOG(LogCampusBilling, Warning, TEXT("QueryReceipts failed: %s"), *Error.GetErrorCode());
				return;
			}
			Self->bRestoredOnce = true;
			Self->DrainReceipts();
		}));
#endif
}

void UCampusPlayBilling::DrainReceipts()
{
#if PLATFORM_ANDROID
	IOnlineSubsystem* const Play = PlaySubsystem();
	const IOnlinePurchasePtr Purchases = Play ? Play->GetPurchaseInterface() : nullptr;
	const FUniqueNetIdPtr UserId = PlayUserId();
	if (!Purchases.IsValid() || !UserId.IsValid())
	{
		return;
	}

	TArray<FPurchaseReceipt> Receipts;
	Purchases->GetReceipts(*UserId, Receipts);

	PendingTokens.Reset();
	int32 Resent = 0;
	for (const FPurchaseReceipt& Receipt : Receipts)
	{
		for (const FPurchaseReceipt::FReceiptOfferEntry& Entry : Receipt.ReceiptOffers)
		{
			for (const FPurchaseReceipt::FLineItemInfo& Item : Entry.LineItems)
			{
				const FString Token = Item.UniqueId.IsEmpty() ? Receipt.TransactionId : Item.UniqueId;
				if (Token.IsEmpty())
				{
					continue;
				}
				// The engine withholds validation info for anything Play still calls Pending, so an empty one is
				// how a not-yet-paid purchase shows up here. There is nothing to credit for it.
				if (!Item.IsRedeemable())
				{
					PendingTokens.Add(Token);
					continue;
				}
				if (SentTokens.Contains(Token))
				{
					// Already accepted this session. Play keeps listing it until the server consumes it, which is
					// expected - do not hammer the endpoint over it.
					continue;
				}
				const FString ProductId = Entry.OfferId.IsEmpty() ? Item.ItemName : Entry.OfferId;
				UE_LOG(LogCampusBilling, Log, TEXT("Restoring unfinished purchase '%s'."), *ProductId);
				++Resent;
				SendToBackend(ProductId, Token, Item.ValidationInfo,
					[](ECampusPurchaseResult Result, const FString& Error)
					{
						// Silent by design: nobody asked for this, it is cleanup. The wallet on UCampusBackend is
						// refreshed by the verify response, so a store screen that reads it will show the change.
						UE_LOG(LogCampusBilling, Log, TEXT("Restored purchase -> %s %s"), LexToString(Result), *Error);
					});
			}
		}
	}
	UE_LOG(LogCampusBilling, Log, TEXT("Drain: %d receipts, %d re-sent, %d pending."), Receipts.Num(), Resent, PendingTokens.Num());
#endif
}
