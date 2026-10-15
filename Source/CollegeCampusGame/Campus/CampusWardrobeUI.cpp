// WARDROBE and LOBBY DECORATION (Slate, lobby level). Two screens, one file, because they are the
// two halves of one feature and they share every rule below.
//
//   WARDROBE (EScreen::Wardrobe) - PUBG's closet. LEFT 45 % is the 3D figure, standing where the
//       profile page stands it (ACampusLobbyPlayerController::FocusProfile + ProfileCamera) and
//       turned by the same RotateDisplay drag. RIGHT is one opaque angular panel - the OWNED grid,
//       three columns - with the category rail (OUTFIT / DECOR) pinned to the screen's right edge
//       and the three preset tiles under the figure. Equipping previews INSTANTLY.
//
//   DECOR (EScreen::Decor) - a BOTTOM SHEET, not a full screen, because the whole point is to watch
//       the prop appear: the line-up and all eight anchors stay visible above it. Pick one of the
//       eight anchors, then pick an owned prop or EMPTY.
//
// THREE RULES THIS FILE OBEYS:
//   1. OWNED ONLY. The grid never shows something the player cannot wear, so there is no locked
//      card, no price and no upsell here - that is the store's job, not the closet's.
//   2. THE SAVE FIRST, THE SERVER SECOND. Every choice is written to UCampusSave and shown in the
//      scene before any request leaves, so the lobby is right offline and right on the next launch.
//      GET /me/cosmetics overwrites all of it on sign-in: the server is truth ON LOAD.
//   3. NOTHING HERE DECIDES WHAT ANYTHING LOOKS LIKE. Meshes, colours, recipes and anchor positions
//      are all CampusCosmetics.h; this file moves ids.
//
// AND FOUR LAYOUT RULES, because this is a landscape phone screen held in two hands:
//   * THE SCENE TAKES THE DRAGS. One SCampusDragArea sits under BOTH screens and every piece of
//     chrome above it is SelfHitTestInvisible / HitTestInvisible, so a finger that lands anywhere
//     except an actual control turns the figure. A chip painted over a card is hit-test invisible
//     too, or it would eat the tap on that corner of the card.
//   * 48 PX IS THE FLOOR for anything tappable, and the preset tile is 96 tall precisely so its
//     SAVE chip can be 48.
//   * NO FIXED WIDTH THAT CAN OVERFLOW. The grid's columns are FillWidth, the panel is FillWidth,
//     and anything with a fixed width (the anchor chips, the prop cards) lives in a scroll box that
//     can carry it off the edge instead of squashing the row. Everything else is on an 8 px grid.
//   * EVERY STATE IS DRAWN: loading, signed out, bare closet, saving, nothing-fits-this-anchor.
//     A blank panel is a bug, and "why is this empty" is answered where the emptiness is.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusCosmetics.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusWardrobeUI"

using namespace CampusStyle;

namespace
{
	// ---- measurements. 8 px grid; nothing tappable under 48. -------------------------------------

	/** The figure owns the left 45 %, exactly as it does on the profile page. */
	constexpr float ClosetSceneShare = 0.45f;
	constexpr float ClosetPanelShare = 0.55f;

	/** Three columns, the owner's number. The COLUMNS fill the panel; only the height is fixed, so
	 *  equipping never reflows the grid and a narrow phone never pushes a card off the edge. */
	constexpr int32 GridColumns = 3;
	constexpr float CardHeight = 216.f;
	constexpr float Gap = 8.f;

	/** The category rail on the screen's right edge. */
	constexpr float RailWidth = 96.f;
	constexpr float RailHeight = 104.f;

	/** A preset tile is 96 tall BECAUSE its SAVE chip has to be 48: that is the touch floor, and a
	 *  hidden long press is not an affordance on a phone. */
	constexpr float PresetHeight = 96.f;
	constexpr float PresetRowMaxWidth = 680.f;
	constexpr float SaveWidth = 80.f;
	constexpr float SaveHeight = 48.f;

	constexpr float CloseSize = 56.f;

	/** The decor sheet. Fixed row heights, so swiping the strips can never make the sheet jump. */
	constexpr float AnchorChipWidth = 200.f;
	constexpr float AnchorChipHeight = 72.f;
	constexpr float DecorCardWidth = 176.f;
	constexpr float DecorCardHeight = 168.f;
	constexpr float EmptyBlockWidth = 420.f;

