#include "Campus/CampusAvatar.h"

#include "Campus/CampusBackend.h"
#include "Dom/JsonObject.h"
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
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Layout/SBox.h"

namespace CampusAvatar
{
	namespace
	{
		/** One URL's picture. Held by TSharedPtr so a rehash never moves the FImage a caller is reading. */
		struct FEntry
		{
			FImage Image;
			FSlateBrush Brush;
			/** Rooted rather than held in a TStrongObjectPtr: these outlive every widget that draws them
			 *  and must not run GC code from a static destructor at shutdown. Same deal as CampusMapArt. */
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

		/** Uploads / deletes in flight, so the EDIT sheet can disable both buttons. */
		int32 Pending = 0;

		const FImage& NoImage()
		{
			static const FImage Empty;
			return Empty;
		}

		FString DiskPath(const FString& Resolved)
		{
			// The URL carries ?v=<n>, so a new upload hashes to a new file and the old one is simply
			// never read again (and is deleted by Forget when we are the ones who replaced it).
			return FPaths::ProjectSavedDir() / TEXT("AvatarCache") / FMD5::HashAnsiString(*Resolved) + TEXT(".img");
		}

		/** Keeps the picture for the next launch. The folder is made here rather than assumed. */
		void SaveToDisk(const FString& Resolved, const TArray<uint8>& Bytes)
		{
			const FString Path = DiskPath(Resolved);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FFileHelper::SaveArrayToFile(Bytes, *Path);
		}

