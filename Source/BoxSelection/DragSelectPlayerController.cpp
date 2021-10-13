// Fill out your copyright notice in the Description page of Project Settings.
#include "DragSelectPlayerController.h"

#include "DrawDebugHelpers.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"

ADragSelectPlayerController::ADragSelectPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

// Pair of integers...
struct IntPair
{
	int i;
	int j;
};

static uint8 GetRegion(const FVector& Pt, const FBoxPlanes& Planes)
{
	if (Planes.TopPlane.PlaneDot(Pt) > 0)
	{
		if (Planes.LeftPlane.PlaneDot(Pt) > 0) //	top left
		{
			return 9; //	1001
		}
		else if (Planes.RightPlane.PlaneDot(Pt) > 0) // top right
		{
			return 5; //	0101
		}
		else //	top middle
		{
			return 1; //	0001
		}
	}
	else if (Planes.BottomPlane.PlaneDot(Pt) > 0)
	{
		if (Planes.LeftPlane.PlaneDot(Pt) > 0) //	bottom left
		{
			return 10; //	1010
		}
		else if (Planes.RightPlane.PlaneDot(Pt) > 0) // bottom right
		{
			return 6; //	0110
		}
		else // bottom middle
		{
			return 2; //	0001
		}
	}
	else //	In between top and bottom
	{
		if (Planes.LeftPlane.PlaneDot(Pt) > 0) //	middle left
		{
			return 8; //	1000
		}
		else if (Planes.RightPlane.PlaneDot(Pt) > 0) // middle right
		{
			return 4; //	0100
		}
		else
		{
			// fall through to 0
		}
	}
	return 0; //	 middle
}

static FColor GetRegionColor(const uint8 Region)
{
	if (Region == 9)
	{
		return FColor::Red;
	}
	else if (Region == 5)
	{
		return FColor::Orange;
	}
	else if (Region == 1)
	{
		return FColor::Yellow;
	}
	else if (Region == 10)
	{
		return FColor::Blue;
	}
	else if (Region == 6)
	{
		return FColor::Green;
	}
	else if (Region == 2)
	{
		return FColor::Cyan;
	}
	else if (Region == 8)
	{
		return FColor::Black;
	}
	else if (Region == 4)
	{
		return FColor::White;
	}
	return FColor::Black;
}

// todo: call me only once in practice, or just figure it out and hardcode it... I'm lazy right now
// PointMultipliers should be integers anyways...
static void ComputeLinePairs(const FVector PointMultipliers[8], IntPair Pairs[12])
{
	int Output = 0;
	for (int i = 0; i < 8; ++i)
	{
		for (int j = i + 1; j < 8; ++j)
		{
			const bool XDiffers = !FMath::IsNearlyEqual(
				PointMultipliers[i].X, PointMultipliers[j].X, 0.1f);
			const bool YDiffers = !FMath::IsNearlyEqual(
				PointMultipliers[i].Y, PointMultipliers[j].Y, 0.1f);
			const bool ZDiffers = !FMath::IsNearlyEqual(
				PointMultipliers[i].Z, PointMultipliers[j].Z, 0.1f);

			const int NumDifferent = XDiffers + YDiffers + ZDiffers;
			check(NumDifferent > 0 && NumDifferent <= 3);
			if (NumDifferent == 1)
			{
				Pairs[Output++] = {i, j};
				if (Output == 12)
				{
					break;
				}
			}
		}
	}
}

static bool RaysIntersect(const FQuat& World_R_Actor, const FVector& World_t_Actor, const FBox& LocalBox,
                          const FVector& CameraLocationInWorld, const FVector& RayDirectionInWorld)
{
	const FVector CameraInActor = World_R_Actor.Inverse() * (CameraLocationInWorld - World_t_Actor);
	const FVector RayDirInActor = World_R_Actor.Inverse() * RayDirectionInWorld;
	return FMath::LineBoxIntersection(LocalBox, CameraInActor, CameraInActor + RayDirInActor * 100.0f * 100.0f,
	                                  RayDirInActor);
}

