// The store screen, Free Fire shaped: a left tab rail (DIAMONDS / GUNS / OFFERS), a wallet strip across the
// top, and a wrapping grid of angular cards.
//
// ONE THING IS SOLD FOR MONEY: DIAMONDS (backend migration 030). Everything else - guns, offers, Room Cards -
// is bought with Diamonds. Two currencies, two completely different buy paths, and the screen never blurs them:
//   * DIAMONDS are bought with real money through Google Play. The price on a pack card is always Play's own
//     localized PriceText (QueryProducts), never a number we worked out here. Where billing is not usable -
//     a sideloaded APK, an emulator, a desktop PIE session - the card says so in words instead of offering a
//     button that cannot work.
//   * GUNS are bought with Diamonds through the backend (POST /v1/store/items/:id/buy via BuyGun). A gun the
//     player cannot afford does not show a dead BUY: it shows BUY DIAMONDS and jumps to the diamonds tab,
//     which is the only useful thing that tap could do.
//
// TILE ART COMES FROM THE CONSOLE WHEN THERE IS ANY (PHASE_B_CONTRACT B2): an item whose `image_key` names a
// published picture shows it, and the drawn silhouette below is what every other item still shows. The two
// layers are built together and swapped by VISIBILITY, so an uploaded picture appears the moment its bytes
// land and no tile is rebuilt to make that happen.
//
// Gun art is drawn, not textured: a per-class angular silhouette assembled from the same FCampusShape painter
// the rest of the UI uses (6-8 draw elements, no texture to fail to load, no scene capture). Attribute bars
// read from FCampusGun's stats, and the skin bonuses the backend sends per gun ("fire_rate:+20") are parsed
// out of its perk lines, so this screen already paints them and will keep painting them when they arrive as a
// dedicated field - green for +, red for -, with the sign spelled out so colour is never the only signal.
//
// Lives in its own file because CampusLobbyUI.cpp is 115k and this screen is a third of a screen's worth of
// layout on its own. Every symbol here is prefixed CampusStoreV2 / SCampusStore so the unity build stays clean.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusPlayBilling.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusRemoteImage.h"
#include "Engine/GameInstance.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusStoreUI"

using namespace CampusStyle;

namespace CampusStoreV2
{
	// ---- tokens. Borrowed from CampusAngular; nothing new invented. --------------------------------
	static const FLinearColor CardFill(0.035f, 0.045f, 0.065f, 0.94f);
	static const FLinearColor CardFillTop(0.075f, 0.095f, 0.130f, 0.94f);
	static const FLinearColor RailFill(0.020f, 0.028f, 0.040f, 0.92f);
	static const FLinearColor Gem(0.38f, 0.78f, 1.00f, 1.f);
	static const FLinearColor GemDeep(0.10f, 0.26f, 0.42f, 1.f);
	static const FLinearColor Gold(1.00f, 0.72f, 0.12f, 1.f);
	static const FLinearColor Plus(0.30f, 0.86f, 0.42f, 1.f);
	static const FLinearColor Minus(0.95f, 0.33f, 0.33f, 1.f);
	static const FLinearColor Track(1.f, 1.f, 1.f, 0.13f);
	static const FLinearColor Steel(0.62f, 0.68f, 0.78f, 1.f);
	static const FLinearColor SteelDark(0.34f, 0.39f, 0.47f, 1.f);

	static const float TileWidth = 300.f;
	static const float ArtHeight = 86.f;

	/** Which silhouette to assemble. Guessed from the gun's name, because the catalogue has no class field
	 *  yet; an unknown name gets the rifle, which is the least wrong answer. */
	enum class EClass : uint8 { Rifle, Smg, Sniper, Shotgun, Pistol, Lmg };

	static EClass ClassOf(const FString& InName)
	{
		const FString N = InName.ToUpper();
		auto Has = [&N](const TCHAR* Key) { return N.Contains(Key); };
		if (Has(TEXT("AWM")) || Has(TEXT("SNIP")) || Has(TEXT("KAR")) || Has(TEXT("M82")) || Has(TEXT("DRAG"))) { return EClass::Sniper; }
		if (Has(TEXT("SHOT")) || Has(TEXT("SPAS")) || Has(TEXT("M1014")) || Has(TEXT("M870")) || Has(TEXT("GAUGE"))) { return EClass::Shotgun; }
		if (Has(TEXT("PISTOL")) || Has(TEXT("DESERT")) || Has(TEXT("G18")) || Has(TEXT("M500")) || Has(TEXT("USP"))) { return EClass::Pistol; }
		if (Has(TEXT("LMG")) || Has(TEXT("M249")) || Has(TEXT("GATL")) || Has(TEXT("MG"))) { return EClass::Lmg; }
		if (Has(TEXT("SMG")) || Has(TEXT("MP5")) || Has(TEXT("MP40")) || Has(TEXT("UMP")) || Has(TEXT("VECTOR")) || Has(TEXT("UZI"))) { return EClass::Smg; }
		return EClass::Rifle;
	}

	static FText ClassLabel(EClass Class)
	{
		switch (Class)
		{
		case EClass::Smg:     return LOCTEXT("ClsSmg", "SMG");
		case EClass::Sniper:  return LOCTEXT("ClsSniper", "SNIPER");
		case EClass::Shotgun: return LOCTEXT("ClsShotgun", "SHOTGUN");
		case EClass::Pistol:  return LOCTEXT("ClsPistol", "PISTOL");
		case EClass::Lmg:     return LOCTEXT("ClsLmg", "LMG");
		default:              return LOCTEXT("ClsRifle", "ASSAULT RIFLE");
		}
	}

	/** One slab of the silhouette, in 0..1 of the art box. */
	struct FPart
	{
		float X, Y, W, H;
		float Slant;
		float Cut;
		/** 0 = dark body, 1 = bright highlight (barrel, scope glass). */
		uint8 Tone;
	};

