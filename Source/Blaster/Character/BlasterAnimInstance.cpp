// Fill out your copyright notice in the Description page of Project Settings.


#include "BlasterAnimInstance.h"
#include "BlasterCharacter.h"
#include "Blaster/BlasterComponents/CombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Blaster/Weapon/Weapon.h"
#include "Blaster/BlasterTypes/CombatState.h"
#include "Curves/CurveVector.h"

void UBlasterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	BlasterCharacter = Cast<ABlasterCharacter>(TryGetPawnOwner());
}

void UBlasterAnimInstance::NativeUpdateAnimation(float DeltaTime)
{
	Super::NativeUpdateAnimation(DeltaTime);

	if (BlasterCharacter == nullptr)
	{
		BlasterCharacter = Cast<ABlasterCharacter>(TryGetPawnOwner());
	}
	if (BlasterCharacter == nullptr) return;

	FVector Velocity = BlasterCharacter->GetVelocity();
	Velocity.Z = 0.f;
	Speed = Velocity.Size();

	bIsInAir = BlasterCharacter->GetCharacterMovement()->IsFalling();
	bIsAccelerating = BlasterCharacter->GetCharacterMovement()->GetCurrentAcceleration().Size() > 0.f ? true : false;
	bWeaponEquipped = BlasterCharacter->IsWeaponEquipped();
	EquippedWeapon = BlasterCharacter->GetEquippedWeapon();
	bIsCrouched = BlasterCharacter->bIsCrouched;
	bAiming = BlasterCharacter->IsAiming();
	TurningInPlace = BlasterCharacter->GetTurningInPlace();
	bRotateRootBone = BlasterCharacter->ShouldRotateRootBone();
	bElimmed = BlasterCharacter->IsElimmed();
	bHoldingTheFlag = BlasterCharacter->IsHoldingTheFlag();

	// Offset Yaw for Strafing
	FRotator AimRotation = BlasterCharacter->GetBaseAimRotation();
	FRotator MovementRotation = UKismetMathLibrary::MakeRotFromX(BlasterCharacter->GetVelocity());
	FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(MovementRotation, AimRotation);
	DeltaRotation = FMath::RInterpTo(DeltaRotation, DeltaRot, DeltaTime, 6.f);
	YawOffset = DeltaRotation.Yaw;

	CharacterRotationLastFrame = CharacterRotation;
	CharacterRotation = BlasterCharacter->GetActorRotation();
	const FRotator Delta = UKismetMathLibrary::NormalizedDeltaRotator(CharacterRotation, CharacterRotationLastFrame);
	const float Target = Delta.Yaw / DeltaTime;
	const float Interp = FMath::FInterpTo(Lean, Target, DeltaTime, 6.f);
	Lean = FMath::Clamp(Interp, -90.f, 90.f);

	AO_Yaw = BlasterCharacter->GetAO_Yaw();
	AO_Pitch = BlasterCharacter->GetAO_Pitch();

	if (bWeaponEquipped && EquippedWeapon && EquippedWeapon->GetWeaponMesh() && BlasterCharacter->GetMesh())
	{
		LeftHandTransform = EquippedWeapon->GetWeaponMesh()->GetSocketTransform(FName("LeftHandSocket"), ERelativeTransformSpace::RTS_World);
		FVector OutPosition;
		FRotator OutRotation;
		BlasterCharacter->GetMesh()->TransformToBoneSpace(FName("hand_r"), LeftHandTransform.GetLocation(), FRotator::ZeroRotator, OutPosition, OutRotation);
		LeftHandTransform.SetLocation(OutPosition);
		LeftHandTransform.SetRotation(FQuat(OutRotation));

		if (BlasterCharacter->IsLocallyControlled())
		{
			bLocallyControlled = true;
			FTransform RightHandTransform = EquippedWeapon->GetWeaponMesh()->GetSocketTransform(FName("hand_r"), ERelativeTransformSpace::RTS_World);
			FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(RightHandTransform.GetLocation(), RightHandTransform.GetLocation() + (RightHandTransform.GetLocation() - BlasterCharacter->GetWeaponRotation()));
			RightHandRotation = FMath::RInterpTo(RightHandRotation, LookAtRotation, DeltaTime, 30.f);
		}
	}

	bUseFABRIK = BlasterCharacter->GetCombatState() == ECombatState::ECS_Unoccupied;
	bool bFABRIKOverride = BlasterCharacter->IsLocallyControlled() &&
		BlasterCharacter->GetCombatState() != ECombatState::ECS_ThrowingGrenade &&
		BlasterCharacter->bFinishedSwapping;
	if (bFABRIKOverride)
	{
		bUseFABRIK = !BlasterCharacter->IsLocallyReloading();
	}
	bUseAimOffsets = BlasterCharacter->GetCombatState() == ECombatState::ECS_Unoccupied && !BlasterCharacter->GetDisableGameplay();
	bTransformRightHand = BlasterCharacter->GetCombatState() == ECombatState::ECS_Unoccupied && !BlasterCharacter->GetDisableGameplay();
}