		UTexture2D* Decode(const uint8* Bytes, int64 Num)
		{
			if (!Bytes || Num <= 0)
			{
				return nullptr;
			}
			IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			// JPEG from our own pipeline, but a PNG the server re-encoded must not be a black square.
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

		/** Decoded bytes -> a ready entry. Fails softly: the caller marks the entry Failed. */
		bool Adopt(FEntry& Entry, const uint8* Bytes, int64 Num)
		{
			UTexture2D* Texture = Decode(Bytes, Num);
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

		void Fetch(const FString& Resolved, TSharedPtr<FEntry> Entry)
		{
			UCampusBackend* Api = SessionRef().Get();
			const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			Request->SetVerb(TEXT("GET"));
			Request->SetURL(Resolved);
			Request->SetHeader(TEXT("Accept"), TEXT("image/*"));
			// Same two headers every other call in this game sends (UCampusBackend::Send): without the
			// ngrok one the tunnel answers with its HTML interstitial and we would "decode" a web page.
			Request->SetHeader(CampusAvatarApi::TunnelHeader, TEXT("1"));
			if (Api && !Api->GetAccessToken().IsEmpty())
			{
				Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Api->GetAccessToken());
			}
			Request->SetTimeout(15.f);
			Request->OnProcessRequestComplete().BindLambda(
				[Resolved, Entry](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
			{
				const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
				bool bOk = false;
				if (Status >= 200 && Status < 300)
				{
					const TArray<uint8>& Body = Response->GetContent();
					if (Body.Num() > 0 && Body.Num() <= CampusAvatarApi::MaxDownloadBytes && Adopt(*Entry, Body.GetData(), Body.Num()))
					{
						SaveToDisk(Resolved, Body);
						bOk = true;
					}
				}
				// No retry loop: a failed avatar keeps its fallback until the next app run. The ngrok
				// budget is 20,000 requests a MONTH and a retrying picture is the cheapest way to
				// spend it on nothing.
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

	FString Resolve(const FString& Url)
	{
		FString Trimmed = Url.TrimStartAndEnd();
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
		// Two shapes are legal: "/v1/avatars/<id>?v=3" (origin-relative, what the API returns) and
		// "/avatars/<id>" (base-relative). Anything already carrying the version prefix is joined to
		// the ORIGIN so the path cannot come out as ".../v1/v1/...".
		if (Trimmed.StartsWith(TEXT("/v1/")))
		{
			FString Origin = Base;
			Origin.RemoveFromEnd(TEXT("/v1"));
			return Origin + Trimmed;
		}
		return Base + Trimmed;
	}

	const FImage& Get(const FString& Url)
	{
		const FString Resolved = Resolve(Url);
		if (Resolved.IsEmpty())
		{
			return NoImage();
		}
		TSharedPtr<FEntry>* Found = Cache().Find(Resolved);
		if (!Found)
		{
			Found = &Cache().Add(Resolved, MakeShared<FEntry>());
		}
		TSharedPtr<FEntry> Entry = *Found;
		if (!Entry->bRequested)
		{
			Entry->bRequested = true;
			// Disk first: a friend's picture seen yesterday costs the tunnel nothing today.
			TArray<uint8> Cached;
			if (FFileHelper::LoadFileToArray(Cached, *DiskPath(Resolved)) && Cached.Num() > 0
				&& Adopt(*Entry, Cached.GetData(), Cached.Num()))
			{
				return Entry->Image;
			}
			Fetch(Resolved, Entry);
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

	EState StateOf(const FString& Url)
	{
		const FString Resolved = Resolve(Url);
		if (Resolved.IsEmpty())
		{
			return EState::None;
		}
		const TSharedPtr<FEntry>* Found = Cache().Find(Resolved);
		return Found ? (*Found)->Image.State : EState::None;
	}

	uint32 Fingerprint(const FString& Url)
	{
		const FString Resolved = Resolve(Url);
		return HashCombine(GetTypeHash(Resolved), uint32(StateOf(Url)));
	}

	void Forget(const FString& Url)
	{
		const FString Resolved = Resolve(Url);
		if (Resolved.IsEmpty())
		{
			return;
		}
		if (TSharedPtr<FEntry> Entry = Cache().FindRef(Resolved))
		{
			if (Entry->Texture)
			{
				Entry->Texture->RemoveFromRoot();
				Entry->Texture = nullptr;
			}
			Entry->Brush.SetResourceObject(nullptr);
			Entry->Image = FImage();
			Entry->bRequested = false;
		}
		Cache().Remove(Resolved);
		IFileManager::Get().Delete(*DiskPath(Resolved), false, false, true);
	}

	bool IsBusy()
	{
		return Pending > 0;
	}

	void Upload(UCampusBackend* Api, const TArray<uint8>& Jpeg,
		TFunction<void(bool bOk, const FString& NewUrl, const FString& Error)> Done)
	{
		UseSession(Api);
		if (!Api || Api->GetAccessToken().IsEmpty())
		{
			if (Done) { Done(false, FString(), TEXT("Sign in before changing your photo.")); }
			return;
		}
		if (Jpeg.Num() == 0)
		{
			if (Done) { Done(false, FString(), TEXT("That picture could not be read.")); }
			return;
		}
		if (Jpeg.Num() > CampusAvatarApi::MaxUploadBytes)
		{
			if (Done) { Done(false, FString(), TEXT("That picture is too big. Try another one.")); }
			return;
		}

		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(TEXT("PUT"));
		Request->SetURL(Api->ApiBaseUrl() + CampusAvatarApi::MeAvatarPath);
		Request->SetHeader(TEXT("Content-Type"), CampusAvatarApi::UploadContentType);
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(CampusAvatarApi::TunnelHeader, TEXT("1"));
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Api->GetAccessToken());
		Request->SetContent(Jpeg);
		// Longer than the 10 s every JSON call gets: this is an upload over a free tunnel on phone data.
		Request->SetTimeout(30.f);
		++Pending;
		// A copy of the bytes rides along: on success they ARE the new picture, so the badge is painted
		// from them instead of downloading what we just sent.
		Request->OnProcessRequestComplete().BindLambda(
			[Done, Bytes = Jpeg](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			--Pending;
			const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
			TSharedPtr<FJsonObject> Json;
			if (Response.IsValid())
			{
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				FJsonSerializer::Deserialize(Reader, Json);
			}
			if (!bConnected || Status < 200 || Status >= 300)
			{
				if (Done)
				{
					// One sentence, from the SAME place every other API error in the game gets its words.
					Done(false, FString(), UCampusBackend::DescribeError(bConnected ? Status : 0, Json));
				}
				return;
			}
			FString NewUrl;
			if (Json.IsValid())
			{
				Json->TryGetStringField(CampusAvatarApi::UrlField, NewUrl);
			}
			if (!NewUrl.IsEmpty())
			{
				const FString Resolved = Resolve(NewUrl);
				if (!Resolved.IsEmpty())
				{
					TSharedPtr<FEntry> Entry = MakeShared<FEntry>();
					Entry->bRequested = true;
					if (Adopt(*Entry, Bytes.GetData(), Bytes.Num()))
					{
						Cache().Add(Resolved, Entry);
						SaveToDisk(Resolved, Bytes);
					}
				}
			}
			if (Done)
			{
				Done(true, NewUrl, FString());
			}
		});
		Request->ProcessRequest();
	}

	void Remove(UCampusBackend* Api, TFunction<void(bool bOk, const FString& NewUrl, const FString& Error)> Done)
	{
		UseSession(Api);
		if (!Api || Api->GetAccessToken().IsEmpty())
		{
			if (Done) { Done(false, FString(), TEXT("Sign in before changing your photo.")); }
			return;
		}
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(TEXT("DELETE"));
		Request->SetURL(Api->ApiBaseUrl() + CampusAvatarApi::MeAvatarPath);
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(CampusAvatarApi::TunnelHeader, TEXT("1"));
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Api->GetAccessToken());
		Request->SetTimeout(15.f);
		++Pending;
		Request->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			--Pending;
			const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
			// 204 is the honest answer to a DELETE, and 404 means it is already gone - both are success.
			const bool bOk = bConnected && ((Status >= 200 && Status < 300) || Status == 404);
			TSharedPtr<FJsonObject> Json;
			if (Response.IsValid() && Response->GetContentLength() > 0)
			{
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				FJsonSerializer::Deserialize(Reader, Json);
			}
			// The account now wears a RANDOM PRESET. A server that bothers to say which hands the new
			// avatar_url straight back; one that answers a bare 204 leaves the caller to re-read /me.
			FString NewUrl;
			if (bOk && Json.IsValid())
			{
				Json->TryGetStringField(CampusAvatarApi::UrlField, NewUrl);
			}
			if (Done)
			{
				Done(bOk, NewUrl, bOk ? FString() : UCampusBackend::DescribeError(bConnected ? Status : 0, Json));
			}
		});
		Request->ProcessRequest();
	}

	// ---- presets -------------------------------------------------------------------------------

	namespace
	{
		TArray<FPreset>& PresetList()
		{
			static TArray<FPreset> List;
			return List;
		}

		bool bPresetsLoaded = false;
		bool bPresetsInFlight = false;
	}

	const TArray<FPreset>& Presets()
	{
		return PresetList();
	}

	bool ArePresetsLoaded()
	{
		return bPresetsLoaded;
	}

	bool ArePresetsLoading()
	{
		return bPresetsInFlight;
	}

	void LoadPresets(UCampusBackend* Api, TFunction<void(bool bOk, const FString& Error)> Done)
	{
		UseSession(Api);
		if (bPresetsLoaded)
		{
			if (Done) { Done(true, FString()); }
			return;
		}
		if (bPresetsInFlight)
		{
			// One flight only. The EDIT sheet repaints several times while this is in the air and must
			// not turn each repaint into another request through the tunnel.
			return;
		}
		if (!Api)
		{
			if (Done) { Done(false, TEXT("Sign in first.")); }
			return;
		}
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(TEXT("GET"));
		Request->SetURL(Api->ApiBaseUrl() + CampusAvatarApi::PresetsPath);
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(CampusAvatarApi::TunnelHeader, TEXT("1"));
		if (!Api->GetAccessToken().IsEmpty())
		{
			Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Api->GetAccessToken());
		}
		Request->SetTimeout(10.f);
		bPresetsInFlight = true;
		Request->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			bPresetsInFlight = false;
			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			if (Status < 200 || Status >= 300)
			{
				if (Done) { Done(false, UCampusBackend::DescribeError(Status, nullptr)); }
				return;
			}
			// A BARE ARRAY, not an object: this one response is read with the array reader rather than
			// through UCampusBackend::CallApi, which only ever hands back an object.
			TArray<TSharedPtr<FJsonValue>> Rows;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			if (!FJsonSerializer::Deserialize(Reader, Rows))
			{
				// Tolerate { "presets": [ ... ] } as well; both shapes have shipped from this API before.
				TSharedPtr<FJsonObject> Object;
				const TSharedRef<TJsonReader<>> Again = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				const TArray<TSharedPtr<FJsonValue>>* Nested = nullptr;
				if (FJsonSerializer::Deserialize(Again, Object) && Object.IsValid()
					&& Object->TryGetArrayField(TEXT("presets"), Nested) && Nested)
				{
					Rows = *Nested;
				}
			}
			PresetList().Reset(Rows.Num());
			for (const TSharedPtr<FJsonValue>& Row : Rows)
			{
				const TSharedPtr<FJsonObject> Item = Row.IsValid() ? Row->AsObject() : nullptr;
				if (!Item.IsValid())
				{
					continue;
				}
				FPreset Preset;
				Item->TryGetStringField(TEXT("id"), Preset.Id);
				Item->TryGetStringField(TEXT("url"), Preset.Url);
				if (Preset.Id.IsEmpty())
				{
					continue;
				}
				if (Preset.Url.IsEmpty())
				{
					// An id with no URL is still usable: the path is part of the contract above.
					Preset.Url = FString(CampusAvatarApi::PresetsPath) + TEXT("/") + Preset.Id;
				}
				PresetList().Add(MoveTemp(Preset));
			}
			bPresetsLoaded = PresetList().Num() > 0;
			if (Done)
			{
				Done(bPresetsLoaded, bPresetsLoaded ? FString() : FString(TEXT("No profile icons came back.")));
			}
		});
		Request->ProcessRequest();
	}

