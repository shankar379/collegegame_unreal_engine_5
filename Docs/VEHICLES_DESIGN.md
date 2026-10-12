# Vehicles — Campus Combat design

> *The Stone of Truth shows the constraint before the code. Here is what fails first under load.*

**Status:** design only, no code written. **Target:** UE 5.8, Android mobile shooter, listen server on a phone, EOS P2P relay.
**Audience:** read "Why this exists" + "MVP scope" (2 min), then the contract you are implementing.

---

## 1. Why this exists

The campus map has **cars today, but they are scenery**. Verified, not assumed:

| Fact | Evidence |
|---|---|
| No vehicle code of any kind | `grep -rniE "vehicle\|wheeled\|chaoswheel"` over `Source/` → zero hits |
| Cars are 14 meshes merged into **one** static mesh | `campus_recipe.json` → `meshes` group `"Cars"` = 14 entries; built into `Content/Campus/Meshes/SM_Campus_Cars.uasset` |
| They are placed as a single immovable actor | `Scripts/build_campus.py::place()` → `StaticMeshActor`, `ComponentMobility.STATIC` |
| **They do not even block bullets** | same `place()`: `set_collision_enabled(CollisionEnabled.NO_COLLISION)` |
| `ChaosVehiclesPlugin` is on disk but **not enabled** | `E:\UE_5.8\Engine\Plugins\Experimental\ChaosVehiclesPlugin\` exists; absent from `CollegeCampusGame.uproject` plugin list; `"IsExperimentalVersion": true` |
| Vehicle **art is already on this disk** | `E:\UE_5.8\Templates\TemplateResources\Standard\Vehicles\Content\` — 55 assets, 40 MB, OffroadCar + SportsCar |

The owner wants: drive a car at runtime, shoot and destroy the car, hit/kill its occupants, and run over a player on foot.

Everything in this document is shaped by **one load-bearing constraint**: the server is a **phone**, and the other phones reach it through an **EOS P2P relay** (expect 80–250 ms RTT, 1–3 % loss). At 1200 cm/s a car travels **60 cm between two 20 Hz snapshots** and sits **300 cm behind** a 250 ms client. That number is why half the decisions below go the way they do.

---

## 2. Constraints

| Axis | Value |
|---|---|
| Server | One player's Android phone (listen server). It also renders, plays, and runs voice. |
| Transport | EOS P2P relay. 80–250 ms RTT, jittery, 1–3 % loss, no QoS. |
| Players | ≤ 8 (4v4), round-based, 3–10 min rounds, elimination (`ACampusMatchGameMode`). |
| Frame budget | 33.3 ms (30 fps) on a mid-range phone. The host is the tightest machine in the match. |
| Existing authority model | Server owns health, ammo, hits. Clients send **intent**; the host decides and replicates one number; every phone animates the rest itself (`ACampusDoor`, `ACampusZipline`, mantle/traversal all follow this). |
| Existing hit model | Server-side rewind: `ACampusCharacter::RewoundVictim()`, `FHitboxFrame` history, `KeepSeconds 0.5`, `MaxRewind 0.3`, `ViewDelay 0.08`. |
| Owner's stated engine stance | `Mover`, `ChaosMover`, `NetworkPrediction`, `MotionWarping`, `PoseSearch` are all `PlatformDenyList: [Android, IOS]` in the `.uproject`. **Heavy predicted-movement plugins are already banned from the phone build.** |
| Art | No vehicle art authored. No vehicle skeleton. No enter/exit animation in the Mixamo clip set. |

### Assumptions (challenge these)

1. 3 drivable cars alive at once is enough for one campus map. (MVP cap.)
2. Fuel is **not** wanted — rounds are 3–10 min on one campus (see §7.8).
3. Driver **cannot** shoot. One thumb cannot steer and aim on a phone.
4. Friendly fire stays off for roadkill, as it already is for bullets (`ServerFire`'s team check).
5. A snap-in / snap-out (no entry animation) is acceptable for MVP; mobile shooters do this.
6. Car-vs-car ramming is Phase 2, not MVP.
7. The drivable car may look different from the 14 static props in MVP; art convergence is Phase 3.

---

## 3. What the market does (research)

### 3.1 Vehicle HP, destruction, damage zones

- **PUBG:** approximate HP — Buggy ~1540, UAZ ~1820, Dacia 1300 ~1820, Van ~1680, Motorcycle ~1025. Damage zones on Buggy/Dacia/UAZ/Mirado/Pickup/Rony/Zima: **Engine 100 %, Body 75 %, Roof 50 %**; all other vehicles take equal damage anywhere. At 0 HP the engine is disabled and the vehicle **catches fire, then explodes after ~5 s**; large radial damage (Red Zone, C4) still detonates instantly. Vehicles block grenade damage. Engines auto-off after 8 s at a standstill, 2 s after the driver exits. ([pubg.wiki.gg/wiki/Vehicles](https://pubg.wiki.gg/wiki/Vehicles))
- **PUBG Mobile:** "Deal continuous damage to the vehicle body until its HP reaches zero, causing it to explode." When it explodes, "all people inside and around the vehicle can get killed." ([PUBG Mobile Help — kill opponents on vehicles](https://pubgmobile.helpshift.com/hc/en/3-pubg-mobile/faq/59-how-do-i-kill-opponents-on-vehicles/), [PUBG Mobile Help — vehicle explodes](https://pubgmobile.helpshift.com/hc/en/3-pubg-mobile/faq/63-what-happens-if-a-vehicle-explodes/))
- **Free Fire (community wiki):** durability drops from **weapon hits *and* wall/building collisions**. At 0 durability the car "catches fire and gets out of control before exploding." Blowing up a vehicle **can still team-kill**. ([NamuWiki — Free Fire/Vehicle](https://en.namu.wiki/w/Free%20Fire/%EC%9D%B4%EB%8F%99%20%EC%88%98%EB%8B%A8))

### 3.2 Occupants — through glass vs through body

- **PUBG Mobile, explicitly:** "Fire at passengers and drivers **through windows or openings**." ([PUBG Mobile Help](https://pubgmobile.helpshift.com/hc/en/3-pubg-mobile/faq/59-how-do-i-kill-opponents-on-vehicles/))
- **PUBG:** "Unless a headshot or other body shot is directly targeted, occupants don't take significant damage while moving in a vehicle." So the metal **does** protect; the greenhouse does not. ([pubg.wiki.gg](https://pubg.wiki.gg/wiki/Vehicles))
- **Free Fire:** the amphibious motorcycle's "driver's seat is completely open, so it's good to get shot" — exposed-seat vehicles are deliberately a different risk class. ([NamuWiki](https://en.namu.wiki/w/Free%20Fire/%EC%9D%B4%EB%8F%99%20%EC%88%98%EB%8B%A8))
- **Exiting a moving vehicle "can cause damage and even instant death."** ([pubg.wiki.gg](https://pubg.wiki.gg/wiki/Vehicles))

### 3.3 Tires

- **PUBG Mobile:** "Destroying tires significantly reduces vehicle speed and maneuverability," making the driver an easy follow-up target. ([PUBG Mobile Help](https://pubgmobile.helpshift.com/hc/en/3-pubg-mobile/faq/59-how-do-i-kill-opponents-on-vehicles/))
- **PUBG:** all drivable vehicles have poppable tires; shoot them "if you don't want others to drive your vehicle." BRDM-2 tires are bulletproof. No published tire HP. ([pubg.wiki.gg](https://pubg.wiki.gg/wiki/Vehicles), [TechRadar](https://www.techradar.com/news/pubg-mobile-0150-update-with-ledge-grab-explosive-barrels-and-a-new-vehicle-to-roll-out-from-october-16))

### 3.4 Who shoots, from where

- **PUBG (PC, Season 9 / 9.2):** drivers **can** draw, reload and fire **sidearms only** while steering; steering power drops slightly while reloading; enabled on all vehicles except boats and BRDM; **in-vehicle recoil increased 10 %** (sidearms excluded). Passengers shooting out of a vehicle can down or kill **others in the same vehicle**. ([pubg.wiki.gg](https://pubg.wiki.gg/wiki/Vehicles), [ComicBook](https://comicbook.com/gaming/news/pubg-update-vehicle-shooting-feature/))
- **COD Mobile:** seats are role-typed — the Antelope seats driver + passenger + a **mounted mini-gun gunner**; the snowmobile's pillion "can shoot and provide fire support." Not every seat is a firing seat. ([BlueStacks CODM vehicle guide](https://www.bluestacks.com/blog/game-guides/call-of-duty-mobile/codm-vehicle-guide-en.html))
- **Fortnite:** the shopping cart established the pattern — "while one player steers, the other can shoot from the cart." ([Fox News](https://www.foxnews.com/tech/fortnites-shopping-carts-are-here-what-you-need-to-know))
- **Black Ops Cold War** gated driver-shooting behind a perk/wildcard rather than giving it away. ([GfinityEsports](https://gfinityesports.com/modern-warfare/call-of-duty-black-ops-cold-war-shooting-driving-first-person-drivers-license-perk-wildcard-ps4-xbox-one-series-x-ps5-pc))
- **Not found, treat as unverified:** any published enter/exit/seat-switch animation *duration* for PUBG Mobile, COD Mobile or Fortnite. No source confirms PUBG Mobile blocks drivers from firing.

### 3.5 Roadkill

Free Fire community data: a car hit "may send you flying for up to 50–60 m and die from the combo of fall damage," but "in most cases when hit by a car, the damage is **over 200**, so there are many cases where the person dies the moment they are hit." Pickup truck: "**400 damage**" on a hard hit at high speed. Amphibious motorcycle at top speed on flat ground: "**182 damage**." Tuk tuk at 80 km/h is "slow, so it's hard to road kill." ([NamuWiki](https://en.namu.wiki/w/Free%20Fire/%EC%9D%B4%EB%8F%99%20%EC%88%98%EB%8B%A8))

**Read:** the design intent across the genre is *speed-gated and lethal on a clean hit* — not a chip-damage bump. Damage scales with vehicle mass/speed, and the launch + fall-damage combo is a deliberate secondary kill path.

### 3.6 Netcode — the decisive research finding

`ChaosVehicleMovementComponent` **has no client-side prediction.** Community reports, consistently, across versions:

- "ChaosVehicles does not come by default with client-side prediction of movement, so lag comes as the move is only considered legal and shown when the server processes it." ([UE forums — client side lag with Chaos Vehicles](https://forums.unrealengine.com/t/how-to-solve-multiplayer-client-side-lag-while-using-chaos-vehicles/623443))
- "This essential code is currently missing in the ChaosVehicleMovement component, compared with CharacterMovement." ([UE forums — proper way to replicate Chaos Vehicle](https://forums.unrealengine.com/t/is-there-a-proper-way-to-replicate-chaos-vehicle/629603))
- UE 5.4: "Chaos Vehicle isn't ready for use, I need to roll my own." ([UE forums — Chaos Vehicle Replication with UE 5.4](https://forums.unrealengine.com/t/chaos-vehicle-replication-with-ue-5-4/1872781))
- Client suspension stutter; the fixes offered are a **paid plugin** (Smooth Sync) or zeroing `Replication Angle Lerp`. ([UE forums — suspension stutters on client](https://forums.unrealengine.com/t/chaos-vehicle-suspension-stutters-on-client-side-in-multiplayer/850908))
- The new `UNetworkPhysicsComponent` auto-added to a vehicle made "the Chaos Vehicle Movement Component no longer function and the vehicle no longer moves." ([UE forums](https://forums.unrealengine.com/t/new-network-physics-component-breaks-chaos-vehicle-movement-componet/1926841))

The 5.8 answer is `UNetworkPhysicsComponent` with **Resimulation**, and its entry price is steep: Project Settings → Physics → **Enable Physics Prediction**, **Tick Physics Async** with an **Async Fixed Time Step Size** (`0.016666667`, i.e. 60 physics steps/s), **Max Supported Latency Prediction** sized to your RTT (more latency = more history memory), the pawn set to `Physics Replication Mode = Resimulation`, plus Iris: `net.IsPushModelEnabled=1`, `net.Iris.PushModelMode=1`, `net.Iris.UseIrisReplication=1`, `net.SubObjects.DefaultUseSubObjectReplicationList=1`. The author's own example is called a "hello world" with no gameplay validation. ([vorixo — Networked Physics and UNetworkPhysicsComponent in UE 5.8](https://vorixo.github.io/devtricks/phys-prediction-use/))

**Translation for us:** adopting Chaos Vehicles on a phone host over an EOS relay means turning on a **fixed 60 Hz async physics scene plus a rollback resimulation window sized to 250 ms** — on the same device that is already running 8 characters' CharacterMovement, 8 hitbox histories, voice, and the render thread. That is the thing that fails first.

### 3.7 Mobile cost of Chaos Vehicles

No official Epic benchmark exists for Chaos Vehicles on Android. What is published: guidance to "simplify the vehicle's physics setup and reduce the number of collision primitives"; a desktop report of **98–102 fps → 50–80 fps** when vehicles were enabled, with **3–5 fps recovered by using a static mesh instead of a skeletal mesh**; and a recommendation to use static meshes / reduced tick rates for non-hero vehicles. ([88Cars3D — optimizing car models for mobile](https://88cars3d.com/2026/04/02/unlocking-performance-optimizing-high-fidelity-3d-car-models-for-mobile-experiences-in-unreal-engine/), [UE forums — chaos vehicle fps drop](https://forums.unrealengine.com/t/chaos-vehicle-fps-drop-problem/1353155), [neutroniogames — Ultimate Chaos Vehicle Guide](https://neutroniogames.wordpress.com/2024/01/17/the-ultimate-chaos-vehicle-guide/))

Precedent worth noting: PUBG Mobile shipped on "an evolution of Unreal Engine 4, optimized to suit the mobile platform." They did not ship stock engine vehicle physics. ([Wikipedia — PUBG Mobile](https://en.wikipedia.org/wiki/PUBG_Mobile))

### 3.8 Lag compensation for non-player entities

The standard recipe: keep a timestamped history, rewind **only entities marked rewindable**, and cap the window — "most implementations cap rollback at 300–500 ms"; "cap how far back you rewind so high-latency players cannot exploit it." A UE4 thesis implementing animation-pose rewind found "inconsistencies in the results remain, with hit rate values sometimes falling below 50 %" — i.e. precision in the rewind model is not optional polish. ([bugnet.io — lag compensation](https://bugnet.io/blog/how-to-add-lag-compensation-for-fair-hit-registration), [Photon Fusion 2 — Lag Compensation & Hitbox Queries](https://doc.photonengine.com/fusion/current/manual/advanced/lag-compensation), [LiU thesis — Hit Registration](https://liu.diva-portal.org/smash/get/diva2:1605200/FULLTEXT01.pdf), [marcohenning/ue5-server-side-rewind](https://github.com/marcohenning/ue5-server-side-rewind))

Our `MaxRewind = 0.3` already sits inside that norm. **No source covers rewinding vehicles or riders specifically** — §8.3 is our own derivation, and it is the most correctness-critical section in this document.

### 3.9 Anti-cheat

"Accept inputs, not positions. Reject or clamp movements that exceed the maximum distance possible in the elapsed time." Drive timing from the server clock, because "speed hacks often work by manipulating the client's clock." Validate at the edge: reject future timestamps, clamp latency against real ping, verify shot origins. ([bugnet.io — client authoritative speed hack](https://bugnet.io/blog/how-to-fix-client-authoritative-movement-speed-hack), [AccelByte — server authoritative logic](https://accelbyte.io/blog/server-authoritative-logic-to-prevent-cheating))

### 3.10 Fuel and vehicle respawn

Nothing authoritative found for PUBG Mobile / BGMI fuel burn rates, canister spawn rates, or per-match vehicle respawn. PUBG PC: a Gas Can adds "a random amount of gas." ([pubg.wiki.gg — Gas Can](https://pubg.wiki.gg/wiki/Gas_Can)) **Unverified — do not copy numbers we cannot source.** This supports cutting fuel (§7.8).

---

## 4. Options considered

### A) ChaosVehicles + `UNetworkPhysicsComponent` resimulation
**Optimizes for:** physical fidelity, suspension, rollovers, engine/gearbox feel, free car-vs-car collision response, and reuse of the engine's `SKM_Offroad` / `ABP_SportsCar` art as-is.
**Costs:** enable an Experimental plugin for Android (new Runtime module in the APK); turn on a fixed 60 Hz async physics scene on a phone host; 1.5–3 ms/car of host CPU (§3.7); a resim history window sized to 250 ms of relay latency; no built-in client prediction (§3.6) so the driver rubber-bands until we build the prediction layer anyway; **non-deterministic transforms that clients cannot reproduce**, which breaks the project's "host decides one number, every phone animates it itself" doctrine; and a `MaxRewind` history of full physics state rather than one transform. Contradicts the owner's own `PlatformDenyList` stance on predicted-movement plugins.

### B) `ACampusVehicle` — custom kinematic car, server-authoritative, input-RPC driven
**Optimizes for:** phone-host frame time, exact reproducibility (the same closed-form integrator runs on every machine), tiny bandwidth, trivial lag-comp (one transform per history frame, identical in shape to the existing `FHitboxFrame`), and architectural consistency with `ACampusDoor` / `ACampusZipline` / the mantle system. No new plugin, no new physics scene, no APK growth.
**Costs:** we author the drive feel ourselves (a steering-rate curve and a lateral-grip decay instead of a tire model, ~1 day of tuning); no free rollovers or suspension travel; car-vs-car collision has to be written by hand; a bad slope will look less convincing than real suspension.

### C) Hybrid — ship B, keep a Chaos seam
Same as B, but `ACampusVehicle` exposes a thin `IVehicleDrive` boundary (`SetInput`, `GetState`, `Tick`) so a Chaos-backed implementation can be dropped behind it later for a desktop/high-end tier, selected by device profile.

**Recommendation: B for MVP, with C's seam from day one** — because the host is a phone over a relay, and B is the only option whose motion is cheap enough to simulate, small enough to replicate at 20 Hz, and *reproducible* enough to rewind exactly. Chaos buys fidelity we cannot render and pays for it in the exact resource (host frame time) that is already scarcest.

---

## 5. MVP scope

**In:**
1. `ACampusVehicle` — one car class, kinematic, server-authoritative, 3 instances placed in `L_Match`.
2. 4 seats: `Seat_Driver`, `Seat_FrontPass`, `Seat_RearL`, `Seat_RearR`.
3. Enter / exit / seat-switch, snap (no animation), touch button that appears near a car.
4. Driver drives with the existing joystick + a brake button; **driver cannot fire**.
5. Passengers fire, inside a per-seat yaw/pitch aperture, with an in-vehicle recoil penalty.
6. Car HP + 4 independent wheel HPs + part multipliers (engine / body / roof / glass).
7. Bullets: body **blocks**, glass **passes through**, all through a **rewound** vehicle test.
8. Destruction: disabled + burning 3 s → explosion (radial to bystanders, lethal to occupants) → wreck for the rest of the round.
9. Roadkill with a minimum-speed gate, a launch impulse that feeds the existing fall-damage system, and no friendly fire.
10. Wall-collision damage to the car.
11. Round reset alongside `ACampusDoor::Repair()`.
12. Client interpolation + driver prediction + correction blending.

**Out (deliberately):** fuel, car-vs-car ramming, enter/exit animation, horn, headlights, more than one car class, repair, mounted weapons, car-as-cover destructible deformation, AI drivers.

---

## 6. Design — contracts

### 6.1 `ACampusVehicle` (new: `Source/CollegeCampusGame/Campus/CampusVehicle.h/.cpp`)

A **replicated `AActor`, not a `APawn`.** This is the single most load-bearing call in the document.

> The driver keeps possessing their own `ACampusCharacter`. Nothing re-possesses. Therefore the camera rig, `SCampusTouchControls`, team/voice, `ACampusPlayerState` scoring, `ApplyHit`, `OnKilled`, respawn, **and the entire `RewoundVictim` lag-comp path keep working with no changes**, because the occupant is still a pawn with a `PlayerState` in `GameState->PlayerArray`, and `RecordHitbox()` in its `Tick` keeps recording its world-space capsule as the car carries it along. Possessing a vehicle pawn would have forced a rewrite of all of that.

```cpp
UENUM()
enum class ECampusSeat : uint8 { Driver = 0, FrontPass = 1, RearL = 2, RearR = 3, None = 255 };

