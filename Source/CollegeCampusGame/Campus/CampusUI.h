// Menu screens built in Slate (no widget assets to cook):
//   Lobby level: Splash -> Sign in -> Event post (optional) -> Lobby -> Create / Join / Settings.
//   Match level: Waiting room -> (game HUD lives in SCampusTouchControls) -> Settings -> Win screen.
//
// Look: Garena Free Fire. Sharp edges only - every panel, tab, counter and button is an
// FCampusShape (slanted parallelogram or clipped corner box, see CampusAngular.h), bold condensed
// italic uppercase type, dark translucent fills, one accent (#FFB800) with white text.

#pragma once

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "Campus/CampusAngular.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class ACampusLobbyPlayerController;
class UCampusBackend;
class UCampusParty;
class UCampusPlayBilling;
class UCampusSave;
class UCampusSocial;
struct FCampusOffer;
struct FCampusEvent;
struct FCampusEventProgress;
struct FCampusEventRewardProgress;
struct FCampusMission;
struct FCampusMilestone;
struct FCampusPartyMember;
struct FCampusPlayProduct;
struct FCampusPlayerCard;
struct FCampusProfile;
struct FCampusSummary;
struct FCampusSummaryRow;
struct FCampusHistoryRow;
class ACampusMatchPlayerController;
class SBox;
class SVerticalBox;

/** Shared look: colours, fonts, angular buttons, angular panels. */
namespace CampusStyle
{
	enum class EButton : uint8 { Dark, Accent, Go, Ghost };

	/** Display type (bold condensed italic) when bBold, readable body type otherwise. */
	FSlateFontInfo Font(int32 Size, bool bBold = true);

	/** Sharp-cornered fallback brushes for the few places that still want a plain SBorder. */
	const FSlateBrush* PanelBrush();
	const FSlateBrush* CardBrush(bool bSelected);

	TSharedRef<SWidget> Button(const FText& Label, FOnClicked OnClicked, EButton Kind = EButton::Dark,
		int32 FontSize = 22, FVector2D MinSize = FVector2D(220.f, 64.f), bool bEnabled = true);

	/** Free Fire's yellow slanted call to action (bottom-right START). */
	TSharedRef<SWidget> StartButton(const FText& Label, FOnClicked OnClicked, FVector2D MinSize = FVector2D(300.f, 96.f));

	/** Slanted dark menu tab (left-hand STORE / CHARACTER / VAULT / WEAPONS column). */
	TSharedRef<SWidget> MenuTab(const FText& Label, CampusAngular::EGlyph Glyph, FOnClicked OnClicked,
		bool bHighlighted = false, FVector2D MinSize = FVector2D(248.f, 62.f));

	/** Small slanted selector pill (team size, map, rounds, store tabs). */
	TSharedRef<SWidget> Pill(const FText& Label, bool bSelected, FOnClicked OnClicked, FVector2D MinSize = FVector2D(140.f, 48.f));

	/** The same pill, but its SELECTED look is an attribute. Tapping one chip in a row restyles every
	 *  chip in that row through its own lambda - no widget is torn down, so a row of mode chips can
	 *  change which one is lit without the lobby rebuilding anything around it. */
	TSharedRef<SWidget> SelectablePill(const FText& Label, TAttribute<bool> Selected, FOnClicked OnClicked,
		FVector2D MinSize = FVector2D(140.f, 48.f));

	/** Square clipped-corner icon button: friends, mail, settings gear, close. */
	TSharedRef<SWidget> IconButton(CampusAngular::EGlyph Glyph, FOnClicked OnClicked, float Size = 60.f,
		FLinearColor Tint = FLinearColor::White, bool bAccentFill = false);

	/** Slanted currency counter with an optional "+" cap, like the coin / gem chips. */
	TSharedRef<SWidget> Counter(CampusAngular::EGlyph Glyph, const FLinearColor& Tint, TAttribute<FText> Value, FOnClicked OnPlus = FOnClicked());

	/** Clipped-corner dark panel. */
	TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, FMargin ContentPadding = FMargin(30.f, 22.f), float Cut = 18.f, bool bStrong = false);

	/** Card plate; the selected one gets an accent border and left edge. */
	TSharedRef<SWidget> Card(TSharedRef<SWidget> Content, bool bSelected, FMargin ContentPadding = FMargin(18.f, 12.f));

	TSharedRef<SWidget> Label(const FText& Text, int32 Size, FLinearColor Color = FLinearColor::White, bool bBold = true);

	extern const FLinearColor Accent;
	extern const FLinearColor Go;
	extern const FLinearColor Muted;
}

/** The one options panel, shared by the lobby gear and the in-match gear. */
namespace CampusOptions
{
	/** Builds the rows; OnChanged is called after every tap so the caller can rebuild and persist. */
	TSharedRef<SWidget> Body(UCampusSave* Save, TFunction<void()> OnChanged);

	/** Pushes the saved graphics choice into the engine's scalability levels. */
	void ApplyGraphics(const UCampusSave* Save);
}

/** Empty area that reports horizontal drags (mouse or one finger). Used to turn the squad. */
class SCampusDragArea : public SLeafWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnDrag, float);

	SLATE_BEGIN_ARGS(SCampusDragArea) {}
		SLATE_EVENT(FOnDrag, OnDrag)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override { return LayerId; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void Drag(float DeltaX, float DeltaTime);

	FOnDrag OnDrag;
	bool bDragging = false;
	int32 PointerIndex = INDEX_NONE;
	float LastX = 0.f;
	double LastMoveTime = 0.0;
	/** Units per second at release; keeps the character spinning like a lobby turntable, then eases out. */
	float Velocity = 0.f;
	/** How far this press has travelled. Nothing turns until it passes DragThreshold: a tap on open
	 *  screen moves a pixel or two, and that used to be enough to fling the line-up side-on. */
	float Travel = 0.f;
	/** Slate units of finger travel before a press counts as a drag at all. */
	static constexpr float DragThreshold = 8.f;
	/** Ceiling on the measured fling speed, so one stuttery frame cannot define the whole spin. */
	static constexpr float MaxFlingSpeed = 900.f;
};