	/** Assembled gun shapes. Deliberately blocky: this is a glyph, not a render. */
	static void PartsFor(EClass Class, TArray<FPart>& Out)
	{
		Out.Reset();
		switch (Class)
		{
		case EClass::Sniper:
			Out.Add({ 0.06f, 0.46f, 0.30f, 0.16f,  6.f, 4.f, 0 });   // stock
			Out.Add({ 0.30f, 0.38f, 0.34f, 0.26f,  0.f, 5.f, 0 });   // receiver
			Out.Add({ 0.36f, 0.20f, 0.26f, 0.12f,  4.f, 3.f, 1 });   // scope
			Out.Add({ 0.62f, 0.44f, 0.34f, 0.09f,  0.f, 2.f, 1 });   // long barrel
			Out.Add({ 0.90f, 0.40f, 0.07f, 0.17f,  3.f, 3.f, 1 });   // muzzle brake
			Out.Add({ 0.40f, 0.62f, 0.09f, 0.22f, -5.f, 3.f, 0 });   // grip
			break;
		case EClass::Shotgun:
			Out.Add({ 0.05f, 0.44f, 0.26f, 0.20f,  7.f, 5.f, 0 });   // stock
			Out.Add({ 0.28f, 0.40f, 0.26f, 0.24f,  0.f, 4.f, 0 });   // receiver
			Out.Add({ 0.52f, 0.42f, 0.42f, 0.13f,  0.f, 3.f, 1 });   // twin barrel
			Out.Add({ 0.52f, 0.58f, 0.34f, 0.08f,  0.f, 3.f, 0 });   // pump
			Out.Add({ 0.37f, 0.63f, 0.09f, 0.22f, -5.f, 3.f, 0 });   // grip
			break;
		case EClass::Pistol:
			Out.Add({ 0.30f, 0.34f, 0.40f, 0.18f,  0.f, 4.f, 0 });   // slide
			Out.Add({ 0.66f, 0.38f, 0.10f, 0.09f,  0.f, 2.f, 1 });   // muzzle
			Out.Add({ 0.32f, 0.51f, 0.17f, 0.30f,-14.f, 4.f, 0 });   // grip
			Out.Add({ 0.47f, 0.50f, 0.16f, 0.07f,  0.f, 2.f, 0 });   // trigger guard
			break;
		case EClass::Lmg:
			Out.Add({ 0.04f, 0.44f, 0.24f, 0.20f,  6.f, 4.f, 0 });   // stock
			Out.Add({ 0.26f, 0.34f, 0.36f, 0.30f,  0.f, 5.f, 0 });   // big receiver
			Out.Add({ 0.60f, 0.42f, 0.32f, 0.11f,  0.f, 3.f, 1 });   // heavy barrel
			Out.Add({ 0.68f, 0.38f, 0.14f, 0.05f,  0.f, 0.f, 1 });   // heat shroud
			Out.Add({ 0.34f, 0.63f, 0.22f, 0.26f,  0.f, 4.f, 0 });   // box magazine
			Out.Add({ 0.58f, 0.60f, 0.08f, 0.18f, -4.f, 2.f, 0 });   // bipod leg
			break;
		case EClass::Smg:
			Out.Add({ 0.10f, 0.44f, 0.20f, 0.16f,  8.f, 4.f, 0 });   // folding stock
			Out.Add({ 0.28f, 0.38f, 0.30f, 0.24f,  0.f, 4.f, 0 });   // receiver
			Out.Add({ 0.56f, 0.43f, 0.22f, 0.10f,  0.f, 2.f, 1 });   // short barrel
			Out.Add({ 0.33f, 0.61f, 0.12f, 0.30f, -9.f, 3.f, 0 });   // long magazine
			Out.Add({ 0.49f, 0.60f, 0.08f, 0.16f, -4.f, 2.f, 0 });   // fore grip
			break;
		default:
			Out.Add({ 0.06f, 0.44f, 0.24f, 0.17f,  7.f, 4.f, 0 });   // stock
			Out.Add({ 0.28f, 0.37f, 0.32f, 0.26f,  0.f, 5.f, 0 });   // receiver
			Out.Add({ 0.38f, 0.28f, 0.16f, 0.07f,  3.f, 2.f, 1 });   // rail sight
			Out.Add({ 0.58f, 0.43f, 0.30f, 0.10f,  0.f, 2.f, 1 });   // barrel
			Out.Add({ 0.86f, 0.40f, 0.06f, 0.15f,  3.f, 2.f, 1 });   // muzzle
			Out.Add({ 0.35f, 0.62f, 0.11f, 0.26f, -8.f, 3.f, 0 });   // magazine
			Out.Add({ 0.52f, 0.60f, 0.08f, 0.15f, -4.f, 2.f, 0 });   // fore grip
			break;
		}
	}

	/** Pulls a signed bonus out of a perk line the backend wrote, e.g. "fire_rate:+20", "ACCURACY +25",
	 *  "reload -10%". Returns false when the line is not about Key at all. */
	static bool ParseBonus(const FString& Line, const TCHAR* Key, int32& OutDelta)
	{
		const FString Low = Line.ToLower();
		const int32 At = Low.Find(Key);
		if (At == INDEX_NONE)
		{
			return false;
		}
		// Walk forward to the first signed number after the key.
		int32 Sign = 0;
		int32 Value = 0;
		bool bGotDigit = false;
		for (int32 i = At; i < Low.Len(); ++i)
		{
			const TCHAR C = Low[i];
			if (C == TEXT('+') || C == TEXT('-'))
			{
				if (bGotDigit) { break; }
				Sign = (C == TEXT('-')) ? -1 : 1;
			}
			else if (FChar::IsDigit(C))
			{
				Value = Value * 10 + (C - TEXT('0'));
				bGotDigit = true;
			}
			else if (bGotDigit)
			{
				break;
			}
		}
		if (!bGotDigit || Sign == 0)
		{
			return false;
		}
		OutDelta = Sign * Value;
		return true;
	}

	/** One attribute row: a 0..1 bar plus the skin bonus, if the gun carries one. */
	struct FAttr
	{
		FText Label;
		float Fill01 = 0.5f;
		int32 Delta = 0;
	};

	static float Norm(float Value, float Max)
	{
		return Max > 0.f ? FMath::Clamp(Value / Max, 0.06f, 1.f) : 0.5f;
	}