UENUM()
enum class ECampusVehiclePart : uint8 { None, Engine, Body, Roof, Glass, Wheel };

UENUM()
enum class ECampusVehiclePhase : uint8 { Alive, Burning, Wreck };

/** One 20 Hz authoritative snapshot. ~22 B of payload; a full state every packet, never a delta. */
USTRUCT()
struct FCampusVehicleState
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize100 Location = FVector::ZeroVector;
    UPROPERTY() uint16 Yaw   = 0;   // degrees * 65536/360
    UPROPERTY() int16  Pitch = 0;   // degrees * 100, ground conform
    UPROPERTY() int16  Roll  = 0;   // degrees * 100
    UPROPERTY() int16  Speed = 0;   // cm/s along forward, signed
    UPROPERTY() int16  YawRate = 0; // deg/s * 10
    UPROPERTY() uint8  Flags = 0;   // 1 handbrake, 2 airborne, 4 engineDead
};

UCLASS()
class ACampusVehicle : public AActor
{
    GENERATED_BODY()
public:
    ACampusVehicle();

    // ---- seats -----------------------------------------------------------------
    /** Nearest free seat within UseDistance of these feet, or ECampusSeat::None. */
    ECampusSeat FindFreeSeat(const FVector& Feet) const;
    bool IsSeatFree(ECampusSeat Seat) const;
    ACampusCharacter* GetOccupant(ECampusSeat Seat) const;
    ACampusCharacter* GetDriver() const { return GetOccupant(ECampusSeat::Driver); }
    /** World transform a seat's capsule sits at (seat socket + capsule half-height). */
    FTransform SeatTransform(ECampusSeat Seat) const;
    /** Seat's firing aperture in the car's local frame. Driver returns a zero cone (cannot fire). */
    void SeatAperture(ECampusSeat Seat, FVector& OutLocalForward, float& OutHalfYawDeg,
                      float& OutMinPitchDeg, float& OutMaxPitchDeg) const;

