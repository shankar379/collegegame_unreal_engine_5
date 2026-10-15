#include "Campus/CampusRemoteImage.h"

#include "Campus/CampusBackend.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

namespace CampusRemoteImage
{
	namespace
	{
		/** The ngrok interstitial killer: the same header UCampusBackend::Send sets on every call.
		 *  Without it the tunnel answers with its HTML page and we would "decode" a web page. */
		const TCHAR* TunnelHeader = TEXT("ngrok-skip-browser-warning");

		/** One key@version's picture. Held by TSharedPtr so a rehash never moves the FImage a caller
		 *  is reading - SCampusArtPlate takes the address of one in OnPaint. */
		struct FEntry
		{
			FString Key;
			int32 Version = 0;
			FImage Image;
			FSlateBrush Brush;
			/** Rooted rather than held in a TStrongObjectPtr: these outlive every widget that draws
			 *  them and must not run GC code from a static destructor at shutdown. */
			UTexture2D* Texture = nullptr;
			bool bRequested = false;
		};

		TMap<FString, TSharedPtr<FEntry>>& Cache()
		{
			static TMap<FString, TSharedPtr<FEntry>> Map;
			return Map;
		}

		TWeakObjectPtr<UCampusBackend>& SessionRef()
		{
			static TWeakObjectPtr<UCampusBackend> Api;
			return Api;
		}

		const FImage& NoImage()
		{
			static const FImage Empty;
			return Empty;
		}

		FString DiskPath(const FString& CacheKey)
		{
			// The cache key carries the version, so a new upload hashes to a new file and the old one
			// is never read again (ForgetStale deletes it the moment a new blob names a new version).
			return FPaths::ProjectSavedDir() / TEXT("ContentCache") / FMD5::HashAnsiString(*CacheKey) + TEXT(".img");
		}

		void SaveToDisk(const FString& CacheKey, const TArray<uint8>& Bytes)
		{
			const FString Path = DiskPath(CacheKey);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Bytes, *Path);
		}

