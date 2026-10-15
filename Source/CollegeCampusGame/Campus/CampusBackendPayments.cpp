// REAL MONEY (UCampusBackend). Two rails, one rule.
//
//   GOOGLE PLAY   the purchase token goes to the server, which checks it with Google, credits the
//                 wallet and consumes the purchase. WE NEVER CONSUME ON THE DEVICE: a consume the
//                 server did not authorise is a purchase the player paid for and cannot recover.
//                 Safe to send the same token again - the server is idempotent on it, which is
//                 exactly how an app killed mid-purchase gets its diamonds.
//   RAZORPAY      the native sheet's outcome is a HINT, not a verdict. On success the server
//                 verifies the signature; on anything else the server asks Razorpay what really
//                 happened and closes the order (cancelled / failed / paid after all).
//
// THE ONE RULE: the device never decides that money moved. Every path above ends in the server
// answering with the new wallet, and that answer is the only thing the counters are read from.
// The polling exists for the case where the sheet's callback never arrives at all.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Campus/CampusRazorpay.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace CampusApi;

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