    // ---- server only -----------------------------------------------------------
    bool ServerTryEnter(ACampusCharacter* Who, ECampusSeat Seat);
    bool ServerTrySwitch(ACampusCharacter* Who, ECampusSeat ToSeat);
    void ServerExit(ACampusCharacter* Who);
    /** Driver intent. Throttle/Steer in [-1,1]; clamped and rate-limited by the caller's RPC. */
    void ServerSetDriveInput(ACampusCharacter* From, float Throttle, float Steer, bool bHandbrake);

    /** A bullet landed on a part. Server only. Credits Instigator for the scoreboard and the explosion team rule. */
    void ApplyPartDamage(ECampusVehiclePart Part, int32 WheelIndex, float Amount, AController* Instigator);
    /** Round start: occupants out, HP + wheels whole, phase Alive, back at HomeTransform, history cleared. */
    void ResetForRound();

    // ---- lag compensation ------------------------------------------------------
    struct FVehicleFrame { double Time = 0.0; FVector Location; FQuat Rotation; };
    /** Recorded transform at Time (lerp + slerp between frames). Mirrors ACampusCharacter::HitboxAt. */
    FVehicleFrame FrameAt(double Time) const;

    /** Segment vs this car's oriented boxes AT the given rewound frame.
     *  Returns true on a hit. bOutBlocks is false for Glass (the bullet carries on to the occupant). */
    bool RewoundTrace(const FVector& Start, const FVector& Dir, float MaxDistance, double When,
                      float& OutDistance, ECampusVehiclePart& OutPart, int32& OutWheelIndex,
                      bool& bOutBlocks) const;

    ECampusVehiclePhase GetPhase() const { return Phase; }
    float GetSpeed() const { return Speed; }
    int32 GetPoppedWheelCount() const;
    /** How far behind its newest snapshot a remote client draws this car. Feeds the shooter's rewind. */
    static constexpr double PlayoutDelay = 0.10;
    static constexpr float  UseDistance  = 220.f;   // cm, button appears
    static constexpr float  AdmitDistance = 310.f;  // cm, server grace (1.4x) for relay latency

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>&) const override;

private:
    // ---- components. One box for the sweep; the mesh never collides. ----------
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent>        Hull;    // root, sweep volume, ECC_Vehicle
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;    // NoCollision
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Wheels[4];
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent>      Seats[4];

    // ---- replicated ----------------------------------------------------------
    UPROPERTY(ReplicatedUsing=OnRep_State) FCampusVehicleState Net;
    UPROPERTY(Replicated)                  float  Health = 600.f;
    UPROPERTY(Replicated)                  uint8  WheelHealth[4];      // 0..100
    UPROPERTY(ReplicatedUsing=OnRep_Phase) ECampusVehiclePhase Phase = ECampusVehiclePhase::Alive;
    UPROPERTY(Replicated)                  TObjectPtr<ACampusCharacter> Occupants[4];

    // ---- server simulation ---------------------------------------------------
    float Speed = 0.f, YawRate = 0.f, LateralSpeed = 0.f, VerticalSpeed = 0.f;
    float InThrottle = 0.f, InSteer = 0.f; bool bInHandbrake = false;
    double LastInputTime = 0.0; int32 InputsThisSecond = 0;
    FTransform HomeTransform;
    float BurnEndTime = 0.f;
    TWeakObjectPtr<AController> LastDamagedBy;
    TArray<FVehicleFrame> History;          // authority only
    TMap<TWeakObjectPtr<ACampusCharacter>, double> RoadkillCooldown;

    void SimulateStep(float Dt);            // the integrator — identical code on server and driver client
    void ProbeGround(float Dt);             // 4 traces, ground conform, airborne detection
    void SweepMove(float Dt);               // SetActorLocationAndRotation(sweep) + wall damage
    void DoRoadkill(float Dt);              // sphere overlap, closing-speed damage, launch
    void RecordFrame();
    void Explode();
    void EnterWreck();

    // ---- client smoothing ----------------------------------------------------
    TArray<FCampusVehicleState> Buffer;     // playout ring, last 4
    FVector  SmoothPosOffset = FVector::ZeroVector;   // residual correction, blended out
    FQuat    SmoothRotOffset = FQuat::Identity;
    void TickRemoteInterp(float Dt);
    void TickDriverPrediction(float Dt);
    UFUNCTION() void OnRep_State();
    UFUNCTION() void OnRep_Phase();
};
```

### 6.2 `ACampusCharacter` — additions

```cpp
// public
ACampusVehicle* GetVehicle() const { return Vehicle; }
ECampusSeat     GetSeat()    const { return Seat; }
bool IsSeated()  const { return Vehicle != nullptr; }
bool IsDriving() const { return Vehicle && Seat == ECampusSeat::Driver; }
/** A free seat within ACampusVehicle::UseDistance, for the touch button. Mirrors CanUseZipline(). */
ACampusVehicle* FindNearbyVehicle(ECampusSeat& OutSeat) const;

// touch / input entry points, mirroring SetFireHeld / JumpPressed
void VehiclePressed();        // enter nearest free seat, or exit if seated
void VehicleLongPressed();    // seat switch (0.4 s hold)
void SetBrakeHeld(bool b);    // the driver's Fire button becomes Brake

