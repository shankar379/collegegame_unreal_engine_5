// Drivable campus car (Docs/VEHICLES_DESIGN.md). A replicated AActor, NOT a Pawn: the driver keeps
// possessing their own ACampusCharacter, attached to a seat component, so the camera rig, touch controls,
// team / voice, scoring, ApplyHit, respawn and the whole RewoundVictim lag-comp path keep working untouched.
//
// Motion is a closed-form kinematic integrator with no contact solver, so the same code replayed on the
// driver's phone reaches the same answer the host got (that is what Chaos cannot offer, and the reason the
// car is not a physics body on a phone host). The host owns the truth; every other phone draws a 20 Hz
// snapshot stream 100 ms behind. Bullets never see the car through the world trace: it is tested only
// through RewoundTrace() at the same instant the occupants are rewound to.
//
// Everything is behind the campus.VehiclesEnabled cvar.

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "CampusVehicle.generated.h"

class ACampusCharacter;
class AController;
class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMeshComponent;

UENUM()
enum class ECampusSeat : uint8
{
	Driver = 0,
	FrontPass = 1,
	RearL = 2,
	RearR = 3,
	None = 255
};

UENUM()
enum class ECampusVehiclePart : uint8
{
	None,
	Engine,
	Body,
	Roof,
	Glass,
	Wheel
};

UENUM()
enum class ECampusVehiclePhase : uint8
{
	Alive,
	Burning,
	Wreck
};

/** One 20 Hz authoritative snapshot: a FULL state every packet, never a delta the receiver accumulates.
 *  Losing one costs 50 ms of smoothing, not a desync - the only safe posture on a 1-3 % loss relay. */
USTRUCT()
struct FCampusVehicleState
{
	GENERATED_BODY()

	UPROPERTY() FVector_NetQuantize100 Location = FVector::ZeroVector;
	/** degrees * 65536 / 360 */
	UPROPERTY() uint16 Yaw = 0;
	/** degrees * 100: ground conform */
	UPROPERTY() int16 Pitch = 0;
	UPROPERTY() int16 Roll = 0;
	/** cm/s along forward, signed */
	UPROPERTY() int16 Speed = 0;
	/** deg/s * 10 */
	UPROPERTY() int16 YawRate = 0;
	/** 1 handbrake, 2 airborne, 4 engine dead */
	UPROPERTY() uint8 Flags = 0;
};

UCLASS()
class ACampusVehicle : public AActor
{
	GENERATED_BODY()

public:
	ACampusVehicle();

	/** campus.VehiclesEnabled: the whole feature's gate. On by default for this game. */
	static bool VehiclesEnabled();
	/** Every car alive in any world; filter by GetWorld(). Avoids a TActorIterator per shot and per HUD frame. */
	static const TArray<TWeakObjectPtr<ACampusVehicle>>& All();

	// ---- seats -----------------------------------------------------------------
	/** Nearest free seat within UseDistance of these feet (driver first), or ECampusSeat::None. */
	ECampusSeat FindFreeSeat(const FVector& Feet) const;
	bool IsSeatFree(ECampusSeat Seat) const;
	ACampusCharacter* GetOccupant(ECampusSeat Seat) const;
	ACampusCharacter* GetDriver() const { return GetOccupant(ECampusSeat::Driver); }
	/** World transform a seat's capsule sits at (seat socket + that occupant's capsule half-height). */
	FTransform SeatTransform(ECampusSeat Seat, float CapsuleHalfHeight) const;
	/** Seat's firing aperture in the car's local frame. Driver returns a zero cone (cannot fire). */
	void SeatAperture(ECampusSeat Seat, FVector& OutLocalForward, float& OutHalfYawDeg,
		float& OutMinPitchDeg, float& OutMaxPitchDeg) const;
	/** The seat the player would switch to (next free one, round robin), or None. */
	ECampusSeat NextFreeSeat(ECampusSeat From) const;

	// ---- server only -----------------------------------------------------------
	bool ServerTryEnter(ACampusCharacter* Who, ECampusSeat Seat);
	bool ServerTrySwitch(ACampusCharacter* Who, ECampusSeat ToSeat);
	void ServerExit(ACampusCharacter* Who);
	/** Driver intent. Throttle / Steer in [-1,1]; the caller's RPC clamps and rate-limits. */
	void ServerSetDriveInput(const ACampusCharacter* From, float Throttle, float Steer, bool bNewHandbrake);
	/** The driver's own phone feeds its local prediction the same intent it just sent the host. */
	void SetPredictedDriveInput(float Throttle, float Steer, bool bNewHandbrake);

	/** PUBG's Engine 100 % / Body 75 % / Roof 50 % zoning, plus glass at 25 %. The car owns these, not the gun. */
	static float PartMultiplier(ECampusVehiclePart Part);
	/** A bullet landed on a part. Server only. Credits the shooter for the scoreboard and the explosion team rule.
	 *  Amount is already multiplied by PartMultiplier() and the weapon's falloff, like ACampusDoor::ApplyDamage. */
	void ApplyPartDamage(ECampusVehiclePart Part, int32 WheelIndex, float Amount, AController* InstigatorController);
	/** Round start: occupants out, HP + wheels whole, phase Alive, back at HomeTransform, history cleared. */
	void ResetForRound();

	// ---- lag compensation ------------------------------------------------------
	struct FVehicleFrame
	{
		double Time = 0.0;
		FVector Location = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
	};
	/** Recorded transform at Time (lerp + slerp between frames). Mirrors ACampusCharacter::HitboxAt. */
	FVehicleFrame FrameAt(double Time) const;

