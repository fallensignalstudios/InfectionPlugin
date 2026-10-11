#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "EclipseSpreadComponent.generated.h"

class UEclipseInfectionMap;
class UMaterialInstanceDynamic;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEclipseSpreadEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEclipseSpotEvent, FVector, Location, float, Radius);

// An outbreak in progress: a sphere revealing the corruption, growing from
// its centre (in the owning actor's space) at Speed up to MaxRadius.
USTRUCT(BlueprintType)
struct INFECTIONRUNTIME_API FEclipseOutbreak
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Eclipse")
	FVector Centre = FVector::ZeroVector;

	// World centimetres revealed so far. Each machine advances its own, so it
	// is not replicated; a client that learns of an outbreak late catches up
	// from StartTime.
	UPROPERTY(BlueprintReadOnly, NotReplicated, SaveGame, Category = "Eclipse")
	float Radius = 0.0f;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Eclipse")
	float MaxRadius = 0.0f;

	// Centimetres a second.
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Eclipse")
	float Speed = 0.0f;

	// Server world time it started, in the game state's clock.
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Eclipse")
	double StartTime = 0.0;

	// Whether this machine has placed its front yet (see Radius).
	UPROPERTY(NotReplicated, Transient)
	bool bLocallyStarted = false;
};

// Everything a spread changes at runtime, for a save game: GetState() before
// saving, RestoreState() after loading the level.
USTRUCT(BlueprintType)
struct INFECTIONRUNTIME_API FEclipseSpreadState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Eclipse")
	float Growth = 0.0f;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Eclipse")
	float PlayRate = 0.0f;

	// Actor-space centre, actor-space radius in W.
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Eclipse")
	TArray<FVector4> Burns;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Eclipse")
	TArray<FEclipseOutbreak> Outbreaks;
};