static bool Intersects(const FQuat& World_R_Actor, const FVector& World_t_Actor, const FBox& LocalBox,
                       const FBoxPlanes& Planes, UWorld* const World)
{
	const FVector PointMultipliers[8] = {
		FVector{1, 1, 1},
		FVector{1, 1, -1},
		FVector{1, -1, -1},
		FVector{1, -1, 1},
		FVector{-1, 1, 1},
		FVector{-1, 1, -1},
		FVector{-1, -1, -1},
		FVector{-1, -1, 1},
	};

	// Convert pts to world
	FVector WorldPts[8];
	for (int i = 0; i < 8; ++i)
	{
		WorldPts[i] = World_R_Actor * (LocalBox.GetExtent() * PointMultipliers[i] + LocalBox.GetCenter()) +
			World_t_Actor;
	}

	//	Assign regions to points
	uint8 Regions[8];
	for (int i = 0; i < 8; ++i)
	{
		Regions[i] = GetRegion(WorldPts[i], Planes);
		if (!Regions[i])
		{
			return true; //	early exit, one point is within the box
		}
		DrawDebugPoint(World, WorldPts[i], 10.0f, GetRegionColor(Regions[i]));
	}

	// lines that make up the box:
	IntPair LinePairs[12];
	ComputeLinePairs(PointMultipliers, LinePairs); //	only do this once in real life obviously...

	// do check on lines
	for (const IntPair& Line : LinePairs)
	{
		const uint8 RegionFirst = Regions[Line.i];
		const uint8 RegionSecond = Regions[Line.j];
		const bool bMightIntersect = (RegionFirst & RegionSecond) == 0;
		constexpr bool bUseEarlyExit = true;

		// if this test fails (cohen sutherland algorithm) then we can early exit this line, it can't possibly intersect
		if (!bMightIntersect && bUseEarlyExit)
		{
			// Cannot pass through, draw point...
			DrawDebugPoint(World, WorldPts[Line.i] * 0.5f + WorldPts[Line.j] * 0.5f, 10.0f, FColor::White);
			continue;
		}

		// Might still pass through, check it against the planes:
		{
			FVector IntersectionPt;
			const bool bIntersectsLeft = FMath::SegmentPlaneIntersection(WorldPts[Line.i],
			                                                             WorldPts[Line.j], Planes.LeftPlane,
			                                                             IntersectionPt);
			if (bIntersectsLeft)
			{
				const uint8 IntersectionRegion = GetRegion(IntersectionPt, Planes);
				if (IntersectionRegion == 0 || IntersectionRegion == 4 || IntersectionRegion == 8)
				{
					// intersection point is in the middle (vertical)
					return true;
				}
			}
		}
		{
			FVector IntersectionPt;
			const bool bIntersectsRight = FMath::SegmentPlaneIntersection(WorldPts[Line.i],
			                                                              WorldPts[Line.j], Planes.RightPlane,
			                                                              IntersectionPt);
			if (bIntersectsRight)
			{
				const uint8 IntersectionRegion = GetRegion(IntersectionPt, Planes);
				if (IntersectionRegion == 0 || IntersectionRegion == 4 || IntersectionRegion == 8)
				{
					// intersection point is in the middle (vertical)
					return true;
				}
			}
		}
		{
			FVector IntersectionPt;
			const bool bIntersectsTop = FMath::SegmentPlaneIntersection(WorldPts[Line.i],
			                                                            WorldPts[Line.j], Planes.TopPlane,
			                                                            IntersectionPt);
			if (bIntersectsTop)
			{
				const uint8 IntersectionRegion = GetRegion(IntersectionPt, Planes);
				if (IntersectionRegion == 0 || IntersectionRegion == 1 || IntersectionRegion == 2)
				{
					// intersection point is in the middle (vertical)
					return true;
				}
			}
		}
		{
			FVector IntersectionPt;
			const bool bIntersectsBottom = FMath::SegmentPlaneIntersection(WorldPts[Line.i],
			                                                               WorldPts[Line.j], Planes.BottomPlane,
			                                                               IntersectionPt);
			if (bIntersectsBottom)
			{
				const uint8 IntersectionRegion = GetRegion(IntersectionPt, Planes);
				if (IntersectionRegion == 0 || IntersectionRegion == 1 || IntersectionRegion == 2)
				{
					// intersection point is in the middle (vertical)
					return true;
				}
			}
		}
	}
	return false;
}

