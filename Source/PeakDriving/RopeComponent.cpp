
// Fill out your copyright notice in the Description page of Project Settings.

#include "RopeComponent.h"

#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionShape.h"
#include "DrawDebugHelpers.h"

URopeComponent::URopeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void URopeComponent::BeginPlay()
{
    Super::BeginPlay();
}

void URopeComponent::StartRope(FVector Start, FVector End)
{
    StartLocation = Start;
    EndLocation = End;

    Particles.Empty();

    const int32 SegmentCount = FMath::Max(
        1,
        FMath::CeilToInt(RopeLength / ParticleSpacing)
    );

    for (int32 i = 0; i <= SegmentCount; i++)
    {
        const float Alpha = static_cast<float>(i) / SegmentCount;

        FRopeParticle Particle;

        Particle.Position = FMath::Lerp(
            Start,
            End,
            Alpha
        );

        Particle.PreviousPosition = Particle.Position;

        Particle.bIsPinned = (i == 0 || i == SegmentCount);

        Particles.Add(Particle);
    }

    bIsSimulating = true;
}

void URopeComponent::StopRope()
{
    bIsSimulating = false;
    Particles.Empty();
}

void URopeComponent::SetRopeLength(float NewLength)
{
    RopeLength = FMath::Max(10.0f, NewLength);
}

const TArray<FRopeParticle>& URopeComponent::GetParticles() const
{
    return Particles;
}

void URopeComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction
)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsSimulating)
        return;

    Simulate(DeltaTime);

    DrawDebugRope();
}

void URopeComponent::ApplyVerletIntegration(float DeltaTime)
{
    const FVector Gravity = FVector(
        0.0f,
        0.0f,
        GetWorld()->GetGravityZ()
    ) * GravityScale;

    const float DeltaTimeSquared = DeltaTime * DeltaTime;

    for (int32 i = 1; i < Particles.Num() - 1; i++)
    {
        FRopeParticle& Particle = Particles[i];

        if (Particle.bIsPinned)
            continue;

        const FVector CurrentPosition = Particle.Position;

        const FVector Velocity =
            (Particle.Position - Particle.PreviousPosition)
            * (1.0f - Damping);

        Particle.Position += Velocity
            + Gravity * DeltaTimeSquared;

        Particle.PreviousPosition = CurrentPosition;
    }
}

void URopeComponent::SolveConstraints()
{
    if (Particles.Num() < 2)
        return;

    const float SegmentLength =
        RopeLength / static_cast<float>(Particles.Num() - 1);

    for (int32 Iteration = 0; Iteration < SolverIterations; Iteration++)
    {
        UpdateEndpoints();

        for (int32 i = 0; i < Particles.Num() - 1; i++)
        {
            FRopeParticle& A = Particles[i];
            FRopeParticle& B = Particles[i + 1];

            const FVector Difference = B.Position - A.Position;

            const float Distance = Difference.Length();

            if (Distance <= KINDA_SMALL_NUMBER)
                continue;

            const float Error = Distance - SegmentLength;

            const FVector Correction =
                (Difference / Distance) * Error;

            const bool bAFixed = A.bIsPinned;
            const bool bBFixed = B.bIsPinned;

            if (!bAFixed && !bBFixed)
            {
                A.Position += Correction * 0.5f;
                B.Position -= Correction * 0.5f;
            }
            else if (bAFixed)
            {
                B.Position -= Correction;
            }
            else if (bBFixed)
            {
                A.Position += Correction;
            }
        }
    }
}

