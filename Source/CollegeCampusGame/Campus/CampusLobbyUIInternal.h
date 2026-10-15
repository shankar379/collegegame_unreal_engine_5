// FILE-LOCAL plumbing shared by the SCampusLobbyWidget screen files, and nothing else.
//
// ONE CLASS, MANY FILES. SCampusLobbyWidget is the whole lobby, and it is far too large for one
// translation unit, so it is split by SCREEN:
//
//   CampusLobbyUI.cpp            the shell - Construct, the destructor, Tick, Show, Refresh, the
//                                confirm/toast/busy plumbing and the Slate pieces every screen uses
//   CampusLobbyAuthUI.cpp        splash -> agreement -> login choice -> login / register / nickname
//   CampusLobbyHomeUI.cpp        the lobby itself: top bar, menu column, squad row, mode select, start
//   CampusLobbyEngagementUI.cpp  weekend board, missions, streak, 18+ confirm, college picker
//   CampusLobbyStoreUI.cpp       the text store cards and the real-money button
//   CampusLobbyRoomsUI.cpp       the room dialog and the account screen
//   CampusEventsUI.cpp           the lobby's live-event banner and the EVENTS sheet behind it
//   CampusReportUI.cpp           the report button on a player card, and its reason picker
//   ... and beside them CampusWardrobeUI.cpp, CampusStoreUI.cpp, CampusProfileUI.cpp,
//       CampusSocialUI.cpp, CampusMapSelect.cpp and CampusLiveConfigUI.cpp, which were already split
//       out the same way.
//
// WHAT GOES IN HERE: the handful of constants two or more of those files read. They used to sit in an
// anonymous namespace in CampusLobbyUI.cpp, which is exactly where they still belong conceptually -
// this header is that namespace, shared, so there is one copy of each and the screens cannot drift.
//
// WHAT DOES NOT: anything a file outside the lobby needs. That goes in Campus/CampusUI.h, which is
// the class's actual public header. This file is private to the CampusLobby*UI.cpp set.
//
// EVERYTHING HERE IS `inline`, which is what makes it safe in a header: one object across all the
// files, no duplicate symbol at link time, and no "unused" warning in whichever file happens not to
// read one of them (clang builds this project with -Werror).

#pragma once

#include "CoreMinimal.h"

#include "Campus/CampusAngular.h"

/** The splash screen's rotating hints. Construct (CampusLobbyUI.cpp) picks the index once per app
 *  run so it cannot change under the player; BuildSplash (CampusLobbyAuthUI.cpp) paints it. */
inline constexpr const TCHAR* Tips[] = {
	TEXT("Tip: run at the corridor wall and jump to vault down."),
	TEXT("Tip: hold the eye to look around without moving your aim."),
	TEXT("Tip: claim a free Room Card every day in the lobby."),
	TEXT("Tip: crouch + push the stick to the edge for a fast low run."),
	TEXT("Tip: upgrade a gun to raise its damage and accuracy."),
};

// THE TWO WALLET COLOURS, named once. DIAMONDS wear the gem, ROOM CARDS wear the coin gold, and the
// top bar, the store, the engagement panels and the guest warning all have to agree about that.
//
// REFERENCES, not copies: CampusAngular::Gem and ::CoinGold are defined in CampusAngular.cpp, so
// copying them here would be dynamic initialisation of an inline variable that depends on another
// translation unit's - unordered, and a lost race leaves the colour black for the whole run. Binding
// a reference to an object with static storage duration is CONSTANT initialisation, so there is no
// order to lose, and every use site reads exactly as it did before.
inline const FLinearColor& Diamond = CampusAngular::Gem;
inline const FLinearColor& CardGold = CampusAngular::CoinGold;