/** 0.10 s while seated (the car's playout buffer draws this mesh), 0.08 s on foot.
 *  Read per candidate inside RewoundVictim — see §8.3. THIS IS THE CORRECTNESS-CRITICAL ONE. */
double ViewDelaySeconds() const;

// private
UFUNCTION(Server, Reliable) void ServerRequestSeat(ACampusVehicle* Car, ECampusSeat Seat);
UFUNCTION(Server, Reliable) void ServerRequestExit();
/** 20 Hz, only when the values move by >8/255 or every 250 ms. int8 to keep it at 4 bytes. */
UFUNCTION(Server, Unreliable) void ServerDriveInput(int8 Throttle, int8 Steer, bool bHandbrake);

UPROPERTY(Replicated) TObjectPtr<ACampusVehicle> Vehicle = nullptr;
UPROPERTY(Replicated) ECampusSeat Seat = ECampusSeat::None;
```

`GetLifetimeReplicatedProps`: `DOREPLIFETIME(ACampusCharacter, Vehicle)`, `DOREPLIFETIME(ACampusCharacter, Seat)` — **unconditional**, not `COND_SkipOwner`: every client needs to know who is in which seat to draw them and to aim at them.

### 6.3 Touch UI contract (`SCampusTouchControls`)

- `enum class EControl` gains `Vehicle`. Follow the existing wiring verbatim: `MakeLayout()` adds `FVector2D Vehicle; float VehicleR;`, `HitTest()` gains `if (In(L.Vehicle, L.VehicleR * Pad)) return EControl::Vehicle;`, `PressControl()`/`ReleaseControl()` call `Character->VehiclePressed()` / long-press, `DrawIcon()` gains `case EControl::Vehicle: IconName = TEXT("car");`, `DrawControls()` adds `Plain(EControl::Vehicle, L.Vehicle, L.VehicleR, false, 1.f)`.
- **Visibility rule:** drawn only when `Character->FindNearbyVehicle(Seat) != nullptr` **or** `Character->IsSeated()`. Label flips `ENTER` → `EXIT`.
- **Position:** immediately above `Jump`, same radius as `Crouch`. A dedicated button, **not** an overloaded `Jump`.
  > *Trade-off:* `Jump` already means jump / mantle / zipline (`CanUseZipline()` + `JumpPressed()`). A car is a multi-second commitment — entering one when you meant to vault a railing is a rage-quit bug. One extra 44 px button is the cheaper cost.
- **Driver layout swap:** while `IsDriving()`, `Fire` / `FireLeft` / `Scope` / `Zoom` / `Reload` are hidden; `Fire`'s slot becomes `BRAKE` (held). `Sprint` becomes the handbrake. The joystick is unchanged: **Y = throttle, X = steer**, which is already how the stick feeds `SetTouchMove`.
- **HUD while seated:** a car-HP bar under the player HP bar (reuse `DrawNotchedBox` + the `LagHealth` easing pattern), 4 small wheel pips that grey out when popped, a seat chip (`D` / `P1` / `P2` / `P3`), and a flashing `BAIL OUT` warning during the 3 s burn.
- Driver cannot aim: `SetAiming` is a no-op while `IsDriving()`.

### 6.4 Collision and channels

| Component | Channel / profile | Why |
|---|---|---|
| `Hull` (box) | Object type `ECC_Vehicle`, blocks `WorldStatic` + `WorldDynamic`, **ignores `ECC_Pawn`** | The car must plough through players, not be stopped by them. Roadkill is a separate overlap (§7.5). Removes a whole bug class. |
| `Body`, `Wheels` meshes | `NoCollision`, `CastShadow = false` | The campus is unlit (`build_campus.py`: three.js Lambert baked into an unlit shader, no shadows). One sweep volume is enough. |
| Glass | **no component at all** — pure math boxes in `RewoundTrace` | Keeps the physics scene thin; glass only ever matters to the bullet test. |

`ECC_Vehicle` is a built-in object channel and is already referenced in `Config/DefaultEngine.ini` (`ObstaclePreset`). `GameTraceChannel1/2/3` are taken (`Traversable`, `Mouse`, `Obstacle`); **`GameTraceChannel4` is free** if a dedicated trace channel is ever wanted. **No new channel is needed for MVP.**

Consequence to internalise: `ServerFire`'s world trace queries `ECC_WorldStatic | ECC_WorldDynamic` only, so **the car is invisible to that trace by design** — it is tested exclusively through its own rewound path (§8.2). That is the whole point: the "now" trace and the "rewound" trace must never disagree about a 300 cm-displaced car.

---

## 7. Damage model — numbers

Anchored to the game's own AK: `Damage = 25`, `HeadMultiplier = 2`, `LimbMultiplier = 0.75`, falloff `1.0 → 0.7` over `3000 → 6000` cm, `MagazineSize = 30`, player `MaxHealth = 100`, `SafeFall = 400`, `DeadlyFall = 1550`.

### 7.1 Car health and parts

```
VehicleMaxHealth = 600
PartMultiplier:  Engine 1.00 | Body 0.75 | Roof 0.50 | Glass 0.25 | Wheel → wheel HP, 0 to the hull
VehicleDamage = Damage * PartMultiplier * Falloff(distance)       // same Falloff the doors use
```

Why 600 and not PUBG's ~1800: PUBG has 30-minute matches and 40 weapons. We have **one AK and a 3-minute round**. 600 HP at the body multiplier is `600 / (25 × 0.75) = 32 shots` — **one magazine plus two**. Engine shots: `600 / 25 = 24`. That is a car a squad can kill in one sustained burst, which is the only pacing a short round can carry. PUBG's 1820 would be 97 body shots = 3.2 magazines and the car would simply always win.

The 1.00 / 0.75 / 0.50 ratio is PUBG's Engine-100 % / Body-75 % / Roof-50 % zoning, transplanted directly.

### 7.2 Wheels

```
WheelMaxHealth = 100      // 4 AK body shots per wheel, no zone multipliers, no falloff
Popped wheel effects, per wheel:
    MaxSpeed      × 0.78
    SteerRate     × 0.70
    LateralGrip   × 0.75   (it slides more)
Floor: total MaxSpeed never below 0.40 of base; with ≥2 popped, hard-cap Speed at 0.40 × base.
```
Two popped wheels → `0.78² = 0.61`, floored to 0.40 → top speed 480 cm/s, barely faster than a sprinting player (660 cm/s on foot). That reproduces PUBG Mobile's "significantly reduces vehicle speed and maneuverability" and makes tire shooting a real tactic rather than a flavour detail. Wheel damage does **not** touch hull HP — tires are a mobility kill, not a vehicle kill.

### 7.3 Destruction sequence

```
Health reaches 0
  → Phase = Burning, BurnEndTime = Now + 3.0 s
    Engine dead: Throttle forced to 0, drag doubled, steering 0.3x. The car coasts to a stop.
    Occupants may still exit — this is the bail-out window. HUD flashes BAIL OUT.
  → at BurnEndTime (or instantly if a future explosive ever deals ≥120 in one hit):
      Explode():
        Occupants still seated: ApplyHit(999, LastDamagedBy)  — lethal, no falloff, no team check.
        Bystanders within ExplosionRadius, line of sight from the hull centre:
            Dmg = ExplosionDamage * (1 - Dist/ExplosionRadius)      // linear
            skipped if same team as LastDamagedBy
        Launch impulse on survivors: away from centre, 500 + 400*(1 - Dist/Radius) cm/s, +300 up
  → Phase = Wreck for the rest of the round: Body swaps to a scorched material instance,
    Hull collision stays (solid cover, blocks bullets at Body multiplier but takes no further damage),
    seats closed. Cleared by ResetForRound().