	/** Reads the six bars off a gun. Missing stats sit at a neutral half bar rather than an empty one, which
	 *  would read as "this gun is bad" instead of "we do not know yet". */
	static void AttrsFor(const FCampusGun& Gun, TArray<FAttr>& Out)
	{
		Out.Reset();
		FAttr Damage;  Damage.Label  = LOCTEXT("AtkDamage", "DAMAGE");
		FAttr Rate;    Rate.Label    = LOCTEXT("AtkRate", "RATE OF FIRE");
		FAttr Acc;     Acc.Label     = LOCTEXT("AtkAcc", "ACCURACY");
		FAttr Range;   Range.Label   = LOCTEXT("AtkRange", "RANGE");
		FAttr Reload;  Reload.Label  = LOCTEXT("AtkReload", "RELOAD");
		FAttr Mag;     Mag.Label     = LOCTEXT("AtkMag", "MAGAZINE");

		Damage.Fill01 = Gun.Damage  > 0.f ? Norm(Gun.Damage, 60.f)   : 0.5f;
		Rate.Fill01   = Gun.FireRate > 0.f ? Norm(Gun.FireRate, 14.f) : 0.5f;
		Acc.Fill01    = Gun.Accuracy > 0.f ? Norm(Gun.Accuracy, 100.f) : 0.5f;
		Mag.Fill01    = Gun.Magazine > 0   ? Norm((float)Gun.Magazine, 60.f) : 0.5f;
		// No range / reload in the catalogue yet: infer a believable shape from the class so the card is not
		// half empty, and let a server bonus move it.
		switch (ClassOf(Gun.Name))
		{
		case EClass::Sniper:  Range.Fill01 = 0.98f; Reload.Fill01 = 0.35f; break;
		case EClass::Shotgun: Range.Fill01 = 0.22f; Reload.Fill01 = 0.45f; break;
		case EClass::Pistol:  Range.Fill01 = 0.35f; Reload.Fill01 = 0.85f; break;
		case EClass::Lmg:     Range.Fill01 = 0.70f; Reload.Fill01 = 0.20f; break;
		case EClass::Smg:     Range.Fill01 = 0.40f; Reload.Fill01 = 0.80f; break;
		default:              Range.Fill01 = 0.65f; Reload.Fill01 = 0.60f; break;
		}

		// Skin bonuses, wherever the backend wrote them: perk lines today, a dedicated attributes field the
		// moment FCampusGun grows one (the same strings end up in Perks either way).
		for (const FString& Perk : Gun.Perks)
		{
			int32 D = 0;
			if (ParseBonus(Perk, TEXT("fire_rate"), D) || ParseBonus(Perk, TEXT("rate of fire"), D) || ParseBonus(Perk, TEXT("fire rate"), D)) { Rate.Delta = D; continue; }
			if (ParseBonus(Perk, TEXT("accuracy"), D)) { Acc.Delta = D; continue; }
			if (ParseBonus(Perk, TEXT("damage"), D))   { Damage.Delta = D; continue; }
			if (ParseBonus(Perk, TEXT("range"), D))    { Range.Delta = D; continue; }
			if (ParseBonus(Perk, TEXT("reload"), D))   { Reload.Delta = D; continue; }
			if (ParseBonus(Perk, TEXT("magazine"), D) || ParseBonus(Perk, TEXT("mag"), D)) { Mag.Delta = D; continue; }
		}

		Out.Add(Damage);
		Out.Add(Rate);
		Out.Add(Acc);
		Out.Add(Range);
		Out.Add(Reload);
		Out.Add(Mag);
	}

	/**
	 * THE CONSOLE'S OWN PICTURE FOR A TILE, over the drawn silhouette, when `image_key` names art the
	 * admin console has published (PHASE_B_CONTRACT B2). Both layers are built every time and it is
	 * their VISIBILITY that decides - an attribute, so the uploaded picture simply appears when the
	 * bytes land and no tile is ever rebuilt for it.
	 *
	 * The REMOTE layer's lambda is the one that calls Get(), because Get is what starts the single
	 * fetch: StateOf deliberately starts nothing, so asking it from the layer that is collapsed first
	 * would be a picture that is never requested.
	 */
	static TSharedRef<SWidget> RemoteArt(const FString& ImageKey)
	{
		return SNew(SBox)
			.Visibility_Lambda([ImageKey]()
			{
				return CampusRemoteImage::Get(ImageKey).IsReady() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SCampusArtPlate).Shape(FCampusShape::Notched(12.f)).RemoteKey(ImageKey).FocusY(0.5f)
			];
	}

	/** The packaged layer beside it: visible until (and unless) the uploaded picture is ready. */
	static TSharedRef<SWidget> PackagedArt(const FString& ImageKey, TSharedRef<SWidget> Art)
	{
		if (ImageKey.IsEmpty())
		{
			return Art;
		}
		return SNew(SBox)
			.Visibility_Lambda([ImageKey]()
			{
				return CampusRemoteImage::StateOf(ImageKey) == CampusRemoteImage::EState::Ready
					? EVisibility::Collapsed : EVisibility::HitTestInvisible;
			})
			[
				Art
			];
	}

	/** The leading number in an id or name: "pack_diamonds_550" -> 550. 0 when there is none. */
	static int32 DiamondsInId(const FString& Id)
	{
		int32 Value = 0;
		bool bGot = false;
		for (int32 i = 0; i < Id.Len(); ++i)
		{
			if (FChar::IsDigit(Id[i])) { Value = Value * 10 + (Id[i] - TEXT('0')); bGot = true; }
			else if (bGot) { break; }
		}
		return bGot ? Value : 0;
	}
}

// -------------------------------------------------------------------------------------------------
// Two tiny painters: the gun silhouette and one attribute bar. Both leaf widgets, both pure shape work.

/** A per-class angular gun glyph. No texture, no scene capture - 4-7 FCampusShape fills. */
class SCampusStoreGunArt : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusStoreGunArt)
		: _GunName()
		, _Tint(CampusStoreV2::Steel)
		, _Height(CampusStoreV2::ArtHeight)
		{}
		SLATE_ARGUMENT(FString, GunName)
		SLATE_ARGUMENT(FLinearColor, Tint)
		SLATE_ARGUMENT(float, Height)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Class = CampusStoreV2::ClassOf(InArgs._GunName);
		Tint = InArgs._Tint;
		Height = InArgs._Height;
		CampusStoreV2::PartsFor(Class, Parts);
		SetCanTick(false);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(120.f, Height); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
		// Keep the glyph inside a 2.4:1 letterbox, centred, so a wide card does not stretch the gun.
		const float BoxW = FMath::Min(Size.X * 0.92f, Size.Y * 2.6f);
		const float BoxH = BoxW / 2.6f;
		const FVector2f Origin(Size.X * 0.5f - BoxW * 0.5f, Size.Y * 0.5f - BoxH * 0.5f);

		const FLinearColor Dark = Tint * 0.55f;
		for (const CampusStoreV2::FPart& Part : Parts)
		{
			FCampusShape Shape;
			Shape.Slant = Part.Slant;
			Shape.CutTL = Shape.CutBR = Part.Cut;
			const FVector2f Min(Origin.X + Part.X * BoxW, Origin.Y + Part.Y * BoxH);
			const FVector2f PartSize(FMath::Max(Part.W * BoxW, 2.f), FMath::Max(Part.H * BoxH, 2.f));
			const FLinearColor Fill = Part.Tone == 1 ? Tint : FLinearColor(Dark.R, Dark.G, Dark.B, 1.f);
			CampusAngular::DrawSlantedPanel(Out, LayerId, AllottedGeometry, Min, PartSize, Shape, Fill);
		}
		return LayerId + 1;
	}

