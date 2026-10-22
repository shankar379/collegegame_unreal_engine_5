#include "Campus/CampusRankBadge.h"

#include "Campus/CampusAngular.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace CampusRank
{
	FLinearColor TierColor(const FString& TierId)
	{
		static const TMap<FString, FLinearColor> Colors = {
			{ TEXT("fresher"),       FLinearColor(0.73f, 0.76f, 0.80f) },
			{ TEXT("sophomore"),     FLinearColor(0.62f, 0.83f, 1.00f) },
			{ TEXT("junior"),        FLinearColor(0.48f, 0.88f, 0.71f) },
			{ TEXT("senior"),        FLinearColor(0.95f, 0.72f, 0.24f) },
			{ TEXT("topper"),        FLinearColor(1.00f, 0.60f, 0.35f) },
			{ TEXT("deans_list"),    FLinearColor(0.84f, 0.55f, 1.00f) },
			{ TEXT("gold_medalist"), FLinearColor(1.00f, 0.85f, 0.42f) },
			{ TEXT("campus_legend"), FLinearColor(1.00f, 0.44f, 0.78f) },
		};
		const FLinearColor* Found = Colors.Find(TierId);
		return Found ? *Found : FLinearColor(0.62f, 0.68f, 0.78f);
	}

	FText TierShort(const FString& TierId)
	{
		static const TMap<FString, FString> Short = {
			{ TEXT("fresher"), TEXT("FR") }, { TEXT("sophomore"), TEXT("SO") }, { TEXT("junior"), TEXT("JR") },
			{ TEXT("senior"), TEXT("SR") }, { TEXT("topper"), TEXT("TP") }, { TEXT("deans_list"), TEXT("DL") },
			{ TEXT("gold_medalist"), TEXT("GM") }, { TEXT("campus_legend"), TEXT("CL") },
		};
		const FString* Found = Short.Find(TierId);
		return FText::FromString(Found ? *Found : TEXT("?"));
	}

	TSharedRef<SWidget> Badge(const FString& TierId, float Size)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Notched(Size * 0.22f))
				.Fill(TierColor(TierId))
				.Border(FLinearColor(1.f, 1.f, 1.f, 0.75f))
				.BorderWidth(2.f)
				[
					SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(TierShort(TierId))
						.Font(CampusAngular::Display(FMath::Max(10, FMath::RoundToInt(Size * 0.36f))))
						.ColorAndOpacity(FLinearColor(0.06f, 0.05f, 0.04f))
					]
				]
			];
	}
}