ExplosionDamage = 85      // lethal inside ~1 m, survivable at 3 m with full health
ExplosionRadius = 450     // cm
OccupantExplosionDamage = 999  // always lethal
BurnSeconds = 3.0
```
PUBG uses a 5 s burn; 3 s fits a 3-minute round and still gives a real bail-out decision. "All people inside and around the vehicle can get killed" (PUBG Mobile) and Free Fire's "catches fire and gets out of control before exploding" are both honoured.

**Friendly fire on the explosion — a deliberate divergence from Free Fire.** Free Fire explicitly permits team-killing by blowing up a vehicle. In a 4v4, 3-minute round that is pure griefing. Our rule: bystanders on `LastDamagedBy`'s team take **no** damage; occupants die regardless of team, because the 3 s burn was their warning.

### 7.4 Occupant exposure — geometric, not a percentage

**Rejected:** a per-seat "exposure %" roll. Players cannot read a dice roll, and the existing hit system is an exact analytic capsule test — a probability would be the only non-deterministic thing in the whole combat path.

**Chosen:** three oriented box sets on the car, in its local frame.

| Box set | Blocks the bullet? | Damage to car | Covers |
|---|---|---|---|
| `SolidBoxes[]` — lower hull | **yes** | Body ×0.75 (or Engine ×1.00 for the front box) | doors, hood, boot, sills; everything below the window line |
| `GlassBoxes[]` — the greenhouse | **no** — the bullet carries on | Glass ×0.25 | windscreen, side and rear windows; everything above the window line |
| `RoofBox` | **yes** | Roof ×0.50 | the roof panel |

MVP geometry for a sedan-sized hull (`~460 × 190 × 150` cm):
```
SolidBoxes[0]  Engine  front  (x +140..+230, y ±95, z 0..95)
SolidBoxes[1]  Body    cabin  (x -150..+140, y ±95, z 0..95)      // window line at z = 95
SolidBoxes[2]  Body    rear   (x -230..-150, y ±95, z 0..110)
GlassBoxes[0]  Glass   cabin  (x -150..+140, y ±95, z 95..150)
RoofBox        Roof           (x -150..+140, y ±95, z 150..160)
```
Seat capsule centres sit at `z ≈ 118` (a seated 100 cm capsule from `z 68` to `z 168`). So of a seated occupant:
- `z 68..95` (**~27 %**, hips and thighs) is behind solid metal → protected;
- `z 95..168` (**~73 %**, torso, shoulders, head) is behind glass → **hittable**, and the top of that range is the head zone;
- a shot from directly above passes the roof box → blocked.

Which is exactly the published behaviour: "fire at passengers and drivers **through windows or openings**" and "unless a headshot or other body shot is directly targeted, occupants don't take significant damage while moving" — the second part falls out naturally because at 1200 cm/s hitting a 73 %-exposed capsule through a window is genuinely hard.

Head/limb zoning needs **zero** new code: `RewoundVictim` already derives the zone from the hit height as a fraction of the capsule, and the capsule is unchanged while seated.

### 7.5 Roadkill

```
ToVictim = Normalize(VictimLocation - HullCenter)
Closing  = max(0, dot(CarVelocity - VictimVelocity, ToVictim))     // cm/s, closing speed only

MinRoadkillSpeed  = 400     // 14.4 km/h — below this: shove only, zero damage
FullRoadkillSpeed = 1200    // our top speed, 43 km/h
RoadkillDamage    = 180     // at or above FullRoadkillSpeed

Dmg = RoadkillDamage * clamp((Closing - MinRoadkillSpeed) / (FullRoadkillSpeed - MinRoadkillSpeed), 0, 1)

Launch = ToVictim * (Closing * 0.60) + Up * 450                    // LaunchCharacter, bXYOverride=true
```
| Closing speed | Damage | Outcome vs 100 HP |
|---|---|---|
| 400 cm/s (14 km/h) | 0 | shoved, unhurt |
| 600 cm/s (22 km/h) | 45 | hurt, survives |
| 800 cm/s (29 km/h) | 90 | one hit from death |
| 1000 cm/s (36 km/h) | 135 | **dead** |
| 1200 cm/s (43 km/h) | 180 | **dead** |

Lethal above 36 km/h, matching the genre's "over 200 damage, dies the moment they are hit" intent at an absolute scale that fits a 100 HP player. The 450 cm/s upward launch also reproduces Free Fire's "sent flying… dies from the combo of fall damage" secondary kill path **for free** — the existing `Landed()` / `FallTopZ` / `SafeFall 400` / `DeadlyFall 1550` system does the rest with no new code.

Rules:
- **Detection:** one sphere overlap per car per tick (radius = hull half-diagonal + 60 cm), only when `|Speed| > MinRoadkillSpeed`, `ECC_Pawn` only. Not a sweep — the hull box deliberately ignores Pawn (§6.4).
- **Friendly fire: off.** A teammate is shoved and launched but takes 0 damage. Matches `ServerFire`'s existing `MyState->Team == VictimState->Team` early return.
- **Credit:** `Victim->ApplyHit(Dmg, DriverController)` — the existing `DamageDealt` / `OnKilled` / scoreboard / kill-feed path works untouched. **No driver → no damage** (a rolling driverless wreck cannot kill).
- **Cooldown:** 0.6 s per (car, victim) pair, so one pass deals one hit rather than 18 ticks of it.
- **Occupants are immune** to their own car.
- **Phase gate:** only while `Alive` or `Burning`. A `Wreck` is static cover.

### 7.6 Wall collision damage to the car

Free Fire: "durability decreases when you collide with a wall or building." We already have the blocked sweep result, so this is nearly free.
```
On a blocked SweepMove with |Speed| > 600:
    Dmg   = 0.08 * (|Speed| - 600)        // top speed 1200 → 48 damage
    Speed = Speed * 0.45                  // and it kills most of the momentum
    Occupants take Dmg * 0.25 (12 at top speed), no team check — a crash hurts everyone in the car.
Hard landing (VerticalSpeed < -900 on touchdown): same formula on |VerticalSpeed|, Speed *= 0.7.
```
Twelve full-speed wall impacts kill a car. This is the thing that stops a car from being an invulnerable battering ram, and it makes bad driving cost something.

### 7.7 Firing from seats

| Seat | Can fire | Local forward | Half-yaw | Pitch range |
|---|---|---|---|---|
| Driver | **no** | — | — | — |
| FrontPass | yes | `+Y` (car's right) | 100° | −35° … +60° |
| RearL | yes | `−Y` (car's left) | 110° | −35° … +60° |
| RearR | yes | `+Y` (car's right) | 110° | −35° … +60° |

- **Driver cannot fire.** Genre-honest on mobile (COD Mobile reserves one seat for the driver; PUBG's driver-shooting is PC-only and sidearm-only), and it removes the "one thumb steers while the other aims" problem that no amount of HUD can solve.
- **Aperture enforcement, both ends.** The client clamps `ApplyLook` to the seat cone (so the camera never shows you an angle you cannot shoot), and `ServerFire` **rejects** a direction outside it. The server check is 3 lines:
  ```cpp
  if (IsSeated()) {
      FVector LocalFwd; float HalfYaw, MinPitch, MaxPitch;
      Vehicle->SeatAperture(Seat, LocalFwd, HalfYaw, MinPitch, MaxPitch);
      const FVector Local = Vehicle->GetActorTransform().InverseTransformVectorNoScale(FVector(Direction));
      if (Seat == ECampusSeat::Driver) return;                             // driver never fires
      if (FVector::DotProduct(Local.GetSafeNormal2D(), LocalFwd) < FMath::Cos(FMath::DegreesToRadians(HalfYaw))) return;
      const float PitchDeg = FMath::RadiansToDegrees(FMath::Asin(Local.GetSafeNormal().Z));
      if (PitchDeg < MinPitch || PitchDeg > MaxPitch) return;
  }
  ```
  Without it, a cheating client shoots through its own car body — and the solid boxes in `RewoundTrace` would not stop it, because the shot *originates inside* them.
- **In-vehicle accuracy penalty** (PUBG raised in-vehicle recoil 10 %; we go further because our only weapon is a full-auto rifle): while seated and `|Speed| > 300`, `RecoilUpDeg × 1.35`, `RecoilSideDeg × 1.60`, and zoom capped at 2× (no 4×). Drive-bys should be chaotic, not a free headshot platform.
- **Friendly fire inside the car is blocked for free** by `ServerFire`'s existing team check. PUBG allows it; we keep ours off.
- **`ServerFire`'s existing muzzle-proximity guard** (`DistSquared(Start, GetActorLocation()) > 250²`) still passes while seated, because the occupant's capsule moves with the car. **No change needed** — verified against the current code.

### 7.8 Fuel — cut, deliberately

PUBG needs fuel because matches run 30 minutes across 8×8 km. Our round is 3–10 minutes on one campus. Fuel would add a replicated float, a HUD gauge, a refuel interaction, a canister pickup, and a failure state that players will read as a bug ("the car stopped working"). We also could not source a single verifiable consumption number for PUBG Mobile or BGMI (§3.10) — so we would be inventing it anyway.

**Decision: no fuel.** If vehicle uptime turns out to be a balance problem, the lever is the **wheel HP and the hull HP**, which are already readable and already shot at.

---

## 8. Netcode

### 8.1 Replication topology

```
Driver phone                     Host phone (authority)                 Other phones
------------                     ----------------------                 ------------
joystick Y/X, brake, handbrake
  │ ServerDriveInput(int8,int8,bool)   20 Hz, Unreliable, ~4 B
  │  (sent only when a value moves >8/255, or every 250 ms as keepalive)
  └────────────────────────────▶ clamp, rate-limit, authorise driver
                                 SimulateStep(Dt)   ← the only truth
                                 RecordFrame()      ← 60 Hz transform history
                                       │
                                 FCampusVehicleState  20 Hz, push-model, ~22 B
                                       ├──────────────────────────────▶ Buffer → interp @ Now-0.10
                                       └──▶ driver: reconcile vs local prediction
                                 Health / WheelHealth / Phase / Occupants  ← reliable, on change only
                                 AttachmentReplication (seats)             ← engine-native