void ADragSelectPlayerController::BoxSelect(const FVector2D& StartPoint, const FVector2D& EndPoint)
{
	const FVector2D TopLeft{FMath::Min(StartPoint.X, EndPoint.X), FMath::Min(StartPoint.Y, EndPoint.Y)};
	const FVector2D TopRight{FMath::Max(StartPoint.X, EndPoint.X), FMath::Min(StartPoint.Y, EndPoint.Y)};
	const FVector2D BottomRight{FMath::Max(StartPoint.X, EndPoint.X), FMath::Max(StartPoint.Y, EndPoint.Y)};
	const FVector2D BottomLeft{FMath::Min(StartPoint.X, EndPoint.X), FMath::Max(StartPoint.Y, EndPoint.Y)};

	const bool bDegenerate = (BottomRight - TopLeft).X < 0.1f || (BottomRight - TopLeft).Y < 0.1f;
	if (bDegenerate)
	{
		UE_LOG(LogEngine, Warning, TEXT("this case is degen yo"));
		return;
	}

	UCameraComponent* const Camera = GetPawn()->FindComponentByClass<UCameraComponent>();
	check(Camera);
	CameraLocationInWorld = Camera->GetComponentLocation();
	CameraForwardInWorld = Camera->GetForwardVector();

	FVector Unused{};
	UGameplayStatics::DeprojectScreenToWorld(this, TopLeft, Unused, TopLeftRayInWorld);
	UGameplayStatics::DeprojectScreenToWorld(this, TopRight, Unused, TopRightRayInWorld);
	UGameplayStatics::DeprojectScreenToWorld(this, BottomRight, Unused, BottomRightRayInWorld);
	UGameplayStatics::DeprojectScreenToWorld(this, BottomLeft, Unused, BottomLeftRayInWorld);

	// PLanes in the world frame
	WorldPlanes = PlanesForCamera(CameraLocationInWorld, CameraForwardInWorld, TopLeftRayInWorld, TopRightRayInWorld,
	                              BottomLeftRayInWorld, BottomRightRayInWorld);
	const FConvexVolume VolumeWorld = WorldPlanes.ToVolume();

	// now try to select
	// Don't use GetAllActorsOfClass in practice, etc...
	TArray<AActor*> Actors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), SelectableActorClass, Actors);

	for (AActor* const Actor : Actors)
	{
		ATargetActor* const AsTargetActor = Cast<ATargetActor>(Actor);
		check(AsTargetActor);
		FVector OriginWorld;
		FVector BoxExtentWorld;
		AsTargetActor->GetActorBounds(false, OriginWorld, BoxExtentWorld, false);

		DrawDebugBox(GetWorld(), OriginWorld, BoxExtentWorld, FColor::Blue);

		const FBox LocalBox = AsTargetActor->CalculateComponentsBoundingBoxInLocalSpace(true, false);
		const FVector World_t_Actor = AsTargetActor->GetActorLocation();
		const FQuat World_R_Actor = AsTargetActor->GetActorRotation().Quaternion();

		const bool bAnyRayIntersects =
			RaysIntersect(World_R_Actor, World_t_Actor, LocalBox, CameraLocationInWorld, TopLeftRayInWorld) ||
			RaysIntersect(World_R_Actor, World_t_Actor, LocalBox, CameraLocationInWorld, TopRightRayInWorld) ||
			RaysIntersect(World_R_Actor, World_t_Actor, LocalBox, CameraLocationInWorld, BottomRightRayInWorld) ||
			RaysIntersect(World_R_Actor, World_t_Actor, LocalBox, CameraLocationInWorld, BottomLeftRayInWorld);

		// if no ray corner intersects, do the other thing:
		const bool bIntersects = bAnyRayIntersects || Intersects(World_R_Actor, World_t_Actor,
		                                                         LocalBox,
		                                                         WorldPlanes, GetWorld());
		if (bIntersects)
		{
			DrawDebugBox(GetWorld(), OriginWorld, BoxExtentWorld, FColor::Blue);
		}

		// update visuals on the selected actor
		AsTargetActor->ChangeSelection(bIntersects);
	}

	bInitialized = true;
}

void ADragSelectPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialized)
	{
		return;
	}
	constexpr float kLineDistance = 50.0f * 100.0f;
	DrawDebugLine(GetWorld(), CameraLocationInWorld, CameraLocationInWorld + TopLeftRayInWorld * kLineDistance,
	              FColor::Blue,
	              false, -1, 0, 5.0f);
	DrawDebugLine(GetWorld(), CameraLocationInWorld, CameraLocationInWorld + TopRightRayInWorld * kLineDistance,
	              FColor::Red,
	              false, -1, 0, 5.0f);
	DrawDebugLine(GetWorld(), CameraLocationInWorld, CameraLocationInWorld + BottomRightRayInWorld * kLineDistance,
	              FColor::Purple, false, -1, 0, 5.0f);
	DrawDebugLine(GetWorld(), CameraLocationInWorld, CameraLocationInWorld + BottomLeftRayInWorld * kLineDistance,
	              FColor::Green, false, -1, 0, 5.0f);

	constexpr float kArrowLength = 200.0f;
	constexpr float kArrowSize = 10.0f;
	DrawDebugDirectionalArrow(GetWorld(), CameraLocationInWorld,
	                          CameraLocationInWorld + WorldPlanes.LeftPlane.GetNormal() * kArrowLength, kArrowSize,
	                          FColor::Yellow,
	                          false, -1.f, 0,
	                          5.0f);
	DrawDebugDirectionalArrow(GetWorld(), CameraLocationInWorld,
	                          CameraLocationInWorld + WorldPlanes.RightPlane.GetNormal() * kArrowLength, kArrowSize,
	                          FColor::Orange,
	                          false, -1.f,
	                          0, 5.0f);

	DrawDebugDirectionalArrow(GetWorld(), CameraLocationInWorld,
	                          CameraLocationInWorld + WorldPlanes.TopPlane.GetNormal() * kArrowLength, kArrowSize,
	                          FColor::Green,
	                          false, -1.f, 0,
	                          5.0f);
	DrawDebugDirectionalArrow(GetWorld(), CameraLocationInWorld,
	                          CameraLocationInWorld + WorldPlanes.BottomPlane.GetNormal() * kArrowLength, kArrowSize,
	                          FColor::Blue,
	                          false, -1.f, 0,
	                          5.0f);
}

FBoxPlanes ADragSelectPlayerController::PlanesForCamera(const FVector& CameraLocation,
                                                        const FVector& CameraForward,
                                                        const FVector& TopLeftRay,
                                                        const FVector& TopRightRay,
                                                        const FVector& BottomLeftRay,
                                                        const FVector& BottomRightRay)
{
	FBoxPlanes Result;
	// form planes defined by the corner vectors
	// SignFlip was here just in case the FConvexVolume wanted the planes reversed.
	// When SignFlip is 1, we are using planes that point _out_ from the frustum:
	constexpr float SignFlip = 1.0f;
	Result.LeftPlane = FPlane{
		CameraLocation, SignFlip * FVector::CrossProduct(BottomLeftRay, TopLeftRay).GetSafeNormal()
	};
	Result.RightPlane = FPlane{
		CameraLocation,
		SignFlip * FVector::CrossProduct(TopRightRay, BottomRightRay).GetSafeNormal()
	};
	Result.TopPlane = FPlane{
		CameraLocation, SignFlip * FVector::CrossProduct(TopLeftRay, TopRightRay).GetSafeNormal()
	};
	Result.BottomPlane = FPlane{
		CameraLocation,
		SignFlip * FVector::CrossProduct(BottomRightRay, BottomLeftRay).GetSafeNormal()
	};
	// Max selection distance:
	constexpr float kNearDistance = 100.0f;
	constexpr float kFarDistance = 50 * 100.0f;
	Result.NearPlane = FPlane{CameraLocation + CameraForward * kNearDistance, -CameraForward * SignFlip};
	Result.FarPlane = FPlane{
		CameraLocation + CameraForward * kFarDistance, CameraForward * SignFlip
	};
	return Result;
}