void URopeComponent::ResolveCollisions()
{
    if (!bEnableCollision)
        return;

    UWorld* World = GetWorld();

    if (!World)
        return;

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(GetOwner());

    for (int32 i = 1; i < Particles.Num() - 1; i++)
    {
        FRopeParticle& Particle = Particles[i];

        const FVector Previous = Particle.PreviousPosition;
        const FVector Current = Particle.Position;

        FHitResult Hit;

        const bool bHit = World->SweepSingleByChannel(
            Hit,
            Previous,
            Current,
            FQuat::Identity,
            ECC_Visibility,
            FCollisionShape::MakeSphere(ParticleRadius),
            QueryParams
        );

        // New collision detected
        if (bHit)
        {
            UPrimitiveComponent* HitComponent = Hit.GetComponent();

            const FVector Normal = Hit.ImpactNormal.GetSafeNormal();

            // Hit.Location is the swept sphere's centre.
            Particle.Position = Hit.Location + Normal * 0.1f;

            Particle.CollisionNormal = Normal;
            Particle.ContactPoint = Hit.ImpactPoint;

            Particle.bHasContact = true;
            Particle.ContactComponent = HitComponent;

            // Store contact in the component's local space.
            if (HitComponent)
            {
                const FTransform Transform =
                    HitComponent->GetComponentTransform();

                Particle.ContactLocalPoint =
                    Transform.InverseTransformPosition(Hit.ImpactPoint);

                Particle.ContactLocalNormal =
                    Transform.InverseTransformVectorNoScale(Normal)
                    .GetSafeNormal();
            }
        }
        // No new collision: maintain previous contact
        else if (Particle.bHasContact)
        {
            UPrimitiveComponent* ContactComponent =
                Particle.ContactComponent.Get();

            if (!ContactComponent)
            {
                Particle.bHasContact = false;
                Particle.CollisionNormal = FVector::ZeroVector;
                Particle.ContactComponent.Reset();
                continue;
            }

            FVector ClosestPoint;

            const float Distance =
                ContactComponent->GetClosestPointOnCollision(
                    Particle.Position,
                    ClosestPoint
                );

            // No valid collision geometry.
            if (Distance < 0.0f)
            {
                Particle.bHasContact = false;
                Particle.CollisionNormal = FVector::ZeroVector;
                Particle.ContactComponent.Reset();
                continue;
            }

            // Allow the particle to release the surface.
            if (Distance > ParticleRadius + ContactReleaseDistance)
            {
                Particle.bHasContact = false;
                Particle.CollisionNormal = FVector::ZeroVector;
                Particle.ContactComponent.Reset();
                continue;
            }

            // Calculate the current surface normal.
            FVector Normal =
                (Particle.Position - ClosestPoint).GetSafeNormal();

            if (Normal.IsNearlyZero())
            {
                Normal = Particle.CollisionNormal;
            }

            // Move the particle outside the actual collision surface.
            Particle.Position =
                ClosestPoint + Normal * (ParticleRadius + 0.1f);

            Particle.ContactPoint = ClosestPoint;
            Particle.CollisionNormal = Normal;

            // Preserve movement along the surface.
            FVector Velocity =
                Particle.Position - Particle.PreviousPosition;

            const float NormalVelocity =
                FVector::DotProduct(Velocity, Normal);

            if (NormalVelocity < 0.0f)
            {
                Velocity -= Normal * NormalVelocity;
            }

            Particle.PreviousPosition =
                Particle.Position - Velocity * 0.8f;
        }
        else
        {
            Particle.CollisionNormal = FVector::ZeroVector;
        }
    }
}

void URopeComponent::UpdateEndpoints()
{
    if (Particles.Num() < 2)
        return;

    if (StartEndpoint)
        StartLocation = StartEndpoint->GetActorLocation();

    if (EndEndpoint)
        EndLocation = EndEndpoint->GetActorLocation();

    Particles[0].Position = StartLocation;
    Particles[0].PreviousPosition = StartLocation;

    Particles.Last().Position = EndLocation;
    Particles.Last().PreviousPosition = EndLocation;
}

void URopeComponent::Simulate(float DeltaTime)
{
    const float FixedTimeStep = 1.0f / 120.0f;

    DeltaTime = FMath::Min(DeltaTime, 0.05f);

    Accumulator += DeltaTime;

    int32 Steps = 0;
    const int32 MaxSteps = 8;

    while (Accumulator >= FixedTimeStep && Steps < MaxSteps)
    {
        UpdateEndpoints();

        ApplyVerletIntegration(FixedTimeStep);

        // Alternate between rope constraints and collision resolution.
        for (int32 Iteration = 0; Iteration < SolverIterations; Iteration++)
        {
            SolveConstraints();
            ResolveCollisions();
        }

        UpdateEndpoints();

        Accumulator -= FixedTimeStep;
        Steps++;
    }

    if (Steps == MaxSteps)
    {
        Accumulator = 0.0f;
    }
}

void URopeComponent::DrawDebugRope()
{
    if (!GetWorld() || Particles.Num() < 2)
        return;

    for (int32 i = 0; i < Particles.Num(); i++)
    {
        const FRopeParticle& Particle = Particles[i];

        if (bDrawDebugParticles)
        {
            const FColor ParticleColor =
                Particle.bIsPinned ? FColor::Green : FColor::Yellow;

            DrawDebugSphere(
                GetWorld(),
                Particle.Position,
                DebugParticleSize,
                8,
                ParticleColor,
                false,
                0.0f
            );
        }

        if (bDrawDebugRope && i < Particles.Num() - 1)
        {
            const FVector NextPosition = Particles[i + 1].Position;

            DrawDebugLine(
                GetWorld(),
                Particle.Position,
                NextPosition,
                FColor::Cyan,
                false,
                0.0f,
                0,
                2.0f
            );
        }

        if (!Particle.CollisionNormal.IsNearlyZero())
        {
            DrawDebugLine(
                GetWorld(),
                Particle.Position,
                Particle.Position + Particle.CollisionNormal * 25.0f,
                FColor::Red,
                false,
                0.0f,
                0,
                2.0f
            );
        }
    }
}
