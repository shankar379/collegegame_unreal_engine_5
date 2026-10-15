// WHAT MAY BE PLAYED, AND WHEN (UCampusBackend): blob key `modes` (PHASE_B_CONTRACT B3,
// ADMIN_CONSOLE_DESIGN decision 7, api/src/lib/modecatalog.js).
//
//   `modes` : { tz, modes[{id, enabled, sort, team_sizes, map_ids, max_players, window}],
//               maps[{id, client_id, name, enabled, sort, max_players}],
//               rotation[], br_schedule[{days, start_ist, end_ist, time_of_day, enabled, note}] }
//
// THE BLOB MAY ONLY TAKE AWAY, REORDER OR RELABEL. CampusCatalog (Campus/CampusMapArt.h) is what this
// APK can actually open - a level, a mesh set, a mode's rules - so an id the blob names and this build
// does not know is ignored, and a key the console has never published leaves the compiled catalogue in
// charge exactly as before. That asymmetry is the whole reason this is safe to publish mid-session.
//
// TWO ID SPACES, AND ONE BRIDGE. The server calls the maps `classic`, `campus` and `real`; this build
// calls the same three Classic, CampusMap and VarsityValley. `maps[].client_id` is the bridge, and it
// is the only thing in this file that is allowed to know both names.
//
// THE BATTLE-ROYALE SCHEDULE IS AN IST WALL-CLOCK RULE WITH NO DATE, which is why `start_ist` arrives
// as "HH:MM" and is evaluated here against the server clock shifted into IST, rather than as an
// instant the server computed once (`open_now` is deliberately NOT in the blob: a field whose truth
// changes with the clock would be a lie the moment a phone cached it for a minute).
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Dom/JsonObject.h"

using namespace CampusApi;

namespace
{
	/** IST is UTC+5:30, with no daylight saving - the one timezone rule this game needs. */
	constexpr int32 IstOffsetMinutes = 330;

	/** ISO weekday, 1 = Monday, which is what `br_schedule.days` carries. */
	int32 IsoWeekday(const FDateTime& When)
	{
		return int32(When.GetDayOfWeek()) + 1;   // EDayOfWeek::Monday == 0
	}

	int32 YesterdayIso(int32 Iso)
	{
		return Iso <= 1 ? 7 : Iso - 1;
	}

	const TCHAR* DayName(int32 Iso)
	{
		static const TCHAR* Names[] = { TEXT("MON"), TEXT("TUE"), TEXT("WED"), TEXT("THU"), TEXT("FRI"), TEXT("SAT"), TEXT("SUN") };
		return (Iso >= 1 && Iso <= 7) ? Names[Iso - 1] : TEXT("");
	}

	/** "21:00" / "21:00:00" -> 1260. INDEX_NONE for anything else, so a malformed row is skipped
	 *  rather than treated as midnight - which would read as "open all night". */
	int32 MinutesOfDay(const FString& Clock)
	{
		FString Hours;
		FString Rest;
		if (!Clock.Split(TEXT(":"), &Hours, &Rest) || Hours.IsEmpty() || Rest.IsEmpty())
		{
			return INDEX_NONE;
		}
		FString Minutes = Rest;
		FString Seconds;
		Rest.Split(TEXT(":"), &Minutes, &Seconds);
		if (!Hours.IsNumeric() || !Minutes.IsNumeric())
		{
			return INDEX_NONE;
		}
		const int32 H = FCString::Atoi(*Hours);
		const int32 M = FCString::Atoi(*Minutes);
		if (H < 0 || H > 23 || M < 0 || M > 59)
		{
			return INDEX_NONE;
		}
		return H * 60 + M;
	}

	TArray<int32> ReadInts(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<int32> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				double Number = 0.0;
				if (Value.IsValid() && Value->TryGetNumber(Number))
				{
					Out.Add(int32(Number));
				}
			}
		}
		return Out;
	}
}

// -------------------------------------------------------------------------------------------------
// FCampusBrWindow

int32 FCampusBrWindow::StartMinutes() const
{
	return MinutesOfDay(StartIst);
}

