
// Fill out your copyright notice in the Description page of Project Settings.

#include "RopeComponent.h"

#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionShape.h"
#include "Components/SceneComponent.h"
#include "CableComponent.h"
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
    StopRope();

    UE_LOG(LogTemp, Warning,
        TEXT("ROPEDEBUG Cable pointer after creation: %s"),
        CableComponent ? TEXT("VALID") : TEXT("NULL"));

    AActor* Owner = GetOwner();

    if (!Owner)
        return;

    // Find the component that the beginning of the rope follows.
    USceneComponent* StartComponent = nullptr;

    if (StartEndpoint)
    {
        StartComponent = StartEndpoint->GetRootComponent();
    }
    else
    {
        StartComponent = Owner->GetRootComponent();
    }

    if (!StartComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("ROPEDEBUG Rope: No valid start component."));
        return;
    }

    // Create the cable.
    CableComponent = NewObject<UCableComponent>(
        Owner,
        TEXT("RuntimeRopeCable")
    );

    if (!CableComponent)
        return;

    // Attach the cable component to the start component.
    CableComponent->SetupAttachment(StartComponent);

    // Configure the cable before registering it.
    CableComponent->CableLength = RopeLength;

    CableComponent->NumSegments = FMath::Clamp(
        FMath::CeilToInt(RopeLength / ParticleSpacing),
        8,
        64
    );

    CableComponent->SolverIterations = 8;
    CableComponent->CableGravityScale = GravityScale;
    CableComponent->CableWidth = 3.0f;
    CableComponent->bUseSubstepping = true;
    CableComponent->SubstepTime = RopeSubstepTime;

    CableComponent->bEnableCollision = false;

    // Fix the beginning of the cable to its parent component.
    CableComponent->bAttachStart = true;

    // Initially leave the end unattached.
    CableComponent->bAttachEnd = false;

    // Position the cable at the supplied world position.
    CableComponent->SetWorldLocation(Start);

    // Register the component so it can simulate and render.
    CableComponent->RegisterComponent();

    // Attach the other end if an endpoint actor exists.


    




    if (EndEndpoint)
    {
        USceneComponent* EndComponent =
            EndEndpoint->GetRootComponent();

        if (EndComponent)
        {
            CableComponent->SetAttachEndToComponent(
                EndComponent,
                NAME_None
            );

            // EndLocation is relative to the end component.
            CableComponent->EndLocation =
                EndComponent->GetComponentTransform()
                .InverseTransformPosition(End);

            CableComponent->bAttachEnd = true;
        }
    }
    else
    {
        // Keep the end free at the supplied world position.
        CableComponent->EndLocation =
            CableComponent->GetComponentTransform()
            .InverseTransformPosition(End);
    }

    EndLocation = End;

    if (EndEndpoint && EndEndpoint->GetRootComponent())
    {
        EndEndpointLocalLocation =
            EndEndpoint->GetRootComponent()
            ->GetComponentTransform()
            .InverseTransformPosition(End);
    }
    else
    {
        EndEndpointLocalLocation =
            CableComponent->GetComponentTransform()
            .InverseTransformPosition(End);
    }

    // Disable the old Verlet simulation.
    bIsSimulating = false;
    Particles.Empty();

    UE_LOG(LogTemp, Log, TEXT("ROPEDEBUG Rope: Cable created. Length: %f"), RopeLength);
}


void URopeComponent::StopRope()
{
    bIsSimulating = false;
    Particles.Empty();
    RoutePoints.Empty();

    // Destroy all additional cable sections.
    for (UCableComponent* Section : CableSections)
    {
        if (Section)
        {
            Section->DestroyComponent();
        }
    }

    CableSections.Empty();

    // Destroy all route anchors.
    for (USceneComponent* Anchor : RouteAnchors)
    {
        if (Anchor)
        {
            Anchor->DestroyComponent();
        }
    }

    RouteAnchors.Empty();

    // Clean up the old single-route components.
    if (RouteCableComponent)
    {
        RouteCableComponent->DestroyComponent();
        RouteCableComponent = nullptr;
    }

    if (RouteAnchor)
    {
        RouteAnchor->DestroyComponent();
        RouteAnchor = nullptr;
    }

    // Destroy the original cable.
    if (CableComponent)
    {
        CableComponent->DestroyComponent();
        CableComponent = nullptr;
    }
}


