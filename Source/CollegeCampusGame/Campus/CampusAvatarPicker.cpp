#include "Campus/CampusAvatarPicker.h"

#include "Campus/CampusAvatar.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidJavaEnv.h"
#endif

#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#endif

namespace CampusAvatarPicker
{
	namespace
	{
		/** One picker at a time; the first result consumes the callback (same shape as CampusRazorpay). */
		FPicked Pending;
		bool bOpen = false;
		/** Why the last Pick came back false. Empty = the player just backed out, which is not an error. */
		FString Reason;
	}

	const FString& LastError()
	{
		return Reason;
	}

	static void Deliver(bool bOk, TArray<uint8> Jpeg, const FString& Error)
	{
		// The JNI callback arrives on Android's UI thread. Everything downstream (the cache, Slate, the
		// toast) is game-thread only, so this hop is not optional.
		AsyncTask(ENamedThreads::GameThread, [bOk, Error, Bytes = MoveTemp(Jpeg)]() mutable
		{
			Reason = Error;
			FPicked Callback = MoveTemp(Pending);
			Pending = nullptr;
			bOpen = false;
			if (Callback)
			{
				Callback(bOk, MoveTemp(Bytes));
			}
		});
	}

	bool IsAvailable()
	{
#if PLATFORM_ANDROID
		return true;
#elif WITH_EDITOR
		return true;
#else
		return false;
#endif
	}

	void Pick(FPicked OnPicked)
	{
		if (bOpen)
		{
			if (OnPicked)
			{
				AsyncTask(ENamedThreads::GameThread, [Callback = MoveTemp(OnPicked)]() { Callback(false, TArray<uint8>()); });
			}
			return;
		}
		Pending = MoveTemp(OnPicked);
		bOpen = true;
		Reason.Reset();

#if PLATFORM_ANDROID
		if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
		{
			static jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID,
				"AndroidThunkJava_CampusPickAvatar", "()V", false);
			if (Method)
			{
				FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method);
				return;
			}
		}
		// The thunk is missing, which means the UPL never merged into GameActivity: loud in the log,
		// quiet in the UI, and NEVER a button with no callback behind it.
		UE_LOG(LogTemp, Error, TEXT("CampusAvatarPicker: AndroidThunkJava_CampusPickAvatar not found - did CampusCombat_UPL.xml merge?"));
		Deliver(false, TArray<uint8>(), TEXT("Photos are not available in this build."));
#elif WITH_EDITOR
		TArray<FString> Files;
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		const void* Parent = nullptr;
		if (FSlateApplication::IsInitialized())
		{
			const TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();
			if (Window.IsValid() && Window->GetNativeWindow().IsValid())
			{
				Parent = Window->GetNativeWindow()->GetOSWindowHandle();
			}
		}
		const bool bPicked = Desktop && Desktop->OpenFileDialog(Parent, TEXT("CHOOSE A PROFILE PICTURE"), FPaths::ProjectDir(),
			FString(), TEXT("Images (*.jpg;*.jpeg;*.png)|*.jpg;*.jpeg;*.png"), EFileDialogFlags::None, Files);
		if (!bPicked || Files.Num() == 0)
		{
			Deliver(false, TArray<uint8>(), FString());
			return;
		}
		TArray<uint8> Source;
		TArray<uint8> Jpeg;
		if (!FFileHelper::LoadFileToArray(Source, *Files[0]))
		{
			Deliver(false, TArray<uint8>(), TEXT("That file could not be read."));
			return;
		}
		if (!CampusAvatar::EncodeJpeg(Source, CampusAvatarApi::MaxPixels, CampusAvatarApi::JpegQuality, Jpeg))
		{
			Deliver(false, TArray<uint8>(), TEXT("That picture could not be read. Try a JPG or PNG."));
			return;
		}
		Deliver(true, MoveTemp(Jpeg), FString());
#else
		Deliver(false, TArray<uint8>(), TEXT("Photos are only available on the phone."));
#endif
	}
}

#if PLATFORM_ANDROID
// GameActivity.nativeCampusAvatarPicked(byte[], String) - see CampusCombat_UPL.xml.
// A null / empty array with an empty error is "the player backed out": no toast, no state change.
JNI_METHOD void Java_com_epicgames_unreal_GameActivity_nativeCampusAvatarPicked(JNIEnv* Env, jobject Thiz,
	jbyteArray Jpeg, jstring Error)
{
	TArray<uint8> Bytes;
	if (Jpeg)
	{
		const jsize Length = Env->GetArrayLength(Jpeg);
		if (Length > 0)
		{
			Bytes.SetNumUninitialized(int32(Length));
			Env->GetByteArrayRegion(Jpeg, 0, Length, reinterpret_cast<jbyte*>(Bytes.GetData()));
		}
	}
	const FString Reason = FJavaHelper::FStringFromParam(Env, Error);
	CampusAvatarPicker::Deliver(Bytes.Num() > 0, MoveTemp(Bytes), Reason);
}
#endif
