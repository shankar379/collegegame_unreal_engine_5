// REMOTE 2D ART the admin console can change without a release (ADMIN_CONSOLE_DESIGN "Game
// integration" 2, PHASE_B_CONTRACT B2): map banners, store tiles, event banners.
//
// THE ONE PICTURE PIPELINE IN THIS GAME, generalised out of Campus/CampusAvatar.h, which now reads
// its session, its URL resolver and its decoder from here:
//
//   UseSession / Session   the API base and the bearer every fetch borrows. Set once by the lobby.
//   ResolveUrl             "/v1/content/x?v=3", "/avatars/07" or an absolute URL -> absolute URL.
//   DecodeTexture          any image bytes (PNG / JPEG / ...) -> a rooted transient UTexture2D.
//   Get                    the picture for a PUBLISHED CONTENT KEY, fetched once per version.
//
// KEYED BY key + VERSION, which is the whole of the cache-invalidation story: the published `content`
// blob says which version of `store/gun_m4a1` is current, the bytes behind one version are immutable
// (routes/content.js answers `Cache-Control: immutable`), so a new upload is a new key@version, a new
// URL and a new file in the disk cache - and the old entry is simply never asked for again. A key the
// blob does not name is NEVER fetched: the console has published no art for it and the caller keeps
// the picture that shipped in the APK.
//
// NOTHING HERE POLLS, NOTHING BLOCKS AND NOTHING RETRIES. The API rides a free ngrok tunnel metered
// at 20,000 requests a MONTH (CollegeCampusGame.Build.cs), so one asset costs at most one request per
// version per device - and zero when yesterday's launch already put it in Saved/ContentCache.
//
// NEVER NULL. An entry that is loading or has failed reports !IsReady(), and every caller keeps its
// own fallback (SCampusArtPlate paints the packaged PNG, the store tile paints its silhouette).

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

class UCampusBackend;
class UTexture2D;

namespace CampusRemoteImage
{
	enum class EState : uint8 { None, Loading, Ready, Failed };

	/** A decoded picture: what to draw with, and the pixel size the "cover" crop needs. */
	struct FImage
	{
		EState State = EState::None;
		FSlateResourceHandle Handle;
		FVector2f Size = FVector2f::ZeroVector;

		bool IsReady() const { return State == EState::Ready && Handle.IsValid() && Size.X > 0.f; }
	};

	/** The session every fetch borrows its base URL and bearer from. Called once by the lobby widget. */
	void UseSession(UCampusBackend* Api);
	/** That session, for the few callers that send a request of their own (CampusAvatar). */
	UCampusBackend* Session();

	/** An absolute URL for an API path, which may arrive absolute, origin-relative ("/v1/content/x")
	 *  or base-relative ("/avatars/07"). Empty before the lobby has handed over the session. */
	FString ResolveUrl(const FString& PathOrUrl);

	/** Any image bytes -> a transient UI texture, ROOTED (these outlive every widget that draws them
	 *  and must not run GC code from a static destructor at shutdown). Null on anything unreadable. */
	UTexture2D* DecodeTexture(const uint8* Bytes, int64 Num);

	/** The picture for a published content key ("map/campusmap", "store/gun_m4a1", "event/diwali"),
	 *  starting the ONE fetch for its current version if nothing has asked yet. Never blocks, never
	 *  null: a key the `content` blob does not name, one still loading and one that failed all report
	 *  !IsReady() and the caller keeps its own fallback. Safe to call from OnPaint - after the first
	 *  call it is a TMap lookup. */
	const FImage& Get(const FString& Key);

	/** Where this key stands WITHOUT starting anything, for visibility lambdas. */
	EState StateOf(const FString& Key);

	/** Drops every decoded picture whose version is no longer the published one. Called when a new
	 *  blob lands, so a re-upload does not keep the old bytes alive for the rest of the app run. */
	void ForgetStale();

	/** A download bigger than this is treated as a failure rather than decoded. The console caps an
	 *  upload at 2 MB and re-encodes it (PHASE_B_CONTRACT B2); this is the client's own backstop. */
	constexpr int32 MaxDownloadBytes = 4 * 1024 * 1024;
}