private:
	CampusStoreV2::EClass Class = CampusStoreV2::EClass::Rifle;
	FLinearColor Tint = CampusStoreV2::Steel;
	float Height = CampusStoreV2::ArtHeight;
	TArray<CampusStoreV2::FPart> Parts;
};

/** One attribute bar: slanted track, accent fill, and the bonus segment in green (+) or red (-). */
class SCampusStoreBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusStoreBar)
		: _Fill01(0.5f)
		, _Delta(0)
		{}
		SLATE_ARGUMENT(float, Fill01)
		SLATE_ARGUMENT(int32, Delta)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Fill01 = FMath::Clamp(InArgs._Fill01, 0.f, 1.f);
		Delta = InArgs._Delta;
		SetCanTick(false);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(90.f, 8.f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
		const FCampusShape Shape = FCampusShape::Slanted(FMath::Min(4.f, Size.Y * 0.6f));
		CampusAngular::DrawSlantedPanel(Out, LayerId, AllottedGeometry, FVector2f(0.f, 0.f), Size, Shape, CampusStoreV2::Track);

		const float Base = FMath::Clamp(Fill01, 0.f, 1.f);
		const float BaseW = Size.X * Base;
		if (BaseW > 1.f)
		{
			CampusAngular::DrawSlantedPanel(Out, LayerId + 1, AllottedGeometry, FVector2f(0.f, 0.f), FVector2f(BaseW, Size.Y),
				Shape, CampusStoreV2::Steel);
		}
		if (Delta != 0)
		{
			// The bonus rides on the end of the bar: growth to the right of the base, loss eaten out of it.
			const float Span = FMath::Clamp(FMath::Abs(Delta) / 100.f, 0.02f, 0.35f) * Size.X;
			const float X = Delta > 0 ? FMath::Min(BaseW, Size.X - Span) : FMath::Max(0.f, BaseW - Span);
			CampusAngular::DrawSlantedPanel(Out, LayerId + 2, AllottedGeometry, FVector2f(X, 0.f), FVector2f(Span, Size.Y),
				Shape, Delta > 0 ? CampusStoreV2::Plus : CampusStoreV2::Minus);
		}
		return LayerId + 3;
	}

private:
	float Fill01 = 0.5f;
	int32 Delta = 0;
};

// -------------------------------------------------------------------------------------------------
// Screen

UCampusPlayBilling* SCampusLobbyWidget::StoreBilling() const
{
	UGameInstance* Instance = Controller.IsValid() ? Controller->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UCampusPlayBilling>() : nullptr;
}

void SCampusLobbyWidget::StoreQueryPlayPrices()
{
	UCampusBackend* Api = Backend();
	UCampusPlayBilling* Play = StoreBilling();
	// ONCE PER VISIT TO THE STORE, NOT ONCE PER REPAINT.
	//
	// This used to be called from BuildStoreV2 and guarded only by "Play has no products yet". On a
	// build that is not installed from Play - or with the SKUs unpublished - QueryProducts answers
	// bOk with an EMPTY list, so the guard never closed: the reply called Refresh, the repaint called
	// this again, and the store rebuilt itself without end. The latch is the hard stop; the caller in
	// Show() clears it when the store is actually opened.
	if (bStorePricesQueried || !Play || !Play->IsAvailable() || Play->GetProducts().Num() > 0)
	{
		return;
	}
	bStorePricesQueried = true;

	// Opening the store is the natural moment to re-offer anything Play is still holding (an interrupted
	// verify, a UPI collect that settled while the app was closed).
	Play->RestoreUnfinished();

	TArray<FString> Wanted;
	if (Api)
	{
		for (const FCampusOffer& Offer : Api->Offers) { Wanted.AddUnique(Api->GooglePlayProductFor(Offer.Id)); }
		for (const FCampusOffer& Pack : Api->Packs)   { Wanted.AddUnique(Api->GooglePlayProductFor(Pack.Id)); }
	}
	// The three diamond packs and the two intro offers exist in the Play Console whether or not the backend
	// catalogue answered, so ask for them by name too: a cold store still shows real prices.
	Wanted.AddUnique(TEXT("pack_diamonds_250"));
	Wanted.AddUnique(TEXT("pack_diamonds_550"));
	Wanted.AddUnique(TEXT("pack_diamonds_3000"));
	Wanted.AddUnique(TEXT("pack_starter"));
	Wanted.AddUnique(TEXT("pack_combo"));
	Wanted.Remove(FString());
	if (Wanted.Num() == 0)
	{
		return;
	}

	// Only repaint when real prices actually arrived. A successful but empty answer changes nothing on
	// screen, and repainting for it is what fed the loop.
	Play->QueryProducts(Wanted, [this](bool bOk, const FString&, const TArray<FCampusPlayProduct>& Products)
	{
		if (bOk && Products.Num() > 0 && Current == EScreen::Store)
		{
			Refresh();
		}
	});
}

void SCampusLobbyWidget::StoreBuyProduct(const FString& ProductId, const FString& ItemName)
{
	UCampusPlayBilling* Play = StoreBilling();
	if (!Play || Play->IsPurchasing())
	{
		return;
	}
	Busy(TEXT("Opening Google Play..."));
	Play->Purchase(ProductId, [this, ItemName](ECampusPurchaseResult Result, const FString& Error)
	{
		bBusy = false;
		switch (Result)
		{
		case ECampusPurchaseResult::Credited:
			// Play charged and the backend credited: the wallet is the truth, so re-read it rather than
			// adding the pack's amount here and hoping.
			if (UCampusBackend* Api = Backend())
			{
				Api->LoadStore([this, ItemName](bool, const FString&)
				{
					Toast(ItemName + TEXT(" added to your wallet!"));
					Refresh();
				});
				return;
			}
			Toast(ItemName + TEXT(" added to your wallet!"));
			break;
		case ECampusPurchaseResult::Pending:
			Toast(TEXT("Payment in progress. Diamonds land as soon as Google confirms it."));
			break;
		case ECampusPurchaseResult::Cancelled:
			break;   // the player backed out of Play's sheet; say nothing
		case ECampusPurchaseResult::Unavailable:
			Toast(TEXT("Purchases work in the Play Store version of the game."));
			break;
		default:
			Toast(Error.IsEmpty() ? TEXT("The payment did not go through.") : Error);
			break;
		}
		Refresh();
	});
}

