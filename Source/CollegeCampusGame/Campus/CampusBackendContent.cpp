// THE ART THE CONSOLE CAN CHANGE WITHOUT A RELEASE (UCampusBackend): blob key `content`
// (PHASE_B_CONTRACT B2, ADMIN_CONSOLE_DESIGN "Game integration" 2).
//
//   `content` : { "base_url": "/v1/content", "assets": { "<key>": <version> } }
//   the bytes : GET <base_url>/<key>?v=<version>   player bearer, strong ETag, immutable for a year
//
// THIS FILE IS THE INDEX AND NOTHING ELSE. It answers two questions - "is there published art for
// this key, and at which version" and "what is its URL" - and Campus/CampusRemoteImage.cpp does the
// fetching, decoding and caching. A key the blob does not name has NO url and is never requested:
// the console has published nothing for it, so the caller keeps the picture that shipped in the APK.
//
// WHY THE VERSION IS LOAD-BEARING. The bytes behind one `?v=` can never change, so one version is one
// immutable URL and one file in the disk cache. That is what makes a lobby of banners free on a tunnel
// metered at 20,000 requests a month - and it is why a new upload must bump the version rather than
// replace bytes in place, which is exactly what migration 026 does.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Campus/CampusRemoteImage.h"

using namespace CampusApi;

/** The base path the console publishes. Kept as the fallback so a blob that omits it still resolves. */
static const TCHAR* GContentDefaultBase = TEXT("/v1/content");

void UCampusBackend::ReadContentKey(const TSharedPtr<FJsonObject>& Key, FCampusContentIndex& Out)
{
	Out = FCampusContentIndex();
	if (!Key.IsValid())
	{
		return;
	}
	Out.BaseUrl = Str(Key, TEXT("base_url"));
	if (Out.BaseUrl.IsEmpty())
	{
		Out.BaseUrl = GContentDefaultBase;
	}
	Out.BaseUrl.RemoveFromEnd(TEXT("/"));
	const TSharedPtr<FJsonObject> Assets = Obj(Key, TEXT("assets"));
	if (!Assets.IsValid())
	{
		return;
	}
	for (const auto& Pair : Assets->Values)
	{
		// FString(*Pair.Key) AND NOT Pair.Key, and `auto` AND NOT a spelled-out TPair: the key type of
		// FJsonObject::Values is a plain FString on MSVC and a TSharedString on the Android toolchain,
		// and this is the one spelling both accept (commit c147cb2, and the note in
		// CampusBackendCosmetics.cpp that it cost us once already). Do not "simplify" it.
		const FString Key2(*Pair.Key);
		double Version = 0.0;
		if (Key2.IsEmpty() || !Pair.Value.IsValid() || !Pair.Value->TryGetNumber(Version) || Version < 1.0)
		{
			// A key with no usable version is a key with no bytes behind it: dropped here rather than
			// turned into a request for "?v=0", which the API answers 404 to.
			continue;
		}
		Out.Versions.Add(Key2, int32(Version));
	}
	Out.bLoaded = Out.Versions.Num() > 0;
}

int32 UCampusBackend::ContentVersionOf(const FString& Key) const
{
	if (Key.IsEmpty())
	{
		return 0;
	}
	const int32* Found = LiveConfig.Content.Versions.Find(Key);
	return Found ? *Found : 0;
}

FString UCampusBackend::ContentUrlFor(const FString& Key) const
{
	const int32 Version = ContentVersionOf(Key);
	if (Version <= 0)
	{
		return FString();
	}
	const FString Base = LiveConfig.Content.BaseUrl.IsEmpty() ? FString(GContentDefaultBase) : LiveConfig.Content.BaseUrl;
	// Origin-relative in, absolute out: ResolveUrl is the one place that knows the API base and the
	// "/v1" prefix rule (it is the same resolver every avatar URL goes through).
	return CampusRemoteImage::ResolveUrl(FString::Printf(TEXT("%s/%s?v=%d"), *Base, *Key, Version));
}