	const FLinearColor CardInk(0.024f, 0.031f, 0.043f, 0.96f);
	const FLinearColor CardInkOn(0.10f, 0.085f, 0.028f, 0.96f);
	const FLinearColor RailInk(0.020f, 0.027f, 0.036f, 0.86f);
	const FLinearColor RailInkOn(0.11f, 0.13f, 0.17f, 0.96f);
	/** The floor of a card's art well: the swatches fade down into this. */
	const FLinearColor ArtFloor(0.010f, 0.014f, 0.020f, 0.95f);
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.85f);
	/** The two quiet inks. NOT ARBITRARY: on this file's near-black plates 0.68 white lands around
	 *  6:1 and 0.48 around 4.5:1, which is the floor for 11-14 px copy. The lobby's older screens
	 *  dim their hints to 0.32-0.35 and they are not readable in daylight on a phone. */
	const FLinearColor Dim(1.f, 1.f, 1.f, 0.68f);
	const FLinearColor Faint(1.f, 1.f, 1.f, 0.48f);

	// ---- small shared pieces --------------------------------------------------------------------

	/** Big italic title laid over the live scene: always with the dark offset shadow, exactly like
	 *  SELECT MAP's, or the first bright pixel of the lobby behind it wins. */
	TSharedRef<SWidget> ClosetTitle(const FText& Text, int32 Size)
	{
		return SNew(STextBlock)
			.Visibility(EVisibility::HitTestInvisible)
			.Text(Text)
			.Font(CampusAngular::Display(Size))
			.ColorAndOpacity(FLinearColor::White)
			.ShadowOffset(FVector2D(3.f, 3.f))
			.ShadowColorAndOpacity(Shadow);
	}

	/** One quiet line - a hint, a count, a step number. HIT-TEST INVISIBLE: most of these sit over
	 *  the 3D scene, and a label that swallowed a drag would be a label that broke the turntable. */
	TSharedRef<SWidget> Quiet(const FText& Text, int32 Size, const FLinearColor& Color, bool bBody = false)
	{
		return SNew(STextBlock)
			.Visibility(EVisibility::HitTestInvisible)
			.Text(Text)
			.Font(bBody ? CampusAngular::Body(Size) : CampusAngular::Display(Size))
			.ColorAndOpacity(Color)
			.ShadowOffset(FVector2D(2.f, 2.f))
			.ShadowColorAndOpacity(Shadow);
	}

	/** The accent flash every screen in this game wears to the left of its title. */
	TSharedRef<SWidget> TitleFlash(float Height)
	{
		return SNew(SCampusAngularPanel)
			.Visibility(EVisibility::HitTestInvisible)
			.Shape(FCampusShape::Slanted(9.f))
			.Fill(Accent)
			[
				SNew(SBox).WidthOverride(16.f).HeightOverride(Height)
			];
	}

	/** The small dark chip a card carries: WORN, PLACED, BARE, a count. HIT-TEST INVISIBLE, because
	 *  it is painted over a card that has to stay tappable right through it. */
	TSharedRef<SWidget> ClosetChip(CampusAngular::EGlyph Glyph, const FText& Label, const FLinearColor& Color)
	{
		return SNew(SCampusAngularPanel)
			.Visibility(EVisibility::HitTestInvisible)
			.Shape(FCampusShape::Notched(7.f))
			.Fill(FLinearColor(0.f, 0.f, 0.f, 0.82f))
			.Border(Color.CopyWithNewOpacity(0.60f))
			.BorderWidth(1.f)
			.ContentPadding(FMargin(10.f, 5.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(Glyph).Size(14.f).Color(Color)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Label).Font(CampusAngular::Display(13)).ColorAndOpacity(Color)
				]
			];
	}

	/** RARITY IS NEVER COLOUR ALONE: the bar carries the word right next to it, so the card reads for
	 *  a player who cannot tell blue from purple. */
	TSharedRef<SWidget> RarityRow(CampusCosmetics::ERarity Rarity, bool bOriginal, int32 Size)
	{
		const FLinearColor Frame = CampusCosmetics::RarityColor(Rarity);
		return SNew(SHorizontalBox)
			.Visibility(EVisibility::HitTestInvisible)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SBox).WidthOverride(18.f).HeightOverride(4.f)
				[
					SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(2.f)).Fill(Frame)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(bOriginal ? TEXT("ORIGINAL") : CampusCosmetics::RarityName(Rarity)))
				.Font(CampusAngular::Display(Size))
				.ColorAndOpacity(Frame)
			];
	}

	/** A 1 px rule across a panel. */
	TSharedRef<SWidget> Rule()
	{
		return SNew(SBox).HeightOverride(1.f).Visibility(EVisibility::HitTestInvisible)
			[
				SNew(SCampusAngularPanel).Fill(CampusAngular::Hairline)
			];
	}

	/** The one empty state both screens use. Never a blank panel, and never just "nothing here": it
	 *  names where the missing thing comes from, which is the difference between an empty screen and
	 *  a screen that is telling the player something. */
	TSharedRef<SWidget> ClosetEmpty(CampusAngular::EGlyph Glyph, const FText& Title, const FText& Hint)
	{
		return SNew(SVerticalBox)
			.Visibility(EVisibility::HitTestInvisible)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SCampusGlyph).Glyph(Glyph).Size(34.f).Color(Faint)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 4.f)
			[
				SNew(STextBlock).Text(Title).Font(CampusAngular::Display(20)).ColorAndOpacity(Dim)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).MaxDesiredWidth(EmptyBlockWidth)
				[
					SNew(STextBlock)
					.Text(Hint)
					.Font(CampusAngular::Body(14))
					.ColorAndOpacity(Faint)
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]
			];
	}

	/** One status line at the foot of the owned panel: a glyph AND a sentence, so the state is never
	 *  carried by colour or by an animation that a still screenshot would lose. */
	TSharedRef<SWidget> StatusLine(CampusAngular::EGlyph Glyph, const FText& Text, const FLinearColor& Color)
	{
		return SNew(SHorizontalBox)
			.Visibility(EVisibility::HitTestInvisible)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SCampusGlyph).Glyph(Glyph).Size(16.f).Color(Color)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Text).Font(CampusAngular::Body(14)).ColorAndOpacity(Color).AutoWrapText(true)
			];
	}

	// ---- card art -------------------------------------------------------------------------------

	/**
	 * THE OUTFIT'S WHOLE PICTURE, and there is no PNG pipeline behind it on purpose: the two colours
	 * the body material is actually tinted with are a truer preview than any icon somebody would have
	 * to keep in step with the catalogue. Two stacked blocks were honest and ugly, so this cuts the
	 * same two colours into a STYLISED KIT - shoulders, sleeves, torso, trim, legs - out of the same
	 * angular shapes as the rest of the UI. Primary is the body, Secondary is the trim, so a card
	 * shows which colour is which instead of just how much of each there is.
	 *
	 * Ten fills, no texture, nothing to cook, nothing to fail to load.
	 */
	class SClosetOutfitArt : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SClosetOutfitArt)
			: _Primary(FLinearColor::White)
			, _Secondary(FLinearColor::Gray)
			{}
			SLATE_ARGUMENT(FLinearColor, Primary)
			SLATE_ARGUMENT(FLinearColor, Secondary)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Primary = InArgs._Primary;
			Secondary = InArgs._Secondary;
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(96.f, 104.f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2f Size = AllottedGeometry.GetLocalSize();
			if (Size.X < 16.f || Size.Y < 16.f)
			{
				return LayerId;
			}

			// The well behind the kit: a wash of the outfit's own primary, dropping into the card's
			// own black. It reads as light on a figure rather than as a second swatch.
			CampusAngular::DrawVerticalFade(OutDrawElements, LayerId, AllottedGeometry, FVector2f::ZeroVector, Size,
				FCampusShape::Notched(10.f), (Primary * 0.34f).CopyWithNewOpacity(0.95f), ArtFloor);

			// One figure box, centred, never wider than it is tall: the same silhouette whatever shape
			// the card ends up when the panel is narrow.
			const float Height = Size.Y * 0.86f;
			const float Width = FMath::Min(Size.X * 0.66f, Height * 0.72f);
			const float Left = (Size.X - Width) * 0.5f;
			const float Top = (Size.Y - Height) * 0.5f;
			const float Mid = Left + Width * 0.5f;

			auto Piece = [&](float X, float Y, float W, float H, const FCampusShape& Shape, const FLinearColor& Color)
			{
				CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId + 1, AllottedGeometry,
					FVector2f(X, Y), FVector2f(FMath::Max(W, 1.f), FMath::Max(H, 1.f)), Shape, Color);
			};

			const FLinearColor Sleeve = (Primary * 0.74f).CopyWithNewOpacity(1.f);
			const FLinearColor Leg = (Primary * 0.52f).CopyWithNewOpacity(1.f);
			const FLinearColor Head(0.14f, 0.15f, 0.17f, 1.f);

			// Head, then the kit from the shoulders down. Painted back to front.
			Piece(Mid - Width * 0.12f, Top, Width * 0.24f, Height * 0.15f, FCampusShape::Clipped(Width * 0.07f), Head);
			Piece(Left, Top + Height * 0.17f, Width, Height * 0.13f, FCampusShape::Clipped(Width * 0.11f), Primary);
			// Sleeves lean out of the shoulder line, cuffed in the trim colour.
			Piece(Left, Top + Height * 0.28f, Width * 0.21f, Height * 0.24f, FCampusShape::Slanted(Width * 0.05f), Sleeve);
			Piece(Left + Width * 0.79f, Top + Height * 0.28f, Width * 0.21f, Height * 0.24f, FCampusShape::Slanted(Width * 0.05f), Sleeve);
			Piece(Left, Top + Height * 0.49f, Width * 0.21f, Height * 0.05f, FCampusShape(), Secondary);
			Piece(Left + Width * 0.79f, Top + Height * 0.49f, Width * 0.21f, Height * 0.05f, FCampusShape(), Secondary);
			// Torso, the trim placket down its middle, and the hem band.
			Piece(Left + Width * 0.22f, Top + Height * 0.28f, Width * 0.56f, Height * 0.42f, FCampusShape::Notched(Width * 0.06f), Primary);
			Piece(Mid - Width * 0.03f, Top + Height * 0.30f, Width * 0.06f, Height * 0.40f, FCampusShape(), Secondary);
			Piece(Left + Width * 0.22f, Top + Height * 0.63f, Width * 0.56f, Height * 0.09f, FCampusShape(), Secondary);
			// The collar: a V cut out of the shoulder line in the trim colour.
			Piece(Mid - Width * 0.13f, Top + Height * 0.17f, Width * 0.26f, Height * 0.11f,
				FCampusShape::Clipped(Width * 0.12f), Secondary);
			// Legs.
			Piece(Mid - Width * 0.24f, Top + Height * 0.72f, Width * 0.19f, Height * 0.28f, FCampusShape(), Leg);
			Piece(Mid + Width * 0.05f, Top + Height * 0.72f, Width * 0.19f, Height * 0.28f, FCampusShape(), Leg);

			return LayerId + 2;
		}

	private:
		FLinearColor Primary = FLinearColor::White;
		FLinearColor Secondary = FLinearColor::Gray;
	};

	/**
	 * THE PROP'S OWN RECIPE, DRAWN. CampusCosmetics.h already says exactly where every cylinder and
	 * cube of a prop sits, so the card paints its front elevation (the catalogue's Y across, Z up)
	 * instead of showing one flat colour and hoping. A bike reads as a bike, a trophy as a trophy,
	 * and a prop added to the table tomorrow gets its card art for free.
	 *
	 * A part laid on its side (Pitch 90, which is how the wheels and the speaker drivers are built)
	 * has its X and Z extents swapped before it is measured, or the wheels would draw as slivers.
	 */
	class SClosetDecorArt : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SClosetDecorArt)
			: _Entry(nullptr)
			{}
			SLATE_ARGUMENT(const CampusCosmetics::FDecorEntry*, Entry)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Entry = InArgs._Entry;
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(96.f, 96.f); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2f Size = AllottedGeometry.GetLocalSize();
			if (!Entry || Size.X < 16.f || Size.Y < 16.f)
			{
				return LayerId;
			}

			// Measure the recipe first: how wide it stands and how tall it reaches.
			float Span = 0.f;
			float Reach = 0.f;
			FLinearColor Wash(0.f, 0.f, 0.f, 1.f);
			bool bFirstPart = true;
			for (int32 Index = 0; Index < CampusCosmetics::MaxDecorParts; ++Index)
			{
				const CampusCosmetics::FDecorPart& Part = Entry->Parts[Index];
				if (!Part.Mesh)
				{
					continue;
				}
				const float PartHeight = Laid(Part) ? Part.SizeX : Part.SizeZ;
				Span = FMath::Max(Span, FMath::Abs(Part.Y) * 2.f + Part.SizeY);
				Reach = FMath::Max(Reach, Part.Z + PartHeight * 0.5f);
				if (bFirstPart)
				{
					// The first primitive in a recipe is the prop's body or base, so its colour is
					// the one the card's backdrop should be lit by.
					Wash = Part.Color;
					bFirstPart = false;
				}
			}
			if (Span <= 0.f || Reach <= 0.f)
			{
				return LayerId;
			}

			CampusAngular::DrawVerticalFade(OutDrawElements, LayerId, AllottedGeometry, FVector2f::ZeroVector, Size,
				FCampusShape::Notched(8.f), (Wash * 0.30f).CopyWithNewOpacity(0.95f), ArtFloor);

			// Fit the elevation inside the well, with room under it for the shadow bar.
			const float Pad = 10.f;
			const float RoomX = FMath::Max(Size.X - Pad * 2.f, 8.f);
			const float RoomY = FMath::Max(Size.Y - Pad * 2.f - 6.f, 8.f);
			const float Scale = FMath::Min(RoomX / Span, RoomY / Reach);
			const float Base = Size.Y - Pad - 4.f;
			const float Centre = Size.X * 0.5f;

			// The prop stands on something: a soft bar, so it does not float in the card.
			CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId + 1, AllottedGeometry,
				FVector2f(Centre - Span * Scale * 0.5f, Base), FVector2f(FMath::Max(Span * Scale, 2.f), 3.f),
				FCampusShape::Clipped(1.5f), FLinearColor(1.f, 1.f, 1.f, 0.14f));

			for (int32 Index = 0; Index < CampusCosmetics::MaxDecorParts; ++Index)
			{
				const CampusCosmetics::FDecorPart& Part = Entry->Parts[Index];
				if (!Part.Mesh)
				{
					continue;
				}
				const bool bLaid = Laid(Part);
				const float W = FMath::Max(Part.SizeY * Scale, 2.f);
				const float H = FMath::Max((bLaid ? Part.SizeX : Part.SizeZ) * Scale, 2.f);
				// +Y is screen LEFT, because the lobby camera looks down -X at the line-up.
				const float X = Centre - Part.Y * Scale - W * 0.5f;
				const float Y = Base - (Part.Z * Scale + H * 0.5f);
				CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId + 2, AllottedGeometry,
					FVector2f(X, Y), FVector2f(W, H), ShapeFor(Part, W, H), Part.Color);
			}
			return LayerId + 3;
		}

	private:
		/** Pitch 90 lays a primitive on its side: its local Z points across the view, so the extent
		 *  that reads as HEIGHT in a front elevation is its X, not its Z. */
		static bool Laid(const CampusCosmetics::FDecorPart& Part)
		{
			const float Pitch = FMath::Abs(FMath::Fmod(Part.Pitch, 180.f));
			return Pitch > 45.f && Pitch < 135.f;
		}

		/** Which angular shape stands in for which engine primitive. A cylinder and a sphere read as
		 *  round once their corners are cut; a cone tapers towards whichever end is up. */
		static FCampusShape ShapeFor(const CampusCosmetics::FDecorPart& Part, float W, float H)
		{
			const float Small = FMath::Min(W, H);
			if (FCString::Strcmp(Part.Mesh, CampusCosmetics::ShapeSphere()) == 0)
			{
				return FCampusShape::Clipped(Small * 0.45f);
			}
			if (FCString::Strcmp(Part.Mesh, CampusCosmetics::ShapeCylinder()) == 0)
			{
				return FCampusShape::Clipped(Small * (Laid(Part) ? 0.45f : 0.22f));
			}
			if (FCString::Strcmp(Part.Mesh, CampusCosmetics::ShapeCone()) == 0)
			{
				const bool bPointDown = FMath::Abs(FMath::Fmod(FMath::Abs(Part.Pitch), 360.f) - 180.f) < 45.f;
				FCampusShape Shape;
				if (bPointDown)
				{
					Shape.CutBL = Shape.CutBR = W * 0.5f;
				}
				else
				{
					Shape.CutTL = Shape.CutTR = W * 0.5f;
				}
				return Shape;
			}
			return FCampusShape::Clipped(Small * 0.10f);
		}

		const CampusCosmetics::FDecorEntry* Entry = nullptr;
	};
}