```

Actor settings:
```cpp
bReplicates = true;                 bAlwaysRelevant = false;
SetNetUpdateFrequency(20.f);        SetMinNetUpdateFrequency(2.f);
NetPriority = 2.5f;                 // above a door (1.0), below a player (3.0)
NetCullDistanceSquared = 2.25e9f;   // 150 m — a car matters long before you can walk to it
PrimaryActorTick.TickInterval = 0.f;
```
`FCampusVehicleState` is **one full snapshot per packet, never a delta the receiver must accumulate.** Losing a packet costs 50 ms of smoothing, not a desync. That is the only safe posture on a 1–3 % loss relay. Health / wheels / phase / occupancy are the only **reliable** car state, and they are low-frequency and change-driven.

**Bandwidth:** 3 cars × 20 Hz × ~26 B (payload + header) ≈ **1.6 KB/s per client**. Players and voice dominate; this is noise.

### 8.2 Smoothing over the relay

**Remote clients — snapshot interpolation with a 100 ms playout buffer.** This mirrors the project's existing philosophy (`CampusLagComp::ViewDelay = 0.08` for characters), raised to 0.10 because the car updates at 20 Hz (50 ms period) and 100 ms absorbs two lost packets.
```
Keep the last 4 states in a ring. Render at ServerTime - 0.10.
Location: Catmull-Rom across 4 samples (a car's path is smooth; a lerp visibly kinks on corners).
Rotation: Slerp on the quaternion built from Yaw/Pitch/Roll.
Buffer starved → dead-reckon forward from Speed + YawRate for up to 250 ms, then freeze in place.
```
**Driver client — local prediction with the same integrator.** `SimulateStep()` is a closed-form kinematic integrator with **no contact solver**, so replaying it on the driver's phone produces the same answer the host got, to within the ground-probe difference (< 30 cm typical). This is precisely what Chaos cannot offer (§3.6) and the reason Option B wins.
```
On OnRep_State: rebuild the predicted state at the snapshot's timestamp and compare.
    error < 150 cm  → fold into SmoothPosOffset / SmoothRotOffset, blend out exponentially (tau 0.08 s)
    error 150–600 cm → same blend, tau 0.04 s (faster, still no pop)
    error > 600 cm  → hard snap (this only happens on a teleport or a round reset)
```
**Never snap under 150 cm.** A snapping car on a phone screen reads as a broken game, where a 200 ms blend reads as nothing at all.

### 8.3 Lag compensation — the correctness-critical section

Three facts drive the whole design, and they must be held together:

**(a) Occupant capsules need no new rewind machinery whatsoever.** `ACampusCharacter::Tick` already calls `RecordHitbox()` on authority, which records `Capsule->GetComponentLocation()` — **world space**. A seated character is attached to a seat component, so its capsule's world location already includes every centimetre of the car's motion. `RewoundVictim` iterates `GameState->PlayerArray`, and a seated occupant is still a `PlayerState`-owning pawn. **The existing rewind finds seated occupants at their correct historical world positions with zero code changes.** That is the single biggest payoff of not making the vehicle a Pawn.

**(b) The *car* needs its own transform history, shaped exactly like `FHitboxFrame`.** The world trace in `ServerFire` sees the world **as it is now**, but occupants are tested **as the shooter saw them**. At 1200 cm/s with a 250 ms client, the car is **300 cm** from where the shooter aimed. If we let the "now" trace decide whether the body blocks, a shot the player clearly placed through a window would be eaten by metal that has since slid into the way — or worse, a shot aimed at the door would sail through to the occupant. Hence: the car is **excluded from the now-trace by channel** (`ECC_Vehicle`, which `ServerFire`'s `FCollisionObjectQueryParams` does not query — §6.4) and is tested **only** through `RewoundTrace()` at the same `When` the occupants use. Same `KeepSeconds = 0.5`, same `MaxRewind = 0.3`.

**(c) The two view delays must agree, or the glass/metal boundary lies.** `RewoundVictim` currently uses a single `ViewDelay = 0.08` constant (character movement smoothing). A car interpolates at **0.10**. A 0.02 s disagreement at 1200 cm/s is **24 cm** of travel — comfortably enough to move a shot from "through the window" to "into the door pillar", on the exact boundary where it matters most.

> **Fix (3 lines, the most important change in this document):** add `double ACampusCharacter::ViewDelaySeconds() const` returning `ACampusVehicle::PlayoutDelay` (0.10) when seated and `CampusLagComp::ViewDelay` (0.08) otherwise, and in `RewoundVictim`'s loop compute `When` **per candidate** from `Other->ViewDelaySeconds()` rather than from the file-scope constant. A seated character's mesh is drawn by the car's interpolator, not by CharacterMovement smoothing — so its view delay *is* the car's. Get this wrong and drive-by hit registration will feel broken in a way no amount of damage tuning can fix.

**Revised `ServerFire` order of operations** (the existing function, extended — no restructuring):

```
 1. existing gates: alive, armed, shot credit, reload window, ammo, muzzle proximity
 1b. NEW: if seated — driver → reject; else reject directions outside the seat aperture (§7.7)
 2. world trace, ECC_WorldStatic | ECC_WorldDynamic (unchanged; cars are invisible to it)
       → WallDist, WallHit
 3. NEW: for each ACampusVehicle: RewoundTrace(Start, Dir, Range, When(shooter, vehicle))
       → nearest VehDist, VehPart, bVehBlocks. Skip the shooter's own car.
       Glass hits are remembered but DO NOT contribute to BlockDistance.
 4. BlockDistance = min(WallDist, bVehBlocks ? VehDist : Range)
 5. RewoundVictim(Start, Dir, BlockDistance, OutPoint, OutZone)        // per-candidate view delay
 6. if a Victim was found closer than BlockDistance:
        → existing team check, zone multiplier, falloff, ApplyHit, ClientConfirmHit  (UNCHANGED)
 7. else if the nearest thing was the vehicle (blocking OR glass):
        → Vehicle->ApplyPartDamage(VehPart, WheelIndex, Damage * PartMult * Falloff, GetController())
        → MulticastFire(hit point, normal, bMark=true)
 8. else existing path: ACampusDoor::ApplyDamage, or a world mark
```
A glass hit therefore does **both**: it chips the car by `Damage × 0.25 × Falloff` *and* lets the occupant behind it be hit — because glass never raised `BlockDistance`. That is the mechanic, falling straight out of the ordering.

**Cost of the car rewind:** 3 cars × ~6 oriented-box segment tests = 18 analytic tests per shot, at ~10 shots/s per shooter. Trivially below the `GameState->PlayerArray` loop it sits beside.

**Memory:** 0.5 s × 60 Hz × 40 B = **1.2 KB per car**.

### 8.4 Attachment and movement handoff

```cpp
// server, on enter
Who->GetCharacterMovement()->StopMovementImmediately();
Who->GetCharacterMovement()->SetMovementMode(MOVE_None);
Who->GetCharacterMovement()->DisableMovement();
Who->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly); // still hit-testable, no physics
Who->SetReplicateMovement(false);          // the attachment carries the transform
Who->AttachToComponent(Seats[(int32)Seat], FAttachmentTransformRules::SnapToTargetNotIncludingScale);
Who->Vehicle = this; Who->Seat = Seat; Occupants[(int32)Seat] = Who;
Who->SetFireHeld(false); Who->SetAiming(false);
if (Who->IsZipping())  { /* cancel the zip first */ }
ForceNetUpdate();
```
`SetReplicateMovement(false)` is **not optional**: leave it on and the character's own movement replication fights the parent transform, producing the classic "passenger vibrates inside the car" artefact. `AActor::AttachmentReplication` is engine-native, so clients re-parent on their own — no custom RPC.

On exit: reverse it, then place the character at a ground-probed spot beside the seat's door (try the near side, then the far side, then the rear; if all three are blocked, **refuse the exit** and tell the client). `SetReplicateMovement(true)`, `SetMovementMode(MOVE_Falling)`, `SetCollisionEnabled(QueryAndPhysics)`, and carry the car's velocity into `LaunchCharacter` so you keep momentum.

Exiting a moving car, per PUBG ("can cause damage and even instant death"):
```
if |Speed| > 900:  ApplyHit(0.05 * (|Speed| - 900), DriverController)   // 1200 → 15 dmg
                   Launch forward + up; the existing fall-damage system finishes the job
