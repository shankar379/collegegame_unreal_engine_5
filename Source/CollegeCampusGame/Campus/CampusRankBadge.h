// The college rank badge (backend migration 035): one drawn plate per tier - its colour and two letters -
// used on the mode screen's Ranked card, the lobby plate and anywhere a rank is shown. Drawn, not textured:
// a tier added on the server shows up as a neutral plate instead of a missing image.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

namespace CampusRank
{
	/** The tier's colour ("fresher" silver ... "campus_legend" pink). Unknown = neutral steel. */
	FLinearColor TierColor(const FString& TierId);
	/** Two letters for the plate: FR, SO, JR, SR, TP, DL, GM, CL. */
	FText TierShort(const FString& TierId);
	/** A square badge of Size px: the tier colour, the two letters, a light frame. */
	TSharedRef<SWidget> Badge(const FString& TierId, float Size);
}