// -------------------------------------------------------------------------------------------------
// Ownership and the one read

bool SCampusLobbyWidget::OwnsCosmetic(FName Id) const
{
	if (Id.IsNone())
	{
		return false;
	}
	if (const UCampusBackend* Api = Backend())
	{
		return Api->OwnsCosmetic(Id.ToString());
	}
	// No backend at all (editor, or a build without one): only what is free, which is the starter.
	const int32 Outfit = CampusCosmetics::FindOutfit(Id);
	return Outfit != INDEX_NONE && CampusCosmetics::Outfit(Outfit).bFree;
}

void SCampusLobbyWidget::RefreshCosmetics()
{
	UCampusBackend* Api = Backend();
	if (bCosmeticsAsked || !Api || !Api->IsSignedIn())
	{
		return;
	}
	bCosmeticsAsked = true;
	Api->LoadCosmetics([this](bool bOk, const FString&)
	{
		if (!bOk)
		{
			// A wardrobe that could not be read is not an error worth a dialog: the player wears the
			// starter and the closet says so. The latch stays set so this cannot loop.
			return;
		}
		// LoadCosmetics has just mirrored the server's answer into the save, so the scene can be
		// rebuilt straight from it - both of these read the save, not the response.
		if (Controller.IsValid())
		{
			Controller->ApplyLobbyOutfit();
			Controller->RefreshLobbyDecor();
		}
		Refresh();
	});
}