```

### 8.5 Anti-cheat

The client gains exactly **one new verb: two `int8`s and a bool.** There is no transform to forge — "accept inputs, not positions."

| Surface | Server-side guard |
|---|---|
| `ServerDriveInput` | clamp to [-1,1]; **reject if the sender is not `Occupants[Driver]`**; rate-limit 30/s (silently drop the excess); reject if `Phase == Wreck` |
| Speed envelope | the server integrates it — a client cannot ask for more than `MaxSpeed × wheel penalties`. Nothing to validate. |
| `ServerRequestSeat` | distance re-checked **against the server's own car transform** with `AdmitDistance = 310` cm (1.4× grace for relay latency); seat must be free; `Phase != Wreck`; not already seated; not mid-zip / mid-mantle; 0.5 s cooldown per player |
| `ServerRequestExit` | must actually be seated; 0.3 s cooldown (stops enter/exit spam as a hit-box dodge) |
| Seat switch | seat free; 0.5 s cooldown; blocked while `Burning` (no dodging the explosion by seat-hopping) |
| Fire from a seat | seat-aperture dot check (§7.7) **plus** every existing guard: shot credit, ammo, reload window, muzzle proximity |
| Rewind abuse | `MaxRewind = 0.3` already clamps it; ping comes from `APlayerState::GetPingInMilliseconds()` (server-measured), never from the client |
| Repeat offenders | count violations on `ACampusPlayerState`; **10 in 10 s → kick.** Log, do not silently correct. |

No new trust is extended. The attack surface grows by one 4-byte unreliable RPC.

---

## 9. Performance budget

**Phone host, 8 players, 3 cars, 30 fps target = 33.3 ms/frame.**

| Work | Where | Cost |
|---|---|---|
| 4 ground probes (120 cm line traces) | host, per car | 0.06 ms |
| 1 box sweep for the move | host, per car | 0.05 ms |
| 1 roadkill sphere overlap (only when `Speed > 400`) | host, per car | 0.04 ms |
| `RecordFrame()` (one transform push) | host, per car | < 0.01 ms |
| Wheel spin / steer visual transforms | all, per car | 0.02 ms |
| **Host total, 3 cars** | | **≈ 0.6 ms (1.8 % of frame)** |
| Remote client, per car | pure interpolation, no traces | 0.02 ms |
| `RewoundTrace`, per shot | 3 cars × ~6 boxes | ≈ 0.01 ms |

**Compare:** a Chaos wheeled vehicle is 1.5–3 ms/car on a phone **and** drags in a fixed 60 Hz async physics scene plus a 250 ms resimulation window on the same device (§3.6, §3.7). At 3 cars that is 4.5–9 ms — **14–27 % of the entire frame budget**, before the resim cost. That is the whole argument in one row.

**Render budget per car** (the campus is unlit — one flat shader, no shadows, see `build_campus.py::master(lit=False)`):
- Body: ≤ 6 000 tris, **1** material, `CastShadow = false`
- Wheels: ≤ 500 tris each, one `UInstancedStaticMeshComponent` → **1** extra draw call
- **2 draw calls per car, 3 cars = 6 draw calls.** (For scale: the 14 static car props are already 1 merged mesh today.)
- `LDMaxDrawDistance` on the wheels at 6 000 cm (same `INK_CULL = 9000` philosophy as the campus meshes)
- Wreck is a **material-instance swap only** — no new mesh, no new draw call

**Memory:** 1.2 KB history per car + ~40 MB of migrated vehicle art before optimisation (target: **under 8 MB** after a tri/texture pass — the APK is the real constraint).

**Degradation gate:** if host frame time exceeds 40 ms for 2 consecutive seconds, halve the history rate (60 → 30 Hz) and the ground-probe rate (every frame → every other frame, interpolating between). Interpolation stays correct; rewind precision drops from 16 ms to 33 ms of granularity, which is inside the existing error budget. Expose as `campus.VehicleHistoryHz` and `campus.VehicleProbeHz`.

---

## 10. Art and asset plan

### 10.1 Today

`Content/Campus/Meshes/SM_Campus_Cars.uasset` — 14 car props from `campus_recipe.json`'s `"Cars"` group, merged into **one** static mesh, placed by `Scripts/build_campus.py::place()` as a single `StaticMeshActor` with STATIC mobility, no shadow, and `NO_COLLISION`. They are painted scenery. **Leave them exactly as they are for MVP** — they cost 1 draw call and they are the map's visual language.

### 10.2 Asset source — already on this disk, nothing to download

```
E:\UE_5.8\Templates\TemplateResources\Standard\Vehicles\Content\
    OffroadCar\  SKM_Offroad, SKM_Offroad_Skeleton, SKM_Offroad_PhysicsAsset,
                 SM_Offroad_Body, SM_Offroad_Tire, Offroad_AnimBP, Offroad_CtrlRig
    SportsCar\   SKM_SportsCar, SK_SportsCar, PA_SportsCar, ABP_SportsCar, CtrlRig_SportsCar,
                 SM_SportsCar, SM_SportsCar_Glass, SM_SportsCar_Wheel
    PhysicsMaterials\  Slippery, NonSlippery
    (55 assets, 40 MB total, plus full material/texture sets)
