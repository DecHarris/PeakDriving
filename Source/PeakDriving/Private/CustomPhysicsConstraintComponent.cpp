// Fill out your copyright notice in the Description page of Project Settings.


#include "CustomPhysicsConstraintComponent.h"

void UCustomPhysicsConstraintComponent::SetAngularRotationOffset(FRotator NewOffset)
{
    ConstraintInstance.AngularRotationOffset = NewOffset;

    // Reinitialise the constraint so the new offset takes effect.
    InitComponentConstraint();
}

FRotator UCustomPhysicsConstraintComponent::GetAngularRotationOffset() const
{
    return ConstraintInstance.AngularRotationOffset;
}