// -------------------------------------------------------------------------------------------------
// Open / close / equip

void SCampusLobbyWidget::OpenWardrobe()
{
	// A FRESH VISIT MAY FIND A WARDROBE THE LAST ONE COULD NOT. The latch exists to stop a failed
	// read feeding itself, not to cost the player their closet for the rest of the session - so it is
	// released here, and only while the read has never actually succeeded. Once it has, reopening the
	// closet costs nothing, which on a tunnel metered at 20,000 requests a month is the point.
	const UCampusBackend* Api = Backend();
	if (Api && !Api->bCosmeticsLoaded)
	{
		bCosmeticsAsked = false;
	}
	RefreshCosmetics();
	Show(EScreen::Wardrobe);
}

void SCampusLobbyWidget::CloseWardrobe()
{
	// Nothing to commit here: every equip already wrote the save and sent its PUT, so closing cannot
	// lose a choice and cannot be the thing that saves one either.
	if (UCampusSave* Save = Profile())
	{
		Save->Persist();
	}
	Show(EScreen::Lobby);
}

void SCampusLobbyWidget::EquipOutfit(FName OutfitId)
{
	if (!OwnsCosmetic(OutfitId))
	{
		Toast(TEXT("YOU DO NOT OWN THAT OUTFIT YET."));
		return;
	}
	// THE PREVIEW IS THE SAVE. One write, then the figure is re-dressed from it, so what the player
	// sees and what the next match sends can never be two different things.
	if (UCampusSave* Save = Profile())
	{
		Save->EquippedOutfit = OutfitId;
		Save->Persist();
	}
	if (Controller.IsValid())
	{
		Controller->ApplyLobbyOutfit();
	}
	Refresh();

	UCampusBackend* Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		return;
	}
	bCosmeticsBusy = true;
	Api->SaveCosmetics(OutfitId.ToString(), [this](bool bOk, const FString& Error)
	{
		bCosmeticsBusy = false;
		if (!bOk)
		{
			// The outfit STAYS ON. The device already believes it and so does the next launch; the
			// honest thing is to say the server has not caught up, not to take the shirt back.
			Toast(Error.IsEmpty() ? TEXT("SAVED ON THIS DEVICE - THE SERVER DID NOT ANSWER.") : Error);
		}
		Refresh();
	});
}

void SCampusLobbyWidget::WearPreset(int32 Index)
{
	const UCampusSave* Save = Profile();
	const FName Stored = Save && Save->OutfitPresets.IsValidIndex(Index) ? Save->OutfitPresets[Index] : NAME_None;
	if (Stored.IsNone())
	{
		Toast(FString::Printf(TEXT("PRESET %d IS EMPTY - TAP ITS SAVE CHIP TO STORE THIS LOOK."), Index + 1));
		return;
	}
	EquipOutfit(Stored);
}

void SCampusLobbyWidget::StorePreset(int32 Index)
{
	UCampusSave* Save = Profile();
	if (!Save || Index < 0 || Index >= CampusCosmetics::NumPresets)
	{
		return;
	}
	const FName Worn = Save->EquippedOutfit;
	// Sized on write, never in the save's declaration: an existing save file has no presets array at
	// all and still has to load.
	if (Save->OutfitPresets.Num() < CampusCosmetics::NumPresets)
	{
		Save->OutfitPresets.SetNum(CampusCosmetics::NumPresets);
	}
	Save->OutfitPresets[Index] = Worn;
	Save->Persist();
	Refresh();
	Toast(FString::Printf(TEXT("PRESET %d SAVED."), Index + 1));

	UCampusBackend* Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		return;
	}
	bCosmeticsBusy = true;
	Api->SavePreset(Index, Worn.ToString(), [this](bool bOk, const FString& Error)
	{
		bCosmeticsBusy = false;
		if (!bOk)
		{
			// THE PRESET STAYS SAVED on this device, whatever the server said (409 NOT_OWNED, 422
			// SLOT_MISMATCH, a dead tunnel). Taking it back would be the one outcome the player
			// cannot act on; the next GET /me/cosmetics is where the server gets the last word.
			Toast(Error.IsEmpty() ? TEXT("SAVED ON THIS DEVICE - THE SERVER DID NOT ANSWER.") : Error);
		}
		Refresh();
	});
}

void SCampusLobbyWidget::SetDecorAt(int32 AnchorIndex, FName ItemId)
{
	UCampusSave* Save = Profile();
	if (!Save || AnchorIndex < 0 || AnchorIndex >= CampusCosmetics::NumAnchors)
	{
		return;
	}
	const FName Key(CampusCosmetics::Anchor(AnchorIndex).Id);
	if (ItemId.IsNone())
	{
		Save->LobbyDecor.Remove(Key);
	}
	else
	{
		const CampusCosmetics::FDecorEntry* Entry = CampusCosmetics::DecorFor(ItemId);
		if (!Entry || !CampusCosmetics::DecorAllowedAt(*Entry, AnchorIndex))
		{
			Toast(TEXT("THAT ONE DOES NOT GO THERE."));
			return;
		}
		if (!OwnsCosmetic(ItemId))
		{
			Toast(TEXT("YOU DO NOT OWN THAT YET."));
			return;
		}
		Save->LobbyDecor.Add(Key, ItemId);
	}
	Save->Persist();
	// The scene changes under the finger: the whole sheet exists so this is watchable.
	if (Controller.IsValid())
	{
		Controller->RefreshLobbyDecor();
	}
	Refresh();

	UCampusBackend* Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		return;
	}
	// The WHOLE room every time, bare anchors included (UCampusBackend::SaveLobbyDecor sends those as
	// null), so "I took the trophy off the pedestal" is something the server can actually be told.
	TMap<FString, FString> Wire;
	Wire.Reserve(Save->LobbyDecor.Num());
	for (const TPair<FName, FName>& Pair : Save->LobbyDecor)
	{
		Wire.Add(Pair.Key.ToString(), Pair.Value.ToString());
	}
	bCosmeticsBusy = true;
	Api->SaveLobbyDecor(Wire, [this](bool bOk, const FString& Error)
	{
		bCosmeticsBusy = false;
		if (!bOk)
		{
			// Same rule as the closet: the prop STAYS where the player put it. A 422 UNKNOWN_ANCHOR
			// or NOT_DECOR means this build and the server disagree about the catalogue, and the
			// answer to that is the next load, not snatching the room back mid-tap.
			Toast(Error.IsEmpty() ? TEXT("SAVED ON THIS DEVICE - THE SERVER DID NOT ANSWER.") : Error);
		}
		Refresh();
	});
}

// -------------------------------------------------------------------------------------------------
// The closet

