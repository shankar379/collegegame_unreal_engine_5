// PROFILE PICTURES - the one place that knows the avatar contract, the one cache that holds the
// decoded pictures, and the one widget that paints them.
//
//   * CampusAvatarApi - the backend contract in ONE struct of constants. The server side of this is
//                       being built in parallel, so when a path or a field name moves, it moves here
//                       and nowhere else.
//   * CampusAvatar    - resolve a (possibly relative) avatar_url against the API base, fetch it once
//                       through FHttpModule with the SAME headers CampusBackend uses (Bearer +
//                       ngrok-skip-browser-warning), decode it with IImageWrapper into a transient
//                       UTexture2D, and hand it out as a Slate resource handle. Memory cache keyed by
//                       the resolved URL, plus a small on-disk cache under Saved/AvatarCache - the API
//                       rides a free ngrok tunnel metered at 20,000 requests a MONTH (see
//                       CollegeCampusGame.Build.cs), and a lobby that re-downloads twenty friend
//                       pictures on every launch would eat that by itself.
//   * SCampusAvatar   - the picture in an FCampusShape, with the EXISTING fallback (the level number
//                       in the lobby badge, the colour + initial in the social UI) as its child. The
//                       fallback simply collapses when the picture is ready, so an avatar that lands
//                       mid-session needs no widget rebuilt.
//
// NOTHING here polls and nothing re-fetches per frame: one request per URL per app run (or zero, when
// the disk cache already has it). The URL carries the version (?v=<n>), so a new upload is a new URL
// and the old entry is simply never asked for again.

#pragma once

#include "CoreMinimal.h"
#include "Campus/CampusAngular.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UCampusBackend;

/**
 * THE CONTRACT. Paths are relative to UCampusBackend::ApiBaseUrl(), which already ends in "/v1".
 *
 *   PUT    <base>/me/avatar          raw image/jpeg body, Bearer   -> { "avatar_url": "<url>" }
 *   DELETE <base>/me/avatar          Bearer    -> 204 / 200, and the account falls back to a RANDOM PRESET
 *   PUT    <base>/me/avatar/preset   { "preset": "07" }           -> { "avatar_url": "<url>" }
 *   GET    <base>/avatars/presets    -> [ { "id": "07", "url": "/v1/avatars/presets/07" }, ... ]
 *   GET    <origin>/v1/avatars/presets/:id  -> image bytes (PNG or JPEG)
 *   GET    <origin>/v1/avatars/:playerId?v=<n>  -> image bytes
 *
 * EVERY account has an avatar_url: the server hands out one of 45 presets at random on creation, so
 * the number / initial fallback is only ever seen while a picture is loading or offline.
 *
 * avatar_url may come back relative ("/v1/avatars/<id>?v=3") or absolute; CampusAvatar::Resolve
 * handles both. Every request carries ngrok-skip-browser-warning, exactly as UCampusBackend::Send does.
 */
namespace CampusAvatarApi
{
	/** PUT raw JPEG here; DELETE the same path to fall back to a random preset. */
	inline const TCHAR* MeAvatarPath = TEXT("/me/avatar");
	/** PUT { "preset": "<id>" } here to wear one of the shipped icons. */
	inline const TCHAR* MePresetPath = TEXT("/me/avatar/preset");
	/** The body field that names the chosen preset. */
	inline const TCHAR* PresetField = TEXT("preset");
	/** GET the catalogue of preset icons (a bare JSON array). */
	inline const TCHAR* PresetsPath = TEXT("/avatars/presets");
	/** Any URL containing this is a preset rather than an uploaded photo (REMOVE PHOTO keys off it). */
	inline const TCHAR* PresetMarker = TEXT("/avatars/presets/");
	/** The field the upload reply and every profile / card payload carries. */
	inline const TCHAR* UrlField = TEXT("avatar_url");
	/** GET <this> + playerId when a payload gave us an id but no URL. */
	inline const TCHAR* ByPlayerPath = TEXT("/avatars/");
	inline const TCHAR* UploadContentType = TEXT("image/jpeg");
	/** The ngrok interstitial killer. The same header UCampusBackend::Send sets on every call. */
	inline const TCHAR* TunnelHeader = TEXT("ngrok-skip-browser-warning");

	/** Longest edge of an uploaded picture. The Java side downscales to this before compressing. */
	constexpr int32 MaxPixels = 512;
	/** JPEG quality the picker compresses at. */
	constexpr int32 JpegQuality = 85;
	/** Refuse to upload more than this (a 512 px JPEG at q85 is ~60 KB; 1 MB means something is wrong). */
	constexpr int32 MaxUploadBytes = 1024 * 1024;
	/** A downloaded avatar bigger than this is treated as a failure rather than decoded. */
	constexpr int32 MaxDownloadBytes = 4 * 1024 * 1024;
}

namespace CampusAvatar
{
	enum class EState : uint8 { None, Loading, Ready, Failed };

	/** A decoded avatar: what to draw with, and the pixel size the "cover" crop needs. */
	struct FImage
	{
		EState State = EState::None;
		FSlateResourceHandle Handle;
		FVector2f Size = FVector2f::ZeroVector;

		bool IsReady() const { return State == EState::Ready && Handle.IsValid() && Size.X > 0.f; }
	};

	/** The session every fetch borrows its base URL and bearer from. Called once by the lobby widget. */
	void UseSession(UCampusBackend* Api);