void URopeComponent::SetRopeLength(float NewLength)
{
    RopeLength = FMath::Max(1.0f, NewLength);

    if (!CableComponent)
        return;

    CableComponent->CableLength = RopeLength;

    UE_LOG(LogTemp, Warning,
        TEXT("ROPEDEBUG Length: %f | Segments: %d | Attach Start: %s | Attach End: %s"),
        CableComponent->CableLength,
        CableComponent->NumSegments,
        CableComponent->bAttachStart ? TEXT("TRUE") : TEXT("FALSE"),
        CableComponent->bAttachEnd ? TEXT("TRUE") : TEXT("FALSE"));
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
    Super::TickComponent(
        DeltaTime,
        TickType,
        ThisTickFunction
    );

    if (!CableComponent)
        return;

    UpdateObstacleDetection();

    UpdateCableRoute();
}


void URopeComponent::UpdateObstacleDetection()
{
    UWorld* World = GetWorld();

    if (!World || !CableComponent)
        return;

    // Get the current start position.
    const FVector TraceStart = CableComponent->GetComponentLocation();

    // Get the actual destination position.
    FVector TraceEnd;

    if (EndEndpoint && EndEndpoint->GetRootComponent())
    {
        TraceEnd = EndEndpoint->GetRootComponent()
            ->GetComponentTransform()
            .TransformPosition(EndEndpointLocalLocation);
    }
    else
    {
        TraceEnd = CableComponent->GetComponentTransform()
            .TransformPosition(EndEndpointLocalLocation);
    }

    // Ignore the rope owner and endpoint actors.
    FCollisionQueryParams QueryParams(
        SCENE_QUERY_STAT(RopeObstacleDetection),
        false
    );

    QueryParams.AddIgnoredActor(GetOwner());

    if (StartEndpoint)
        QueryParams.AddIgnoredActor(StartEndpoint);

    if (EndEndpoint)
        QueryParams.AddIgnoredActor(EndEndpoint);

    // Find the first obstructed section.
    for (int32 SectionIndex = 0;
        SectionIndex <= RoutePoints.Num();
        SectionIndex++)
    {
        FVector SectionStart = TraceStart;
        FVector SectionEnd = TraceEnd;

        // Start at the previous route point.
        if (SectionIndex > 0)
        {
            const FRopeRoutePoint& PreviousPoint =
                RoutePoints[SectionIndex - 1];

            if (!PreviousPoint.ObstacleComponent.IsValid())
                continue;

            SectionStart =
                PreviousPoint.ObstacleComponent->GetComponentTransform()
                .TransformPosition(PreviousPoint.LocalPosition);
        }

        // End at the next route point.
        if (SectionIndex < RoutePoints.Num())
        {
            const FRopeRoutePoint& NextPoint =
                RoutePoints[SectionIndex];

            if (!NextPoint.ObstacleComponent.IsValid())
                continue;

            SectionEnd =
                NextPoint.ObstacleComponent->GetComponentTransform()
                .TransformPosition(NextPoint.LocalPosition);
        }

        FHitResult Hit;
        FCollisionQueryParams SectionQueryParams = QueryParams;

        // Ignore the obstacle belonging to the previous route point.
        if (SectionIndex > 0)
        {
            if (UPrimitiveComponent* PreviousObstacle =
                RoutePoints[SectionIndex - 1].ObstacleComponent.Get())
            {
                SectionQueryParams.AddIgnoredComponent(PreviousObstacle);
            }
        }

        // Ignore the obstacle belonging to the next route point.
        if (SectionIndex < RoutePoints.Num())
        {
            if (UPrimitiveComponent* NextObstacle =
                RoutePoints[SectionIndex].ObstacleComponent.Get())
            {
                SectionQueryParams.AddIgnoredComponent(NextObstacle);
            }
        }
        const bool bHitObstacle = GetWorld()->SweepSingleByChannel(
            Hit,
            SectionStart,
            SectionEnd,
            FQuat::Identity,
            ObstacleTraceChannel,
            FCollisionShape::MakeSphere(ObstacleTraceRadius),
            SectionQueryParams
        );

        // Draw the current section trace.
        if (bDrawDebugObstacleTrace)
        {
            DrawDebugLine(
                World,
                SectionStart,
                SectionEnd,
                bHitObstacle ? FColor::Red : FColor::Green,
                false,
                0.0f,
                0,
                2.0f
            );
        }

        // No obstacle found in this section.
        if (!bHitObstacle || !Hit.GetComponent())
            continue;

        UPrimitiveComponent* HitComponent = Hit.GetComponent();

        // Avoid adding another point to the same obstacle
        // when it is already the end of this section.
        if (SectionIndex < RoutePoints.Num() &&
            RoutePoints[SectionIndex].ObstacleComponent.Get() == HitComponent)
        {
            continue;
        }

        // Create a new route point.
        FRopeRoutePoint NewPoint;

        NewPoint.ObstacleComponent = HitComponent;

        const FVector Normal = Hit.ImpactNormal.GetSafeNormal();

        const FVector WorldPosition =
            Hit.ImpactPoint + Normal * (ObstacleTraceRadius + 5.0f);

        const FTransform Transform =
            HitComponent->GetComponentTransform();

        NewPoint.LocalPosition =
            Transform.InverseTransformPosition(WorldPosition);

        NewPoint.LocalNormal =
            Transform.InverseTransformVectorNoScale(Normal)
            .GetSafeNormal();

        // Insert the new point at the section where
        // the obstruction was detected.
        RoutePoints.Insert(NewPoint, SectionIndex);

        UE_LOG(LogTemp, Warning,
            TEXT("ROPE: Inserted route point %d on %s"),
            SectionIndex,
            *HitComponent->GetName());

        // Only insert one point per tick.
        break;
    }

    // Draw all existing route points.
    if (bDrawDebugObstacleTrace)
    {
        for (const FRopeRoutePoint& Point : RoutePoints)
        {
            UPrimitiveComponent* Component =
                Point.ObstacleComponent.Get();

            if (!Component)
                continue;

            const FTransform Transform =
                Component->GetComponentTransform();

            const FVector WorldPosition =
                Transform.TransformPosition(Point.LocalPosition);

            const FVector WorldNormal =
                Transform.TransformVectorNoScale(Point.LocalNormal)
                .GetSafeNormal();

            DrawDebugSphere(
                World,
                WorldPosition,
                10.0f,
                12,
                FColor::Magenta,
                false,
                0.0f
            );

            DrawDebugDirectionalArrow(
                World,
                WorldPosition,
                WorldPosition + WorldNormal * 25.0f,
                8.0f,
                FColor::Blue,
                false,
                0.0f,
                0,
                2.0f
            );
        }
    }
}



