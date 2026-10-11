// Razorpay Android Checkout from C++: opens the in-app payment sheet (UPI apps, cards, netbanking) for an order our
// server created, and hands the outcome back on the game thread. Java side: CampusCombat_UPL.xml.

#pragma once

#include "CoreMinimal.h"

namespace CampusRazorpay
{
	/** Code passed back on success (Razorpay's own error codes are never this). */
	constexpr int32 Success = 1000;

	/** Code, Razorpay order id, payment id, signature (success only), message (Razorpay's error JSON / text). */
	using FResult = TFunction<void(int32 Code, const FString& OrderId, const FString& PaymentId, const FString& Signature, const FString& Message)>;

	/** True where the native sheet exists (Android); elsewhere the browser payment link is used. */
	bool IsAvailable();

	/** Opens the sheet. Exactly one OnResult call follows (success, cancel, failure). */
	void Open(const FString& KeyId, const FString& OrderId, int32 AmountPaise, const FString& Title, const FString& Description,
		const FString& Email, FResult OnResult);
}