TSharedRef<SWidget> SCampusLobbyWidget::BuildOutfitCard(int32 OutfitIndex)
{
	const CampusCosmetics::FOutfitEntry& Entry = CampusCosmetics::Outfit(OutfitIndex);
	const FName Id(Entry.Id);
	const UCampusSave* Save = Profile();
	// Through OutfitFor, so an old save holding a retired id still ticks the starter card rather than
	// leaving the grid with nothing selected at all.
	const bool bWorn = Save && FName(CampusCosmetics::OutfitFor(Save->EquippedOutfit).Id) == Id;
	const FLinearColor Frame = CampusCosmetics::RarityColor(Entry.Rarity);

	// The starter wears the mesh's own authored texture rather than a tint, so its card shows a
	// neutral pair and says ORIGINAL instead of pretending to a colour it does not have.
	const bool bOriginal = Entry.bKeepMeshMaterial;
	const FLinearColor Primary = bOriginal ? FLinearColor(0.58f, 0.60f, 0.64f, 1.f) : Entry.Primary;
	const FLinearColor Secondary = bOriginal ? FLinearColor(0.30f, 0.32f, 0.36f, 1.f) : Entry.Secondary;

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Notched(14.f))
			.Fill(bWorn ? CardInkOn : CardInk)
			// RARITY IS THE FRAME, SELECTION IS THE ACCENT, and neither is ever the only signal: the
			// rarity word is printed on the card and the worn one carries a WORN tick.
			.Border(bWorn ? Accent : Frame.CopyWithNewOpacity(0.75f))
			.BorderWidth(bWorn ? 3.f : 2.f)
			.AccentEdge(bWorn ? Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bWorn ? 6.f : 0.f)
			.ContentPadding(FMargin(10.f, 10.f))
			.OnClicked(FOnClicked::CreateLambda([this, Id]()
			{
				EquipOutfit(Id);
				return FReply::Handled();
			}))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SClosetOutfitArt).Primary(Primary).Secondary(Secondary)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Visibility(EVisibility::HitTestInvisible)
					.Text(FText::FromString(FString(Entry.Name).ToUpper()))
					.Font(CampusAngular::Display(17))
					.ColorAndOpacity(FLinearColor::White)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					RarityRow(Entry.Rarity, bOriginal, 12)
				]
			]
		]
		// Above the card, so it is painted over the plate rather than inside its padding - and
		// hit-test invisible, or it would eat the tap on the card's top-left corner.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.f)
		[
			bWorn
				? ClosetChip(CampusAngular::EGlyph::Tick, LOCTEXT("Worn", "WORN"), Accent)
				: SNullWidget::NullWidget
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildPresetRow()
{
	const UCampusSave* Save = Profile();
	const FName WornId = Save ? FName(CampusCosmetics::OutfitFor(Save->EquippedOutfit).Id) : NAME_None;

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < CampusCosmetics::NumPresets; ++Index)
	{
		const FName Stored = Save && Save->OutfitPresets.IsValidIndex(Index) ? Save->OutfitPresets[Index] : NAME_None;
		const bool bFilled = !Stored.IsNone();
		// "ACTIVE" is the honest reading of a preset slot here: there is no fourth piece of state in
		// the save saying which one was tapped, so the lit tile is the one holding the worn look.
		const bool bActive = bFilled && FName(CampusCosmetics::OutfitFor(Stored).Id) == WornId;
		const FText Label = bFilled
			? FText::FromString(FString(CampusCosmetics::OutfitFor(Stored).Name).ToUpper())
			: LOCTEXT("PresetFree", "EMPTY SLOT");

		// FillWidth, not a fixed width: three tiles share whatever the figure's column leaves them.
		Row->AddSlot().FillWidth(1.f).Padding(Index == 0 ? 0.f : Gap, 0.f, 0.f, 0.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox).HeightOverride(PresetHeight)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Slanted(10.f))
					.Fill(bActive ? CardInkOn : (bFilled ? CardInk : FLinearColor(0.f, 0.f, 0.f, 0.62f)))
					.Border(bActive ? Accent : (bFilled ? Accent.CopyWithNewOpacity(0.45f) : CampusAngular::Hairline))
					.BorderWidth(bActive ? 2.f : 1.f)
					.AccentEdge(bActive ? Accent : FLinearColor::Transparent)
					.AccentEdgeWidth(bActive ? 5.f : 0.f)
					// The right inset is the SAVE chip's room: the two must never overlap, whatever
					// the column width turns out to be.
					.ContentPadding(FMargin(14.f, 8.f, SaveWidth + 16.f, 8.f))
					.OnClicked(FOnClicked::CreateLambda([this, Index]()
					{
						WearPreset(Index);
						return FReply::Handled();
					}))
					[
						// CLIPPED: the longest outfit name in the catalogue fits, but the tile shares
						// its row with two others and a future "Varsity Jacket Mk II" must spill
						// nowhere near the SAVE chip rather than paint over it.
						SNew(SVerticalBox)
						.Clipping(EWidgetClipping::ClipToBounds)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							// A hanger, so the tile reads as a saved look before a word is read.
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Hanger).Size(16.f)
								.Color(bFilled ? Accent : Faint)
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("PresetNumber", "PRESET {0}"), FText::AsNumber(Index + 1)))
								.Font(CampusAngular::Display(15))
								.ColorAndOpacity(bFilled ? Accent : Faint)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								bActive
									? StaticCastSharedRef<SWidget>(SNew(SCampusGlyph)
										.Glyph(CampusAngular::EGlyph::Tick).Size(16.f).Color(Accent))
									: SNullWidget::NullWidget
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(Label)
							.Font(CampusAngular::Body(12))
							.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, bFilled ? 0.86f : 0.52f))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							// Never colour alone: the lit tile also says what it is.
							bActive
								? Quiet(LOCTEXT("PresetActive", "WEARING THIS"), 11, Accent)
								: SNullWidget::NullWidget
						]
					]
				]
			]
			// SAVE SITS ABOVE THE TILE so it takes the tap instead of wearing the preset - the same
			// trick the mode cards' green "?" uses, and it beats a hidden long press on a phone. It
			// is a WORD, not a "+": 80 x 48 is a real target and it says what it will do.
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox).WidthOverride(SaveWidth).HeightOverride(SaveHeight)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Slanted(10.f))
					.Fill(CampusAngular::Go)
					.Border(FLinearColor(1.f, 1.f, 1.f, 0.35f))
					.BorderWidth(1.f)
					.ContentPadding(FMargin(4.f, 2.f))
					.OnClicked(FOnClicked::CreateLambda([this, Index]()
					{
						StorePreset(Index);
						return FReply::Handled();
					}))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PresetSave", "SAVE"))
						.Font(CampusAngular::Display(16))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		];
	}
	return Row;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildWardrobe()
{
	// The camera moves to the figure and the rest of the line-up hides: the SAME call the profile page
	// makes, so there is still exactly one 3D path in the lobby.
	if (Controller.IsValid())
	{
		Controller->FocusProfile(true);
	}

	// OWNED ONLY, and the order is the catalogue's so a new outfit lands where the designer put it.
	TArray<int32> Wearable;
	for (int32 Index = 0; Index < CampusCosmetics::NumOutfits(); ++Index)
	{
		if (OwnsCosmetic(FName(CampusCosmetics::Outfit(Index).Id)))
		{
			Wearable.Add(Index);
		}
	}

	const UCampusBackend* Api = Backend();
	const bool bSignedIn = Api && Api->IsSignedIn();
	const bool bLoading = bSignedIn && !Api->bCosmeticsLoaded;
	const UCampusSave* Save = Profile();
	const CampusCosmetics::FOutfitEntry& Worn = CampusCosmetics::OutfitFor(Save ? Save->EquippedOutfit : NAME_None);

	// ---- the grid. Three FillWidth columns, one fixed row height. -------------------------------
	TSharedRef<SScrollBox> Grid = SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed);
	for (int32 First = 0; First < Wearable.Num(); First += GridColumns)
	{
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		for (int32 Column = 0; Column < GridColumns; ++Column)
		{
			const int32 At = First + Column;
			Line->AddSlot().FillWidth(1.f).Padding(Column == 0 ? 0.f : Gap, 0.f, 0.f, 0.f)
			[
				SNew(SBox).HeightOverride(CardHeight)
				[
					// An EMPTY column in the tail of the last line, so one card and three cards leave
					// the grid the same shape and the cards never stretch to fill a short row.
					Wearable.IsValidIndex(At) ? BuildOutfitCard(Wearable[At]) : SNullWidget::NullWidget
				]
			];
		}
		Grid->AddSlot().Padding(0.f, First == 0 ? 0.f : Gap, 0.f, 0.f)[Line];
	}

	// The empty state goes INSIDE the scroll, under the starter card, where the emptiness is - and it
	// names the shop, because "nothing here" is not an answer the player can act on.
	if (bLoading)
	{
		Grid->AddSlot().Padding(0.f, 32.f, 0.f, 8.f)
		[
			ClosetEmpty(CampusAngular::EGlyph::Clock, LOCTEXT("ClosetLoadingTitle", "LOADING YOUR WARDROBE"),
				LOCTEXT("ClosetLoadingHint", "Everything you have unlocked is on its way. The free outfit is wearable now."))
		];
	}
	else if (Wearable.Num() <= 1)
	{
		Grid->AddSlot().Padding(0.f, 32.f, 0.f, 8.f)
		[
			bSignedIn
				? ClosetEmpty(CampusAngular::EGlyph::Lock, LOCTEXT("ClosetBareTitle", "ONLY YOUR STARTER OUTFIT"),
					LOCTEXT("ClosetBareHint", "Buy outfits in the Store, or win them from events. Anything you unlock lands here and can be worn straight away."))
				: ClosetEmpty(CampusAngular::EGlyph::Wifi, LOCTEXT("ClosetOutTitle", "SIGNED OUT"),
					LOCTEXT("ClosetOutHint", "Only the free outfit is available while you are signed out. Sign in from the lobby to wear the rest of your closet."))
		];
	}

	// One honest line at the foot of the panel, with a glyph as well as a colour.
	TSharedRef<SWidget> Status = StatusLine(CampusAngular::EGlyph::Tick,
		LOCTEXT("ClosetHint", "Tap an outfit to wear it. The figure changes as you tap."), Dim);
	if (bCosmeticsBusy)
	{
		Status = StatusLine(CampusAngular::EGlyph::Clock, LOCTEXT("ClosetSaving", "Saving your choice..."), Accent);
	}
	else if (!bSignedIn)
	{
		Status = StatusLine(CampusAngular::EGlyph::Wifi,
			LOCTEXT("ClosetOffline", "Signed out - only the free outfit is available."), Faint);
	}
	else if (bLoading)
	{
		Status = StatusLine(CampusAngular::EGlyph::Clock, LOCTEXT("ClosetLoading", "Loading your wardrobe..."), Faint);
	}

	// ---- the OWNED panel ------------------------------------------------------------------------
	TSharedRef<SWidget> Panel = SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Notched(20.f))
		// Opaque: this panel sits over the live 3D scene and has to stay readable against any of it.
		.Fill(CampusAngular::InkStrong)
		.Border(CampusAngular::Hairline)
		.BorderWidth(1.f)
		.ContentPadding(FMargin(20.f, 16.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Hanger).Size(20.f).Color(Accent)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Quiet(LOCTEXT("ClosetOwned", "OWNED"), 22, FLinearColor::White)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
				[
					ClosetChip(CampusAngular::EGlyph::Tick,
						FText::Format(LOCTEXT("ClosetCount", "{0} OUTFITS"), FText::AsNumber(Wearable.Num())), Dim)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Visibility(EVisibility::HitTestInvisible)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f)[Rule()]
			+ SVerticalBox::Slot().FillHeight(1.f)[Grid]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)[Rule()]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[Status]
		];

	// ---- the category rail, pinned to the screen's right edge -----------------------------------
	auto RailEntry = [](const FText& Text, CampusAngular::EGlyph Glyph, bool bOn, FOnClicked OnClicked)
	{
		return StaticCastSharedRef<SWidget>(SNew(SBox).HeightOverride(RailHeight)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Tab(8.f, 10.f))
				.Fill(bOn ? RailInkOn : RailInk)
				.Border(bOn ? Accent : CampusAngular::Hairline)
				.BorderWidth(bOn ? 2.f : 1.f)
				.AccentEdge(bOn ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.18f))
				.AccentEdgeWidth(5.f)
				.ContentPadding(FMargin(8.f, 10.f))
				.OnClicked(OnClicked)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SCampusGlyph).Glyph(Glyph).Size(26.f).Color(bOn ? Accent : Dim)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(Text)
						.Font(CampusAngular::Display(14))
						.ColorAndOpacity(bOn ? FLinearColor::White : Dim)
						.Justification(ETextJustify::Center)
					]
				]
			]);
	};

	TSharedRef<SWidget> Rail = SNew(SBox).WidthOverride(RailWidth)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				// Already here, so this one only has to look selected - but it still answers the tap,
				// because a control that does nothing at all feels broken.
				RailEntry(LOCTEXT("RailOutfit", "OUTFIT"), CampusAngular::EGlyph::Hanger, true,
					FOnClicked::CreateLambda([this]() { Refresh(); return FReply::Handled(); }))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, Gap, 0.f, 0.f)
			[
				// The decor sheet lives behind this rail rather than behind a tab of its own: both are
				// "how my lobby looks", and one rail for the pair keeps the lobby's menu column short.
				RailEntry(LOCTEXT("RailDecor", "DECOR"), CampusAngular::EGlyph::Trophy, false,
					FOnClicked::CreateLambda([this]()
					{
						DecorAnchor = FMath::Clamp(DecorAnchor, 0, CampusCosmetics::NumAnchors - 1);
						Show(EScreen::Decor);
						return FReply::Handled();
					}))
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer).Visibility(EVisibility::HitTestInvisible)]
		];

	// ---- the figure's column: presets at the bottom, everything else out of the way --------------
	TSharedRef<SWidget> Figure = SNew(SVerticalBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer).Visibility(EVisibility::HitTestInvisible)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 12.f)
		[
			Quiet(LOCTEXT("ClosetDrag", "DRAG THE FIGURE TO TURN IT"), 13, Faint)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Quiet(LOCTEXT("PresetsTitle", "PRESETS"), 16, FLinearColor::White)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(10.f, 2.f, 0.f, 0.f)
			[
				Quiet(LOCTEXT("PresetsHint", "TAP TO WEAR  -  SAVE STORES THIS LOOK"), 12, Faint)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SBox).MaxDesiredWidth(PresetRowMaxWidth).HAlign(HAlign_Left)
			// Only the cards and the presets go dead while a PUT is in the air; the X never does.
			.IsEnabled(!bCosmeticsBusy)
			[
				BuildPresetRow()
			]
		];

	// NO BACKDROP, for the same reason the profile page has none: a scrim over the left half would
	// hide the very thing the screen is for. ONE drag area under the whole screen, with every piece
	// of chrome above it hit-test transparent, so the figure turns from anywhere that is not a card.
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusDragArea)
			.OnDrag_Lambda([this](float DeltaX)
			{
				if (Controller.IsValid())
				{
					Controller->RotateDisplay(-DeltaX * 0.4f);
				}
			})
		]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Visibility(EVisibility::SelfHitTestInvisible).Padding(FMargin(24.f, 16.f))
			[
				SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
				// Header: the title and what is being worn on the left, the wallet and the X on the
				// right. OUTSIDE the panel, so the grid can never scroll the X away and an equip still
				// in flight cannot trap the player on this screen.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						TitleFlash(32.f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
						+ SVerticalBox::Slot().AutoHeight()
						[
							ClosetTitle(LOCTEXT("WardrobeTitle", "WARDROBE"), 30)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
						[
							// What you have on, in words, so the screen answers its own first question.
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
							[
								Quiet(LOCTEXT("ClosetWearing", "WEARING"), 13, Faint)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
							[
								Quiet(FText::FromString(FString(Worn.Name).ToUpper()), 16, FLinearColor::White)
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								RarityRow(Worn.Rarity, Worn.bKeepMeshMaterial, 12)
							]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Visibility(EVisibility::HitTestInvisible)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						// Read-only on purpose: buying is the store's screen, and a "+" here would be a
						// second place that has to know how diamonds are sold.
						CampusStyle::Counter(CampusAngular::EGlyph::Gem, CampusAngular::Gem,
							FText::AsNumber(Api ? Api->Diamonds : 0))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close, FOnClicked::CreateLambda([this]()
						{
							CloseWardrobe();
							return FReply::Handled();
						}), CloseSize)
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
					+ SHorizontalBox::Slot().FillWidth(ClosetSceneShare)[Figure]
					+ SHorizontalBox::Slot().FillWidth(ClosetPanelShare).Padding(16.f, 0.f, 0.f, 0.f)
					[
						SNew(SBox).IsEnabled(!bCosmeticsBusy)[Panel]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(Gap, 0.f, 0.f, 0.f)[Rail]
				]
			]
		];
}