void SCampusLobbyWidget::StoreBuyGun(int32 Index)
{
	UCampusBackend* Api = Backend();
	if (!Api || !Api->Guns.IsValidIndex(Index))
	{
		return;
	}
	const FString Id = Api->Guns[Index].Id;
	const FString Name = Api->Guns[Index].Name;
	Busy(TEXT("Buying..."));
	Api->BuyGun(Id, [this, Name](bool bOk, const FString& Error)
	{
		Done(bOk, Error, [this, Name]() { Toast(Name + TEXT(" unlocked!")); });
	});
}

void SCampusLobbyWidget::StoreEquipGun(int32 Index)
{
	UCampusBackend* Api = Backend();
	if (!Api || !Api->Guns.IsValidIndex(Index))
	{
		return;
	}
	const FString Id = Api->Guns[Index].Id;
	const FString Name = Api->Guns[Index].Name;
	Api->EquipGun(Id, [this, Name](bool bOk, const FString& Error)
	{
		Done(bOk, Error, [this, Name]() { Toast(Name + TEXT(" equipped")); });
	});
}

/** The left rail: three tabs, the selected one carrying the accent edge. */
TSharedRef<SWidget> SCampusLobbyWidget::BuildStoreSideTabs()
{
	auto Rail = [this](const FText& Text, CampusAngular::EGlyph Glyph, EStoreTab Which)
	{
		const bool bOn = StoreTab == Which;
		return SNew(SCampusAngularButton)
			.Shape(FCampusShape::Tab(10.f, 12.f))
			.Fill(bOn ? FLinearColor(0.11f, 0.13f, 0.17f, 0.96f) : CampusStoreV2::RailFill)
			.Border(bOn ? CampusStyle::Accent : CampusAngular::Hairline)
			.BorderWidth(1.f)
			.AccentEdge(bOn ? CampusStyle::Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bOn ? 5.f : 0.f)
			.ContentPadding(FMargin(20.f, 14.f, 14.f, 14.f))
			.OnClicked(FOnClicked::CreateLambda([this, Which]()
			{
				StoreTab = Which;
				Refresh();
				return FReply::Handled();
			}))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(Glyph).Size(22.f).Color(bOn ? CampusStyle::Accent : CampusStoreV2::Steel)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					CampusStyle::Label(Text, 19, bOn ? FLinearColor::White : CampusStoreV2::Steel)
				]
			];
	};

	return SNew(SBox).WidthOverride(216.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				Rail(LOCTEXT("TabDia", "DIAMONDS"), CampusAngular::EGlyph::Gem, EStoreTab::Packs)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				Rail(LOCTEXT("TabGun", "GUNS"), CampusAngular::EGlyph::Target, EStoreTab::Guns)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Rail(LOCTEXT("TabOff", "OFFERS"), CampusAngular::EGlyph::Crown, EStoreTab::Offers)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
		];
}

/** One gun card: name, class, drawn silhouette, six attribute bars, price, and the one action that makes
 *  sense for its state (EQUIP / EQUIPPED / BUY / BUY DIAMONDS). */
