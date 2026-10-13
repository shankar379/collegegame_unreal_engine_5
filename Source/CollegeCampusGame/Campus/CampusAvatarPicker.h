// THE GALLERY PICKER. One function: hand me JPEG bytes the player chose, or tell me they did not.
//
// Android (the real path, Java in CampusCombat_UPL.xml):
//   MediaStore.ACTION_PICK_IMAGES - the system PHOTO PICKER - wherever it exists (Android 13+, and
//   Android 11/12 through the R SDK extension), otherwise ACTION_GET_CONTENT. BOTH are user-mediated
//   pickers that hand back a single grant for one item, so NEITHER needs a storage permission:
//   READ_MEDIA_IMAGES / READ_EXTERNAL_STORAGE are deliberately NOT in the manifest, and must not be
//   added - Play's photo-and-video-permissions policy would then want a justification we do not need.
//   ACTION_PICK on MediaStore.EXTERNAL_CONTENT_URI is the trap here: it DOES need READ_EXTERNAL_STORAGE
//   on API <= 32, so it is not used.
//   The Java side reads the stream, respects the EXIF rotation, downscales to 512 px, compresses to
//   JPEG q85 and calls nativeCampusAvatarPicked(byte[]) - the phone does the pixel work, never C++.
//
// Windows editor: an ordinary open-file dialog, then CampusAvatar::EncodeJpeg does the downscale, so
// the whole upload / display path can be tested without a phone.
// Anything else: Pick reports false and the UI says photos are not available on this device.
//
// The callback ALWAYS arrives on the GAME THREAD, exactly once per Pick (cancel included).

#pragma once

#include "CoreMinimal.h"

namespace CampusAvatarPicker
{
	/** bOk = the player chose a picture and it compressed; Jpeg is then ready to PUT as image/jpeg. */
	using FPicked = TFunction<void(bool bOk, TArray<uint8> Jpeg)>;

	/** True where a picker exists (Android, and the Windows editor). */
	bool IsAvailable();

	/** Opens the picker. Exactly one callback follows. A second call while one is open is refused
	 *  (bOk = false) rather than queued - two pickers over one activity is a lost callback. */
	void Pick(FPicked OnPicked);

	/** Why the last Pick came back false. EMPTY means the player simply backed out of the picker,
	 *  which is not an error and must not be toasted; anything else is worth showing. */
	const FString& LastError();
}
