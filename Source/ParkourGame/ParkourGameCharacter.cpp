// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParkourGameCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/SpringArmComponent.h"

//////////////////////////////////////////////////////////////////////////
// AParkourGameCharacter

AParkourGameCharacter::AParkourGameCharacter()
	:m_wallNormal()
	,m_wallLocation()
	,m_isClimbing( false )
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)
}

//////////////////////////////////////////////////////////////////////////
// Input

void AParkourGameCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	PlayerInputComponent->BindAxis("MoveForward", this, &AParkourGameCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AParkourGameCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AParkourGameCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AParkourGameCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &AParkourGameCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &AParkourGameCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &AParkourGameCharacter::OnResetVR);

	// Sprint
	PlayerInputComponent->BindAction( "Sprint", IE_Pressed, this, &AParkourGameCharacter::StartSprint );
	PlayerInputComponent->BindAction( "Sprint", IE_Released, this, &AParkourGameCharacter::StopSprint );

}

void AParkourGameCharacter::Tick( float DeltaTime )
{
	Super::Tick( DeltaTime );

	ForwardTrace();
	HeightTrace();
}

void AParkourGameCharacter::Hang()
{
	PlayAnimMontage( Climb, 1.0f );
	GetMesh()->GetAnimInstance()->Montage_Pause();
	m_isClimbing = true;
}

void AParkourGameCharacter::ForwardTrace()
{
	FVector start = GetActorLocation();
	FVector end = ( GetActorForwardVector() * 150.0f ) + start;

	FCollisionQueryParams traceParams( SCENE_QUERY_STAT( ForwardTrace ), true, GetInstigator() );
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add( GetOwner() );
	FHitResult hit( ForceInit );
	bool bHit = UKismetSystemLibrary::SphereTraceSingle( GetWorld(), start, end, 10.0f, UEngineTypes::ConvertToTraceType( ECC_Visibility ), false, ActorsToIgnore, EDrawDebugTrace::ForOneFrame, hit, true, FLinearColor::Red);

	m_wallNormal = hit.Normal;
	m_wallLocation = hit.Location;
}

void AParkourGameCharacter::HeightTrace()
{
	FVector start = ( GetActorLocation() + ( GetActorForwardVector() * 75.0f ) ) + FVector( 0.0f, 0.0f, 500.0f );
	FVector end = start - FVector( 0.0f, 0.0f, 500.0f );

	FCollisionQueryParams traceParams( SCENE_QUERY_STAT( ForwardTrace ), true, GetInstigator() );
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add( GetOwner() );
	FHitResult hit( ForceInit );
	bool bHit = UKismetSystemLibrary::SphereTraceSingle( GetWorld(), start, end, 10.0f, UEngineTypes::ConvertToTraceType( ECC_Visibility ), false, ActorsToIgnore, EDrawDebugTrace::ForOneFrame, hit, true, FLinearColor::Blue );


	if( bHit )
	{
		bool location = UKismetMathLibrary::InRange_FloatFloat( hit.Location.Z - GetMesh()->GetSocketLocation( "HipsSocket" ).Z, -50.0f, 0.0f, true, true );
		
		if( location )
		{
			GetCharacterMovement()->SetMovementMode( EMovementMode::MOVE_Flying );
			GetCharacterMovement()->StopMovementImmediately();
			Hang();
		}
	}
}




void AParkourGameCharacter::OnResetVR()
{
	// If ParkourGame is added to a project via 'Add Feature' in the Unreal Editor the dependency on HeadMountedDisplay in ParkourGame.Build.cs is not automatically propagated
	// and a linker error will result.
	// You will need to either:
	//		Add "HeadMountedDisplay" to [YourProject].Build.cs PublicDependencyModuleNames in order to build successfully (appropriate if supporting VR).
	// or:
	//		Comment or delete the call to ResetOrientationAndPosition below (appropriate if not supporting VR)
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void AParkourGameCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
		Jump();
}

void AParkourGameCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
		StopJumping();
}

void AParkourGameCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AParkourGameCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void AParkourGameCharacter::MoveForward(float Value)
{
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput( Direction, Value );
	}
}

void AParkourGameCharacter::MoveRight(float Value)
{
	if( ( Controller != nullptr ) && ( Value != 0.0f ) && ( m_isClimbing == false ) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation( 0, Rotation.Yaw, 0 );

		// get right vector 
		const FVector Direction = FRotationMatrix( YawRotation ).GetUnitAxis( EAxis::Y );
		// add movement in that direction
		AddMovementInput( Direction, Value );
	}	
}

void AParkourGameCharacter::StartSprint()
{
	GetCharacterMovement()->MaxWalkSpeed = 1000.0f;
}

void AParkourGameCharacter::StopSprint()
{
	GetCharacterMovement()->MaxWalkSpeed = 600.0f;
}