// Plays a grown corruption during the game. Every mesh on this actor (and on
// actors attached to it) that uses an Eclipse material follows Growth: 0
// shows nothing, 1 the corruption as grown, FullGrowth also the veins that
// ran ahead (the growth panel sets it from the vein reach). Burn() cuts it
// back locally with glowing edges; StartOutbreak() reveals it outward from a
// point, wherever it was grown; GetInfectionAt() tells gameplay how infected
// a spot is right now. The Eclipse Infection subsystem does the same for a
// whole level without knowing which actor is hit.
//
// In multiplayer it replicates (when its actor does): call Burn,
// StartOutbreak and the playback functions on the server and every client
// follows, including clients that join later.
//
// The Eclipse Growth panel adds one of these to every host it grows on.
UCLASS(ClassGroup = (Eclipse), meta = (BlueprintSpawnableComponent, DisplayName = "Eclipse Spread"))
class INFECTIONRUNTIME_API UEclipseSpreadComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Burns are permanent scars; past this many, a new one merges into the
	// nearest. Outbreaks past their limit are refused.
	static constexpr int32 MaxBurns = 64;
	static constexpr int32 MaxOutbreaks = 64;

	UEclipseSpreadComponent();

	// Where the corruption lies, for GetInfectionAt. Written by the growth panel.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse")
	TObjectPtr<UEclipseInfectionMap> InfectionMap;

	// How far the growth has played; see the class comment.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Growth, Category = "Eclipse", meta = (ClampMin = "0"))
	float Growth = 1.5f;

	// Growth at which the spread counts as finished.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0.1"))
	float FullGrowth = 1.5f;

	// Seconds a spread takes from nothing to FullGrowth at play rate 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0.01", Units = "s"))
	float SpreadDuration = 30.0f;

	// Start from nothing and spread as soon as play begins.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse")
	bool bSpreadOnBeginPlay = false;

	// Width of the glowing edge around a burn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "1", Units = "cm"))
	float BurnSoftness = 40.0f;

	// Width of an outbreak's advancing edge.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Outbreaks", meta = (ClampMin = "1", Units = "cm"))
	float OutbreakSoftness = 30.0f;

	// How ragged an outbreak's front is: tissue that arrived later in the
	// grown spread lags up to this far behind it, so the front follows the
	// corruption's own veins and fingers instead of a clean circle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Outbreaks", meta = (ClampMin = "0", Units = "cm"))
	float OutbreakRoughness = 60.0f;

	// The spread reached FullGrowth.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse")
	FEclipseSpreadEvent OnSpreadFinished;

	// A recession reached nothing.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse")
	FEclipseSpreadEvent OnFullyReceded;

	// A burn landed (after merging, its world centre and radius).
	UPROPERTY(BlueprintAssignable, Category = "Eclipse")
	FEclipseSpotEvent OnBurned;

	// An outbreak began, and one reached its full radius.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Outbreaks")
	FEclipseSpotEvent OnOutbreakStarted;

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Outbreaks")
	FEclipseSpotEvent OnOutbreakFinished;

	// Play the spread forward; from nothing unless bFromCurrent.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void StartSpread(bool bFromCurrent = false);

	// Play it backward until nothing is left.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void Recede();

	// 1 spreads, -1 recedes, 0 holds; any other speed scales SpreadDuration.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void SetPlayRate(float Rate);

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	float GetPlayRate() const { return PlayRate; }

	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void SetGrowth(float NewGrowth);

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	float GetGrowth() const { return Growth; }

	// Burn the corruption away within Radius of a point, leaving a permanent
	// scar. Burns stay with the actor if it moves.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void Burn(FVector WorldLocation, float Radius);

	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void ClearBurns();

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	int32 GetBurnCount() const { return Burns.Num(); }

	// Reveal the grown corruption outward from a point, the front advancing
	// at Speed until it reaches MaxRadius, where it stays. Only what was
	// grown there appears: grow the area first, and start with Growth 0 to
	// have outbreaks be the only way it shows.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Outbreaks")
	void StartOutbreak(FVector WorldLocation, float MaxRadius = 300.0f, float Speed = 60.0f);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Outbreaks")
	void ClearOutbreaks();

	UFUNCTION(BlueprintPure, Category = "Eclipse|Outbreaks")
	int32 GetOutbreakCount() const { return Outbreaks.Num(); }

	// Everything changed at runtime (growth, playback, burns, outbreaks), to
	// keep in a save game.
	UFUNCTION(BlueprintPure, Category = "Eclipse|Saving")
	FEclipseSpreadState GetState() const;

	// Put back a state from GetState(), carrying on from where it was.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Saving")
	void RestoreState(const FEclipseSpreadState& State);

	// Where the corruption was grown, in world space (conservative). Empty
	// without an infection map.
	FBox GetWorldBounds() const;

	// The strongest infection within SearchRadius of a world point that has
	// arrived (by Growth or an outbreak) and not been burned: 0 clean, 1
	// fully overtaken.
	UFUNCTION(BlueprintPure, Category = "Eclipse")
	float GetInfectionAt(FVector WorldLocation, float SearchRadius = 30.0f) const;

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	bool IsInfectedAt(FVector WorldLocation, float SearchRadius = 30.0f, float Threshold = 0.5f) const
	{
		return GetInfectionAt(WorldLocation, SearchRadius) >= Threshold;
	}

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	UFUNCTION()
	void OnRep_Growth();
	UFUNCTION()
	void OnRep_PlayRate();
	UFUNCTION()
	void OnRep_Events();

	// The game state's server clock, or this world's when there is none.
	double ServerTime() const;
	// Places the front of outbreaks this machine has not started yet, from their start time.
	void CatchUpOutbreaks();

	// Dynamic instances of every Eclipse material slot under this actor,
	// stopping at attached actors that have a spread component of their own.
	void CollectMaterials();
	void CollectFrom(AActor* Actor);
	void Apply();
	// Writes burns and outbreaks, relative to EventOrigin, into the texture
	// the materials loop over.
	void UpdateEvents(const FVector& EventOrigin);
	bool NeedsTick() const;
	// How revealed a world point of arrival `Arrival` is by the outbreaks, 0..1, as the material computes it.
	float OutbreakReveal(const FVector& WorldPoint, float Arrival) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EventTexture;

	UPROPERTY(ReplicatedUsing = OnRep_Events)
	TArray<FVector4> Burns;  // actor-space centre, actor-space radius in W

	UPROPERTY(ReplicatedUsing = OnRep_Events)
	TArray<FEclipseOutbreak> Outbreaks;

	UPROPERTY(ReplicatedUsing = OnRep_PlayRate)
	float PlayRate = 0.0f;
};