		/** Decoded bytes -> a ready entry. Fails softly: the caller marks the entry Failed. */
		bool Adopt(FEntry& Entry, const uint8* Bytes, int64 Num)
		{
			UTexture2D* Texture = DecodeTexture(Bytes, Num);
			if (!Texture || !FSlateApplication::IsInitialized())
			{
				return false;
			}
			if (Entry.Texture)
			{
				Entry.Texture->RemoveFromRoot();
			}
			Entry.Texture = Texture;
			Entry.Brush.SetResourceObject(Texture);
			Entry.Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Entry.Brush.DrawAs = ESlateBrushDrawType::Image;
			Entry.Image.Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Entry.Brush);
			Entry.Image.Size = FVector2f(float(Texture->GetSizeX()), float(Texture->GetSizeY()));
			Entry.Image.State = EState::Ready;
			return true;
		}

		void Fetch(const FString& Url, const FString& CacheKey, TSharedPtr<FEntry> Entry)
		{
			const UCampusBackend* Api = SessionRef().Get();
			const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			Request->SetVerb(TEXT("GET"));
			Request->SetURL(Url);
			Request->SetHeader(TEXT("Accept"), TEXT("image/*"));
			Request->SetHeader(TunnelHeader, TEXT("1"));
			// GET /v1/content/:key needs a player bearer (routes/content.js): the art is not personal
			// data, but an open image endpoint on a metered tunnel is an open bandwidth bill.
			if (Api && !Api->GetAccessToken().IsEmpty())
			{
				Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Api->GetAccessToken());
			}
			Request->SetTimeout(15.f);
			Request->OnProcessRequestComplete().BindLambda(
				[CacheKey, Entry](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
			{
				const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
				bool bOk = false;
				if (Status >= 200 && Status < 300)
				{
					const TArray<uint8>& Body = Response->GetContent();
					if (Body.Num() > 0 && Body.Num() <= MaxDownloadBytes && Adopt(*Entry, Body.GetData(), Body.Num()))
					{
						SaveToDisk(CacheKey, Body);
						bOk = true;
					}
				}
				// NO RETRY LOOP. A picture that failed keeps the caller's fallback until the next app
				// run; a retrying banner is the cheapest way to spend 20,000 monthly requests on
				// nothing. The console's own 404 (a retired key) lands here too, and is the same thing.
				if (!bOk)
				{
					Entry->Image.State = EState::Failed;
				}
			});
			Entry->Image.State = EState::Loading;
			Request->ProcessRequest();
		}
	}

	void UseSession(UCampusBackend* Api)
	{
		SessionRef() = Api;
	}

	UCampusBackend* Session()
	{
		return SessionRef().Get();
	}

	FString ResolveUrl(const FString& PathOrUrl)
	{
		FString Trimmed = PathOrUrl.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return FString();
		}
		if (Trimmed.StartsWith(TEXT("http://")) || Trimmed.StartsWith(TEXT("https://")))
		{
			return Trimmed;
		}
		const UCampusBackend* Api = SessionRef().Get();
		if (!Api)
		{
			// Nothing can be fetched before the lobby hands us the session; the caller keeps its fallback.
			return FString();
		}
		FString Base = Api->ApiBaseUrl();   // ".../v1", after the ini quote / "//" defences
		Base.RemoveFromEnd(TEXT("/"));
		if (!Trimmed.StartsWith(TEXT("/")))
		{
			Trimmed = TEXT("/") + Trimmed;
		}
		// Two shapes are legal: "/v1/content/x?v=3" (origin-relative, what the API publishes) and
		// "/avatars/07" (base-relative). Anything already carrying the version prefix is joined to the
		// ORIGIN so the path cannot come out as ".../v1/v1/...".
		if (Trimmed.StartsWith(TEXT("/v1/")))
		{
			FString Origin = Base;
			Origin.RemoveFromEnd(TEXT("/v1"));
			return Origin + Trimmed;
		}
		return Base + Trimmed;
	}

	UTexture2D* DecodeTexture(const uint8* Bytes, int64 Num)
	{
		if (!Bytes || Num <= 0)
		{
			return nullptr;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		// The format is DETECTED, never assumed: the console re-encodes every upload and may hand back
		// a PNG where a JPEG went in, and a guessed format is a black square.
		const EImageFormat Format = Module.DetectImageFormat(Bytes, Num);
		if (Format == EImageFormat::Invalid)
		{
			return nullptr;
		}
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Format);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes, Num))
		{
			return nullptr;
		}
		TArray64<uint8> Raw;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return nullptr;
		}
		const int32 Width = Wrapper->GetWidth();
		const int32 Height = Wrapper->GetHeight();
		if (Width <= 0 || Height <= 0 || Raw.Num() < int64(Width) * Height * 4)
		{
			return nullptr;
		}
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->NeverStream = true;
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Pixels = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Pixels, Raw.GetData(), int64(Width) * Height * 4);
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		Texture->AddToRoot();
		return Texture;
	}

	const FImage& Get(const FString& Key)
	{
		const UCampusBackend* Api = SessionRef().Get();
		// THE PUBLISHED VERSION IS THE PERMISSION TO FETCH. No blob, or a blob that does not name this
		// key, means the console has published no art for it: the caller keeps the packaged picture and
		// the tunnel is never touched.
		const int32 Version = Api ? Api->ContentVersionOf(Key) : 0;
		if (Version <= 0)
		{
			return NoImage();
		}
		const FString CacheKey = FString::Printf(TEXT("%s@%d"), *Key, Version);
		TSharedPtr<FEntry>* Found = Cache().Find(CacheKey);
		if (!Found)
		{
			Found = &Cache().Add(CacheKey, MakeShared<FEntry>());
			(*Found)->Key = Key;
			(*Found)->Version = Version;
		}
		TSharedPtr<FEntry> Entry = *Found;
		if (!Entry->bRequested)
		{
			Entry->bRequested = true;
			// Disk first: a banner seen yesterday costs the tunnel nothing today.
			TArray<uint8> Cached;
			if (FFileHelper::LoadFileToArray(Cached, *DiskPath(CacheKey)) && Cached.Num() > 0
				&& Adopt(*Entry, Cached.GetData(), Cached.Num()))
			{
				return Entry->Image;
			}
			const FString Url = Api->ContentUrlFor(Key);
			if (Url.IsEmpty())
			{
				Entry->Image.State = EState::Failed;
				return Entry->Image;
			}
			Fetch(Url, CacheKey, Entry);
			return Entry->Image;
		}
		// Slate drops resource handles when the renderer flushes its atlases: re-acquire, never re-decode.
		if (Entry->Image.State == EState::Ready && !Entry->Image.Handle.IsValid()
			&& Entry->Brush.GetResourceObject() && FSlateApplication::IsInitialized())
		{
			Entry->Image.Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Entry->Brush);
		}
		return Entry->Image;
	}

	EState StateOf(const FString& Key)
	{
		const UCampusBackend* Api = SessionRef().Get();
		const int32 Version = Api ? Api->ContentVersionOf(Key) : 0;
		if (Version <= 0)
		{
			return EState::None;
		}
		const TSharedPtr<FEntry>* Found = Cache().Find(FString::Printf(TEXT("%s@%d"), *Key, Version));
		return Found ? (*Found)->Image.State : EState::None;
	}

	void ForgetStale()
	{
		const UCampusBackend* Api = SessionRef().Get();
		if (!Api)
		{
			return;
		}
		for (auto It = Cache().CreateIterator(); It; ++It)
		{
			const TSharedPtr<FEntry>& Entry = It.Value();
			if (!Entry.IsValid() || Api->ContentVersionOf(Entry->Key) == Entry->Version)
			{
				continue;
			}
			// A re-upload: these bytes can never be wanted again, so the texture is unrooted and the
			// file goes with it rather than sitting in Saved for the life of the install.
			if (Entry->Texture)
			{
				Entry->Texture->RemoveFromRoot();
				Entry->Texture = nullptr;
			}
			Entry->Brush.SetResourceObject(nullptr);
			Entry->Image = FImage();
			IFileManager::Get().Delete(*DiskPath(It.Key()), false, false, true);
			It.RemoveCurrent();
		}
	}
}