TSharedRef<SWidget> SCampusLobbyWidget::BuildStoreGunTile(int32 Index)
{
	const UCampusBackend* Api = Backend();
	if (!Api || !Api->Guns.IsValidIndex(Index))
	{
		return SNullWidget::NullWidget;
	}
	const FCampusGun& Gun = Api->Guns[Index];
	const bool bEquipped = Api->EquippedGun == Gun.Id;
	const bool bOwned = Gun.bOwned || Gun.bStarter;
	const int32 Price = Gun.PriceDiamonds;
	const bool bAfford = Price > 0 && Api->Diamonds >= Price;

	TArray<CampusStoreV2::FAttr> Attrs;
	CampusStoreV2::AttrsFor(Gun, Attrs);

	TSharedRef<SVerticalBox> Bars = SNew(SVerticalBox);
	for (const CampusStoreV2::FAttr& Attr : Attrs)
	{
		const FLinearColor DeltaColor = Attr.Delta > 0 ? CampusStoreV2::Plus : CampusStoreV2::Minus;
		Bars->AddSlot().AutoHeight().Padding(0.f, 1.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(104.f)
				[
					SNew(STextBlock).Text(Attr.Label).Font(CampusAngular::Display(12))
					.ColorAndOpacity(CampusStoreV2::Steel)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(6.f, 0.f)
			[
				SNew(SBox).HeightOverride(8.f)
				[
					SNew(SCampusStoreBar).Fill01(Attr.Fill01).Delta(Attr.Delta)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(42.f)
				[
					// The sign is written out, so the bonus does not depend on telling green from red.
					SNew(STextBlock)
					.Text(Attr.Delta == 0 ? FText::GetEmpty()
						: FText::FromString(FString::Printf(TEXT("%s%d"), Attr.Delta > 0 ? TEXT("+") : TEXT("-"), FMath::Abs(Attr.Delta))))
					.Font(CampusAngular::Numeric(13))
					.Justification(ETextJustify::Right)
					.ColorAndOpacity(DeltaColor)
				]
			]
		];
	}

	// The action. Never a dead button: a gun you cannot afford offers the diamonds tab instead.
	TSharedRef<SWidget> Action = SNullWidget::NullWidget;
	if (bEquipped)
	{
		Action = SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Wedge(10.f, 8.f))
			.Fill(FLinearColor(0.10f, 0.30f, 0.16f, 0.95f))
			.Border(CampusStyle::Go).BorderWidth(1.f)
			.ContentPadding(FMargin(16.f, 10.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Tick).Size(16.f).Color(CampusStyle::Go)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					CampusStyle::Label(LOCTEXT("Equipped", "EQUIPPED"), 17, CampusStyle::Go)
				]
			];
	}
	else if (bOwned)
	{
		Action = CampusStyle::Button(LOCTEXT("Equip", "EQUIP"),
			FOnClicked::CreateLambda([this, Index]() { StoreEquipGun(Index); return FReply::Handled(); }),
			EButton::Accent, 17, FVector2D(132.f, 48.f));
	}
	else if (Price <= 0)
	{
		Action = CampusStyle::Label(LOCTEXT("Soon", "COMING SOON"), 15, CampusStyle::Muted);
	}
	else if (bAfford)
	{
		Action = CampusStyle::Button(LOCTEXT("Buy", "BUY"),
			FOnClicked::CreateLambda([this, Index]() { StoreBuyGun(Index); return FReply::Handled(); }),
			EButton::Go, 17, FVector2D(132.f, 48.f));
	}
	else
	{
		Action = CampusStyle::Button(LOCTEXT("GetDia", "BUY DIAMONDS"),
			FOnClicked::CreateLambda([this]()
			{
				StoreTab = EStoreTab::Packs;
				Toast(TEXT("Top up Diamonds, then come back for the gun."));
				Refresh();
				return FReply::Handled();
			}), EButton::Dark, 14, FVector2D(158.f, 48.f));
	}

	return SNew(SBox).WidthOverride(CampusStoreV2::TileWidth)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(CampusStoreV2::CardFill)
			.Border(bEquipped ? CampusStyle::Go : (bOwned ? CampusStyle::Accent : CampusAngular::Hairline))
			.BorderWidth(1.f)
			.AccentEdge(bEquipped ? CampusStyle::Go : FLinearColor::Transparent)
			.AccentEdgeWidth(bEquipped ? 4.f : 0.f)
			.ContentPadding(FMargin(16.f, 12.f, 16.f, 14.f))
			[
				SNew(SVerticalBox)
				// name + class
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromString(Gun.Name.ToUpper()))
							.Font(CampusAngular::Display(22)).ColorAndOpacity(FLinearColor::White)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(CampusStoreV2::ClassLabel(CampusStoreV2::ClassOf(Gun.Name)))
							.Font(CampusAngular::Display(12)).ColorAndOpacity(CampusStoreV2::SteelDark)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
					[
						Gun.Badge.IsEmpty()
							? SNullWidget::NullWidget
							: StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
								.Shape(FCampusShape::Slanted(8.f)).Fill(CampusStyle::Accent)
								.ContentPadding(FMargin(9.f, 3.f))
								[
									SNew(STextBlock).Text(FText::FromString(Gun.Badge.ToUpper()))
									.Font(CampusAngular::Display(11)).ColorAndOpacity(FLinearColor::Black)
								])
					]
				]
				// drawn silhouette on a faded plate
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 8.f)
				[
					SNew(SBox).HeightOverride(CampusStoreV2::ArtHeight)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SCampusAngularPanel).Shape(FCampusShape::Notched(12.f)).Fill(CampusStoreV2::CardFillTop)
						]
						// THE CONSOLE'S PICTURE, when `image_key` names art it has published; the drawn
						// silhouette otherwise. Exactly one of the two layers is ever visible.
						+ SOverlay::Slot()
						[
							Gun.ImageKey.IsEmpty()
								? SNullWidget::NullWidget
								: CampusStoreV2::RemoteArt(Gun.ImageKey)
						]
						+ SOverlay::Slot()
						[
							CampusStoreV2::PackagedArt(Gun.ImageKey,
								SNew(SCampusStoreGunArt)
								.GunName(Gun.Name)
								.Tint(bOwned ? CampusStoreV2::Steel : CampusStoreV2::SteelDark * 1.3f)
								.Height(CampusStoreV2::ArtHeight))
						]
						// level chip, for a gun that can be upgraded
						+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
						[
							(bOwned && Gun.MaxLevel > 1)
								? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
									.Shape(FCampusShape::Slanted(8.f)).Fill(FLinearColor(0.f, 0.f, 0.f, 0.55f))
									.ContentPadding(FMargin(8.f, 2.f))
									[
										SNew(STextBlock)
										.Text(FText::FromString(FString::Printf(TEXT("LV %d/%d"), FMath::Max(Gun.Level, 1), Gun.MaxLevel)))
										.Font(CampusAngular::Numeric(12)).ColorAndOpacity(CampusStyle::Accent)
									])
								: SNullWidget::NullWidget
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight()[Bars]
				// price + action
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						(bOwned || Price <= 0)
							? SNullWidget::NullWidget
							: StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 7.f, 0.f)
								[
									SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Gem).Size(20.f)
									.Color(CampusStoreV2::Gem)
								]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(FText::AsNumber(Price))
									.Font(CampusAngular::Numeric(22))
									.ColorAndOpacity(bAfford ? FLinearColor::White : CampusStoreV2::Minus)
								])
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Action]
				]
			]
		];
}

