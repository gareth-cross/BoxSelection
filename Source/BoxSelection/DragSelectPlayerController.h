// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "SelectionBoxFunctionLibrary.h"

#include "DragSelectPlayerController.generated.h"


// Subclassed in BP so we can update visual state of actor when selected.
UCLASS(Abstract, Blueprintable)
class BOXSELECTION_API ATargetActor : public AActor
{
	GENERATED_BODY()
public:
	// Mark selection or not selection
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable)
	void ChangeSelection(bool bSelected);
};


/**
 * Implement box selection.
 */
UCLASS(Abstract, Blueprintable)
class BOXSELECTION_API ADragSelectPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ADragSelectPlayerController();

	// Set to ATargetActor BP type in the controller BP.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<ATargetActor> SelectableActorClass{};

	// Run box selection logic and update the selected actors...
	UFUNCTION(BlueprintCallable)
	void BoxSelect(const FVector2D& StartPoint, const FVector2D& EndPoint);

	// Draw debug stuff...
	virtual void Tick(float DeltaSeconds) override;

	// True if we have at least one set of debug quantities to draw...
	bool bInitialized{false};

	// Member variables so we can draw these in Tick() when BoxSelect() is not running after de-possess.
	// Normally there is no reason for these to be member variables.
	FSelectionRegion Region{};
};
