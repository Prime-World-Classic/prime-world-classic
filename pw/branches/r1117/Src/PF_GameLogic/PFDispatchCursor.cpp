#include "stdafx.h"
#include "PFDispatchCursor.h"
#include "TileMap.h"
#if defined(PW_LINUX_NULL_RENDER)
#include "PFWorld.h"
#include "PFPlayer.h"
#include <cmath>
#else
#include "PFDispatchCursorClient.h"
#endif
#include "PFBaseUnit.h"
#include "../Core/WorldCommand.h"
#include "PFHero.h"

namespace NWorld
{

PFDispatchCursor::PFDispatchCursor( CreateParams const &cp )
: Base(cp)
, lastTargetPos( cp.target.AcquirePosition().AsVec2D() )
{
  duration = RetrieveParam( GetDB().lifeTime );
  curPos = cp.source.AcquirePosition().AsVec2D();
  speed = RetrieveParam( GetDB().speed );
  maxSpeed = RetrieveParam( GetDB().maxSpeed );
  m_startTime = GetWorld()->GetTimeElapsed();
}

void PFDispatchCursor::Reset()
{
  //TODO!!!
}

void PFDispatchCursor::OnStart()
{
  UpdatePersistentApplicatorsPosition();
  Base::OnStart();
  lastTargetPos = target.AcquirePosition().AsVec2D();
  originalSenderPos = curPos;
}

void PFDispatchCursor::CreateClientDispatch()
{
#if !defined(PW_LINUX_NULL_RENDER)
  CreateClientObject<TClientObject>( GetWorld()->GetScene() );
#endif
}

void PFDispatchCursor::UpdatePersistentApplicatorsPosition()
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

bool PFDispatchCursor::Step(float dtInSeconds)
{
#if defined(PW_LINUX_NULL_RENDER)
	if (!std::isfinite(dtInSeconds) || dtInSeconds < 0)
		return false;
#endif
  NI_PROFILE_FUNCTION

    if ( IsObjectDead() )
      return false;

  if (state == STATE_PROCEED)
  {
#if defined(PW_LINUX_NULL_RENDER)
		if (!std::isfinite(duration))
			return false;
#endif
    bool time2Die = false;
    if (IsUnitValid(pSender))
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

#if defined(PW_LINUX_NULL_RENDER)
		// Invalid travel must not suspend the original lifetime or persistent effects.
		const CVec2 from = source.AcquirePosition().AsVec2D();
		const CVec2 to = target.AcquirePosition().AsVec2D();
		if (!std::isfinite(maxSpeed) || maxSpeed < 0 || !std::isfinite(dtInSeconds * maxSpeed) ||
			!curPos.IsFinite() || !std::isfinite(GetDB().linkRange) || GetDB().linkRange < 0 ||
			!from.IsFinite() || !to.IsFinite() || !std::isfinite(fabs2(to - curPos)) ||
			!std::isfinite(fabs2(to - from)) || !std::isfinite(fabs2(curPos - from)))
		{
			if (curPos.IsFinite()) UpdatePersistentApplicatorsPosition();
			Base::Step(dtInSeconds);
			return false;
		}
#endif
    float const linkRange = GetDB().linkRange;
    CVec2 const senderPos = source.AcquirePosition().AsVec2D();
    CVec2 receiverPos = target.AcquirePosition().AsVec2D();

    NI_ASSERT(curPos.IsFinite(), "Bad curpos");

    lastTargetPos = receiverPos;

    CVec2 const delta = receiverPos - curPos;
    float const deltaLength = fabs(delta);

    // calculate new world pos
    if ( deltaLength > EPS_VALUE )
    {
      float const maxMove = dtInSeconds * maxSpeed;

      float desiredMove = maxMove;
      if (deltaLength <= maxMove)
      {
        desiredMove = deltaLength;
      }

      CVec2 newPos = curPos + delta * ( desiredMove / deltaLength );
      NI_ASSERT( newPos.IsFinite(), "Bad curpos" );

      TileMap* pMap = GetWorld()->GetTileMap();
#if defined(PW_LINUX_NULL_RENDER)
			if (!pMap || !newPos.IsFinite() || !std::isfinite(pMap->GetTileSize()) || pMap->GetTileSize() <= 0 ||
				newPos.x < 0 || newPos.y < 0 || double(newPos.x) / pMap->GetTileSize() >= pMap->GetSizeX() ||
				double(newPos.y) / pMap->GetTileSize() >= pMap->GetSizeY())
			{
				UpdatePersistentApplicatorsPosition();
				Base::Step(dtInSeconds);
				return false;
			}
#endif
      SVector tile = pMap->GetTile( newPos );
      if ( !pMap->IsPointOutsideMap( tile.x, tile.y ) )
      {
        CVec2 dirFromSender = newPos - senderPos;
        if ( linkRange > EPS_VALUE && fabs2( dirFromSender ) > fabs2( linkRange ) )
        {
          Normalize( &dirFromSender );
          newPos = dirFromSender * linkRange + senderPos;
        }
        curPos = newPos;
      }
    }
    else if ( linkRange > EPS_VALUE && fabs2( curPos - senderPos ) > fabs2( linkRange ) )
    {
      CVec2 dirFromSender = receiverPos - senderPos;
      Normalize( &dirFromSender );
      curPos = dirFromSender * linkRange + senderPos;
    }
    // show logical dispatch pos
    //float terrainHeight;
    //GetWorld()->GetScene()->GetHeightsController().GetHeight( curPos.x, curPos.y, 1, &terrainHeight, NULL );
    //Render::DebugRenderer::DrawPoint3D( CVec3( curPos, terrainHeight) , 0.5, Render::Color( 0xFF ), false );
  }

  UpdatePersistentApplicatorsPosition();
  Base::Step(dtInSeconds);

  if ( IsObjectDead() )
    return false;

  return false;
}

void PFDispatchCursor::UpdateCursorPos( CVec2 pos )
{
#if defined(PW_LINUX_NULL_RENDER)
	if (IsObjectDead() || HasArrived() || !pos.IsFinite())
		return;
#endif
  target.SetPosition( CVec3(pos.x,pos.y,0.0f) );
}

DEFINE_2_PARAM_CMD_CHECK( 0x8D85F480, CmdUpdateCursorPos, CPtr<PFDispatchCursor>, pDispatch, CVec2, pos);

NCore::WorldCommand* CreateCmdUpdateCursorPos(PFDispatchCursor *pDispatch, CVec2 pos)
{
  NI_VERIFY(NULL != pDispatch, "Dispatch object should exists", return 0);
  return new CmdUpdateCursorPos( pDispatch, pos );
}

bool CmdUpdateCursorPos::CanExecute() const
{
#if defined(PW_LINUX_NULL_RENDER)
	if (!IsValid(pDispatch) || pDispatch->IsObjectDead() || !pDispatch->Started() ||
		pDispatch->HasArrived() || !pos.IsFinite())
		return false;
	PFWorld* world = pDispatch->GetWorld();
	PFBaseHero* hero = dynamic_cast<PFBaseHero*>(pDispatch->GetSender().GetPtr());
	if (!world || !IsUnitValid(hero) || hero->GetWorld() != world ||
		world->FindLinuxUnitByObjectId(hero->GetObjectId()) != hero || !world->IsSubscribed(pDispatch.GetPtr()) ||
		world->GetObjectById(pDispatch->GetObjectId()) != pDispatch.GetPtr())
		return false;
	const PFPlayer* player = hero->GetPlayer();
	return IsValid(player) && player->GetWorld() == world && player->GetHero() == hero &&
		(player->GetUserID() == GetId() || player->IsBot());
#else
  if ( IsValid(pDispatch) )
    if ( NWorld::PFBaseHero* hero = dynamic_cast<NWorld::PFBaseHero*>( pDispatch->GetSender().GetPtr() ) )
      if ( const NWorld::PFPlayer * pPlayer = hero->GetPlayer() )
        if ( pPlayer->GetUserID() == GetId() || pPlayer->IsBot() )
          return true;

  return false;
#endif
}

#if defined(PW_LINUX_NULL_RENDER)
/** Recheck command ownership after queueing; never resolve a replacement actor. */
void CmdUpdateCursorPos::Execute(NCore::IWorldBase* world)
{
	if (CanExecute() && world == pDispatch->GetWorld())
		pDispatch->UpdateCursorPos(pos);
}
#else
void CmdUpdateCursorPos::Execute(NCore::IWorldBase *)
{
  if ( IsValid(pDispatch) )
    pDispatch->UpdateCursorPos(pos);
}
#endif

}

REGISTER_SAVELOAD_CLASS_NM( CmdUpdateCursorPos, NWorld )
#if defined(PW_LINUX_NULL_RENDER)
REGISTER_WORLD_OBJECT_NM(PFDispatchCursor, NWorld)
NGameX::PFDispatchCursorClient* NWorld::PFDispatchCursor::ClientObject() const { return nullptr; }
#else
REGISTER_WORLD_OBJECT_WITH_CLIENT_NM( PFDispatchCursor, NWorld)
#endif