	void SetPreset(UCampusBackend* Api, const FString& PresetId,
		TFunction<void(bool bOk, const FString& NewUrl, const FString& Error)> Done)
	{
		UseSession(Api);
		if (!Api || Api->GetAccessToken().IsEmpty())
		{
			if (Done) { Done(false, FString(), TEXT("Sign in before changing your picture.")); }
			return;
		}
		if (PresetId.IsEmpty())
		{
			if (Done) { Done(false, FString(), TEXT("That icon is not available.")); }
			return;
		}
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(TEXT("PUT"));
		Request->SetURL(Api->ApiBaseUrl() + CampusAvatarApi::MePresetPath);
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(CampusAvatarApi::TunnelHeader, TEXT("1"));
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Api->GetAccessToken());
		Request->SetContentAsString(FString::Printf(TEXT("{\"%s\":\"%s\"}"), CampusAvatarApi::PresetField, *PresetId));
		Request->SetTimeout(10.f);
		++Pending;
		Request->OnProcessRequestComplete().BindLambda([Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			--Pending;
			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			TSharedPtr<FJsonObject> Json;
			if (Response.IsValid() && Response->GetContentLength() > 0)
			{
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				FJsonSerializer::Deserialize(Reader, Json);
			}
			if (Status < 200 || Status >= 300)
			{
				if (Done) { Done(false, FString(), UCampusBackend::DescribeError(Status, Json)); }
				return;
			}
			FString NewUrl;
			if (Json.IsValid())
			{
				Json->TryGetStringField(CampusAvatarApi::UrlField, NewUrl);
			}
			if (Done)
			{
				Done(true, NewUrl, FString());
			}
		});
		Request->ProcessRequest();
	}