/** Which button on a notice card was tapped; the lobby owns the answer, because it owns dialogs. */
DECLARE_DELEGATE_OneParam(FOnCampusNoticeAction, FString);

/**
 * ONE slide-in invite card (Campus/CampusNoticeUI.cpp). 460 x 128, notched, accent left edge, a colour
 * avatar, the sender's name, a DUO / SQUAD chip, ACCEPT + REJECT at 52 tall, and a countdown bar driven
 * off the SERVER clock.
 *
 * The card body is deliberately NOT tappable: an accidental accept is worse than a missed invite.
 * Every motion is a RenderTransform, never layout padding - a layout change would re-flow the whole
 * lobby overlay every frame, which shows up immediately on a budget phone.
 */
class SCampusNoticeCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusNoticeCard) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCampusSocial>, Social)
		SLATE_ARGUMENT(FString, Key)
		SLATE_EVENT(FOnCampusNoticeAction, OnAccept)
		SLATE_EVENT(FOnCampusNoticeAction, OnReject)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	const FString& GetKey() const { return Key; }
	/** Starts the 200 ms slide back out to the right. Idempotent. */
	void BeginExit();
	bool IsExiting() const { return bExiting; }
	/** The exit finished, so the stack may drop this widget. */
	bool IsGone() const;
	/** The cards below a departing card rise by one card height over 150 ms. */
	void PlayReflow(float FromOffsetY);

private:
	/** The live notice, or null once the subsystem has dropped it. */
	const struct FCampusNotice* Notice() const;

	TWeakObjectPtr<UCampusSocial> Social;
	FString Key;
	FOnCampusNoticeAction OnAccept;
	FOnCampusNoticeAction OnReject;

	FCurveSequence Enter;
	FCurveHandle Slide;
	FCurveHandle Fade;
	FCurveSequence Exit;
	FCurveHandle ExitSlide;
	FCurveSequence Reflow;
	FCurveHandle Rise;
	float ReflowFrom = 0.f;
	bool bExiting = false;
	/** Cached so the countdown bar's width lambda does no string or date work per frame. */
	float Remaining = 0.f;
	float Total = 45.f;
};

/**
 * The card stack, above every lobby screen and above the busy scrim (SOCIAL_PARTY §14.1) - an invite
 * that lands while the Store is open, or while a claim is in flight, must still be answerable. Slate
 * hit-tests top down, so the card's buttons win while the scrim still blocks everything beneath it.
 *
 * At most three cards are drawn; a fourth collapses into a "+N MORE" chip that opens the tray.
 */
class SCampusNoticeStack : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusNoticeStack) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UCampusSocial>, Social)
		SLATE_EVENT(FOnCampusNoticeAction, OnAccept)
		SLATE_EVENT(FOnCampusNoticeAction, OnReject)
		SLATE_EVENT(FSimpleDelegate, OnOpenTray)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/** UCampusSocial survives level travel and this widget does not, so the OnNoticesChanged binding
	 *  comes off here or the next lobby load fires into a dead `this`. */
	~SCampusNoticeStack();
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Card geometry, shared with the lobby so the stack never overlaps the top bar or the logo. */
	static constexpr float CardWidth = 460.f;
	static constexpr float CardHeight = 128.f;
	static constexpr float CardGap = 12.f;
	static constexpr int32 MaxVisible = 3;

private:
	/** Rebuilds the column only when the set of live keys actually changes. */
	void Reconcile();

	TWeakObjectPtr<UCampusSocial> Social;
	FOnCampusNoticeAction OnAccept;
	FOnCampusNoticeAction OnReject;
	FSimpleDelegate OnOpenTray;
	TSharedPtr<SVerticalBox> Column;
	TArray<TSharedPtr<SCampusNoticeCard>> Cards;
	int32 Overflow = 0;
	bool bDirty = true;
	FDelegateHandle ChangedHandle;
};

class SCampusLobbyWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusLobbyWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACampusLobbyPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SCampusLobbyWidget();
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	// Free Fire / PUBG style flow: Splash -> (first time) Agreement -> Login choice -> Email login / Register ->
	// (first time) Nickname -> Lobby. The lobby opens the Store, Rooms (quick / create / join by code), Account.
	enum class EScreen : uint8 { Splash, Agreement, LoginChoice, EmailLogin, Register, Nickname, Lobby, Store, Rooms,
		Account, Event, Daily, Settings, Missions, Streak, AdultConfirm, CollegePick, MapSelect,
		/** COSMETICS (Campus/CampusWardrobeUI.cpp): the PUBG-style closet, and the lobby decor sheet
		 *  reached from inside it. Wardrobe keeps the profile camera on the figure; Decor gives the
		 *  scene back to the full line-up, because decor is judged against the whole room. */
		Wardrobe, Decor,
		/** BATTLE ROYALE queue overlay (Campus/CampusBRUI.cpp): MATCHING..., the timer, big CANCEL. */
		Matchmaking,
		// migration 012 / 013: the friends list, the invite picker opened from a "+ INVITE" plate, the
		// stacked invite tray, the full-screen profile, and the player-of-the-match screen.
		Friends, FriendPick, InviteTray, Profile, Summary };
	enum class EStoreTab : uint8 { Guns, Offers, Packs };
	enum class EMissionTab : uint8 { Daily, Weekly, Weekend };
	enum class EFriendTab : uint8 { Friends, Add, Requests };
	/** Which trailing action a roster row carries - the only thing that differs between the
	 *  GAME FRIENDS list, the ADD results, the REQUESTS lists and the blocked list. */
	enum class EFriendRow : uint8 { Game, Add, Incoming, Outgoing, Blocked };
	enum class EProfileTab : uint8 { Showcase, Profile, History };

	void Show(EScreen Screen);
	/** Asks for ONE repaint of the current screen on the next Tick. Deliberately deferred: a single
	 *  tap reaches this two or three times (the chip's own handler, UCampusParty::OnChanged, then the
	 *  server's reply), and repainting from inside the tap tears down the very button that is still
	 *  routing it - which is what the lobby's visible glitch on every interaction was. */
	void Refresh();
	void Toast(const FString& Message);
	/** Runs an API call with a "please wait" overlay; the reply re-enables the screen. */
	void Busy(const FString& What);
	void Done(bool bOk, const FString& Error, TFunction<void()> OnOk = nullptr);
	/** Where to go once signed in: the nickname screen the first time, else the lobby. */
	void AfterSignIn();
	void SaveMatchChoice();

	UCampusBackend* Backend() const;
	UCampusSave* Profile() const;
	UCampusParty* Party() const;
	UCampusSocial* Social() const;
	/** Pushes the local player into the squad roster and keeps the plate row the right width.
	 *  GATED: while a SERVER party exists the roster is the server's, and this returns early -
	 *  otherwise the local guess would fight ApplyRoster on every repaint. */
	void SyncParty();

	TSharedRef<SWidget> Backdrop(float Alpha) const;
	TSharedRef<SWidget> Dialog(const FText& Title, TSharedRef<SWidget> Body, float Width, bool bClose, EScreen CloseTo);
	/** A panel over whatever screen is showing. Lives in the root overlay, so it works from the lobby,
	 *  the friends list and the full-screen profile alike - none of which can host a dialog of their own. */
	void ShowFloating(TSharedRef<SWidget> Content, float Width);
	/** A yes/no on top of the current screen: leaving a squad for another, removing a member. */
	void AskConfirm(const FText& Title, const FText& Body, const FText& Yes, TFunction<void()> OnYes);
	void CloseConfirm();
	TSharedRef<SWidget> Field(const FText& Hint, FString& Target, bool bPassword = false);

	TSharedRef<SWidget> BuildSplash();
	TSharedRef<SWidget> BuildAgreement();
	TSharedRef<SWidget> BuildLoginChoice();
	TSharedRef<SWidget> BuildEmailLogin();
	TSharedRef<SWidget> BuildRegister();
	TSharedRef<SWidget> BuildNickname();
	/** Builds the lobby overlay ONCE and hands the same widget back on every later repaint. */
	TSharedRef<SWidget> BuildLobby();
	/** Re-fills the lobby's regions in place. The overlay, the safe zone and the drag area are never
	 *  re-created, and a region whose own data did not move is left exactly as it is - which is why a
	 *  mode chip, the ONLINE strip or an invite can no longer flicker the left menu column. */
	void RepaintLobby();
	/** One region. Re-filled only when Stamp moves; Stamp 0 means "never filled yet". */
	void FillRegion(const TSharedPtr<SBox>& Slot, uint32& Stamp, uint32 Now, TFunctionRef<TSharedRef<SWidget>()> Build);
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildMenuColumn();
	TSharedRef<SWidget> BuildBottomBar();
	TSharedRef<SWidget> BuildSquadRow();
	TSharedRef<SWidget> BuildNamePlate(const FCampusPartyMember& Member);
	TSharedRef<SWidget> BuildInvitePlate();
	/** The SOLO / DUO / SQUAD chips alone. The map plate below them is its own region. */
	TSharedRef<SWidget> BuildModeSelect();
	/** The squad size the chips should show: the server party's when there is one, else the local
	 *  choice. Read through a lambda by the chips, so changing it restyles rather than rebuilds. */
	int32 ShownTeamSize() const;
	/** START, or "WAITING FOR ARJUN" when somebody else leads: a member cannot start their own room
	 *  out from under the squad. */
	TSharedRef<SWidget> BuildStartButton();

	// ---- map / mode select (Campus/CampusMapSelect.cpp). Opened from the lobby plate above START.
	/** The lobby plate next to START: the chosen mode over the chosen map, tap to change. */
	TSharedRef<SWidget> BuildModePlate();
	/** The full screen: map banners on the left, mode cards on the right, CONFIRM bottom-right. */
	TSharedRef<SWidget> BuildMapSelect();
	/** One left-hand map banner. */
	TSharedRef<SWidget> BuildMapBanner(int32 Index);
	/** One right-hand mode card, with its green "?" badge and any lock / time row. */
	TSharedRef<SWidget> BuildModeCard(int32 Index);
	/** Opens the map-select screen with the saved choice loaded as the pending one. */
	void OpenMapSelect();
	/** CONFIRM: writes the pending choice to the profile and goes back to the lobby. */
	void ConfirmMapSelect();
	/** CampusCatalog indices in the order the published `modes` blob asks for, with the rows it
	 *  switched off dropped. The compiled order is the fallback for anything the blob does not name,
	 *  and the WHOLE compiled list is the fallback when the blob would leave nothing to play: a
	 *  console mistake may not take the game away. */
	void OrderedMaps(TArray<int32>& Out) const;
	void OrderedModes(TArray<int32>& Out) const;
	/** The content key for a map banner: `map/<client id lowercase>`, which is what the console
	 *  uploads against. Art for a key nobody published is simply never asked for. */
	static FString MapBannerKey(const TCHAR* MapId);

	// ---- wardrobe + lobby decoration (Campus/CampusWardrobeUI.cpp)
	/** The closet: the figure on the left under the profile camera, a 3-column grid of OWNED outfits
	 *  on the right, the three preset slots along the bottom. */
	TSharedRef<SWidget> BuildWardrobe();
	/** One outfit card: rarity frame, two colour swatches, name, and a tick when it is worn. */
	TSharedRef<SWidget> BuildOutfitCard(int32 OutfitIndex);
	/** The 1 / 2 / 3 preset row: tap to wear, SAVE to store what is being worn. */
	TSharedRef<SWidget> BuildPresetRow();
	/** The decor sheet: the eight anchors on the left, the owned props (and EMPTY) on the right. */
	TSharedRef<SWidget> BuildDecorScreen();
	/** One of the eight anchor chips along the top of the sheet. */
	TSharedRef<SWidget> BuildDecorAnchorChip(int32 AnchorIndex);
	/** One prop card. DecorIndex < 0 is the EMPTY card that clears the anchor. */
	TSharedRef<SWidget> BuildDecorCard(int32 DecorIndex);
	/** Opens the closet, asking the server for the owned list if it has not already. */
	void OpenWardrobe();
	/** X: writes the choice (save first, then PUT) and goes back to the lobby. */
	void CloseWardrobe();
	/** Equip previews INSTANTLY: the save and the lobby figure change on the tap, the PUT follows. */
	void EquipOutfit(FName OutfitId);
	void WearPreset(int32 Index);
	void StorePreset(int32 Index);
	/** Puts an item (or nothing, for NAME_None) on an anchor and rebuilds the scene at once. */
	void SetDecorAt(int32 AnchorIndex, FName ItemId);
	/** True when this cosmetic may be shown as wearable: owned, or free. */
	bool OwnsCosmetic(FName Id) const;
	/** GET /me/cosmetics, once per app run. Latched for the same reason bStorePricesQueried is. */
	void RefreshCosmetics();

	// ---- BATTLE ROYALE matchmaking (Campus/CampusBRUI.cpp). The queue itself lives in UCampusBRQueue.
	/** True when the confirmed mode is Battle Royale, which is what makes START queue instead of
	 *  opening the Custom Room sheet. */
	bool IsBattleRoyaleSelected() const;
	/** START with BR selected: POST /v1/br/queue and show the overlay. */
	void StartBattleRoyale();
	/** The overlay's one big button: DELETE /v1/br/queue and back to the lobby. */
	void CancelMatchmaking();
	/** MATCHING... / timer / players found / big CANCEL, Free Fire's queue screen. */
	TSharedRef<SWidget> BuildMatchmaking();
	/** bs.placed (or the POST reply, or the fallback poll): host the map, or travel to the session's
	 *  server. Travels IMMEDIATELY - the session is still FILLING and the waiting area is where the rest
	 *  of it arrives. A failed travel is retried twice before the player goes back to searching. */
	void EnterBRMatch(const struct FCampusBRMatch& Match);
	class UCampusBRQueue* BRQueue() const;
	TSharedRef<SWidget> BuildStore();
	TSharedRef<SWidget> BuildGunCard(int32 Index);
	TSharedRef<SWidget> BuildOfferCard(const FCampusOffer& Offer);
	/** The real-money button: Google Play where it is available, Razorpay everywhere else. */
	TSharedRef<SWidget> MoneyButton(const FString& ItemId, const FString& ItemName, float PriceInr, FVector2D Size);
	/** Play Billing for this game instance, or null off Android / when IAP is not packaged. */
	UCampusPlayBilling* Billing() const;

	// ---- the store screen (CampusStoreUI.cpp). Left tab rail, wallet strip, wrapping card grid.
	//      Diamonds are bought from Google Play; guns are bought with Diamonds from our backend.
	TSharedRef<SWidget> BuildStoreV2();
	TSharedRef<SWidget> BuildStoreWallet();
	TSharedRef<SWidget> BuildStoreSideTabs();
	TSharedRef<SWidget> BuildStoreGunGrid();
	TSharedRef<SWidget> BuildStoreGunTile(int32 Index);
	TSharedRef<SWidget> BuildStoreDiamondGrid();
	TSharedRef<SWidget> BuildStoreOfferGrid();
	/** A money card (diamond pack or bundle). Amount 0 = not a plain diamond pack, so show its name. */
	TSharedRef<SWidget> BuildStorePackTile(const FCampusOffer& Pack, int32 Amount, bool bFeatured);
	TSharedRef<SWidget> BuildStoreEmpty(const FText& Title, const FText& Hint);
	/** Asks Play once per store visit for its localized prices, and re-offers anything it still holds. */
	void StoreQueryPlayPrices();
	void StoreBuyProduct(const FString& ProductId, const FString& ItemName);
	void StoreBuyGun(int32 Index);
	void StoreEquipGun(int32 Index);
	UCampusPlayBilling* StoreBilling() const;

	// ---- live config: maintenance, minimum version, announcements (Campus/CampusLiveConfigUI.cpp).
	//      Everything the admin console can change without a release, as the player sees it.
	/** The full-screen plate above every lobby screen: UPDATE REQUIRED, else the MAINTENANCE lock.
	 *  Built ONCE into the root overlay and driven entirely by attributes, so it costs nothing while
	 *  it is collapsed and never has to be rebuilt to change state. It is in the LOBBY widget and
	 *  nowhere else, which is what stops it ever closing over a live match. */
	TSharedRef<SWidget> BuildLiveGate();
	/** Lobby entry, and the lock's RETRY: paint the cached blob, then GET /v1/live-config. The call is
	 *  throttled inside the subsystem - once per app run, then only after a stale resume. */
	void RefreshLiveConfig();
	/** Re-fills the announcement column under the invite stack. Called from Tick only. */
	void RefreshAnnouncements();
	TSharedRef<SWidget> BuildAnnouncementCard(const struct FCampusAnnouncement& Row);
	/** THE SOFT UPDATE NUDGE, for a floor this phone only knows from the blob cached on disk
	 *  (UCampusBackend::IsUpdateAvailable). It sits at the TOP of the announcement column, wears the
	 *  gem edge like a NEWS card, and carries the store link when the console published one. It is
	 *  never the dead-end gate: nothing has confirmed the floor, so nothing may be taken away. */
	TSharedRef<SWidget> BuildUpdateCard();

	TSharedRef<SWidget> BuildRooms();
	TSharedRef<SWidget> BuildAccount();
	TSharedRef<SWidget> BuildDaily();
	TSharedRef<SWidget> BuildSettings();

	// ---- live events (Campus/CampusEventsUI.cpp). What the console is running right now: the lobby
	//      banner, and the sheet behind it with every reward, its progress and CLAIM.
	/** The lobby banner for the live event: the console's picture, the label, the time left and the
	 *  multipliers. Zero footprint when nothing is running or starting inside the published horizon. */
	TSharedRef<SWidget> BuildEventBanner();
	/** The EVENTS sheet. The menu tab, the mail icon and the banner all land here. */
	TSharedRef<SWidget> BuildEvent();
	/** One reward row: what it gives, how far along it is, and CLAIM / CLAIMED / the count. */
	TSharedRef<SWidget> BuildEventRewardRow(const FCampusEventRewardProgress& Reward);
	/** ENTERING the sheet: picks the event to show and clears the progress latch, so the one read
	 *  happens here and never from a Build function. */
	void OpenEventSheet();
	/** GET /v1/events/:id/progress for the event the sheet is showing. Latched, asked once per visit. */
	void RefreshEventProgress();
	/** CLAIM on a row: POST with an idempotency key, then the wallet and the rows come back from the
	 *  server rather than being guessed at here. */
	void ClaimEventReward(int32 RewardIndex);

	// ---- engagement
	/** The weekend board panel under the menu tabs: countdown, top 3, my row, empty state. */
	TSharedRef<SWidget> BuildWeekendPanel();
	/** The banner for a settled weekend's unclaimed reward. */
	TSharedRef<SWidget> BuildClaimBanner();
	TSharedRef<SWidget> BuildMissions();
	TSharedRef<SWidget> BuildMissionCard(const FCampusMission& Mission, const FString& PeriodId);
	TSharedRef<SWidget> BuildStreak();
	TSharedRef<SWidget> BuildAdultConfirm();
	TSharedRef<SWidget> BuildCollegePick();
	/** One live "2D 04:11:08" countdown, driven off the server clock by a text lambda (no rebuild). */
	TSharedRef<SWidget> Countdown(const FDateTime& Until, const FText& Prefix, int32 Size, FLinearColor Color) const;
	/** Refreshes GET /engagement/home and repaints when it lands. */
	void RefreshEngagement();
	/** Shared tail of every claim: wallet from the response, toast, repaint, reward-cap message. */
	void AfterClaim(bool bOk, const FString& Error, const FString& OkText);
	/** The IST game day (YYYYMMDD) by the server clock, for the once-a-day streak popup. */
	FString ServerGameDay() const;

	// ---- social: friends, the invite picker, the tray (Campus/CampusSocialUI.cpp)
	/** The full-screen friends page: left tab rail (GAME FRIENDS / ADD), the REQUESTS button in its
	 *  bottom corner, the counter top-right, and - on ADD - the search bar along the bottom. */
	TSharedRef<SWidget> BuildFriends();
	/** One rail tab: active is a SOLID accent plate with dark text, as the reference has it. */
	TSharedRef<SWidget> BuildFriendTab(EFriendTab Tab, const FText& Label);
	/** The angular name banner with the level pinned to its far end. */
	TSharedRef<SWidget> BuildNameBanner(const FCampusPlayerCard& Card, float Width);
	/** The lobby's right-edge invite strip, collapsed until the player opens it. */
	TSharedRef<SWidget> BuildLobbyFriends();
	/** The same rows as a picker, opened from a "+ INVITE" plate: one tap sends the invite. */
	TSharedRef<SWidget> BuildFriendPick();
	/** Every stacked invite with ACCEPT / REJECT rows, behind the stack's "+N MORE" chip. */
	TSharedRef<SWidget> BuildInviteTray();
	/** One 72-tall roster row: square avatar, name banner, centred status, trailing action. */
	TSharedRef<SWidget> BuildFriendRow(const FCampusPlayerCard& Card, EFriendRow Kind);
	/** The colour plate that stands in for an avatar everywhere in the social UI. */
	TSharedRef<SWidget> Avatar(const FCampusPlayerCard& Card, float Size) const;
	/** "ONLINE" / "SOCIAL OFFLINE - TAP TO RETRY": one honest line at the top of the friends panel. */
	TSharedRef<SWidget> BuildSocialStatus();
	/** The count on the top-bar friends icon: friend requests waiting on me. Nothing when zero. */
	TSharedRef<SWidget> FriendRequestBadge();
	/** ACCEPT on a card: confirms first when it would abandon another squad. */
	void AnswerInvite(const FString& InviteId, bool bAccept);
	void SendInvite(const FString& PlayerId);
	void OpenProfile(const FString& PlayerId);
	/** The squad strip under the plate row: who leads, LEAVE SQUAD, and the leader's MANAGE. */
	TSharedRef<SWidget> BuildPartyBar();
	/** The leader's remove list. A visible row with its own button beats a hidden long press, and
	 *  beats a tiny X on every plate - which on a phone is a mis-tap generator. */
	void OpenManageSquad();

	// ---- post-match (Campus/CampusSocialUI.cpp)
	/** PLAYER OF THE MATCH: the MVP big, every player's line, friend buttons, CONTINUE. */
	TSharedRef<SWidget> BuildSummary();
	TSharedRef<SWidget> BuildSummaryRow(const FCampusSummaryRow& Row);
	/** Fetches GET /rooms/:code/summary and shows the screen when it lands. */
	void OpenSummary(const FString& RoomCode);
	/** + ADD FRIEND / REQUESTED / ACCEPT / FRIENDS, from friend_state, optimistic on tap. */
	TSharedRef<SWidget> FriendButton(const FCampusPlayerCard& Card, FVector2D Size);

	// ---- reporting a player (Campus/CampusReportUI.cpp). One surface, reachable from every player
	//      card: the profile page (which the party plates, the friends rows and the ONLINE strip all
	//      tap through to) and the post-match scoreboard, which has no profile tap of its own.
	/** The square "!" button that opens the reason picker. Reads SENT once this session's report
	 *  landed, and is simply absent for my own row. */
	TSharedRef<SWidget> BuildReportButton(const FString& PlayerId, const FString& PlayerName,
		const FString& RoomCode, float Size);
	/** The sheet: the five reasons a moderator can act on, one tap each. Floats over whatever screen
	 *  asked for it, so it works from the profile page and the scoreboard alike. */
	void OpenReportPlayer(const FString& PlayerId, const FString& PlayerName, const FString& RoomCode);
	/** POST /v1/players/:id/report. 409 (already reported today) and 429 are both answered with a
	 *  sentence rather than an error the player did not earn. */
	void SendPlayerReport(const FString& PlayerId, const FString& PlayerName, const FString& Reason,
		const FString& RoomCode);

	// ---- profile (Campus/CampusProfileUI.cpp)
	/** Full screen: the 3D figure on the left, the angular tabbed panel on the right. */
	TSharedRef<SWidget> BuildProfile();
	TSharedRef<SWidget> BuildProfileHeader();
	TSharedRef<SWidget> BuildProfileTab();
	TSharedRef<SWidget> BuildShowcaseTab();
	TSharedRef<SWidget> BuildHistoryTab();
	TSharedRef<SWidget> BuildRankBlock();
	TSharedRef<SWidget> BuildStatsStrip();
	TSharedRef<SWidget> BuildMilestone(const FCampusMilestone& Badge);
	TSharedRef<SWidget> BuildHistoryRow(const FCampusHistoryRow& Row);
	/** One stats cell: glyph, number, label. */
	TSharedRef<SWidget> StatCell(CampusAngular::EGlyph Glyph, const FText& Value, const FText& Caption) const;
	void CloseProfile();

	// ---- profile picture (Campus/CampusAvatar.h + Campus/CampusAvatarPicker.h)
	//      Reached ONLY from the profile page's EDIT button, which opens the ACCOUNT sheet: one place
	//      to change the picture, so there is no second path to keep in step with the server.
	/** My avatar_url: the server's word when it has spoken, else the one cached in the save (so the
	 *  badge paints the real picture on the first frame rather than a level number that then flips). */
	FString MyAvatarUrl() const;
	/** The ACCOUNT sheet's PROFILE PICTURE block: the current plate, the preset grid, UPLOAD, REMOVE. */
	TSharedRef<SWidget> BuildAvatarChooser();
	/** One preset tile, by index into CampusAvatar::Presets(). */
	TSharedRef<SWidget> BuildAvatarPresetTile(int32 Index);
	/** Asks for the preset catalogue once per app run; repaints the sheet when it lands. */
	void EnsureAvatarPresets();
	/** UPLOAD PHOTO: the system photo picker, then PUT /me/avatar as raw image/jpeg. */
	void ChangeAvatarPhoto();
	/** REMOVE PHOTO: DELETE /me/avatar, after which the server puts a random preset on the account. */
	void RemoveAvatarPhoto();
	/** A preset tile tap. Optimistic - the plate changes at once - and rolled back if the server says no. */
	void ChooseAvatarPreset(const FString& PresetId);
	/** One place that writes a new avatar_url everywhere it is mirrored: the backend's cache, the save,
	 *  and the open profile page. */
	void ApplyMyAvatarUrl(const FString& NewUrl);

	TWeakObjectPtr<ACampusLobbyPlayerController> Controller;
	TSharedPtr<SBox> Content;
	/** What Content currently holds, so re-showing the same body does not re-attach it. */
	TSharedPtr<SWidget> ShownBody;

	// ---- the lobby's persistent widget tree.
	// THE RULE: the lobby is built once and repainted region by region. Nothing above this line is
	// re-created by a repaint, so the character drag, the touch that is still being routed and every
	// region that did not change all survive it.
	TSharedPtr<SBox> LobbyBody;
	TSharedPtr<SBox> TopBarBox;
	TSharedPtr<SBox> MenuColumnBox;
	TSharedPtr<SBox> WeekendBox;
	TSharedPtr<SBox> ClaimBannerBox;
	TSharedPtr<SBox> FriendsStripBox;
	TSharedPtr<SBox> SquadRowBox;
	TSharedPtr<SBox> PartyBarBox;
	TSharedPtr<SBox> ModeSelectBox;
	TSharedPtr<SBox> ModePlateBox;
	TSharedPtr<SBox> StartBox;
	/** The live event banner, under the claim banner in the same top-centre column. */
	TSharedPtr<SBox> EventBannerBox;
	/** EVERY region is fingerprinted. A repaint re-fills only the regions whose own data moved, so a
	 *  SOLO / DUO / SQUAD tap leaves the menu column, the map plate, START and the ONLINE strip alone -
	 *  and the chips themselves restyle from a lambda, so even they are not rebuilt. */
	uint32 TopBarStamp = 0;
	uint32 MenuColumnStamp = 0;
	uint32 WeekendStamp = 0;
	uint32 ClaimBannerStamp = 0;
	uint32 FriendsStripStamp = 0;
	uint32 SquadRowStamp = 0;
	uint32 PartyBarStamp = 0;
	uint32 ModeSelectStamp = 0;
	uint32 ModePlateStamp = 0;
	uint32 StartStamp = 0;
	uint32 EventBannerStamp = 0;

	EScreen Current = EScreen::Splash;
	EStoreTab StoreTab = EStoreTab::Guns;
	/** The Play price query has been asked for on this visit to the store. A build function must never
	 *  start a round trip: when this one did, the reply repainted the store, the repaint asked again,
	 *  and on a build Play answers with an empty catalogue the two fed each other without end. */
	bool bStorePricesQueried = false;
	/** The confirmed choice, mirrored from UCampusSave; indices into CampusCatalog. */
	int32 SelectedMap = 0;
	int32 SelectedMode = 0;
	int32 SelectedTeamSize = 4;
	/** What the map-select screen is showing before CONFIRM; closing with X throws it away. */
	int32 PendingMap = 0;
	int32 PendingMode = 0;
	bool bPendingRandomMap = false;
	/** The DAY / NIGHT chip on the BATTLE ROYALE card, before CONFIRM writes it to the profile. */
	bool bPendingBrNight = false;
	/** Mode whose "?" rules sheet is open, or INDEX_NONE. */
	int32 HelpMode = INDEX_NONE;

	// ---- cosmetics state (Campus/CampusWardrobeUI.cpp)
	/** GET /me/cosmetics has been asked for. LATCHED: a Build function must never start a round trip,
	 *  and a failed read whose error path repaints would otherwise feed itself - the same trap
	 *  bStorePricesQueried and bPresetsAsked exist for. */
	bool bCosmeticsAsked = false;
	/** HOW MANY cosmetic PUTs are in flight: every card and preset button disables while this is
	 *  above zero, so a double tap cannot race two equips at the server.
	 *
	 *  A COUNT AND NOT A FLAG. Equipping an outfit and saving a preset are two different requests the
	 *  player can start inside one second, and with a bool the FIRST reply cleared the latch while the
	 *  second was still in the air - the grid came back to life mid-save, which is the exact double
	 *  tap the latch exists to stop. */
	int32 CosmeticsInFlight = 0;
	bool IsCosmeticsBusy() const { return CosmeticsInFlight > 0; }
	/** Which of the eight anchors the decor sheet is editing. */
	int32 DecorAnchor = 0;
	float SplashTime = 0.f;
	int32 SplashTip = 0;
	bool bSplashReady = false;
	bool bResuming = false;
	bool bAgreeTicked = false;
	float ToastTime = 0.f;
	FString ToastText;
	bool bBusy = false;
	/** How long the call in flight has been waiting. The scrim BLOCKS taps from the first frame, but
	 *  only paints itself once this passes BusyScrimDelay - a LAN reply lands in two frames, and a
	 *  black wash that appears and vanishes inside 30 ms reads as the screen glitching. */
	float BusyTime = 0.f;
	static constexpr float BusyScrimDelay = 0.2f;
	FString BusyText;
	/** One repaint per frame, applied from Tick instead of from inside the event that asked for it. */
	bool bRefreshQueued = false;
	FDelegateHandle PartyChangedHandle;

	// ---- engagement / 18+ state
	EMissionTab MissionTab = EMissionTab::Daily;
	/** The "I am 18 or older" tick on the sign-in screen; nothing proceeds without it. */
	bool bAdultTicked = false;
	/** The streak popup has been offered this session, so it never re-opens behind the player. */
	bool bStreakOffered = false;
	/** A claim is in flight: every CLAIM button is disabled so nothing is double-tapped. */
	bool bClaiming = false;
	/** Profile screen: the college chosen but not yet saved, and the board-privacy tick. */
	FString PickedCollegeId;
	bool bShowCollegeTick = false;
	FString CollegeFilter;

	// ---- live events state (Campus/CampusEventsUI.cpp)
	/** Which event the sheet is showing; 0 before one has been chosen. */
	int32 EventShownId = 0;
	/** The server's progress for that event, held by pointer so this header needs no event struct. */
	TSharedPtr<FCampusEventProgress> EventProgress;
	bool bEventProgressLoading = false;
	/** The progress read has been asked for on this visit to the sheet. LATCHED for the reason
	 *  bStorePricesQueried is: a Build function must never start a round trip its own reply repaints
	 *  it into starting again. Cleared by OpenEventSheet. */
	bool bEventProgressAsked = false;
	/** The reward whose claim is in flight, or INDEX_NONE. One at a time, so a double tap cannot
	 *  race two POSTs - and the row that is waiting is the one that says so. */
	int32 EventClaimIndex = INDEX_NONE;

	// ---- moderation (Campus/CampusReportUI.cpp)
	/** Players reported during this session, so the button reads SENT without a second request. The
	 *  server's own rule is one report per target per IST game day; this is only the local echo. */
	TSet<FString> ReportedPlayers;
	/** A report is in flight: every reason button disables so nothing is sent twice. */
	bool bReportInFlight = false;

	// ---- social / profile state
	EFriendTab FriendTab = EFriendTab::Friends;
	EProfileTab ProfileTab = EProfileTab::Profile;
	/** Where the friends screen goes back to: the lobby, or straight back to a profile. */
	EScreen FriendsReturn = EScreen::Lobby;
	/** Whose profile is open; empty = mine. */
	FString ProfileTarget;
	/** A picture change (upload, preset or remove) is in flight: every tile and both buttons disable,
	 *  so a double tap cannot race two PUTs at the server. */
	bool bAvatarBusy = false;
	/** The preset the player just tapped, painted as selected while the server catches up. */
	FString OptimisticPresetId;
	/** The preset catalogue has been asked for once. LATCHED, because BuildAvatarChooser runs on every
	 *  repaint and its failure path repaints: without this, one failed GET would feed itself for ever -
	 *  the same trap bStorePricesQueried exists for. RETRY in the sheet clears it. */
	bool bPresetsAsked = false;
	/** Held by pointer so this header does not have to pull in CampusSocial.h. */
	TSharedPtr<FCampusProfile> ProfileData;
	TSharedPtr<FCampusSummary> SummaryData;
	bool bProfileLoading = false;
	bool bHistoryLoading = false;
	/** The post-match screen holds for a few seconds before CONTINUE is tappable. */
	float SummaryTime = 0.f;
	/** Friend requests sent this session: the button reads REQUESTED before the roster catches up. */
	TSet<FString> OptimisticFriends;
	/** Likes given this session, so the heart stays filled without a second request. */
	TSet<FString> OptimisticLikes;
	/** The ADD tab's search box. One request per SEARCH tap, never per keystroke. */
	FString SearchText;
	/** The lobby's right-edge friends strip starts collapsed: the notice stack owns that corner
	 *  whenever a card is in flight, and an always-open list is screen the player did not ask for. */
	bool bLobbyFriendsOpen = false;
	/** The yes/no that floats over whatever screen is showing; empty content = nothing asked. */
	TSharedPtr<SBox> ConfirmBox;

	// ---- live config state (Campus/CampusLiveConfigUI.cpp)
	/** The announcement column, directly under the invite stack and at the same width, so the two
	 *  never overlap and the corner reads as one surface. */
	TSharedPtr<SBox> AnnounceBox;
	/** A blob landed, or a card was dismissed: re-fill the column on the next Tick. NEVER from inside
	 *  the tap or the network callback that asked for it - that tears down the button still routing
	 *  the touch, which is the whole reason bRefreshQueued exists. */
	bool bAnnouncementsDirty = false;
	/** The soft "update available" card has been closed. SESSION-LOCAL on purpose, unlike a real
	 *  announcement's dismissal: the card is not news that happened once, it is a standing fact about
	 *  this APK, and it stops being true the moment the player updates. Returning on the next launch
	 *  is the right behaviour for a nudge; remembering it in the save for ever is not. */
	bool bUpdateCardDismissed = false;
	/** A blob landed: raise the dirty flag and let Tick re-fill the column. Bound with AddSP so a
	 *  stale binding can only ever be inert - see Construct. The REPAINT is asked for too, because
	 *  the published keys also decide the event banner and what map select offers, and both are
	 *  fingerprinted on the revision - Refresh is coalesced, so this costs one repaint at most. */
	void OnLiveConfigPushed() { bAnnouncementsDirty = true; Refresh(); }
	/** The subsystem OnLiveConfigChanged was bound on, so the destructor can unbind without going
	 *  back through Controller - which may already be gone by then. */
	TWeakObjectPtr<UCampusBackend> LiveConfigApi;
	FDelegateHandle LiveConfigHandle;
	FDelegateHandle SocialPartyHandle;
	FDelegateHandle SocialRosterHandle;
	FDelegateHandle SocialStateHandle;
	FDelegateHandle SocialNoticeHandle;
	FDelegateHandle SocialRoomHandle;
	FDelegateHandle SocialSummaryHandle;
	/** UCampusBRQueue outlives this widget (it survives level travel), so every one of these comes off
	 *  in the destructor exactly like the social handles above. */
	FDelegateHandle BRChangedHandle;
	FDelegateHandle BRMatchedHandle;
	FDelegateHandle BREndedHandle;
	FDelegateHandle BRScoredHandle;
	/** The battle session the travel is working on, and how many tries it has spent on it. Only the KEY
	 *  is kept: the session object itself has one owner, UCampusBRQueue, and a retry reads it from
	 *  there rather than holding a copy that can go stale. */
	FString BRTravelKey;
	int32 BRTravelTries = 0;
	/** Seconds until the next travel retry; 0 = nothing pending (see Tick). */
	float BRRetryIn = 0.f;
	/** The travel ran out of tries, so the overlay is SEARCHING again and the cancel is back in reach. */
	bool bBRTravelFailed = false;

	// form fields
	FString EmailText;
	FString PasswordText;
	FString NameText;
	FString RollText;
	FString BranchText;
	FString CollegeText;
	FString CodeText;
};

class SCampusMatchWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusMatchWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACampusMatchPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** ROOM ONLY: the two team columns, SWITCH TEAM and the host's START. */
	TSharedRef<SWidget> BuildWaiting();
	/** ROOM ONLY: RED WINS / BLUE WINS and the round score. */
	TSharedRef<SWidget> BuildWin();
	/** BATTLE ROYALE: the one result screen (SCampusBREnd, Campus/CampusBREnd.h) - CAMPUS KING at #1,
	 *  the honest placement otherwise. Never a team colour. */
	TSharedRef<SWidget> BuildBattleRoyaleResult();
	TSharedRef<SWidget> BuildSettings();
	TSharedRef<SWidget> TeamColumn(uint8 Team);

	TWeakObjectPtr<ACampusMatchPlayerController> Controller;
	TSharedPtr<SBox> Content;
	FString RoomSignature() const;

	int32 ShownPhase = -1;
	FString ShownSignature;
	bool bShownSettings = false;
};
