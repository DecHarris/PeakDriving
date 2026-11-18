// Fill out your copyright notice in the Description page of Project Settings.


#pragma once

#include "CoreMinimal.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "CustomPhysicsConstraintComponent.generated.h"

UCLASS(
    ClassGroup = Physics,
    meta = (BlueprintSpawnableComponent),
    HideCategories = (Activation, "Components|Activation", Physics, Mobility),
    ShowCategories = ("Physics|Components|PhysicsConstraint")
)
class PEAKDRIVING_API UCustomPhysicsConstraintComponent
    : public UPhysicsConstraintComponent
{
    GENERATED_BODY()

public:

    UFUNCTION(BlueprintCallable, Category = "Physics|Components|PhysicsConstraint")
    void SetAngularRotationOffset(FRotator NewOffset);

    UFUNCTION(BlueprintPure, Category = "Physics|Components|PhysicsConstraint")
    FRotator GetAngularRotationOffset() const;
};