	bool IsPresetUrl(const FString& Url)
	{
		return !Url.IsEmpty() && Url.Contains(CampusAvatarApi::PresetMarker);
	}

	FString PresetIdOf(const FString& Url)
	{
		if (!IsPresetUrl(Url))
		{
			return FString();
		}
		FString Tail = Url;
		int32 At = INDEX_NONE;
		if (Tail.FindLastChar(TEXT('/'), At))
		{
			Tail.MidInline(At + 1);
		}
		// "07?v=2" / "07#x" -> "07"
		if (Tail.FindChar(TEXT('?'), At))
		{
			Tail.LeftInline(At);
		}
		if (Tail.FindChar(TEXT('#'), At))
		{
			Tail.LeftInline(At);
		}
		return Tail;
	}

	bool EncodeJpeg(const TArray<uint8>& AnyImage, int32 MaxPixels, int32 Quality, TArray<uint8>& OutJpeg)
	{
		OutJpeg.Reset();
		if (AnyImage.Num() == 0)
		{
			return false;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const EImageFormat Format = Module.DetectImageFormat(AnyImage.GetData(), AnyImage.Num());
		if (Format == EImageFormat::Invalid)
		{
			return false;
		}
		const TSharedPtr<IImageWrapper> In = Module.CreateImageWrapper(Format);
		if (!In.IsValid() || !In->SetCompressed(AnyImage.GetData(), AnyImage.Num()))
		{
			return false;
		}
		TArray64<uint8> Raw;
		if (!In->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return false;
		}
		const int32 SrcW = In->GetWidth();
		const int32 SrcH = In->GetHeight();
		if (SrcW <= 0 || SrcH <= 0 || Raw.Num() < int64(SrcW) * SrcH * 4)
		{
			return false;
		}

		const int32 Longest = FMath::Max(SrcW, SrcH);
		const float Scale = Longest > MaxPixels ? float(MaxPixels) / float(Longest) : 1.f;
		const int32 DstW = FMath::Max(1, FMath::FloorToInt(SrcW * Scale));
		const int32 DstH = FMath::Max(1, FMath::FloorToInt(SrcH * Scale));

		TArray<uint8> Small;
		const uint8* Src = Raw.GetData();
		if (DstW != SrcW || DstH != SrcH)
		{
			// Box average, not point sampling: a 12 MP phone photo point-sampled to 512 is a field of
			// aliased noise, and this runs once per upload on the editor path only.
			Small.SetNumUninitialized(int64(DstW) * DstH * 4);
			for (int32 Y = 0; Y < DstH; ++Y)
			{
				const int32 Y0 = (Y * SrcH) / DstH;
				const int32 Y1 = FMath::Max(Y0 + 1, ((Y + 1) * SrcH) / DstH);
				for (int32 X = 0; X < DstW; ++X)
				{
					const int32 X0 = (X * SrcW) / DstW;
					const int32 X1 = FMath::Max(X0 + 1, ((X + 1) * SrcW) / DstW);
					int32 Sum[4] = { 0, 0, 0, 0 };
					int32 Count = 0;
					for (int32 Sy = Y0; Sy < Y1; ++Sy)
					{
						const uint8* Row = Src + (int64(Sy) * SrcW + X0) * 4;
						for (int32 Sx = X0; Sx < X1; ++Sx, Row += 4)
						{
							Sum[0] += Row[0]; Sum[1] += Row[1]; Sum[2] += Row[2]; Sum[3] += Row[3];
							++Count;
						}
					}
					uint8* Out = Small.GetData() + (int64(Y) * DstW + X) * 4;
					Out[0] = uint8(Sum[0] / Count);
					Out[1] = uint8(Sum[1] / Count);
					Out[2] = uint8(Sum[2] / Count);
					Out[3] = 255;
				}
			}
		}

		const TSharedPtr<IImageWrapper> Jpeg = Module.CreateImageWrapper(EImageFormat::JPEG);
		if (!Jpeg.IsValid())
		{
			return false;
		}
		const void* Pixels = Small.Num() > 0 ? (const void*)Small.GetData() : (const void*)Src;
		const int64 PixelBytes = int64(DstW) * DstH * 4;
		if (!Jpeg->SetRaw(Pixels, PixelBytes, DstW, DstH, ERGBFormat::BGRA, 8))
		{
			return false;
		}
		const TArray64<uint8> Compressed = Jpeg->GetCompressed(Quality);
		if (Compressed.Num() == 0)
		{
			return false;
		}
		OutJpeg.Append(Compressed.GetData(), Compressed.Num());
		return true;
	}
}

// ---------------------------------------------------------------------------------------------
// SCampusAvatar

void SCampusAvatar::Construct(const FArguments& InArgs)
{
	Url = InArgs._Url;
	Size = InArgs._Size;
	Shape = InArgs._Shape;
	Fill = InArgs._Fill;
	Border = InArgs._Border;
	BorderWidth = InArgs._BorderWidth;
	Tint = InArgs._Tint;

	// The fallback is the caller's own face (the level number, the colour initial). It collapses the
	// moment the picture is ready - an attribute, so no rebuild is needed when one lands mid-session.
	TAttribute<FString> UrlCopy = Url;
	ChildSlot
	[
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Visibility(TAttribute<EVisibility>::CreateLambda([UrlCopy]()
		{
			return CampusAvatar::StateOf(UrlCopy.Get(FString())) == CampusAvatar::EState::Ready
				? EVisibility::Collapsed
				: EVisibility::HitTestInvisible;
		}))
		[
			InArgs._Fallback.Widget
		]
	];
}

int32 SCampusAvatar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Extent = AllottedGeometry.GetLocalSize();
	const FVector2f Zero = FVector2f::ZeroVector;
	// ONE lookup per paint, and after the first it is a TMap find: this is also what starts the single
	// fetch for this URL, exactly like CampusMapArt::Get does for the map banners.
	const CampusAvatar::FImage& Picture = CampusAvatar::Get(Url.Get(FString()));