int32 FCampusBrWindow::EndMinutes() const
{
	return MinutesOfDay(EndIst);
}

bool FCampusBrWindow::IsOpenAt(const FDateTime& IstNow) const
{
	const int32 Start = StartMinutes();
	const int32 End = EndMinutes();
	if (!bEnabled || Days.Num() == 0 || Start == INDEX_NONE || End == INDEX_NONE)
	{
		return false;
	}
	const int32 Minutes = IstNow.GetHour() * 60 + IstNow.GetMinute();
	const int32 Today = IsoWeekday(IstNow);
	if (Start <= End)
	{
		return Days.Contains(Today) && Minutes >= Start && Minutes <= End;
	}
	// A WINDOW THAT WRAPS PAST MIDNIGHT names the day it STARTS: 21:00 -> 02:00 on {FRI} includes
	// 01:00 on Saturday and excludes 01:00 on Friday. Same rule as modecatalog.js's SQL.
	return (Days.Contains(Today) && Minutes >= Start)
		|| (Days.Contains(YesterdayIso(Today)) && Minutes <= End);
}

FString FCampusBrWindow::Describe() const
{
	TArray<int32> Sorted = Days;
	Sorted.Sort();
	FString When;
	if (Sorted.Num() == 0)
	{
		When = TEXT("EVERY DAY");
	}
	else
	{
		// One contiguous run reads as a range ("FRI-SUN"); anything else is listed, because
		// "MON-SUN" for {MON, WED} would simply be false.
		bool bRun = true;
		for (int32 Index = 1; Index < Sorted.Num(); ++Index)
		{
			bRun = bRun && Sorted[Index] == Sorted[Index - 1] + 1;
		}
		if (bRun && Sorted.Num() > 2)
		{
			When = FString::Printf(TEXT("%s-%s"), DayName(Sorted[0]), DayName(Sorted.Last()));
		}
		else
		{
			TArray<FString> Parts;
			for (const int32 Day : Sorted)
			{
				Parts.Add(DayName(Day));
			}
			When = FString::Join(Parts, TEXT(" "));
		}
	}
	const int32 Start = StartMinutes();
	const int32 End = EndMinutes();
	if (Start == INDEX_NONE || End == INDEX_NONE)
	{
		return When;
	}
	return FString::Printf(TEXT("%s %02d:%02d-%02d:%02d"), *When, Start / 60, Start % 60, End / 60, End % 60);
}

// -------------------------------------------------------------------------------------------------
// The published blob

void UCampusBackend::ReadModesKey(const TSharedPtr<FJsonObject>& Key, FCampusModes& Out)
{
	Out = FCampusModes();
	if (!Key.IsValid())
	{
		return;
	}
	Out.Tz = Str(Key, TEXT("tz"));
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (Key->TryGetArrayField(TEXT("modes"), Rows))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			const FString Id = Str(Row, TEXT("id"));
			if (Id.IsEmpty())
			{
				continue;
			}
			FCampusModeRow& Mode = Out.Modes.AddDefaulted_GetRef();
			Mode.Id = Id;
			Mode.bEnabled = Bool(Row, TEXT("enabled"));
			Mode.Sort = int32(Num(Row, TEXT("sort")));
			Mode.TeamSizes = ReadInts(Row, TEXT("team_sizes"));
			Mode.MapIds = Strings(Row, TEXT("map_ids"));
			Mode.MaxPlayers = int32(Num(Row, TEXT("max_players")));
		}
	}
	if (Key->TryGetArrayField(TEXT("maps"), Rows))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			const FString Id = Str(Row, TEXT("id"));
			if (Id.IsEmpty())
			{
				continue;
			}
			FCampusMapRow& Map = Out.Maps.AddDefaulted_GetRef();
			Map.Id = Id;
			Map.ClientId = Str(Row, TEXT("client_id"));
			Map.Name = Str(Row, TEXT("name"));
			Map.bEnabled = Bool(Row, TEXT("enabled"));
			Map.Sort = int32(Num(Row, TEXT("sort")));
			Map.MaxPlayers = int32(Num(Row, TEXT("max_players")));
		}
	}
	if (Key->TryGetArrayField(TEXT("br_schedule"), Rows))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Row.IsValid())
			{
				continue;
			}
			FCampusBrWindow& Window = Out.BrSchedule.AddDefaulted_GetRef();
			Window.Days = ReadInts(Row, TEXT("days"));
			Window.StartIst = Str(Row, TEXT("start_ist"));
			Window.EndIst = Str(Row, TEXT("end_ist"));
			Window.TimeOfDay = Str(Row, TEXT("time_of_day"));
			Window.bEnabled = Bool(Row, TEXT("enabled"));
			Window.Note = Str(Row, TEXT("note"));
		}
	}
	// ONE TEST FOR "THE CONSOLE HAS SPOKEN": a `modes` key with no modes and no maps in it says
	// nothing, and must not be allowed to hide the whole game behind an empty list.
	Out.bLoaded = Out.Modes.Num() > 0 || Out.Maps.Num() > 0;
}