void URopeComponent::UpdateCableRoute()
{
    if (!CableComponent)
        return;

    const int32 NumRoutePoints = RoutePoints.Num();

    // --------------------------------------------------
    // VALIDATE ROUTE POINTS
    // --------------------------------------------------

    for (int32 i = RoutePoints.Num() - 1; i >= 0; i--)
    {
        if (!RoutePoints[i].ObstacleComponent.IsValid())
        {
            RoutePoints.RemoveAt(i);
        }
    }

    const int32 ValidRouteCount = RoutePoints.Num();

    // --------------------------------------------------
    // REMOVE OLD SINGLE-ROUTE COMPONENTS
    // --------------------------------------------------

    if (RouteAnchor)
    {
        RouteAnchor->DestroyComponent();
        RouteAnchor = nullptr;
    }

    if (RouteCableComponent)
    {
        RouteCableComponent->DestroyComponent();
        RouteCableComponent = nullptr;
    }

    // --------------------------------------------------
    // ENSURE CORRECT NUMBER OF ANCHORS
    // --------------------------------------------------

    while (RouteAnchors.Num() > ValidRouteCount)
    {
        if (RouteAnchors.Last())
            RouteAnchors.Last()->DestroyComponent();

        RouteAnchors.RemoveAt(RouteAnchors.Num() - 1);
    }

    while (RouteAnchors.Num() < ValidRouteCount)
    {
        RouteAnchors.Add(nullptr);
    }

    // --------------------------------------------------
    // CREATE / UPDATE ROUTE ANCHORS
    // --------------------------------------------------

    for (int32 i = 0; i < ValidRouteCount; i++)
    {
        FRopeRoutePoint& Point = RoutePoints[i];

        UPrimitiveComponent* Obstacle =
            Point.ObstacleComponent.Get();

        if (!Obstacle)
            continue;

        if (!RouteAnchors[i])
        {
            RouteAnchors[i] = NewObject<USceneComponent>(
                GetOwner(),
                NAME_None
            );

            if (!RouteAnchors[i])
                continue;

            RouteAnchors[i]->SetupAttachment(Obstacle);
            RouteAnchors[i]->RegisterComponent();
        }
        else if (RouteAnchors[i]->GetAttachParent() != Obstacle)
        {
            RouteAnchors[i]->AttachToComponent(
                Obstacle,
                FAttachmentTransformRules::KeepWorldTransform
            );
        }

        RouteAnchors[i]->SetRelativeLocation(Point.LocalPosition);
    }

    // --------------------------------------------------
    // CREATE / REMOVE CABLE SECTIONS
    // --------------------------------------------------

    const int32 RequiredExtraCables = ValidRouteCount;

    while (CableSections.Num() > RequiredExtraCables)
    {
        if (CableSections.Last())
            CableSections.Last()->DestroyComponent();

        CableSections.RemoveAt(CableSections.Num() - 1);
    }

    while (CableSections.Num() < RequiredExtraCables)
    {
        CableSections.Add(nullptr);
    }

    // --------------------------------------------------
    // CONFIGURE ORIGINAL CABLE
    // --------------------------------------------------

    if (ValidRouteCount > 0)
    {
        CableComponent->CableLength =
            RopeLength / (ValidRouteCount + 1);

        CableComponent->SetAttachEndToComponent(
            RouteAnchors[0],
            NAME_None
        );

        CableComponent->EndLocation = FVector::ZeroVector;
        CableComponent->bAttachEnd = true;
    }
    else
    {
        CableComponent->CableLength = RopeLength;

        if (EndEndpoint && EndEndpoint->GetRootComponent())
        {
            USceneComponent* EndComponent =
                EndEndpoint->GetRootComponent();

            CableComponent->SetAttachEndToComponent(
                EndComponent,
                NAME_None
            );

            CableComponent->EndLocation =
                EndEndpointLocalLocation;

            CableComponent->bAttachEnd = true;
        }
        else
        {
            CableComponent->SetAttachEndToComponent(nullptr);
            CableComponent->EndLocation =
                EndEndpointLocalLocation;

            CableComponent->bAttachEnd = false;
        }
    }

    // --------------------------------------------------
    // CONFIGURE ADDITIONAL CABLE SECTIONS
    // --------------------------------------------------

    for (int32 i = 0; i < ValidRouteCount; i++)
    {
        UCableComponent* Section = CableSections[i];

        if (!Section)
        {
            Section = NewObject<UCableComponent>(
                GetOwner(),
                NAME_None
            );

            if (!Section)
                continue;

            Section->SetupAttachment(RouteAnchors[i]);

            Section->CableLength =
                RopeLength / (ValidRouteCount + 1);

            Section->NumSegments = FMath::Clamp(
                CableComponent->NumSegments / (ValidRouteCount + 1),
                4,
                32
            );

            Section->SolverIterations = 8;
            Section->CableGravityScale = GravityScale;
            Section->CableWidth = CableComponent->CableWidth;

            Section->bUseSubstepping = true;
            Section->SubstepTime = RopeSubstepTime;

            Section->bEnableCollision = false;

            Section->bAttachStart = true;
            Section->bAttachEnd = false;

            Section->RegisterComponent();

            CableSections[i] = Section;
        }

        // Attach the end of this section to the next anchor,
        // or to the final destination.
        if (i + 1 < ValidRouteCount)
        {
            Section->SetAttachEndToComponent(
                RouteAnchors[i + 1],
                NAME_None
            );

            Section->EndLocation = FVector::ZeroVector;
            Section->bAttachEnd = true;
        }
        else if (EndEndpoint && EndEndpoint->GetRootComponent())
        {
            Section->SetAttachEndToComponent(
                EndEndpoint->GetRootComponent(),
                NAME_None
            );

            Section->EndLocation = EndEndpointLocalLocation;
            Section->bAttachEnd = true;
        }
        else
        {
            Section->SetAttachEndToComponent(nullptr);
            Section->EndLocation = EndEndpointLocalLocation;
            Section->bAttachEnd = false;
        }
    }
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