	if (Picture.IsReady())
	{
		CampusAngular::DrawShapedImage(OutDrawElements, LayerId, AllottedGeometry, Zero, Extent, Shape,
			Picture.Handle, Picture.Size, Tint.Get(FLinearColor::White), 0.42f);
	}
	else
	{
		CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId, AllottedGeometry, Zero, Extent, Shape,
			Fill.Get(CampusAngular::Ink));
	}

	const FLinearColor Edge = Border.Get(FLinearColor::Transparent);
	if (Edge.A > 0.f && BorderWidth > 0.f)
	{
		CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId + 1, AllottedGeometry, Zero, Extent, Shape,
			FLinearColor::Transparent, Edge, BorderWidth);
	}
	if (Picture.State == CampusAvatar::EState::Loading)
	{
		// The honest loading state: a 3px accent bar along the bottom of the plate while the picture is
		// on its way. No spinner widget, no Tick, nothing to tear down when it arrives.
		const float Bar = FMath::Min(3.f, Extent.Y);
		CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId + 2, AllottedGeometry,
			FVector2f(0.f, Extent.Y - Bar), FVector2f(Extent.X, Bar), FCampusShape(),
			CampusAngular::Accent.CopyWithNewOpacity(0.85f));
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 3, InWidgetStyle, bParentEnabled);
}