// -------------------------------------------------------------------------------------------------
// What the lobby asks

bool UCampusBackend::IsModeEnabled(const FString& ModeId) const
{
	for (const FCampusModeRow& Row : LiveConfig.Modes.Modes)
	{
		if (Row.Id == ModeId)
		{
			return Row.bEnabled;
		}
	}
	// NOT NAMED = NOT TOUCHED. The compiled catalogue is the authority until the console says otherwise.
	return true;
}

bool UCampusBackend::IsMapEnabled(const FString& ClientMapId) const
{
	for (const FCampusMapRow& Row : LiveConfig.Modes.Maps)
	{
		if (Row.ClientId == ClientMapId)
		{
			return Row.bEnabled;
		}
	}
	return true;
}

int32 UCampusBackend::ModeSortOf(const FString& ModeId) const
{
	for (const FCampusModeRow& Row : LiveConfig.Modes.Modes)
	{
		if (Row.Id == ModeId)
		{
			return Row.Sort;
		}
	}
	return INDEX_NONE;
}

int32 UCampusBackend::MapSortOf(const FString& ClientMapId) const
{
	for (const FCampusMapRow& Row : LiveConfig.Modes.Maps)
	{
		if (Row.ClientId == ClientMapId)
		{
			return Row.Sort;
		}
	}
	return INDEX_NONE;
}

FDateTime UCampusBackend::IstNow() const
{
	// The server clock, shifted. Engagement::Now() already falls back to the device's own when no
	// response has landed, so this answers on the first frame as well as on the hundredth.
	return Engagement.Now() + FTimespan::FromMinutes(IstOffsetMinutes);
}

bool UCampusBackend::IsBattleRoyaleOpenNow() const
{
	bool bAnyEnabled = false;
	const FDateTime Now = IstNow();
	for (const FCampusBrWindow& Window : LiveConfig.Modes.BrSchedule)
	{
		if (!Window.bEnabled)
		{
			continue;
		}
		bAnyEnabled = true;
		if (Window.IsOpenAt(Now))
		{
			return true;
		}
	}
	// NO SCHEDULE IS NO RESTRICTION. An empty or unpublished schedule must read as "any time" and
	// never as "closed": a console outage may not take the game's own mode away.
	return !bAnyEnabled;
}

FString UCampusBackend::BattleRoyaleWindowText() const
{
	TArray<FString> Parts;
	for (const FCampusBrWindow& Window : LiveConfig.Modes.BrSchedule)
	{
		if (Window.bEnabled)
		{
			Parts.Add(Window.Describe());
		}
	}
	if (Parts.Num() == 0)
	{
		return FString();
	}
	// Two windows at most on a card: a third line of small print on a mode tile is not information.
	if (Parts.Num() > 2)
	{
		Parts.SetNum(2);
		Parts.Add(TEXT("AND MORE"));
	}
	// IST, spelled out: the blob's `tz` is "Asia/Kolkata", which is the right name for a server and
	// the wrong one for a mode card. Every schedule in this system is stated in one zone.
	return FString::Join(Parts, TEXT("  /  ")) + TEXT("  IST");
}
