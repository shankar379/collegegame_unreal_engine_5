# Internet play: Epic Online Services setup

Campus Combat matches are hosted on a player's phone. Over Wi-Fi that phone is reachable at
`ip:port`; over mobile data it is not — both phones sit behind carrier NAT. Epic Online Services
(EOS) P2P fixes that: the two phones try a direct connection and fall back to **Epic's relays** when
they cannot reach each other. The host is addressed by its EOS *ProductUserId* instead of an IP.

Players do **not** need an Epic account and never see a sign-in screen. The game logs in with an EOS
**device id** credential, created silently on first launch.

**Until the five keys below are filled in, nothing changes.** The game sees an unset EOS artifact,
never calls EOS, and hosts/joins on the LAN exactly as it does today.

---

## What the owner has to do, once

Everything happens at <https://dev.epicgames.com/portal> and takes about ten minutes.

### 1. Create the product

1. Sign in to the **Epic Games Dev Portal** and accept the developer agreement.
2. **Create a product** → name it `Campus Combat`.
3. Open the product → **Product Settings**. Copy the **Product ID** (32 hex characters).

### 2. Create the client policy

This is the step that decides whether the game works. In **Product Settings → Clients → Policies**:

1. **Add a policy** → name it `campus-combat-p2p`.
2. Policy type: **Peer-to-Peer (Client)** (or **Custom**, if the portal offers types).
3. Turn **on**:
   - **Connect interface** — the device-id login. Without this, login fails with
     `EOS_InvalidAuth` / `EOS_NotConfigured`.
   - **Peer-to-Peer (P2P)** — the match traffic. Without this, login succeeds and the match then
     fails to connect, which is the confusing failure. Make sure **relay use is allowed**.
   - **Allow anonymous / Device ID users** (sometimes "Allow Connect without an Epic account") if
     the policy screen lists it separately.
4. Leave Epic Account Services (EAS), friends, presence, achievements, leaderboards, lobbies,
   sessions, title storage **off**. The game uses none of them, and every extra permission is one
   more thing Epic can reject.
5. Save.

### 3. Create the client

**Product Settings → Clients → Add client**:

1. Name it `campus-combat-game`.
2. Client policy: the `campus-combat-p2p` policy from step 2.
3. Save, then copy the **Client ID** (32 hex) and the **Client Secret**.

### 4. Find the sandbox and deployment

**Product Settings → Sandboxes**: ship the **Live** sandbox. Copy its **Sandbox ID** (32 hex).

**Product Settings → Deployments**: the deployment belonging to that sandbox. Copy its
**Deployment ID** (32 hex). (A product starts with one deployment per sandbox; if you add more,
the one you point the game at is the one whose player data and relay quota you use.)

### 5. Paste the five values into the game

`CollegeCampusGame/Config/DefaultEngine.ini`, section `[EOSSDK.Platform.CampusCombat]`:

```ini
[OnlineServices.EOS]
PlatformConfigName=CampusCombat

[EOSSDK.Platform.CampusCombat]
ProductId=<Product ID>
SandboxId=<Sandbox ID>
DeploymentId=<Deployment ID>
ClientId=<Client ID>
ClientSecret=<Client Secret>
ClientEncryptionKey=
CacheBaseSubdirectory=CampusCombatEOS
bCheckRuntimeType=true
```

