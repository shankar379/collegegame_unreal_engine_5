// REPORTING A PLAYER (UCampusBackend): the one moderation call the game makes
// (PHASE_B_CONTRACT B3, api/src/routes/reports.js).
//
//   POST /v1/players/:id/report  { reason, detail?, room_code? }  Idempotency-Key  -> 201
//
// A REPORT IS A SIGNAL, NOT A VERDICT. Nothing is counted on the device, nothing is muted, no
// threshold fires: the route writes one row and a moderator reads the queue. So the only thing this
// file owes the player is an honest answer to "did it go through", which is exactly the part the
// server's two refusals make interesting:
//
//   409 REPORT_ALREADY_SENT  one report per target per IST game day, enforced by migration 028's
//                            unique index. This is NOT a failure the player caused and must not read
//                            like one - they reported the same person this morning.
//   429 RATE_LIMITED         five different targets a minute. A real player never meets this; a
//                            script does. Either way the sentence says "wait", not "error".
//
// AND AN IDEMPOTENCY-KEY, because this fires from a tap that may be retried on a flaky tunnel: a
// replay has to return the stored answer instead of hitting the unique index and turning a retry into
// an error nobody earned.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Dom/JsonObject.h"

using namespace CampusApi;

void UCampusBackend::ReportPlayer(const FString& TargetId, const FString& Reason, const FString& RoomCode, FReportDone Done)
{
	if (TargetId.IsEmpty() || Reason.IsEmpty())
	{
		if (Done) { Done(false, false, TEXT("That player cannot be reported.")); }
		return;
	}
	if (TargetId == PlayerId)
	{
		// The server answers this with a field-level 400; refusing it here keeps a pointless request
		// off a metered tunnel and lets the UI say something sensible.
		if (Done) { Done(false, false, TEXT("You cannot report yourself.")); }
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("reason"), Reason);
	// `detail` is CONTEXT FOR A HUMAN, never evidence the server trusts: a moderator checks the build
	// and the room against match_results before acting on either. The schema is CLOSED (no unknown
	// keys), so only what it declares goes in - and the only thing the game knows that the server
	// cannot read off the token is which APK this was sent from.
	//
	// NO FREE TEXT, deliberately. The five reasons are the ones a queue can be filtered and counted
	// by, and `detail.note` is the server's room for a sentence nobody has asked the player for yet:
	// when a screen does ask for one, it arrives here with its caller and not before.
	TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();
	Detail->SetNumberField(TEXT("client_build"), ClientBuild());
	Body->SetObjectField(TEXT("detail"), Detail);
	if (!RoomCode.IsEmpty())
	{
		Body->SetStringField(TEXT("room_code"), RoomCode);
	}
	Request(TEXT("POST"), FString::Printf(TEXT("/players/%s/report"), *TargetId), Body, true, true,
		[Done](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (!Done)
			{
				return;
			}
			if (Status == 200 || Status == 201)
			{
				Done(true, false, FString());
				return;
			}
			const FString Code = Str(Obj(Json, TEXT("error")), TEXT("code"));
			if (Status == 409 && Code == TEXT("REPORT_ALREADY_SENT"))
			{
				// OK, AND ALREADY THERE. The unique index on (reporter, target, game_day) refused a
				// second row, which means the first one is in the queue - so the player's report did
				// happen and the only honest answer is to say when.
				Done(true, true, TEXT("YOU ALREADY REPORTED THEM TODAY. OUR TEAM IS LOOKING AT IT."));
				return;
			}
			if (Status == 429)
			{
				Done(false, false, TEXT("THAT IS A LOT OF REPORTS AT ONCE. WAIT A MINUTE AND TRY AGAIN."));
				return;
			}
			Done(false, false, ErrorText(Status, Json));
		});
}