/** A diamond pack: amount in big numerals, bonus ribbon, and Play's own price string. */
TSharedRef<SWidget> SCampusLobbyWidget::BuildStorePackTile(const FCampusOffer& Pack, int32 Amount, bool bFeatured)
{
	const UCampusBackend* Api = Backend();
	UCampusPlayBilling* Play = StoreBilling();
	const FString Product = Api ? Api->GooglePlayProductFor(Pack.Id) : FString();
	const FString PlayId = Product.IsEmpty() ? Pack.Id : Product;
	const FCampusPlayProduct* Known = Play ? Play->FindProduct(PlayId) : nullptr;
	const bool bCanBuy = Play && Play->IsAvailable();

	// Price: Play's string, or honest silence. We never print a rupee figure of our own next to a Play product.
	TSharedRef<SWidget> Buy = SNullWidget::NullWidget;
	if (Pack.PriceDiamonds > 0)
	{
		// AN OFFER: bought with Diamonds through the backend, exactly like a gun - never a Play product.
		const int32 Price = Pack.PriceDiamonds;
		const bool bAfford = Api && Api->Diamonds >= Price;
		const FString Id = Pack.Id;
		const FString Name = Pack.Name;
		Buy = bAfford
			? CampusStyle::Button(FText::FromString(FString::Printf(TEXT("%d DIAMONDS"), Price)),
				FOnClicked::CreateLambda([this, Id, Name]()
				{
					if (UCampusBackend* A = Backend())
					{
						Busy(TEXT("Buying..."));
						A->BuyItem(Id, [this, Name](bool bOk, const FString& Error)
						{
							Done(bOk, Error, [this, Name]() { Toast(Name + TEXT(" is yours!")); });
						});
					}
					return FReply::Handled();
				}), EButton::Go, 17, FVector2D(236.f, 52.f))
			: CampusStyle::Button(FText::FromString(FString::Printf(TEXT("BUY DIAMONDS  (%d)"), Price)),
				FOnClicked::CreateLambda([this]()
				{
					StoreTab = EStoreTab::Packs;
					Toast(TEXT("Top up Diamonds, then come back for the offer."));
					Refresh();
					return FReply::Handled();
				}), EButton::Dark, 15, FVector2D(236.f, 52.f));
	}
	else if (!bCanBuy)
	{
		Buy = SNew(SBox).WidthOverride(236.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoBilling", "Purchases work in the Play Store version"))
				.Font(CampusAngular::Body(13)).ColorAndOpacity(CampusStyle::Muted)
				.AutoWrapText(true)
			];
	}
	else if (Known && !Known->PriceText.IsEmpty())
	{
		const bool bEnabled = !Play->IsPurchasing();
		Buy = CampusStyle::Button(FText::FromString(Known->PriceText),
			FOnClicked::CreateLambda([this, PlayId, Pack]()
			{
				StoreBuyProduct(PlayId, Pack.Name.IsEmpty() ? TEXT("Diamonds") : Pack.Name);
				return FReply::Handled();
			}), EButton::Go, 20, FVector2D(236.f, 54.f), bEnabled);
	}
	else
	{
		// Play has not answered yet (or does not know this id): say "loading", not a fake price.
		Buy = SNew(SBox).WidthOverride(236.f).HeightOverride(54.f)
			[
				SNew(SCampusAngularPanel).Shape(FCampusShape::Wedge(10.f, 8.f))
				.Fill(FLinearColor(1.f, 1.f, 1.f, 0.06f)).ContentPadding(FMargin(12.f, 14.f))
				[
					SNew(STextBlock).Text(LOCTEXT("PriceWait", "CHECKING PRICE..."))
					.Font(CampusAngular::Display(14)).ColorAndOpacity(CampusStyle::Muted)
					.Justification(ETextJustify::Center)
				]
			];
	}

	const FText Ribbon = !Pack.Badge.IsEmpty()
		? FText::FromString(Pack.Badge.ToUpper())
		: (bFeatured ? LOCTEXT("BestValue", "BEST VALUE") : FText::GetEmpty());

	return SNew(SBox).WidthOverride(262.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(bFeatured ? FLinearColor(0.05f, 0.09f, 0.14f, 0.95f) : CampusStoreV2::CardFill)
			.Border(bFeatured ? CampusStoreV2::Gem : CampusAngular::Hairline)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(18.f, 14.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(SBox).HeightOverride(22.f)
					[
						Ribbon.IsEmpty()
							? SNullWidget::NullWidget
							: StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(9.f))
									.Fill(bFeatured ? CampusStoreV2::Gem : CampusStyle::Accent)
									.ContentPadding(FMargin(10.f, 3.f))
									[
										SNew(STextBlock).Text(Ribbon)
										.Font(CampusAngular::Display(12)).ColorAndOpacity(FLinearColor::Black)
									]
								])
					]
				]
				// amount
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
					[
						// The console's pack art when it has uploaded some, the gem plate otherwise.
						// A fixed square either way, so a tile with art and a tile without still line up.
						SNew(SBox).WidthOverride(52.f).HeightOverride(52.f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()
							[
								Pack.ImageKey.IsEmpty()
									? SNullWidget::NullWidget
									: CampusStoreV2::RemoteArt(Pack.ImageKey)
							]
							+ SOverlay::Slot()
							[
								CampusStoreV2::PackagedArt(Pack.ImageKey,
									SNew(SCampusAngularPanel).Shape(FCampusShape::Notched(10.f))
									.Fill(CampusStoreV2::GemDeep).ContentPadding(FMargin(9.f))
									[
										SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Gem).Size(30.f).Color(CampusStoreV2::Gem)
									])
							]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(Amount > 0 ? FText::AsNumber(Amount) : FText::FromString(Pack.Name.ToUpper()))
							.Font(CampusAngular::Numeric(Amount > 0 ? 36 : 20)).ColorAndOpacity(FLinearColor::White)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(Amount > 0 ? LOCTEXT("Diamonds", "DIAMONDS") : FText::GetEmpty())
							.Font(CampusAngular::Display(13)).ColorAndOpacity(CampusStoreV2::Gem)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 10.f)
				[
					SNew(SBox).HeightOverride(34.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Pack.Description.IsEmpty()
							? (Known ? Known->Description : FString()) : Pack.Description))
						.Font(CampusAngular::Body(13)).ColorAndOpacity(CampusStyle::Muted).AutoWrapText(true)
					]
				]
				+ SVerticalBox::Slot().AutoHeight()[Buy]
			]
		];
}

/** DIAMONDS: the money tab. Packs from the backend catalogue, falling back to the Play ids we know exist. */
TSharedRef<SWidget> SCampusLobbyWidget::BuildStoreDiamondGrid()
{
	const UCampusBackend* Api = Backend();

	TArray<FCampusOffer> Packs;
	if (Api)
	{
		Packs = Api->Packs;
	}
	if (Packs.Num() == 0)
	{
		// A cold or offline catalogue still shows the shelf, priced by Play.
		const TCHAR* Ids[] = { TEXT("pack_diamonds_300_offer"), TEXT("pack_diamonds_250"), TEXT("pack_diamonds_550"), TEXT("pack_diamonds_3000") };
		for (const TCHAR* Id : Ids)
		{
			FCampusOffer Pack;
			Pack.Id = Id;
			Pack.Name = TEXT("Diamonds");
			Packs.Add(Pack);
		}
	}

	// The biggest pack is the featured one, which is the honest reading of "best value".
	int32 Best = INDEX_NONE;
	int32 BestAmount = 0;
	TArray<int32> Amounts;
	Amounts.Reserve(Packs.Num());
	for (int32 i = 0; i < Packs.Num(); ++i)
	{
		const int32 Amount = CampusStoreV2::DiamondsInId(Packs[i].Id.IsEmpty() ? Packs[i].Name : Packs[i].Id);
		Amounts.Add(Amount);
		if (Amount > BestAmount) { BestAmount = Amount; Best = i; }
	}

	TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.f, 12.f));
	for (int32 i = 0; i < Packs.Num(); ++i)
	{
		Grid->AddSlot()[BuildStorePackTile(Packs[i], Amounts[i], i == Best)];
	}
	return Grid;
}