- `ClientEncryptionKey` stays **blank**. It is only for EOS Title Storage / Player Data Storage,
  which this game does not use. (Note the name: a plain `EncryptionKey` key is stripped out of
  packaged builds by the engine's ini denylist, which is why the modern name exists.)
- `CacheBaseSubdirectory` must stay set — the device id that *is* the player's EOS identity lives in
  that cache. Clear it and every launch would create a new identity.
- The `ClientSecret` is not a user secret; it identifies the game build. It ships inside the APK
  either way, which is exactly why the client policy above must be as narrow as possible.

### 6. Rebuild and ship

The keys are read at launch, but the Android packaging step also reads `ClientId`, so **rebuild the
APK** after filling them in — do not just push an ini.

---

## What the backend has to send

| Direction | Field | Meaning |
|---|---|---|
| `POST /v1/rooms` (game → API) | `eos_puid` | The host phone's ProductUserId, `^[0-9a-f]{32}$`. Sent only when the host really is EOS-logged-in; absent for a LAN room. |
| `POST /v1/rooms/{code}/heartbeat` | `eos_puid` | Repeated every 30 s while hosting, so a host that finished its EOS login *after* the room was created still registers (or replaces) its address. |
| `POST /v1/rooms/{code}/heartbeat` | `status` | `playing` once the match starts (that is what starts the match clock server-side), `open` before. Unchanged behaviour, sent immediately on the change. |
| `POST /v1/rooms/{code}/result` | `Idempotency-Key` header | Required. One UUID per match, minted once and reused by `Send()` on every internal retry (token refresh, 18+ confirm), so a replayed report cannot pay out twice. |
| room view (API → game) | `connect: {"type":"eos","puid":"<32 hex>"}` (address/port null) | Joiners travel over EOS P2P. |
| room view (API → game) | `connect: {"type":"lan","address":"...","port":7777}`, plus flat `address`/`port` | Joiners travel to `ip:port`, as today. |

The game reads both shapes and both generations of the response
(`Campus/CampusBackend.cpp`, `ReadRoom`): the `connect` object wins when present, the flat
`address`/`port` are the fallback, a null/absent port keeps 7777, and any `type` other than `eos` is
treated as LAN. So an old API response and a new one both work.

---

## How it works in the game

```
lobby  ──EnsureLogin()──►  EOS_Connect_CreateDeviceId  ──►  EOS_Connect_Login (DeviceIdAccessToken)
                                                                        │
                                                            ProductUserId ("puid")
                                                                        │
 host:  POST /rooms {eos_puid}  ──►  OpenLevel("L_Match?listen")  ──►  NetDriverEOS listens on P2P
 join:  POST /rooms/join       ──►  ClientTravel("[EOS:<host puid>]?Ticket=…")  ──►  NetDriverEOS connects
```

- `Campus/CampusEOS.cpp` owns the login. One login per app run, warmed up when the lobby opens.
- `Config/DefaultEngine.ini` makes `NetDriverEOS` the `GameNetDriver` and sets
  `RelayControl=AllowRelays`.
- The join URL is `[EOS:<puid>]` **with square brackets**. They are not decoration: the engine's URL
  parser only keeps a colon inside a host name when the host is bracketed (the IPv6 rule). Without
  them, `EOS:<puid>` is parsed as the protocol `EOS`.
- The room code, the Room Card, the join ticket and `POST /rooms/{code}/admit` are unchanged. EOS
  only replaces the *address*; the API still decides who gets in.

### Voice is unchanged

Team voice still rides the match connection itself (the engine's voice channel on `UNetConnection`)
and still resolves through `OnlineSubsystemNull`, which stays `DefaultPlatformService`. The EOS
connection class derives from the IP one, so the same VOIP, the same push-to-talk and the same
team-only `GameplayMutePlayer` / `MuteRemoteTalker` logic work over P2P with no change. EOS RTC voice
was deliberately **not** adopted: it would need its own RTC room lifecycle, a second permission path
and extra portal config, for no gain over voice that already works.

### Google Play billing is unchanged

Billing reaches the Google Play online subsystem **by name** from `Campus/CampusPlayBilling.cpp`, and
EOS lives in the separate v2 online-services stack. `DefaultPlatformService` is still `Null`, so
nothing that depends on it moved.

---

## Fallbacks — every way this can fail, and what happens

| Situation | Behaviour |
|---|---|
| Keys blank / placeholder | `UCampusEOS::IsConfigured()` is false. No EOS call is made. Rooms are created without `eos_puid` and hosted/joined on `ip:port`. |
| EOS platform fails to start (bad keys) | Login fails, host falls back to a LAN room, joining an EOS room shows "Could not reach the match network". |
| Device-id creation or login fails (no network, portal policy wrong) | Same as above. Retried on the next CREATE/JOIN. |
| EOS unusable at runtime | `NetDriverEOS::IsAvailable()` is false, so the engine takes `DriverClassNameFallback` and the whole game runs on `IpNetDriver`. |
| Host is LAN, joiner has EOS | `connect` has no `type:"eos"`, joiner travels to `ip:port`. |
| Host is EOS, joiner build has no EOS | Joiner is told the room is an internet room instead of hanging on a timeout. |
| Quick Match (`JoinRoom()`) | Still the fixed LAN address from `CampusNetSettings`. A non-`EOS:` host passes straight through the EOS driver to IP sockets. |

---

## Limitations, honestly

- **Not testable in PIE.** The engine binds its EOS socket subsystem to `UGameEngine`'s game world,
  which does not exist in Play-In-Editor, so `IsConfigured()` deliberately returns false in the
  editor and the editor always uses the LAN path. Test internet play in a packaged build, or in
  **Standalone Game** (a separate process). This is an engine limitation, not a project choice.
- **Relay bandwidth is metered by Epic.** EOS P2P relay traffic counts against the product's quota.
  A listen-server shooter relays every player's traffic through the host, so watch the Dev Portal
  usage page once there are real players.
- **Still a phone as the server.** EOS fixes reachability, not the host's uplink, battery or the
  Android OEM that may kill the app when the player switches away. A host that leaves still ends the
  match for everyone.
- **One identity per install.** The device id lives in the app's cache: uninstalling the app (or
  clearing its data) creates a new ProductUserId. That is fine here — the player's real account is
  ours, held by the API; the EOS id only names the transport.
- **No EOS sessions or lobbies.** Discovery stays entirely in our API (room code + ticket). Nothing
  is listed on Epic's side.
- **Voice is still host-relayed.** It goes over the same P2P connection, so it shares the host's
  uplink with the match.
