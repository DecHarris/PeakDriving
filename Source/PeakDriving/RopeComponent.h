
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "RopeComponent.generated.h"

USTRUCT()
struct FRopeParticle
{
    GENERATED_BODY()

    FVector Position = FVector::ZeroVector;
    FVector PreviousPosition = FVector::ZeroVector;

    // Debug information
    FVector CollisionNormal = FVector::ZeroVector;
    FVector ContactPoint = FVector::ZeroVector;

    // Local-space wrapping information
    FVector ContactLocalPoint = FVector::ZeroVector;
    FVector ContactLocalNormal = FVector::ZeroVector;

    TWeakObjectPtr<UPrimitiveComponent> ContactComponent;

    bool bIsPinned = false;
    bool bHasContact = false;
    bool bIsWrappingNode = false;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class PEAKDRIVING_API URopeComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    URopeComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope")
    float RopeLength = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope")
    float ParticleSpacing = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope")
    float ParticleRadius = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope")
    int32 SolverIterations = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope")
    float Damping = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope")
    float GravityScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope")
    bool bEnableCollision = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Debug")
    bool bDrawDebugRope = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Debug")
    bool bDrawDebugParticles = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Debug")
    float DebugParticleSize = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Collision")
    float ContactReleaseDistance = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Endpoints")
    AActor* StartEndpoint = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Endpoints")
    AActor* EndEndpoint = nullptr;

    UFUNCTION(BlueprintCallable, Category = "Rope")
    void StartRope(FVector Start, FVector End);

    UFUNCTION(BlueprintCallable, Category = "Rope")
    void StopRope();

    UFUNCTION(BlueprintCallable, Category = "Rope")
    void SetRopeLength(float NewLength);

    void DrawDebugRope();

    const TArray<FRopeParticle>& GetParticles() const;

protected:

    virtual void BeginPlay() override;

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction
    ) override;

private:

    TArray<FRopeParticle> Particles;

    FVector StartLocation;
    FVector EndLocation;

    bool bIsSimulating = false;
    float Accumulator = 0.0f;

    void Simulate(float DeltaTime);
    void ApplyVerletIntegration(float DeltaTime);
    void SolveConstraints();
    void ResolveCollisions();
    void UpdateEndpoints();
    void UpdateWrappingNodes();
};
