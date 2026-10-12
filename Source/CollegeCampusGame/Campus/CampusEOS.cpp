#include "Campus/CampusEOS.h"

#include "Misc/ConfigCacheIni.h"
#include "Online/Auth.h"
// Auth.h only forward-declares the async op handle and the result it hands back.
#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineResult.h"
#include "Online/OnlineServices.h"

#if WITH_EOS_SDK
#include "EOSShared.h"
#include "IEOSSDKManager.h"
#include "eos_connect.h"
#include "eos_sdk.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCampusEOS, Log, All);

namespace CampusEOS
{
	/** The [OnlineServices.EOS] key that names the artifact section; see docs/EOS_SETUP.md. */
	const TCHAR* ServicesSection = TEXT("OnlineServices.EOS");

	/** Everything EOS_Platform_Create insists on. A blank one of these means "not set up". */
	const TCHAR* RequiredKeys[] = { TEXT("ProductId"), TEXT("SandboxId"), TEXT("DeploymentId"), TEXT("ClientId"), TEXT("ClientSecret") };

	FString PlatformSection()
	{
		FString Name;
		GConfig->GetString(ServicesSection, TEXT("PlatformConfigName"), Name, GEngineIni);
		return Name.IsEmpty() ? FString() : FString(TEXT("EOSSDK.Platform.")) + Name;
	}
}

void UCampusEOS::Deinitialize()
{
	Waiting.Reset();
	Platform.Reset();
	Super::Deinitialize();
}

bool UCampusEOS::IsConfigured() const
{
#if WITH_EOS_SDK
	// EOS P2P in the editor would need the socket subsystem bound to the PIE world, which the engine
	// does not do (FSocketSubsystemEOS matches its instance against UGameEngine's game world). So in
	// the editor we stay on the LAN path on purpose - that keeps the editor test loop working.
	// Internet play is verified in a packaged build, or in Standalone Game (its own process).
	if (GIsEditor)
	{
		return false;
	}

	const FString Section = CampusEOS::PlatformSection();
	if (Section.IsEmpty())
	{
		return false;
	}
	for (const TCHAR* Key : CampusEOS::RequiredKeys)
	{
		FString Value;
		GConfig->GetString(*Section, Key, Value, GEngineIni);
		if (Value.TrimStartAndEnd().IsEmpty() || Value.Contains(TEXT("FILL")))
		{
			return false;
		}
	}
	return true;
#else
	return false;
#endif
}

FString UCampusEOS::ConnectUrlFor(const FString& HostPuid)
{
	return FString::Printf(TEXT("[EOS:%s]"), *HostPuid);
}

void UCampusEOS::EnsureLogin(FDone Done)
{
	if (IsReady())
	{
		if (Done)
		{
			Done(true);
		}
		return;
	}
	if (!IsConfigured())
	{
		LastError = TEXT("Internet play is not set up in this build.");
		if (Done)
		{
			Done(false);
		}
		return;
	}
	if (Done)
	{
		Waiting.Add(MoveTemp(Done));
	}
	if (Phase == EPhase::LoggingIn)
	{
		return; // the callback already queued will be told
	}
	// A previous failure is worth retrying: the player may have had no network then.
	Phase = EPhase::LoggingIn;
	LastError.Reset();
	StartLogin();
}

void UCampusEOS::Finish(bool bOk, const FString& Error)
{
	Phase = bOk ? EPhase::Ready : EPhase::Failed;
	LastError = bOk ? FString() : Error;
	if (!bOk)
	{
		UE_LOG(LogCampusEOS, Warning, TEXT("Campus EOS: login failed (%s)"), *Error);
	}
	else
	{
		UE_LOG(LogCampusEOS, Log, TEXT("Campus EOS: logged in, puid %s"), *LocalPuid);
	}
	TArray<FDone> Ready = MoveTemp(Waiting);
	Waiting.Reset();
	for (const FDone& Done : Ready)
	{
		if (Done)
		{
			Done(bOk);
		}
	}
}

#if WITH_EOS_SDK

namespace
{
	/** One EOS callback's worth of context: EOS hands ClientData back and we delete it there. */
	struct FCampusEOSCall
	{
		TWeakObjectPtr<UCampusEOS> Owner;
	};

	EOS_HConnect ConnectHandleOf(const TSharedPtr<IEOSPlatformHandle, ESPMode::ThreadSafe>& Platform)
	{
		return Platform.IsValid() ? EOS_Platform_GetConnectInterface(*Platform) : nullptr;
	}

	/** The API only accepts ^[0-9a-f]{32}$, so anything else must not be sent as a puid at all. */
	bool IsProductUserId(const FString& Text)
	{
		if (Text.Len() != 32)
		{
			return false;
		}
		for (const TCHAR Character : Text)
		{
			if (!FChar::IsHexDigit(Character))
			{
				return false;
			}
		}
		return true;
	}
}