	/** Segment vs this car's oriented boxes AT the given rewound frame. True on a hit.
	 *  bOutBlocks is false for Glass (the bullet carries on to the occupant behind it). */
	bool RewoundTrace(const FVector& Start, const FVector& Dir, float MaxDistance, double When,
		float& OutDistance, ECampusVehiclePart& OutPart, int32& OutWheelIndex, bool& bOutBlocks) const;

	ECampusVehiclePhase GetPhase() const { return Phase; }
	float GetSpeed() const { return Speed; }
	float GetHealth() const { return Health; }
	float GetMaxHealth() const;
	int32 GetWheelHealth(int32 Index) const;
	int32 GetPoppedWheelCount() const;
	/** Seconds left of the bail-out window, 0 when not burning (HUD: flashing BAIL OUT). */
	float GetBurnSecondsLeft() const;

	/** How far behind its newest snapshot a remote client draws this car. Feeds the shooter's rewind. */
	static constexpr double PlayoutDelay = 0.10;
	static constexpr float UseDistance = 220.f;   // cm, the ENTER button appears
	static constexpr float AdmitDistance = 310.f; // cm, server grace (1.4x) for relay latency

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// ---- components. One box for the sweep; the art never collides. ----------
	UPROPERTY(VisibleAnywhere, Category="Vehicle")
	TObjectPtr<UBoxComponent> Hull;

	UPROPERTY(VisibleAnywhere, Category="Vehicle")
	TObjectPtr<UStaticMeshComponent> Body;

	/** SM_SportsCar_Glass: art only. The glass that matters to a bullet is the maths box in RewoundTrace. */
	UPROPERTY(VisibleAnywhere, Category="Vehicle")
	TObjectPtr<UStaticMeshComponent> Glass;

	/** Four instances in one component: 1 extra draw call per car (Section 9's render budget). */
	UPROPERTY(VisibleAnywhere, Category="Vehicle")
	TObjectPtr<UInstancedStaticMeshComponent> Wheels;

	UPROPERTY(VisibleAnywhere, Category="Vehicle")
	TObjectPtr<USceneComponent> Seats[4];

	// ---- replicated ----------------------------------------------------------
	UPROPERTY(ReplicatedUsing=OnRep_State)
	FCampusVehicleState Net;

	UPROPERTY(Replicated)
	float Health = 600.f;

	UPROPERTY(Replicated)
	uint8 WheelHealth[4];

	UPROPERTY(ReplicatedUsing=OnRep_Phase)
	ECampusVehiclePhase Phase = ECampusVehiclePhase::Alive;

	UPROPERTY(Replicated)
	TObjectPtr<ACampusCharacter> Occupants[4];

	// ---- simulation (authority, and the driver's own client) ------------------
	float Speed = 0.f;
	float YawRate = 0.f;
	float LateralSpeed = 0.f;
	float VerticalSpeed = 0.f;
	float InThrottle = 0.f;
	float InSteer = 0.f;
	bool bInHandbrake = false;
	bool bAirborne = false;
	/** Ground conform target, degrees; the drawn pitch / roll eases toward it. */
	float GroundPitch = 0.f;
	float GroundRoll = 0.f;
	float WheelSpin = 0.f;
	double LastInputTime = 0.0;
	FTransform HomeTransform;
	float BurnEndTime = 0.f;
	TWeakObjectPtr<AController> LastDamagedBy;
	TArray<FVehicleFrame> History;
	TMap<TWeakObjectPtr<ACampusCharacter>, double> RoadkillCooldown;

	/** The integrator. Identical code on the server and on the driver's client. */
	void SimulateStep(float Dt);
	/** 4 wheel traces: ground height, pitch / roll conform, airborne detection. */
	void ProbeGround(float Dt);
	/** SetActorLocationAndRotation(sweep) plus wall-impact damage on a blocked move. */
	void SweepMove(float Dt);
	void DoRoadkill(float Dt);
	void RecordFrame();
	void Explode();
	void EnterWreck();
	void PruneOccupants();
	void PublishState();
	void ApplyWreckLook();
	void UpdateWheelArt(float Dt);
	/** Fit the imported art to the 460 x 190 x 150 hull from its own bounds (no magic offsets). */
	void FitArt();
	/** Top speed after wheel penalties. */
	float MaxSpeedNow() const;
	float SteerRateNow() const;
	float GripDecayNow() const;
	bool IsEngineDead() const { return Phase != ECampusVehiclePhase::Alive; }
	/** Seat index of a character, or INDEX_NONE. */
	int32 SeatIndexOf(const ACampusCharacter* Who) const;
	/** Server: attach / detach and hand movement over. */
	void SeatOccupant(ACampusCharacter* Who, ECampusSeat Seat);
	/** A clear spot beside the seat's door to step out at, or false if every side is blocked. */
	bool FindExitSpot(const ACampusCharacter* Who, ECampusSeat Seat, FVector& OutFeet) const;
#if ENABLE_DRAW_DEBUG
	void DrawDebugVehicle(double When) const;
#endif

	// ---- client smoothing ----------------------------------------------------
	/** Playout ring of the last few snapshots, with the local time each arrived. */
	struct FPlayoutSample
	{
		FCampusVehicleState State;
		double Time = 0.0;
	};
	TArray<FPlayoutSample> Buffer;
	/** Local playout clock: runs at real time, resynced when it drifts out of the window. */
	double PlayoutTime = 0.0;
	FVector SmoothPosOffset = FVector::ZeroVector;
	FQuat SmoothRotOffset = FQuat::Identity;
	float SmoothTau = 0.08f;
	bool bHasPredicted = false;

	void TickRemoteInterp(float Dt);
	void TickDriverPrediction(float Dt);
	/** True on the machine whose player is driving this car and is not the server. */
	bool IsPredictingDriver() const;

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void OnRep_Phase();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WreckMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BodyMaterial;

	bool bWreckLookApplied = false;
};