/** OFFERS: the bundles, bought with Diamonds (migration 030), described by the backend. */
TSharedRef<SWidget> SCampusLobbyWidget::BuildStoreOfferGrid()
{
	const UCampusBackend* Api = Backend();
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.f, 12.f));
	if (!Api || Api->Offers.Num() == 0)
	{
		return BuildStoreEmpty(LOCTEXT("NoOffers", "NO OFFERS RIGHT NOW"),
			LOCTEXT("NoOffersHint", "Starter and combo bundles show up here when an event is running."));
	}
	for (int32 i = 0; i < Api->Offers.Num(); ++i)
	{
		Grid->AddSlot()[BuildStorePackTile(Api->Offers[i], CampusStoreV2::DiamondsInId(Api->Offers[i].Id), i == 0)];
	}
	return Grid;
}

/** GUNS: the Diamond tab. */
TSharedRef<SWidget> SCampusLobbyWidget::BuildStoreGunGrid()
{
	const UCampusBackend* Api = Backend();
	if (!Api || Api->Guns.Num() == 0)
	{
		return BuildStoreEmpty(LOCTEXT("NoGuns", "THE ARMOURY IS LOADING"),
			LOCTEXT("NoGunsHint", "Guns appear as soon as the store answers. Pull the lobby open again if this stays empty."));
	}
	TSharedRef<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.f, 12.f));
	for (int32 i = 0; i < Api->Guns.Num(); ++i)
	{
		Grid->AddSlot()[BuildStoreGunTile(i)];
	}
	return Grid;
}

/** The shared empty state: never a blank panel. */
TSharedRef<SWidget> SCampusLobbyWidget::BuildStoreEmpty(const FText& Title, const FText& Hint)
{
	return SNew(SBox).HeightOverride(240.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Lock).Size(38.f).Color(CampusStoreV2::SteelDark)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 4.f)
			[
				CampusStyle::Label(Title, 22, CampusStoreV2::Steel)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(460.f)
				[
					SNew(STextBlock).Text(Hint).Font(CampusAngular::Body(14))
					.ColorAndOpacity(CampusStyle::Muted).AutoWrapText(true)
					.Justification(ETextJustify::Center)
				]
			]
		];
}

/** Wallet + status strip. One line that answers "what can I spend" and "is a payment still running". */
TSharedRef<SWidget> SCampusLobbyWidget::BuildStoreWallet()
{
	const UCampusBackend* Api = Backend();
	UCampusPlayBilling* Play = StoreBilling();
	const int32 Pending = Play ? Play->GetPendingCount() : 0;
	const bool bWorking = Play && Play->IsPurchasing();

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
		[
			SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(10.f)).Fill(CampusStyle::Accent)
			[
				SNew(SBox).WidthOverride(14.f).HeightOverride(32.f)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 24.f, 0.f)
		[
			CampusStyle::Label(LOCTEXT("StoreV2Title", "STORE"), 32, FLinearColor::White)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			// In-flight and pending money states live next to the title, where the eye already is.
			bWorking
				? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Slanted(12.f)).Fill(FLinearColor(0.08f, 0.20f, 0.32f, 0.92f))
					.ContentPadding(FMargin(14.f, 6.f))
					[
						SNew(STextBlock).Text(LOCTEXT("Working", "TALKING TO GOOGLE PLAY..."))
						.Font(CampusAngular::Display(14)).ColorAndOpacity(CampusStoreV2::Gem)
					])
				: (Pending > 0
					? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
						.Shape(FCampusShape::Slanted(12.f)).Fill(FLinearColor(0.35f, 0.26f, 0.02f, 0.92f))
						.ContentPadding(FMargin(14.f, 6.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Clock).Size(16.f).Color(CampusStoreV2::Gold)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PendingPay", "PAYMENT PENDING  -  WAITING FOR GOOGLE"))
								.Font(CampusAngular::Display(14)).ColorAndOpacity(CampusStoreV2::Gold)
							]
						])
					: SNullWidget::NullWidget)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			CampusStyle::Counter(CampusAngular::EGlyph::Coin, CampusStoreV2::Gold, FText::AsNumber(Api ? Api->RoomCards : 0))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
		[
			CampusStyle::Counter(CampusAngular::EGlyph::Gem, CampusStoreV2::Gem, FText::AsNumber(Api ? Api->Diamonds : 0),
				FOnClicked::CreateLambda([this]() { StoreTab = EStoreTab::Packs; Refresh(); return FReply::Handled(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			CampusStyle::IconButton(CampusAngular::EGlyph::Close,
				FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }), 62.f)
		];

	return Row;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildStoreV2()
{
	// NO SIDE EFFECTS HERE. The Play price query used to run from this function, which made every
	// repaint of the store start another round trip - and the reply repainted the store. Opening the
	// store is the event that wants prices, so Show() asks for them; a repaint only draws.

	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	FText Footnote = FText::GetEmpty();
	switch (StoreTab)
	{
	case EStoreTab::Packs:
		Body = BuildStoreDiamondGrid();
		Footnote = LOCTEXT("FootDia", "Diamonds are the only thing sold for money. Prices come from Google Play in your own currency; Diamonds are credited by our server, not by the app.");
		break;
	case EStoreTab::Offers:
		Body = BuildStoreOfferGrid();
		Footnote = LOCTEXT("FootOff", "Offers are bought with Diamonds. A bundle with a gun in it is a one-time buy.");
		break;
	default:
		Body = BuildStoreGunGrid();
		Footnote = LOCTEXT("FootGun", "Guns are bought with Diamonds. Green is a bonus, red is a trade-off - every skin changes how the gun handles.");
		break;
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.78f)]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(24.f, 18.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[BuildStoreWallet()]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 16.f, 0.f)
					[
						BuildStoreSideTabs()
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SCampusAngularPanel)
						.Shape(FCampusShape::Notched(20.f))
						.Fill(FLinearColor(0.012f, 0.018f, 0.028f, 0.80f))
						.Border(CampusAngular::Hairline).BorderWidth(1.f)
						.ContentPadding(FMargin(18.f, 16.f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().FillHeight(1.f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()[Body]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
							[
								SNew(STextBlock).Text(Footnote).Font(CampusAngular::Body(13))
								.ColorAndOpacity(CampusStyle::Muted).AutoWrapText(true)
							]
						]
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
