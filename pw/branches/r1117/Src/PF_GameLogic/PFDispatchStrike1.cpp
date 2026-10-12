#include "stdafx.h"

#include "PFDispatchStrike1.h"
#include "PFBaseUnit.h"
#include "PFAbilityInstance.h"
#include "PFBaseMovingUnit.h"
#include "TileMap.h"
#include "../Core/Scheduler.h"

#if defined(PW_LINUX_NULL_RENDER)
#include <cmath>
#include <limits>

#endif
#ifndef VISUAL_CUTTED
#include "PFClientDispatch.h"
#include "PFClientApplicators.h"
#include "PFClientBaseUnit.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif

#define DISPATCH_APPROACH_RANGE 0.5f

namespace NWorld
{

namespace
{
  CObj<PFBaseApplicator> CreateApplicator(PFApplCreatePars const &cp)
  {
	  CObj<PFBaseApplicator> pAppl(cp.pDBAppl->Create(cp));
	  if (pAppl)
	  {
		  if (!pAppl->Init())
			  pAppl = 0; // Kill applicator
	  }
	  return pAppl;
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchImmediate::OnStart()
{
  if ( GetDB().instantApply )
  {
    Step(0.0f);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFDispatchImmediate::Step(float dtInSeconds)
{
  NI_PROFILE_FUNCTION
  
	if ( IsObjectDead() )
		return false;

	Base::Step(dtInSeconds);

  if (state == STATE_PROCEED)
		Apply();
  
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFDispatchContinuous::PFDispatchContinuous(CreateParams const &cp)
  : Base(cp)
{
  AddPersistentApplicators( GetDB().persistentApplicators );
}

PFDispatchContinuous::~PFDispatchContinuous()
{
  Clear(persistentApplicators);
}

void PFDispatchContinuous::OnStart()
{
	// start persistent applicators
  struct StartFunc
  {
    void operator()(CObj<PFBaseApplicator> &applicator)
    {
      if (applicator->Start())
      {
        applicator->Stop();
        MemorizeApplicator(applicator);
      }
    }
  } sf;
  PFBaseApplicator::ForallRing(persistentApplicators, sf);
}

bool PFDispatchContinuous::Step(float dtInSeconds)
{
	if ( IsObjectDead() )
		return false;

	if ( state == STATE_PROCEED )
  {
    // step persistent applicators
    PFBaseApplicator::StepRing( persistentApplicators, dtInSeconds );
  }
  
  Base::Step(dtInSeconds);

  return false;
}

void PFDispatchContinuous::Die()
{
	if (Started())
  {
    // stop persistent applicators
    struct StopFunc
    {
      void operator()(CObj<PFBaseApplicator> &applicator) 
      { 
        //CObj<PFBaseApplicator> pHolder(applicator);
        applicator->Stop();
        //MemorizeApplicator(pHolder);
      }
    } sf;
    PFBaseApplicator::ForallRing(persistentApplicators, sf);
  }
	PFDispatch::Die();
}

void PFDispatchContinuous::AddPersistentApplicators( vector<NDb::Ptr<NDb::BaseApplicator>> const& newPersistentApplicators, PFAbilityData* upgrader /* = NULL */ )
{
  // create persistent applicators
  PFApplCreatePars acp(pAbility, target, pParentApplicator, 0, false, upgrader);
  int const numApplicators = newPersistentApplicators.size();
  for (int i = 0; i < numApplicators; ++i)
  {
    acp.pDBAppl = newPersistentApplicators[i];

    CObj<PFBaseApplicator> pApplicator(CreateApplicator(acp));
    if (pApplicator)
    {
      persistentApplicators.addLast(pApplicator);
    }
  }

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFDispatchUniformLinearMove::PFDispatchUniformLinearMove(CreateParams const &cp)
  : Base(cp)
  , lastTargetPos( cp.target.AcquirePosition().AsVec2D() )
  , travelledDistance(0.f)
  , originalSenderPos(VNULL2)
  , originalDir(CVec2(0.f, -1.f))
  , clientStartDelay( cp.clientStartDelay )
{
  curPos = cp.source.AcquirePosition().AsVec2D();

  suspendTime = GetDB().suspendTime;
	speed = GetDB().speed;
	maxDistance = GetDB().maxDistance > 0.0f ? GetDB().maxDistance : 1.0e6f;

  m_startTime = GetWorld()->GetTimeElapsed();

  if (cp.target.IsUnitValid())
  {
    cp.target.GetUnit()->RegisterLinearDispatch(this);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchUniformLinearMove::CreateClientDispatch()
{
  CreateClientObject<TClientObject>( GetWorld()->GetScene(), GetDB().locatorOrient, clientStartDelay );
}

void PFDispatchUniformLinearMove::Reset()
{
	if ( ClientObject() )
		ClientObject()->OnWorldObjectDie();
	PFDispatchContinuous::Reset();
	CreateClientObject<TClientObject>( GetWorld()->GetScene(), GetDB().locatorOrient, clientStartDelay );
  if ( ClientObject() )
  {
	  float curTime = GetWorld()->GetTimeElapsed();
	  float deltaTime = curTime - m_startTime;
	  if ( clientStartDelay < EPS_VALUE )
	  {
		  while ( deltaTime >= 0.0f )
		  {
			  ClientObject()->Update( 0.01f );
			  deltaTime -= 0.01f;
		  }
	  }
	  else
	  {
		  if ( clientStartDelay > deltaTime )
		  {
			  while ( deltaTime >= 0.0f )
			  {
				  ClientObject()->UpdateAfterScene( 0.01f );
				  deltaTime -= 0.01f;
			  }
		  }
		  else
		  {
			  while ( !ClientObject()->IsStarted() )
			  {
				  ClientObject()->UpdateAfterScene( 0.01f );
			  }
			  deltaTime -= clientStartDelay;
			  while ( deltaTime >= 0.0f )
			  {
				  ClientObject()->Update( 0.01f );
				  deltaTime -= 0.01f;
			  }
		  }
	  }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchUniformLinearMove::UpdatePersistentApplicatorsPosition()
{
	struct StepFunc : public NonCopyable
	{
		StepFunc(Target const &target): target(target) {}

		void operator()(CObj<PFBaseApplicator> &applicator) { applicator->SetTarget(target); }

		Target const &target;
	};

	// update target (position) for persistent applicators
	Target t(GetCurrentPosition());
	StepFunc sf(t);
	PFBaseApplicator::ForallRing( persistentApplicators, sf );
}

// precise dispatch approach algorithm for case of fast targets moving towards slow dispatches
inline bool IsApproached( CVec2 const& prevPos, CVec2 const& nextPos, CVec2 const& approachingPoint, float speed, const CPtr<PF_Core::IWorld> & pWorld )
{
  if ( nextPos == prevPos ) // target doesn't move
    return false;

  CVec2 targetMove = nextPos - prevPos;
  float targetDist = fabs( targetMove );

  if ( targetDist < speed * pWorld->GetStepLengthInSeconds() ) // target moves slower than dispatch
    return false;

  CVec2 toPoint = approachingPoint - prevPos;
  CVec2 targetMoveDir = targetMove / targetDist;
  float proj = toPoint.Dot( targetMoveDir );

  if ( proj <= 0 || proj >= targetDist ) // target movement is out of range
    return false;

  float toPointLen = fabs( toPoint );
  if ( toPointLen < DISPATCH_APPROACH_RANGE ) // target movement begins or ends within approach range
    return true;

  float len = fabs( Cross( toPoint, targetMoveDir ) );
  if ( len < DISPATCH_APPROACH_RANGE ) // target passes through approach range
    return true;

  // More precision if required
  /*
  float speed2 = speed * pWorld->GetStepLengthInSeconds();
  float sinAlpha = speed2 / targetDist;
  float sinBetta = DISPATCH_APPROACH_RANGE / toPointLen;
  proj /= toPointLen;
  return acos( proj ) < asin( sinAlpha ) + asin( sinBetta ); // target can be catched up
  */
  return false;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFDispatchUniformLinearMove::Step(float dtInSeconds)
{
  NI_PROFILE_FUNCTION
  
	if ( IsObjectDead() )
		return false;

	if ((target.IsUnit() && !target.IsUnitValid()) || target.IsUnitMounted())
    DropTarget(); // while flying or mounted, target lost

  bool arrived = false;
  bool apply = false;
  
  if (state == STATE_PROCEED)
  {
    if ( suspendTime > EPS_VALUE )
    {
      suspendTime -= dtInSeconds;
    }
    else
    {
      CVec2 const receiverPos = target.AcquirePosition().AsVec2D();

      // check deviation angle
      if ( GetDB().aiming == NDb::DISPATCHAIMING_STRAIGHT )
      {
        CVec2 curDir = receiverPos - originalSenderPos;
        if ( Normalize(&curDir) )
        {
          float c = originalDir.Dot(curDir);
          c = Clamp(c, -1.f, 1.f);
          if ( acos(c) > ToRadian(GetDB().maxDeviation) )
          {
            DropTarget();
          }
        }
      }
      else if ( GetDB().aiming == NDb::DISPATCHAIMING_PURSUE )
      {
        if(target.IsUnit() && target.GetUnit()->IsMounted())
        {
          DropTarget();
        }
      }

      NI_ASSERT(curPos.IsFinite(), "Bad curpos");

      CVec2 const delta = receiverPos - curPos;
      float const deltaLength = fabs(delta);
      float const maxMove = dtInSeconds * speed;
      CVec2 const oldCurPos = curPos;

      float desiredMove = maxMove;
      if (deltaLength <= maxMove)
      {
        desiredMove = deltaLength;
      }

      bool hasReachedMaxDist = travelledDistance + desiredMove > maxDistance;
      if (hasReachedMaxDist)
      {
        desiredMove = maxDistance - travelledDistance;
      }

      travelledDistance += desiredMove;

      if ( ( deltaLength <= maxMove || IsApproached( lastTargetPos, receiverPos, curPos, speed, pWorld ) )&& !hasReachedMaxDist ) // check for target proximity
      {
        curPos = receiverPos;
        arrived = true;
        apply = true;
      } 
      else
      {
        // calculate new world pos
        if ( deltaLength > EPS_VALUE )
        {
          CVec2 newPos = curPos + delta * ( desiredMove / deltaLength );
          NI_ASSERT( newPos.IsFinite(), "Bad curpos" );

          TileMap* pMap = GetWorld()->GetTileMap();
          SVector tile = pMap->GetTile( newPos );
          if ( !pMap->IsPointOutsideMap( tile.x, tile.y ) )
          {
            curPos = newPos;
          }
          else
          {
            arrived = true;
          }
        }

        if (hasReachedMaxDist)
        {
          arrived = true;
        }
      }

      lastTargetPos = receiverPos;
    }
  }

  UpdatePersistentApplicatorsPosition();
  Base::Step(dtInSeconds);

  if ( IsValid(pParentApplicator) )
    pParentApplicator->OnDispatchStep( this );

	if ( IsObjectDead() )
		return false;

	if (arrived && !HasArrived())
    OnArrive(apply);
  
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchUniformLinearMove::OnStart()
{
	UpdatePersistentApplicatorsPosition();

	Base::OnStart();
  lastTargetPos = target.AcquirePosition().AsVec2D();

  originalSenderPos = curPos;
  originalDir = lastTargetPos - curPos;
  if (!Normalize(&originalDir))
  {
    originalDir = CVec2(0.f, -1.f);
  }

  if ( IsValid(pParentApplicator) )
    pParentApplicator->OnDispatchStep( this );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchUniformLinearMove::DropTarget()
{
  target.SetPosition( CVec3(lastTargetPos.x, lastTargetPos.y, 0.f) );

  if ( IsValid(pParentApplicator) )
    pParentApplicator->OnDispatchTargetDropped( this );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchUniformLinearMove::OnTargetTeleport()
{
  if (GetDB().aiming != NDb::DISPATCHAIMING_PURSUE)
  {
    DropTarget();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchUniformLinearMove::OnStopAttackingTarget()
{
  if (GetDB().aiming != NDb::DISPATCHAIMING_PURSUE)
  {
    DropTarget();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFDispatchUniformLinearMove::OnArrive(bool canApply, bool playApplyEffect /* = true*/ )
{
  state = STATE_READY_TO_APPLY;
  if(canApply)
  {
    Apply( playApplyEffect );
  }
  else
  {
    Die();
    return true;
  }
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchUniformLinearMove::ForceArrive( Target const& target, bool playApplyEffect )
{
  curPos = target.AcquirePosition().AsVec2D();
  SetNewTarget( target );

  struct ApplicatorsUpdater : public NonCopyable
  {
    ApplicatorsUpdater( Target const& target ) : target(target) {}

    void operator()( PFBaseApplicator* appl )
    {
      appl->SetTarget( target );
      appl->Init();
    }

    Target const& target;
  } applicatorsUpdater( target );

  ring::ForAll( applicators, applicatorsUpdater );

  OnArrive( true, playApplyEffect );

  if ( ClientObject() )
    ClientObject()->OnArrive();
}

void PFDispatchUniformLinearMove::SetNewTarget( const Target & _target )
{
  Base::SetNewTarget( _target );

  lastTargetPos = _target.AcquirePosition().AsVec2D();

  UnitDispatchesRing::safeRemove( this );

  if ( _target.IsUnitValid() )
  {
    _target.GetUnit()->RegisterLinearDispatch( this );
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFDispatchByTime::PFDispatchByTime(CreateParams const &cp)
  : Base(cp)
{
  duration = RetrieveParam( GetDB().lifeTime );

  if (target.IsPosition())
    destination = cp.target.GetPosition();
  else
  {
    NI_ASSERT(cp.target.IsObjectValid(true), "Object must be valid");
    destination = cp.target.GetObject()->GetPosition();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchByTime::CreateClientDispatch()
{
  CreateClientObject<TClientObject>( GetWorld()->GetScene() );
}

void PFDispatchByTime::Reset()
{
	if ( ClientObject() )
		ClientObject()->OnWorldObjectDie();
	PFDispatchContinuous::Reset();
	CreateClientObject<TClientObject>( GetWorld()->GetScene() );
  CALL_CLIENT(Start);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchByTime::OnStart()
{
  PFDispatchContinuous::OnStart();
  CALL_CLIENT(Start);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFDispatchByTime::Step(float dtInSeconds)
{
  NI_PROFILE_FUNCTION
  
	if ( IsObjectDead() )
		return false;

	if( state == STATE_PROCEED )
  {
		bool time2Die = false;
		if ( !GetDB().cancelOnSenderDeath || IsUnitValid(pSender) )
		{
			if (duration <= 0)
			{
				// if negative lifetime was specified, check if there're any persistent applicators left
				time2Die = persistentApplicators.empty();
			}
			else
			{
				// if positive lifetime was specified, decrease it
				duration -= dtInSeconds;
				time2Die = (duration <= 0);
			}
		}
		else
		{
			time2Die = true; // Sender is dead. We have to die also.
		}

		if (time2Die)
		{
			Apply();
			return true;
		}
  }
  
  Base::Step(dtInSeconds);

  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchWithLink::CreateClientDispatch()
{
  CreateClientObject<TClientObject>( GetWorld()->GetScene(), GetDB().locatorOrient, clientStartDelay );
}

void PFDispatchWithLink::Reset()
{
	PFDispatchUniformLinearMove::Reset();
}

#if defined(PW_LINUX_NULL_RENDER)
namespace
{
/** Tile conversion and the map's signed bounds expression must both remain representable. */
bool LinuxMacePositionFits(const CVec2& pos, const TileMap& map)
{
	if (!pos.IsFinite() || !std::isfinite(map.GetTileSize()) || map.GetTileSize() <= 0 ||
		map.GetSizeX() <= 0 || map.GetSizeY() <= 0)
		return false;
	const double x = static_cast<double>(pos.x) / map.GetTileSize();
	const double y = static_cast<double>(pos.y) / map.GetTileSize();
	const double limit = std::numeric_limits<int>::max();
	return x > -limit + map.GetSizeX() && x < limit - map.GetSizeX() &&
		y > -limit + map.GetSizeY() && y < limit - map.GetSizeY();
}
}

bool PFDispatchRockmanMace::CanCreateNative(const NDb::DispatchRockmanMace& db, const CreateParams& cp)
{
	if (!cp.pWorld || !cp.pWorld->GetTileMap() || !IsValid(cp.pSender) ||
		!cp.source.IsValid(true) || !cp.target.IsValid(true) ||
		!std::isfinite(db.speed) || db.speed < 0 || !std::isfinite(db.maxDistance) ||
		!std::isfinite(db.minDistance) || !std::isfinite(db.suspendTime) || !std::isfinite(db.maxDeviation))
		return false;
	const TileMap& map = *cp.pWorld->GetTileMap();
	const CVec3 source = cp.source.AcquirePosition(), destination = cp.target.AcquirePosition();
	if (!source.IsFinite() || !destination.IsFinite() || !LinuxMacePositionFits(source.AsVec2D(), map) ||
		!LinuxMacePositionFits(destination.AsVec2D(), map))
		return false;
	const CVec2 delta = destination.AsVec2D() - source.AsVec2D();
	const float length = fabs(delta);
	if (!delta.IsFinite() || !std::isfinite(length))
		return false;
	PFBaseMovingUnit* moving = dynamic_cast<PFBaseMovingUnit*>(cp.pSender.GetPtr());
	if (!moving)
		return true;
	if (!LinuxMacePositionFits(moving->GetPos(), map))
		return false;
	float distance = moving->CheckStraightMove(destination.AsVec2D(), MAP_MODE_BUILDING);
	if (!std::isfinite(distance) || distance < 0)
		return false;
	distance = min(distance, db.maxDistance > 0 ? db.maxDistance : 1e6f);
	if (distance >= length)
		return true;
	// Match the constructor's float operation order, including minDistance overriding the trace.
	const CVec2 scaled = delta * max(distance, db.minDistance);
	const CVec2 clipped = source.AsVec2D() + scaled / length;
	return scaled.IsFinite() && clipped.IsFinite() && LinuxMacePositionFits(clipped, map);
}

bool PFDispatchRockmanMace::Step(float dtInSeconds)
{
	if (IsObjectDead() || !Started() || !std::isfinite(dtInSeconds) || dtInSeconds <= 0 ||
		!std::isfinite(speed) || speed < 0 || !std::isfinite(dtInSeconds * speed) ||
		!std::isfinite(suspendTime) || !std::isfinite(maxDistance) || !std::isfinite(travelledDistance))
		return false;
	const TileMap* map = GetWorld() ? GetWorld()->GetTileMap() : nullptr;
	// Lost or mounted units follow the original DropTarget path to lastTargetPos.
	const CVec2 destination = (target.IsUnit() && (!target.IsUnitValid() || target.IsUnitMounted()))
		? lastTargetPos : target.AcquirePosition().AsVec2D();
	const CVec2 delta = destination - curPos;
	const float length = fabs(delta);
	if (!map || !LinuxMacePositionFits(curPos, *map) || !LinuxMacePositionFits(destination, *map) ||
		!lastTargetPos.IsFinite() || !delta.IsFinite() || !std::isfinite(length) ||
		!std::isfinite(travelledDistance + min(length, dtInSeconds * speed)) ||
		!std::isfinite(GetDB().maxDeviation) ||
		(suspendTime > EPS_VALUE && !std::isfinite(suspendTime - dtInSeconds)))
		return false;
	return PFDispatchUniformLinearMove::Step(dtInSeconds);
}

#endif
PFDispatchRockmanMace::PFDispatchRockmanMace( CreateParams const &cp ) : Base(cp)
{
  lastTargetPos = target.AcquirePosition().AsVec2D();

  // calculate collision
  {
    PFBaseMovingUnit *pUnit = dynamic_cast<PFBaseMovingUnit*>(GetSender().GetPtr());

    if (pUnit)
    {
      float distFromStartPos = fabs(lastTargetPos - curPos);
      float dist = pUnit->CheckStraightMove(lastTargetPos, MAP_MODE_BUILDING);

      dist = min(dist, maxDistance);

      // For some reason (tiling?) distance returned by CheckStraightMove() is usually slightly less than
      // distance from the object position to the target (up to 0.25m) which makes the following code execute
      // almost always. If this causes problems maybe we should use some tolerance value.

      if (dist < distFromStartPos)
      {
        // if collision occurred set new target at collision point
        lastTargetPos = curPos + (lastTargetPos - curPos) * max( dist, GetDB().minDistance ) / distFromStartPos;
        float terrainHeight = 0.f;
#if !defined(PW_LINUX_NULL_RENDER)
        GetWorld()->GetScene()->GetHeightsController().GetHeight(lastTargetPos.x, lastTargetPos.y, 0, &terrainHeight, NULL);
#else
		// Native gameplay uses the same Z=0 plane as the inherited GetCurrentPosition().
#endif
        target.SetPosition( CVec3(lastTargetPos.x, lastTargetPos.y, terrainHeight) );
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatchRockmanMace::CreateClientDispatch()
{
#if !defined(PW_LINUX_NULL_RENDER)
  CreateClientObject<TClientObject>( GetWorld()->GetScene(), GetDB().locatorOrient, clientStartDelay );
#endif
}

void PFDispatchRockmanMace::Reset()
{
#if defined(PW_LINUX_NULL_RENDER)
	// The linear reset only adds client reconstruction after this gameplay reset.
	PFDispatch::Reset();
#else
	PFDispatchWithLink::Reset();
#endif
}

bool PFDispatchRockmanMace::OnArrive(bool canApply, bool playApplyEffect /*= true*/ )
{
  state = STATE_READY_TO_APPLY;
  return false;
}

}

REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFDispatchUniformLinearMove, NWorld)
REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFDispatchByTime, NWorld)
REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFDispatchWithLink, NWorld)
REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFDispatchRockmanMace, NWorld)
REGISTER_WORLD_OBJECT_NM(PFDispatchImmediate, NWorld)
REGISTER_WORLD_OBJECT_NM(PFDispatchContinuous, NWorld)