```
Engine template content, shipped with the engine under the UE EULA, usable in a UE-based product. **Zero download, zero license friction, verified present.**

**For the kinematic design we need only the static pieces:**
- Body: `SM_SportsCar` (has a separate `SM_SportsCar_Glass` — exactly the solid/glass split §7.4 wants) **or** `SM_Offroad_Body`
- Wheel: `SM_SportsCar_Wheel` or `SM_Offroad_Tire`

That sidesteps skeletal meshes entirely — worth 3–5 fps on its own per the forum report in §3.7 — and means **no vehicle skeleton, no physics asset, no AnimBP, no control rig**.

Migration steps:
1. Migrate the 4 static meshes into `/Game/Campus/Vehicles/`.
2. Reauthor the materials as unlit three.js-style instances so the car matches the campus's flat look — reuse the `build_campus.py::master(lit=False)` recipe (hemisphere 1.6 + sun 1.8 baked into an unlit shader). A PBR sports car parked in a flat-shaded pencil campus will look wrong before anyone notices the physics.
3. Tri/texture pass to the §9 budget. Build LOD0/LOD1.
4. Build `BP_CampusCar` (or a C++-only `ACampusVehicle` default) with the hardpoints below.

### 10.3 The "skeleton" — four scene components, no bones

```
ACampusVehicle (root = Hull, UBoxComponent 460 × 190 × 150 cm)
├── Body            UStaticMeshComponent, NoCollision
├── WheelInstances  UInstancedStaticMeshComponent × 4  (FL +150,-85,-35 / FR +150,+85,-35
│                                                       RL -140,-85,-35 / RR -140,+85,-35)
├── Seat_Driver     USceneComponent  (-20, -45, +18)
├── Seat_FrontPass  USceneComponent  (-20, +45, +18)
├── Seat_RearL      USceneComponent  (-130, -45, +18)
└── Seat_RearR      USceneComponent  (-130, +45, +18)
```
Wheels are spun by `Speed / WheelRadius` and the front pair steered by `InSteer × 30°` — two lines in `Tick`, no animation asset. Seat Z `+18` puts a 100 cm seated capsule's centre at world `z ≈ 118`, which is what §7.4's window line at `z = 95` was computed against.

### 10.4 Phase 3 — art convergence

Author the drivable car in the **same box vocabulary as the rest of the campus**. `export-unreal-recipe.mjs` + `build_campus.py` already generate box/cylinder geometry with per-colour sections and hidden-face masks. A drivable car emitted through that pipeline would share a silhouette with the 14 static props, let a few of the props be *replaced* by drivable instances, and cost a fraction of the template car's triangles. This is the right long-term answer; it is not the right MVP answer.

---

## 11. Round reset

Extend the loop that already exists at the top of `ACampusMatchGameMode::StartRound`:

```cpp
void ACampusMatchGameMode::StartRound(int32 Round, bool bDecider)
{
    for (TActorIterator<ACampusDoor> It(GetWorld()); It; ++It)    { It->Repair(); }
    for (TActorIterator<ACampusVehicle> It(GetWorld()); It; ++It) { It->ResetForRound(); }   // NEW
    ...
}
```
`ResetForRound()`, server only — the exact shape of `ACampusDoor::Repair()`:
```
eject every occupant (the GameMode respawns them at their spawn point anyway)
Health = VehicleMaxHealth;  WheelHealth[0..3] = 100;  Phase = Alive
Body mesh + material restored;  Hull collision restored;  seats reopened
SetActorTransform(HomeTransform)        // captured in BeginPlay
Speed = YawRate = LateralSpeed = VerticalSpeed = 0;  inputs cleared
History.Empty();  RoadkillCooldown.Empty();  Buffer.Empty()
ForceNetUpdate();  OnRep_Phase()        // the host's own view, exactly as Repair() does
```
`History.Empty()` matters: a stale pre-teleport frame in the rewind buffer would let a shot taken right after the reset hit a car that is no longer there.

**No mid-round vehicle respawn.** A car killed in round 2 stays a wreck until round 3 — the wreck is cover, and the decision to burn a car becomes permanent for that round. That is the right tension for elimination rounds, and it is one fewer timer to replicate.

---

## 12. Test plan

### 12.1 Local (PIE, 1 server + 2 clients, `Net PktLag=120 Net PktLagVariance=40 Net PktLoss=2`)

| # | Case | Expected |
|---|---|---|
| 1 | Walk to a car, button appears at 220 cm, tap | seated in the nearest free seat within one frame |
| 2 | Two clients tap the same seat in the same frame | exactly one is seated; the loser gets the next free seat or nothing |
| 3 | Tap enter from 500 cm (hacked client) | server refuses (`AdmitDistance` 310) |
| 4 | Drive a full lap, watch the driver's own view | no rubber-band, no snap; correction error stays < 30 cm |
| 5 | Watch the same lap from the other client | smooth; no kinks on corners (Catmull-Rom); no jitter |
| 6 | Kill the host's packets for 300 ms mid-corner | dead-reckon forward, then freeze, then blend back in; never a teleport |
| 7 | Shoot the door panel of a stationary car | car takes `25 × 0.75 = 18.75`; occupant **unharmed** |
| 8 | Shoot the side window of a stationary car at occupant head height | occupant takes a **headshot** (`25 × 2`); car takes `25 × 0.25` |
| 9 | Same two shots at a car moving 1200 cm/s, 200 ms client | identical outcomes. **This is the test that proves §8.3 — if it fails, the view-delay fix is wrong.** |
| 10 | 4 shots into one tire | that tire pops; top speed drops ~22 %; steering loosens |
| 11 | Pop 2 tires | top speed floors at 40 % (480 cm/s) — a sprinting player (660) outruns the car |
| 12 | 32 body shots | burns for 3 s, then explodes |
| 13 | Stay seated through the burn | dead |
| 14 | Bail out at 1.0 s into the burn | survive, uninjured |
| 15 | Stand 200 cm from the explosion | ~48 damage + launched |
| 16 | Stand 500 cm from the explosion | 0 damage |
| 17 | Stand behind a wall, 200 cm away | 0 damage (line-of-sight check) |
| 18 | Teammate of the killer stands 100 cm away | 0 damage |
| 19 | Drive at 350 cm/s into an enemy | shoved, 0 damage |
| 20 | Drive at 1200 cm/s into an enemy | dead; body launched; the kill is credited to the **driver** |
| 21 | Drive at 1200 cm/s into a **teammate** | shoved and launched, **0 damage** |
| 22 | Drive over the same enemy twice in 0.4 s | one hit only (0.6 s cooldown) |
| 23 | Drive a driverless wreck downhill into a player | 0 damage (no driver, and `Wreck` phase is gated) |
| 24 | Ram a wall at 1200 cm/s | car takes 48; occupants 12; speed drops to 540 |
| 25 | Front passenger fires forward through the windscreen | **rejected** by the aperture check |
| 26 | Front passenger fires right, at 90° | allowed |
| 27 | Driver taps where Fire used to be | brakes; never fires |
| 28 | Hacked client sends `ServerFire` from the driver seat | rejected |
| 29 | Hacked client sends `ServerDriveInput` while a passenger | rejected |
| 30 | Hacked client sends `ServerDriveInput` at 500 Hz | clamped to 30/s, 10 violations in 10 s → kicked |
| 31 | Exit a car moving at 1200 cm/s | 15 damage, launched, momentum carried |
| 32 | Exit with both sides blocked by walls | exit refused, still seated, no teleport into geometry |
| 33 | Switch driver → rear-left while moving | smooth; the car coasts (no driver input) during the switch |
| 34 | Switch seats during the burn | refused |
| 35 | Round ends with a wrecked car and 2 occupants | next round: car whole, at `HomeTransform`, seats empty |
| 36 | Shoot at the reset car's **old** position immediately after reset | miss (`History` was cleared) |
| 37 | Drive off the map below `KillHeight` | occupants die through the existing kill-plane path |
| 38 | Enter a car while on a zipline | zip cancelled first, then seated (or refused — pick one and assert it) |
| 39 | Driver disconnects mid-drive | car coasts to a stop, seat freed, no crash |
| 40 | Host migrates / match ends mid-drive | no crash; occupants detach cleanly |

### 12.2 On device (3 phones, EOS relay, LAN host on `192.168.1.5:7777`)

| # | Measure | Pass |
|---|---|---|
| 41 | Host frame time, 3 cars driving, 6 players, with Unreal Insights | ≤ +1.0 ms vs the no-vehicle baseline |
| 42 | Client frame time, 3 cars visible | ≤ +0.3 ms |
| 43 | Bandwidth per client (`stat net`) | ≤ +2 KB/s |
| 44 | 2 cars driving + 6 players firing, 5-minute soak | no frame-time creep, no history leak |
| 45 | Drive-by hit registration, felt, over the relay at 150–250 ms | a player who clearly aimed through the window gets the hit |
| 46 | Visual smoothness of a remote car over the relay | no stutter at 20 Hz + 100 ms playout |
| 47 | APK size delta | ≤ +8 MB |
| 48 | Thermal: 10 minutes of continuous driving | no new throttle step vs baseline |

### 12.3 Instrumentation to build first

`campus.VehicleDebug 1` → draw, on the host: the rewound car boxes at `When` (green = solid, cyan = glass, red = roof) next to the live hull in white, the seat capsules, the roadkill overlap sphere, and the last 10 history frames as a ribbon. **Build this before the damage model.** Every bug in §8.3 is invisible without it, and `MaxRewind = 0.3` means you are debugging a 300 ms-old world you cannot otherwise see.

---

## 13. Rollout / migration plan

| Phase | Deliverable | Effort | Reversible? |
|---|---|---|---|
| **0** | Art migration: 4 static meshes into `/Game/Campus/Vehicles/`, unlit materials, tri/LOD pass | **1 day** | yes — asset-only |
| **1** | `ACampusVehicle` skeleton: components, replicated state, `SimulateStep`, `ProbeGround`, `SweepMove`. Drive it with a debug key on a standalone host. **Tune the feel.** | **3 days** | yes — new file, nothing else touched |
| **2** | Seats: enter / exit / switch, attachment handoff, server validation. Touch button + HUD. | **2.5 days** | yes — `ACampusCharacter` additions are additive |
| **3** | Netcode: input RPC, 20 Hz snapshot, remote interpolation, driver prediction + correction blending. **Test under `Net PktLag`, then on the relay.** | **3 days** | yes |
| **4** | `campus.VehicleDebug` + the rewind visualiser | **0.5 day** | yes |
| **5** | Damage: `RewoundTrace`, the `ServerFire` extension, **the `ViewDelaySeconds()` fix**, part multipliers, wheels | **2.5 days** | **`ServerFire` is modified — the one place that touches live combat. Behind `campus.VehiclesEnabled`.** |
| **6** | Destruction: burn → explode → wreck, radial damage, FX (reuse `UCampusFxSubsystem`) | **1.5 days** | yes |
| **7** | Roadkill + wall-collision damage | **1 day** | yes |
| **8** | Round reset, 3 placements in `L_Match`, `.uproject` untouched (no plugin needed) | **0.5 day** | yes |
| **9** | Test pass §12.1 + §12.2 on 3 phones, tuning | **2.5 days** | — |
| | **MVP total** | **≈ 18 working days** | |

**Rollback:** every phase is additive except Phase 5. Gate the whole feature on `campus.VehiclesEnabled` (default 0 until Phase 9 passes). The `ServerFire` extension is a strictly-additive branch — with no `ACampusVehicle` in the level, step 3 iterates an empty list and step 7 is unreachable, so the existing combat path is byte-for-byte the current behaviour. **`ViewDelaySeconds()` is the one change that alters existing behaviour for on-foot players — and it returns the current constant `0.08` for them. Verify that explicitly in the test pass.** Removing vehicles = delete the actors from `L_Match` and set the cvar to 0; no migration, no save-game change, no API change.

**Phase 2 (post-MVP), roughly in value order:** car-vs-car ramming · a second car class (a slow 6-seat van vs. a fast 2-seat buggy — the Free Fire risk-class pattern) · horn · the `IVehicleDrive` Chaos seam for a high-end device tier · campus-styled car art through `build_campus.py` · replacing some of the 14 static props with drivable instances · mid-round respawn timers if rounds run long.

---

## 14. Open questions — owner's call

1. **Car count and placement.** 3 is the MVP assumption. Where? Suggest: one by the main gate, one in the central quad, one at the far parking bay — so a car is a contested objective rather than a free spawn perk. **Do you want them team-neutral, or one per team's side?**
2. **Driver cannot fire — confirm.** This is the biggest feel decision in the document. The alternative is PUBG's PC compromise: driver may fire a *sidearm* only. We have no sidearm, so that would mean a new weapon first. **Recommendation: confirm "driver cannot fire" for MVP.**
3. **Top speed 1200 cm/s (43 km/h).** That is 1.8× a sprinting player (660 cm/s). Faster is more fun to drive and far harder to shoot; slower makes the car a rolling coffin. **Want 1000 (safer) or 1400 (more fun)?** This number drives the roadkill curve, the interpolation error, and how hard drive-bys are.
4. **Car HP 600 (≈ one magazine + 2 body shots).** PUBG-equivalent would be ~1800 (3.2 magazines). **Confirm 600, or go to 800 if cars are dying too fast in playtest?**
5. **Explosion friendly fire.** Recommended: teammates of the killer are spared, occupants always die. Free Fire allows the team-kill. **Keep ours protective?**
6. **Fuel: cut.** Confirm it stays cut (§7.8). The lever for vehicle uptime is wheel HP, not fuel.
7. **Round-based wreck persistence.** Recommended: a destroyed car stays a wreck until the next round starts. The alternative is a 45 s mid-round respawn. **Which fits the mode better?**
8. **Art direction for MVP.** Ship the engine's `SM_SportsCar` reskinned to the flat campus palette (fast, slightly off-style), or hold vehicles until a campus-styled car exists (slower, consistent)? **Recommendation: ship reskinned, converge in Phase 3** — the mechanic needs playtesting far more than the silhouette needs to be right.
9. **Seat count 4.** The 14 static props are sedan-shaped. A 4-seat car in a 4v4 means an entire team can ride in one vehicle. **Is that wanted, or cap the MVP car at 2 seats (driver + 1 gunner) to keep teams split?**

---

*I have already seen this system five years out. The foundation holds because the car is one transform, one history, and one 4-byte intent — not a physics scene on a phone. Magic without structure is chaos; the runes are laid.*