	/** An absolute URL for a payload's avatar_url (absolute, "/v1/..." or "/avatars/..."), or empty. */
	FString Resolve(const FString& Url);

	/** The picture for this URL, starting the ONE fetch for it if nothing has asked yet. Never blocks,
	 *  never null: an entry that is still loading or has failed reports !IsReady() and the caller keeps
	 *  its own fallback. Safe to call from OnPaint - after the first call it is a TMap lookup. */
	const FImage& Get(const FString& Url);

	/** Where this URL stands WITHOUT starting anything: for visibility lambdas and region stamps. */
	EState StateOf(const FString& Url);

	/** url + state, for RepaintLobby's FillRegion fingerprints: the region repaints exactly once, when
	 *  the picture lands (or fails), and never because some OTHER player's avatar arrived. */
	uint32 Fingerprint(const FString& Url);

	/** Drops one entry from memory and from the disk cache (REMOVE PHOTO, or a failed URL being retried). */
	void Forget(const FString& Url);

	/** PUT the JPEG. Done gets the new avatar_url, which is already in the cache when it fires - the
	 *  bytes we just uploaded ARE the picture, so the badge never waits on a download of its own. */
	void Upload(UCampusBackend* Api, const TArray<uint8>& Jpeg,
		TFunction<void(bool bOk, const FString& NewUrl, const FString& Error)> Done);

	/** DELETE the picture. The server then puts a RANDOM PRESET on the account, and hands the new
	 *  avatar_url back when it says so; NewUrl is empty on a server that answers a bare 204. */
	void Remove(UCampusBackend* Api, TFunction<void(bool bOk, const FString& NewUrl, const FString& Error)> Done);

	// ---- presets: 45 shipped icons, one of which every new account already wears.

	/** One row of GET /avatars/presets. */
	struct FPreset
	{
		FString Id;
		FString Url;
	};

	/** The catalogue, once per app run (it is static art behind a metered tunnel). Empty until loaded. */
	const TArray<FPreset>& Presets();
	bool ArePresetsLoaded();
	bool ArePresetsLoading();
	/** GET the catalogue. Harmless to call again: a second call while one is in flight is dropped. */
	void LoadPresets(UCampusBackend* Api, TFunction<void(bool bOk, const FString& Error)> Done);
	/** PUT { "preset": Id }. Done gets the server's new avatar_url. */
	void SetPreset(UCampusBackend* Api, const FString& PresetId,
		TFunction<void(bool bOk, const FString& NewUrl, const FString& Error)> Done);
	/** True when this avatar_url is one of the presets rather than an uploaded photo. */
	bool IsPresetUrl(const FString& Url);
	/** "07" for a preset URL (query stripped), empty for an upload. */
	FString PresetIdOf(const FString& Url);

	/** True while an upload or a delete is in flight (the EDIT sheet disables both buttons). */
	bool IsBusy();

	/** Any image bytes (JPEG / PNG / ...) -> a JPEG whose longest edge is MaxPixels, at Quality.
	 *  The ANDROID path never needs this - Java downscales and compresses before the bytes ever reach
	 *  C++ - but the editor's file-dialog path does, and so would any future "take a photo" source. */
	bool EncodeJpeg(const TArray<uint8>& AnyImage, int32 MaxPixels, int32 Quality, TArray<uint8>& OutJpeg);
}

/**
 * One avatar plate: the picture when there is one, the caller's own fallback when there is not.
 *
 * The fallback is the DEFAULT SLOT, so each caller keeps the face it already had - the lobby badge
 * keeps its level number, the social rows keep the colour + initial - and this widget decides only
 * which of the two is visible. That visibility is an attribute, and the picture is looked up in
 * OnPaint, so a picture that arrives after the widget was built needs no rebuild at all.
 */
class SCampusAvatar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusAvatar)
		: _Size(54.f)
		, _Shape(FCampusShape())
		, _Fill(CampusAngular::Ink)
		, _Border(CampusAngular::Hairline)
		, _BorderWidth(1.f)
		, _Tint(FLinearColor::White)
		{}
		/** The payload's avatar_url, relative or absolute. Empty = fallback only, no request. */
		SLATE_ATTRIBUTE(FString, Url)
		SLATE_ARGUMENT(float, Size)
		SLATE_ARGUMENT(FCampusShape, Shape)
		/** Painted behind the fallback; never seen once the picture is up. */
		SLATE_ATTRIBUTE(FLinearColor, Fill)
		SLATE_ATTRIBUTE(FLinearColor, Border)
		SLATE_ARGUMENT(float, BorderWidth)
		/** Multiplies the picture (dim an unaccepted squad plate). */
		SLATE_ATTRIBUTE(FLinearColor, Tint)
		SLATE_DEFAULT_SLOT(FArguments, Fallback)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** ALWAYS Size x Size. The fallback child COLLAPSES when the picture lands, and without this the
	 *  plate would collapse with it anywhere a caller did not wrap it in a sized SBox - which is a
	 *  picture that vanishes at the exact moment it finishes loading. */
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(Size, Size); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TAttribute<FString> Url;
	TAttribute<FLinearColor> Fill;
	TAttribute<FLinearColor> Border;
	TAttribute<FLinearColor> Tint;
	FCampusShape Shape;
	float Size = 54.f;
	float BorderWidth = 1.f;
};