void UCampusEOS::StartLogin()
{
	// Asking for the services brings the EOS platform up (it reads the artifact keys and creates it).
	// A null result means the keys are wrong or the SDK could not start: fall back to the LAN.
	const TSharedPtr<UE::Online::IOnlineServices> Services = UE::Online::GetServices(UE::Online::EOnlineServices::Epic);
	if (!Services.IsValid())
	{
		Finish(false, TEXT("Could not start Epic Online Services (check the EOS keys in DefaultEngine.ini)."));
		return;
	}

	IEOSSDKManager* Manager = IEOSSDKManager::Get();
	if (!Manager || !Manager->IsInitialized())
	{
		Finish(false, TEXT("The EOS SDK is not loaded in this build."));
		return;
	}
	const TArray<IEOSPlatformHandlePtr> Platforms = Manager->GetActivePlatforms();
	if (Platforms.IsEmpty())
	{
		Finish(false, TEXT("No EOS platform was created."));
		return;
	}
	if (Platforms.Num() > 1)
	{
		UE_LOG(LogCampusEOS, Warning, TEXT("Campus EOS: %d EOS platforms active; using the first."), Platforms.Num());
	}
	Platform = Platforms[0];

	EOS_HConnect Connect = ConnectHandleOf(Platform);
	if (!Connect)
	{
		Finish(false, TEXT("No EOS Connect interface."));
		return;
	}

	// A device id has to exist on this phone before a device-id login will be accepted. It is created
	// once and then lives in the SDK's cache directory (uninstalling the app deletes it, which is
	// fine - the player's real account is ours, held by the API; this id only names the transport).
	const FString Model = FPlatformMisc::GetDeviceMakeAndModel();
	const FTCHARToUTF8 ModelUtf8(Model.IsEmpty() ? TEXT("Campus Combat device") : *Model);

	EOS_Connect_CreateDeviceIdOptions Options = {};
	Options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
	Options.DeviceModel = ModelUtf8.Get();

	FCampusEOSCall* Call = new FCampusEOSCall{ this };
	EOS_Connect_CreateDeviceId(Connect, &Options, Call, [](const EOS_Connect_CreateDeviceIdCallbackInfo* Data)
	{
		TUniquePtr<FCampusEOSCall> Context(static_cast<FCampusEOSCall*>(Data->ClientData));
		UCampusEOS* Self = Context.IsValid() ? Context->Owner.Get() : nullptr;
		if (!Self)
		{
			return;
		}
		// DuplicateNotAllowed = this phone already has one, which is the normal case after the first run.
		if (Data->ResultCode == EOS_EResult::EOS_Success || Data->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed)
		{
			Self->ConnectLogin();
			return;
		}
		Self->Finish(false, FString::Printf(TEXT("EOS device id refused (%s)."), *LexToString(Data->ResultCode)));
	});
}

void UCampusEOS::ConnectLogin()
{
	using namespace UE::Online;

	const TSharedPtr<IOnlineServices> Services = GetServices(EOnlineServices::Epic);
	IAuthPtr Auth = Services.IsValid() ? Services->GetAuthInterface() : nullptr;
	if (!Auth.IsValid())
	{
		Finish(false, TEXT("No EOS auth interface."));
		return;
	}

	// Connect-only login (no Epic account): the device id *is* the credential, so the token is empty.
	// The services stack creates the product user by itself the first time this id is seen.
	FExternalAuthToken Token;
	Token.Type = ExternalLoginType::DeviceIdAccessToken;

	FAuthLogin::Params Params;
	Params.PlatformUserId = FPlatformMisc::GetPlatformUserForUserIndex(0);
	Params.CredentialsType = LoginCredentialsType::ExternalAuth;
	Params.CredentialsToken.Emplace<FExternalAuthToken>(MoveTemp(Token));

	TWeakObjectPtr<UCampusEOS> WeakThis(this);
	Auth->Login(MoveTemp(Params)).OnComplete([WeakThis](const TOnlineResult<FAuthLogin>& Result)
	{
		UCampusEOS* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (Result.IsError())
		{
			Self->Finish(false, Result.GetErrorValue().GetLogString(true));
			return;
		}
		Self->ReadLocalPuid();
		const bool bHavePuid = !Self->LocalPuid.IsEmpty();
		Self->Finish(bHavePuid, bHavePuid ? FString() : FString(TEXT("EOS login returned no product user id.")));
	});
}

void UCampusEOS::ReadLocalPuid()
{
	LocalPuid.Reset();
	EOS_HConnect Connect = ConnectHandleOf(Platform);
	if (!Connect || EOS_Connect_GetLoggedInUsersCount(Connect) <= 0)
	{
		return;
	}
	const EOS_ProductUserId Puid = EOS_Connect_GetLoggedInUserByIndex(Connect, 0);
	char Buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
	int32_t Length = sizeof(Buffer);
	if (EOS_ProductUserId_ToString(Puid, Buffer, &Length) != EOS_EResult::EOS_Success)
	{
		return;
	}
	// The API's contract is 32 lowercase hex characters. EOS already gives us that, but we hold it to
	// the contract here rather than let the room call fail validation later.
	const FString Text = FString(UTF8_TO_TCHAR(Buffer)).ToLower();
	if (!IsProductUserId(Text))
	{
		UE_LOG(LogCampusEOS, Warning, TEXT("Campus EOS: unexpected product user id '%s'; ignoring it."), *Text);
		return;
	}
	LocalPuid = Text;
}

#else // WITH_EOS_SDK

void UCampusEOS::StartLogin()
{
	Finish(false, TEXT("This build has no EOS SDK."));
}

void UCampusEOS::ConnectLogin()
{
}

void UCampusEOS::ReadLocalPuid()
{
}

#endif // WITH_EOS_SDK
