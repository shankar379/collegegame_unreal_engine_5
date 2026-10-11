#include "Campus/CampusRazorpay.h"

#include "Async/Async.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidJavaEnv.h"
#endif

namespace CampusRazorpay
{
	namespace
	{
		// one sheet at a time; the callback is consumed by the first result
		FResult Pending;
	}

	bool IsAvailable()
	{
#if PLATFORM_ANDROID
		return true;
#else
		return false;
#endif
	}

	static void Deliver(int32 Code, const FString& OrderId, const FString& PaymentId, const FString& Signature, const FString& Message)
	{
		AsyncTask(ENamedThreads::GameThread, [Code, OrderId, PaymentId, Signature, Message]()
		{
			FResult Callback = MoveTemp(Pending);
			Pending = nullptr;
			if (Callback)
			{
				Callback(Code, OrderId, PaymentId, Signature, Message);
			}
		});
	}

	void Open(const FString& KeyId, const FString& OrderId, int32 AmountPaise, const FString& Title, const FString& Description,
		const FString& Email, FResult OnResult)
	{
		Pending = MoveTemp(OnResult);
#if PLATFORM_ANDROID
		if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
		{
			static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_RazorpayOpen",
				"(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", false);
			if (Method)
			{
				auto JKey = FJavaHelper::ToJavaString(Env, KeyId);
				auto JOrder = FJavaHelper::ToJavaString(Env, OrderId);
				auto JTitle = FJavaHelper::ToJavaString(Env, Title);
				auto JDescription = FJavaHelper::ToJavaString(Env, Description);
				auto JEmail = FJavaHelper::ToJavaString(Env, Email);
				FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method, *JKey, *JOrder, (jint)AmountPaise, *JTitle, *JDescription, *JEmail);
				return;
			}
		}
		Deliver(-1, OrderId, FString(), FString(), TEXT("Payment sheet unavailable."));
#else
		Deliver(-1, OrderId, FString(), FString(), TEXT("In-app payment is only on Android."));
#endif
	}
}

#if PLATFORM_ANDROID
// GameActivity.nativeRazorpayResult(int, String, String, String, String) (see CampusCombat_UPL.xml)
JNI_METHOD void Java_com_epicgames_unreal_GameActivity_nativeRazorpayResult(JNIEnv* Env, jobject Thiz, jint Code, jstring OrderId,
	jstring PaymentId, jstring Signature, jstring Message)
{
	CampusRazorpay::Deliver(int32(Code), FJavaHelper::FStringFromParam(Env, OrderId), FJavaHelper::FStringFromParam(Env, PaymentId),
		FJavaHelper::FStringFromParam(Env, Signature), FJavaHelper::FStringFromParam(Env, Message));
}
#endif