// -------------------------------------------------------------------------------------------------
// The decor sheet

TSharedRef<SWidget> SCampusLobbyWidget::BuildDecorAnchorChip(int32 AnchorIndex)
{
	const CampusCosmetics::FAnchor& Anchor = CampusCosmetics::Anchor(AnchorIndex);
	const bool bSelected = DecorAnchor == AnchorIndex;
	const UCampusSave* Save = Profile();
	const FName* OnIt = Save ? Save->LobbyDecor.Find(FName(Anchor.Id)) : nullptr;
	const CampusCosmetics::FDecorEntry* Placed = OnIt ? CampusCosmetics::DecorFor(*OnIt) : nullptr;

	return SNew(SBox).WidthOverride(AnchorChipWidth).HeightOverride(AnchorChipHeight)
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Slanted(10.f))
			.Fill(bSelected ? CardInkOn : FLinearColor(0.f, 0.f, 0.f, 0.62f))
			.Border(bSelected ? Accent : (Placed ? Accent.CopyWithNewOpacity(0.40f) : CampusAngular::Hairline))
			.BorderWidth(bSelected ? 2.f : 1.f)
			.AccentEdge(bSelected ? Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bSelected ? 5.f : 0.f)
			.ContentPadding(FMargin(12.f, 6.f, 10.f, 6.f))
			.OnClicked(FOnClicked::CreateLambda([this, AnchorIndex]()
			{
				DecorAnchor = AnchorIndex;
				Refresh();
				return FReply::Handled();
			}))
			[
				SNew(SHorizontalBox)
				// FILLED OR BARE IS A GLYPH, not a shade: a tick for something standing there, a plus
				// for a spot waiting to be filled.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SCampusGlyph)
					.Glyph(Placed ? CampusAngular::EGlyph::Tick : CampusAngular::EGlyph::Plus)
					.Size(18.f)
					.Color(Placed ? CampusAngular::Go : Faint)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString(Anchor.Name).ToUpper()))
						.Font(CampusAngular::Display(15))
						.ColorAndOpacity(bSelected ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.86f))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(Placed
							? FText::FromString(FString(Placed->Name).ToUpper())
							: LOCTEXT("AnchorBare", "NOTHING HERE"))
						.Font(CampusAngular::Body(12))
						.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Placed ? 0.82f : 0.50f))
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildDecorCard(int32 DecorIndex)
{
	const UCampusSave* Save = Profile();
	const FName Key(CampusCosmetics::Anchor(DecorAnchor).Id);
	const FName* OnIt = Save ? Save->LobbyDecor.Find(Key) : nullptr;

	// DecorIndex < 0 is the EMPTY card: clearing an anchor has to be a card like any other, or it
	// becomes a gesture the player has to be told about.
	if (DecorIndex < 0)
	{
		const bool bBare = OnIt == nullptr;
		return SNew(SBox).WidthOverride(DecorCardWidth).HeightOverride(DecorCardHeight)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Notched(12.f))
					.Fill(bBare ? CardInkOn : CardInk)
					.Border(bBare ? Accent : CampusAngular::Hairline)
					.BorderWidth(bBare ? 3.f : 1.f)
					.ContentPadding(FMargin(12.f, 10.f))
					.OnClicked(FOnClicked::CreateLambda([this]()
					{
						SetDecorAt(DecorAnchor, NAME_None);
						return FReply::Handled();
					}))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Close).Size(32.f).Color(Dim)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							Quiet(LOCTEXT("DecorEmpty", "EMPTY"), 16, FLinearColor(1.f, 1.f, 1.f, 0.82f))
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							// The word changes with the state, so this never relies on the accent frame.
							Quiet(bBare
								? LOCTEXT("DecorAlreadyBare", "THIS SPOT IS BARE")
								: LOCTEXT("DecorClear", "TAP TO CLEAR IT"), 11, Faint)
						]
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.f)
				[
					bBare
						? ClosetChip(CampusAngular::EGlyph::Tick, LOCTEXT("DecorBareChip", "BARE"), Accent)
						: SNullWidget::NullWidget
				]
			];
	}

	const CampusCosmetics::FDecorEntry& Entry = CampusCosmetics::Decor(DecorIndex);
	const FName Id(Entry.Id);
	const bool bPlaced = OnIt && *OnIt == Id;
	const FLinearColor Frame = CampusCosmetics::RarityColor(Entry.Rarity);

	return SNew(SBox).WidthOverride(DecorCardWidth).HeightOverride(DecorCardHeight)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Notched(12.f))
				.Fill(bPlaced ? CardInkOn : CardInk)
				.Border(bPlaced ? Accent : Frame.CopyWithNewOpacity(0.70f))
				.BorderWidth(bPlaced ? 3.f : 2.f)
				.AccentEdge(bPlaced ? Accent : FLinearColor::Transparent)
				.AccentEdgeWidth(bPlaced ? 6.f : 0.f)
				.ContentPadding(FMargin(10.f, 10.f))
				.OnClicked(FOnClicked::CreateLambda([this, Id]()
				{
					SetDecorAt(DecorAnchor, Id);
					return FReply::Handled();
				}))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						// The prop's own recipe, drawn: no icon to keep in step with the catalogue.
						SNew(SClosetDecorArt).Entry(&Entry)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 7.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Visibility(EVisibility::HitTestInvisible)
						.Text(FText::FromString(FString(Entry.Name).ToUpper()))
						.Font(CampusAngular::Display(15))
						.ColorAndOpacity(bPlaced ? Accent : FLinearColor::White)
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
					[
						RarityRow(Entry.Rarity, false, 11)
					]
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.f)
			[
				bPlaced
					? ClosetChip(CampusAngular::EGlyph::Tick, LOCTEXT("DecorPlaced", "PLACED"), Accent)
					: SNullWidget::NullWidget
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildDecorScreen()
{
	const UCampusSave* Save = Profile();
	const CampusCosmetics::FAnchor& Spot = CampusCosmetics::Anchor(DecorAnchor);
	const FName* OnSpot = Save ? Save->LobbyDecor.Find(FName(Spot.Id)) : nullptr;
	const CampusCosmetics::FDecorEntry* OnSpotEntry = OnSpot ? CampusCosmetics::DecorFor(*OnSpot) : nullptr;

	// How full the room is: the one number that says whether this screen has anything left to do.
	int32 Filled = 0;
	for (int32 Index = 0; Index < CampusCosmetics::NumAnchors; ++Index)
	{
		const FName* At = Save ? Save->LobbyDecor.Find(FName(CampusCosmetics::Anchor(Index).Id)) : nullptr;
		Filled += At && CampusCosmetics::DecorFor(*At) ? 1 : 0;
	}

	// The anchors along the top of the sheet. One horizontal SScrollBox: the chips tap on RELEASE
	// (SCampusAngularButton's PreciseTap), so a sideways swipe across them scrolls instead of picking.
	TSharedRef<SScrollBox> Anchors = SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		.ScrollBarVisibility(EVisibility::Collapsed)
		.AllowOverscroll(EAllowOverscroll::Yes);
	for (int32 Index = 0; Index < CampusCosmetics::NumAnchors; ++Index)
	{
		Anchors->AddSlot().Padding(Index == 0 ? 0.f : Gap, 0.f, 0.f, 0.f)[BuildDecorAnchorChip(Index)];
	}

	// The props the selected anchor will take, EMPTY first. OWNED ONLY, like the closet.
	TSharedRef<SScrollBox> Props = SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		.ScrollBarVisibility(EVisibility::Collapsed)
		.AllowOverscroll(EAllowOverscroll::Yes);
	Props->AddSlot()[BuildDecorCard(INDEX_NONE)];
	int32 Offered = 0;
	for (int32 Index = 0; Index < CampusCosmetics::NumDecor(); ++Index)
	{
		const CampusCosmetics::FDecorEntry& Entry = CampusCosmetics::Decor(Index);
		if (!CampusCosmetics::DecorAllowedAt(Entry, DecorAnchor) || !OwnsCosmetic(FName(Entry.Id)))
		{
			continue;
		}
		Props->AddSlot().Padding(Gap, 0.f, 0.f, 0.f)[BuildDecorCard(Index)];
		++Offered;
	}
	if (Offered == 0)
	{
		// Not a bare strip: the EMPTY card, then why there is nothing beside it.
		Props->AddSlot().Padding(24.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(EmptyBlockWidth).HeightOverride(DecorCardHeight).VAlign(VAlign_Center)
			[
				ClosetEmpty(CampusAngular::EGlyph::Lock, LOCTEXT("DecorNoneTitle", "NOTHING FITS THIS SPOT YET"),
					LOCTEXT("DecorNoneHint", "Decor comes from the Store and from events. The overhead spot takes banners only."))
			]
		];
	}

	TSharedRef<SWidget> Sheet = SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Notched(18.f))
		.Fill(CampusAngular::InkStrong)
		.Border(CampusAngular::Hairline)
		.BorderWidth(1.f)
		.ContentPadding(FMargin(20.f, 14.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Quiet(LOCTEXT("DecorPickAnchor", "1.  PICK A SPOT"), 15, Dim)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Visibility(EVisibility::HitTestInvisible)]
				// The readout: which spot is being edited and what is standing on it right now.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Quiet(FText::Format(LOCTEXT("DecorOnSpot", "{0}  -  {1}"),
						FText::FromString(FString(Spot.Name).ToUpper()),
						OnSpotEntry ? FText::FromString(FString(OnSpotEntry->Name).ToUpper())
							: LOCTEXT("DecorOnSpotBare", "EMPTY")), 15, Accent)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SBox).HeightOverride(AnchorChipHeight)[Anchors]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)[Rule()]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 8.f)
			[
				Quiet(Offered > 0
					? LOCTEXT("DecorPickItem", "2.  PICK WHAT GOES THERE")
					: LOCTEXT("DecorPickNone", "2.  NOTHING YOU OWN FITS THIS SPOT"), 15, Dim)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(DecorCardHeight)[Props]
			]
		];

	// A SHEET, NOT A SCREEN: everything above it is the live lobby, so the prop that just landed on
	// the pedestal is visible the instant it is tapped. The drag area keeps working up there too.
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusDragArea)
			.OnDrag_Lambda([this](float DeltaX)
			{
				if (Controller.IsValid())
				{
					Controller->RotateDisplay(-DeltaX * 0.4f);
				}
			})
		]
		+ SOverlay::Slot()
		[
			// SELF-HIT-TEST-INVISIBLE ALL THE WAY DOWN THE FRAME. The drag area is this overlay's
			// SIBLING, not its child, so a hit-testable container stacked on top of it would swallow
			// every drag that did not land on an actual control - and turning the line-up is half of
			// what this screen is for. Only the header row and the sheet itself take touches.
			SNew(SSafeZone).Visibility(EVisibility::SelfHitTestInvisible).Padding(FMargin(24.f, 16.f))
			[
				SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						TitleFlash(30.f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						ClosetTitle(LOCTEXT("DecorTitle", "DECORATE LOBBY"), 28)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)
					[
						ClosetChip(CampusAngular::EGlyph::Tick,
							FText::Format(LOCTEXT("DecorFilled", "{0} / {1} SPOTS FILLED"),
								FText::AsNumber(Filled), FText::AsNumber(CampusCosmetics::NumAnchors)),
							Filled > 0 ? Accent : Dim)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Visibility(EVisibility::HitTestInvisible)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						// NAMED, not an X: this goes back to the closet it was opened from, and a bare
						// cross on a sheet reads as "close to the lobby", which is not what it does.
						SNew(SBox).HeightOverride(CloseSize)
						[
							SNew(SCampusAngularButton)
							.Shape(FCampusShape::Slanted(12.f))
							.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.86f))
							.Border(CampusAngular::Hairline)
							.BorderWidth(1.f)
							.ContentPadding(FMargin(18.f, 6.f))
							.OnClicked(FOnClicked::CreateLambda([this]()
							{
								// Back to the closet, which is where this was opened from.
								Show(EScreen::Wardrobe);
								return FReply::Handled();
							}))
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
								[
									SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Hanger).Size(18.f).Color(Accent)
								]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(LOCTEXT("DecorBack", "WARDROBE"))
									.Font(CampusAngular::Display(17)).ColorAndOpacity(FLinearColor::White)
								]
							]
						]
					]
				]
				// The gap between the header and the sheet IS the lobby: it must pass touches down to
				// the drag area rather than absorb them.
				+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Bottom)
				[
					SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
					+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
					[
						Quiet(LOCTEXT("DecorDrag", "DRAG THE LOBBY TO TURN IT"), 13, Faint)
					]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).IsEnabled(!bCosmeticsBusy)[Sheet]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
