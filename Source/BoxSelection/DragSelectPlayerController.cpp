// Fill out your copyright notice in the Description page of Project Settings.
#include "DragSelectPlayerController.h"

#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

#include "SelectionBoxFunctionLibrary.h"

ADragSelectPlayerController::ADragSelectPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ADragSelectPlayerController::BoxSelect(const FVector2D& StartPoint, const FVector2D& EndPoint)
{
	const FVector2D TopLeft{FMath::Min(StartPoint.X, EndPoint.X), FMath::Min(StartPoint.Y, EndPoint.Y)};
	const FVector2D BottomRight{FMath::Max(StartPoint.X, EndPoint.X), FMath::Max(StartPoint.Y, EndPoint.Y)};

	const bool bDegenerate = (BottomRight - TopLeft).X < 0.1f || (BottomRight - TopLeft).Y < 0.1f;
	if (bDegenerate)
	{
		UE_LOG(LogEngine, Warning, TEXT("this case is degenerate, the box is too small"));
		return;
	}

	// Compute the selection region vectors:
	USelectionBoxFunctionLibrary::CreateSelectionRegionForBoxCorners(this, StartPoint, EndPoint, Region);

	// precompute the planes for a saving
	const FRegionPlanes Planes = Region.ComputePlanes();

	// now try to select
	// Don't use GetAllActorsOfClass in practice, etc...
	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), SelectableActorClass, Actors);

	// In actual practice, you would definitely want to compute the local bound in advance and save that:
	TArray<FBoxSphereBounds> Bounds;
	TArray<FBoxSphereBounds> WorldBounds;
	for (const AActor* const Actor : Actors)
	{
		// compute local bounds
		Bounds.Add(Actor->CalculateComponentsBoundingBoxInLocalSpace(true, false));
		// compute world-bounds as well:
		WorldBounds.Add(Actor->GetComponentsBoundingBox(true, false));
	}

	// store which actors were intersected
	TArray<bool> bIntersections;
	bIntersections.SetNumZeroed(Actors.Num());

#define DO_FAST	//	We want to do the faster pre-computed version.

	int Counter = 0;
	const double Start = FPlatformTime::Seconds();
	for (const AActor* const Actor : Actors)
	{
#ifdef DO_FAST
		// first check the bounding sphere
		const FBoxSphereBounds& WorldBoxSphere = WorldBounds[Counter];
		const bool bSpherePossiblyOverlaps =
			USelectionBoxFunctionLibrary::SelectionRegionOverlapsSphere2(Planes, WorldBoxSphere.Origin,
			                                                             WorldBoxSphere.SphereRadius);
		if (!bSpherePossiblyOverlaps)
		{
			// don't bother checking the box:
			Counter++;
			continue;
		}

		// then do the more expensive OBB check
		bIntersections[Counter] = USelectionBoxFunctionLibrary::SelectionRegionOverlapsTransformedBox2(
				Region, Planes, Actor->GetActorTransform(), Bounds[Counter].Origin, Bounds[Counter].BoxExtent) !=
			ETransformedBoxTestResult::NoIntersection;
#else
		// slow version that doesn't use precomputed bounds or planes
		bIntersections[Counter] = USelectionBoxFunctionLibrary::SelectionRegionOverlapsActor(
			Region, Actor, true, false);
#endif
		Counter++;
	}
	const double End = FPlatformTime::Seconds();

	// Update visuals:
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		ATargetActor* const TargetActor = Cast<ATargetActor>(Actors[Index]);
		check(TargetActor);
		if (bIntersections[Index])
		{
			FVector OriginWorld;
			FVector BoxExtentWorld;
			TargetActor->GetActorBounds(false, OriginWorld, BoxExtentWorld, false);
			DrawDebugBox(GetWorld(), OriginWorld, BoxExtentWorld, FColor::Blue);
		}
		// update visuals on the selected actor
		TargetActor->ChangeSelection(bIntersections[Index]);
	}

	UE_LOG(LogEngine, Display, TEXT("Time per actor: %.8f microseconds"), ((End - Start) / Actors.Num()) * 1e6);
	bInitialized = true;
}

void ADragSelectPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialized)
	{
		return;
	}
#ifdef DRAW_DEBUG
	constexpr float kLineDistance = 50.0f * 100.0f;
	DrawDebugLine(GetWorld(), Region.CameraOrigin, Region.CameraOrigin + Region.TopLeftRay * kLineDistance,
	              FColor::Blue,
	              false, -1, 0, 5.0f);
	DrawDebugLine(GetWorld(), Region.CameraOrigin, Region.CameraOrigin + Region.TopRightRay * kLineDistance,
	              FColor::Red,
	              false, -1, 0, 5.0f);
	DrawDebugLine(GetWorld(), Region.CameraOrigin, Region.CameraOrigin + Region.BottomRightRay * kLineDistance,
	              FColor::Purple, false, -1, 0, 5.0f);
	DrawDebugLine(GetWorld(), Region.CameraOrigin, Region.CameraOrigin + Region.BottomLeftRay * kLineDistance,
	              FColor::Green, false, -1, 0, 5.0f);

	const FRegionPlanes WorldPlanes = Region.ComputePlanes();
	constexpr float kArrowLength = 200.0f;
	constexpr float kArrowSize = 10.0f;
	DrawDebugDirectionalArrow(GetWorld(), Region.CameraOrigin,
	                          Region.CameraOrigin + WorldPlanes.LeftPlane.GetNormal() * kArrowLength, kArrowSize,
	                          FColor::Yellow,
	                          false, -1.f, 0,
	                          5.0f);
	DrawDebugDirectionalArrow(GetWorld(), Region.CameraOrigin,
	                          Region.CameraOrigin + WorldPlanes.RightPlane.GetNormal() * kArrowLength, kArrowSize,
	                          FColor::Orange,
	                          false, -1.f,
	                          0, 5.0f);

	DrawDebugDirectionalArrow(GetWorld(), Region.CameraOrigin,
	                          Region.CameraOrigin + WorldPlanes.TopPlane.GetNormal() * kArrowLength, kArrowSize,
	                          FColor::Green,
	                          false, -1.f, 0,
	                          5.0f);
	DrawDebugDirectionalArrow(GetWorld(), Region.CameraOrigin,
	                          Region.CameraOrigin + WorldPlanes.BottomPlane.GetNormal() * kArrowLength, kArrowSize,
	                          FColor::Blue,
	                          false, -1.f, 0,
	                          5.0f);
#endif
}