void ABlasterCharacter::AddRecoilRotation(float DeltaTime)
{

	if (Controller && Combat)
	{
		AWeapon* EquippedWeapon = Combat->EquippedWeapon;
		FRotator ControllerRotation = GetController()->GetControlRotation();;
		if ( EquippedWeapon&& Combat->EquippedWeapon->RecoilCurve)
		{
			if (Combat->bFireButtonPressed   &&  GetWorld()->GetTimerManager().IsTimerActive(Combat->RecoilTimerHandle)&& EquippedWeapon->bCanRecoil && GetCombatState() == ECombatState::ECS_Unoccupied)
			{
				if ((GetWorld()->GetTimerManager().GetTimerElapsed(Combat->RecoilTimerHandle) > EquippedWeapon->UseYawRecoil && !EquippedWeapon->bUseY )|| EquippedWeapon->RecoilRecoveryRotation.Pitch >EquippedWeapon->MaxPitchRecoil)
				{
					EquippedWeapon->bUseY = true;
				}
				UE_LOG(LogTemp,Warning,TEXT("bUseY %f"),EquippedWeapon->bUseY)
				float RecoilInterpSpeed = Combat->bAiming ?EquippedWeapon->AimRecoilInterSpeed :EquippedWeapon->RecoilInterSpeed;
				FVector Recoil = EquippedWeapon->RecoilCurve->GetVectorValue(
					GetWorld()->GetTimerManager().GetTimerElapsed(Combat->RecoilTimerHandle));
				EquippedWeapon->RecoilRecoveryRotation = FMath::RInterpTo(EquippedWeapon->RecoilRecoveryRotation,EquippedWeapon->RecoilRecoveryRotation + FRotator(EquippedWeapon->bUseY ? 0.f:Recoil.Z,EquippedWeapon->bUseY? Recoil.Y:0.f,0.f) , DeltaTime,
																 RecoilInterpSpeed);
				UE_LOG(LogTemp,Warning,TEXT("Recovery Rotaion  %s"),*EquippedWeapon->RecoilRecoveryRotation.ToString())
				UE_LOG(LogTemp,Warning,TEXT("ControllerRotation %s"),*ControllerRotation.ToString())
				UE_LOG(LogTemp,Warning,TEXT("RecoilRotation  %s"),*EquippedWeapon->RecoilRotation.ToString())
				UE_LOG(LogTemp,Warning,TEXT("Real ControllerRotation  %s"),*GetController()->GetControlRotation().ToString())
				ControllerRotation.Add(EquippedWeapon->bUseY ? 0.f:Recoil.Z,EquippedWeapon->bUseY? Recoil.Y:0.f,0.f);
				EquippedWeapon->RecoilRotation = FMath::RInterpTo(GetController()->GetControlRotation(), ControllerRotation, DeltaTime,
												  RecoilInterpSpeed);
				UE_LOG(LogTemp,Warning,TEXT("Recovery Rotaion 2nd  %s"),*EquippedWeapon->RecoilRecoveryRotation.ToString())
				UE_LOG(LogTemp,Warning,TEXT("ControllerRotation 2nd  %s"),*ControllerRotation.ToString())
				UE_LOG(LogTemp,Warning,TEXT("RecoilRotation 2nd %s"),*EquippedWeapon->RecoilRotation.ToString())
			
				Controller->SetControlRotation(EquippedWeapon->RecoilRotation);
				UE_LOG(LogTemp,Warning,TEXT("Real ControllerRotation 2nd %s"),*GetController()->GetControlRotation().ToString())
			
				
			}
			else if (EquippedWeapon->bRecoilRecover || Combat->EquippedWeapon->IsEmpty())
			{
				Combat->StopRecoilTimer();
				if (EquippedWeapon->RecoilRecoveryRotation.Yaw > EquippedWeapon->MaxYawRecoil)
				{
					EquippedWeapon->RecoilRecoveryRotation.Yaw = EquippedWeapon->MaxYawRecoil;
				}
				
				if (EquippedWeapon->RecoilRecoveryRotation.Pitch >=0.f && EquippedWeapon->RecoilRecoveryRotation.Yaw >=-10.f && EquippedWeapon->RecoilRecoveryRotation.Yaw <=10.f)
				{
					FRotator OldCOntrollerRotation = GetController()->GetControlRotation();
					EquippedWeapon->RecoilRotation= FMath::RInterpTo(GetController()->GetControlRotation(),  GetController()->GetControlRotation() - EquippedWeapon->RecoilRecoveryRotation, GetWorld()->GetDeltaSeconds(),
													  Combat->EquippedWeapon->RecoilRecoveryInterSpeed);
					EquippedWeapon->RecoilRecoveryRotation.Pitch = FMath::Clamp(
						EquippedWeapon->RecoilRecoveryRotation.Pitch - (OldCOntrollerRotation.Pitch < 0.f
							                         ? FMath::Abs(EquippedWeapon->RecoilRotation.Pitch) - FMath::Abs(
								                         OldCOntrollerRotation.Pitch)
							                         : FMath::Abs(OldCOntrollerRotation.Pitch) - FMath::Abs(
								                         EquippedWeapon->RecoilRotation.Pitch)), 0.f,
						Combat->EquippedWeapon->MaxPitchRecoil);
					
					EquippedWeapon->RecoilRecoveryRotation.Yaw = FMath::Clamp(EquippedWeapon->RecoilRecoveryRotation.Yaw - (OldCOntrollerRotation.Yaw < 0.f
						                      ? FMath::Abs(EquippedWeapon->RecoilRotation.Yaw) - FMath::Abs(OldCOntrollerRotation.Yaw)
						                      : FMath::Abs(OldCOntrollerRotation.Yaw) - FMath::Abs(
							                      EquippedWeapon->RecoilRotation.Yaw)),0.f,Combat->EquippedWeapon->MaxYawRecoil);
					Controller->SetControlRotation(EquippedWeapon->RecoilRotation);
					
				}
			
			}
				
				
			
			
		}
		
	}


	
	
}
