#include "stdafx.h"
#include "PFDispatchCursorClient.h"
#include "../System/RandomGen.h"
#include "../PF_Core/EffectsPool.h"
#include "../PF_Core/RecolorableTexture.h"
#include "PFBaseUnit.h"
#include "PFClientBaseUnit.h"
#include "../Core/Scheduler.h"

#ifndef VISUAL_CUTTED
#include "PFClientApplicators.h"
#else
#include "../Game/pf/Audit/ClientStubs.h"
#endif

#include "../Render/debugrenderer.h"
#include "../Render/AOERenderer.h"

#include "AdventureScreen.h"
#include "../Scene/AnimatedSceneComponent.h"
#include "../Scene/AnimGraphApplicator.hpp"
#include "../Render/SkeletalMesh.h"
#include "PFClientVisibilityMap.h"
#include "PFHero.h"

namespace NGameX
{

static NDb::EffectBase const *GetDispatchEffectDB( const NWorld::PFDispatch* dispatch )
{
  if (!IsValid(dispatch))
  {
    return NULL;
  }

  int teamID = dispatch->GetSender()->GetOriginalTeamId();
  if (teamID == -1)
  {
    teamID = NDb::TEAMID_A;
  }
  if ( dispatch->GetDBBase()->dispatchEffect[teamID].IsEmpty() )
  {
    teamID = teamID == NDb::TEAMID_B ? NDb::TEAMID_A : NDb::TEAMID_B;
  }

  return dispatch->GetDBBase()->dispatchEffect[teamID];
}

PFDispatchCursorClient::PFDispatchCursorClient( PF_Core::WorldObjectRef pWO, NScene::IScene *pScene )
: NWorld::PFClientObjectBase(pWO)
, pScene(pScene)
, dest(VNULL3)
, currentPos(VNULL3)
, startMatrix(QNULL, VNULL3)
, updateCursorTime( 0.0f )
, needSendCommandUpdateCursor( false )
, aoeIsVisible(false)
, clientDir( 0.0f, 1.0f )
, clientDirInited( false )
, lastLogicPos( VNULL2 )
, lastLogicAge( 0.0f )
, curYaw( 0.0f )
, bend( 0.0f )
, bendInited( false )
, pBendSkeleton( 0 )
{
  NGameX::AdventureScreen* advScreen = NGameX::AdventureScreen::Instance();
  if ( advScreen )
  {
    if ( NWorld::PFBaseHero* hero = dynamic_cast<NWorld::PFBaseHero*>(WorldObject()->GetSender().GetPtr()) )
      needSendCommandUpdateCursor = hero->IsLocal();
  }
  bool bOk = GetStartPos(currentPos, startMatrix);
  NI_ASSERT(bOk, NStr::StrFmt("Failed to retrieve dispatch start pos for %s !", GetFormattedDbId(WorldObject()->GetDB().GetDBID()).c_str()) );
  Start();
}

void PFDispatchCursorClient::OnWorldObjectDie()
{
  if ( pBendSkeleton )
  {
    pBendSkeleton->ClearExtraYaw();   // the effect goes back to the pool
    pBendSkeleton = 0;
  }

  if (pEffect)
  {
    pEffect->Die();
    pEffect = 0;
  }

  if ( pLinkEffect )
  {
    pLinkEffect->Die();
    pLinkEffect = 0;
  }

  if ( aoeIsVisible )
  {
    aoeIsVisible = false;
    if (Render::AOERenderer* pAOE = Render::GetAOERenderer())
      pAOE->ResetArea(Render::AOERenderer::CHANNEL_RANGE2);
  }

  PFClientObjectBase::OnWorldObjectDie();
}

void PFDispatchCursorClient::Update( float timeDelta )
{
  updateCursorTime += timeDelta;

  float updateFrequency = WorldObject()->GetDB().cursorUpdateFrequency;
  if ( updateFrequency * pScene->GetStepLengthInSeconds() < updateCursorTime )
  {
    UpdateCursor();
    updateCursorTime = 0.0f;
  }

  if ( !pEffect )
  {
    return;
  }

  RetrieveTargetCoords();

  if ( WorldObject()->GetDB().turnRate > 0.0f )
  {
    UpdateSteered( timeDelta );
    return;
  }

  const float linkRange = WorldObject()->GetDB().linkRange;

  CVec2 delta2d = ( dest - currentPos ).AsVec2D();
  float dist2d = fabs( delta2d );
  float speed = dist2d * WorldObject()->GetSpeed();
  speed = Min( speed, WorldObject()->GetMaxSpeed() );
  Normalize( &delta2d );

  CVec2 source = WorldObject()->GetSender()->ClientObject()->GetPosition().pos.AsVec2D();
  CVec2 newPos = currentPos.AsVec2D() + delta2d * speed * timeDelta;
  CVec2 dirFromSender = newPos - source;

  bool showLinkEffect = false;
  if ( linkRange > EPS_VALUE && fabs2( dirFromSender ) > fabs2( linkRange ) )
  {
    Normalize( &dirFromSender );
    newPos = dirFromSender * linkRange + source;
    showLinkEffect = true;
  }

  currentPos = CVec3( newPos, 0.0f );

  pScene->GetHeightsController().GetHeight( currentPos.x, currentPos.y, 1, &currentPos.z, 0 );
  currentPos.z += WorldObject()->GetDB().height;

  Placement placement;
  placement.pos = currentPos;
  pEffect->SetPosition(placement);

  const NDb::Ptr<NDb::LightningEffect>& pDBLinkEffect = WorldObject()->GetDB().linkEffect;

  if ( showLinkEffect && !pLinkEffect && pDBLinkEffect )
  {
    pLinkEffect = NGameX::CreateLightningEffect( pDBLinkEffect, WorldObject()->GetSender()->GetSkinId() );
    if ( pLinkEffect )
      pLinkEffect->Apply( this, WorldObject()->GetSender()->ClientObject() ); // considering sender is actually a source
  }
  else if ( !showLinkEffect && pLinkEffect )
  {
    pLinkEffect->Die();
    pLinkEffect = 0;
  }

  UpdateVisibility( GetSceneObject(), this );

  NDb::Ptr<NDb::AOEMaterial> const& pDBMaterial = WorldObject()->GetDB().maxRangeMaterial;
  Render::AOERenderer* pAOE = Render::GetAOERenderer();
  if ( pDBMaterial && pAOE )
  {
    if ( showLinkEffect )
    {
      if( NWorld::PFBaseHero* hero = dynamic_cast<NWorld::PFBaseHero*>( WorldObject()->GetSender().GetPtr() ) )
      {
        if( hero->IsLocal() )
        {
          if ( !aoeIsVisible )
          {
            aoeIsVisible = true;
            pAOE->SetAreaParams( Render::AOERenderer::CHANNEL_RANGE2, Render::AOERenderer::AREA_DISK, pDBMaterial, linkRange + WorldObject()->GetDB().size, 0 );
          }
          if ( PFClientLogicObject const* pClient = WorldObject()->GetSender()->ClientObject() )
          {
            pAOE->SetAreaPosition( Render::AOERenderer::CHANNEL_RANGE2, pClient->GetPosition().pos, CVec3( dirFromSender, 0.0f ), true );
          }
        }
      }
    }
    else if ( aoeIsVisible )
    {
      aoeIsVisible = false;
      pAOE->ResetArea(Render::AOERenderer::CHANNEL_RANGE2);
    }
  }
}

namespace
{
  const float s_steeredBodyLength = 8.0f;   // the effect is turned along the path over this length (the whale body)
  const float s_steeredYawSpeed = 4.0f;     // yaw smoothing, 1/s
  const float s_steeredPathStep = 0.25f;

  float SteeredYaw( const CVec2& dir )
  {
    float yaw = FP_PI2 + atan2( dir.y, dir.x );
    if ( yaw > FP_PI )
      yaw -= FP_2PI;
    return yaw;
  }
}

// Same motion as the logic (PFDispatchCursor::StepSteered) run every frame and pulled softly to the logic position
// extrapolated between logic steps (the logic position itself lags and jumps by steps). The effect origin (the head)
// goes along the path and the effect is turned from the path point one body length back to the head, so the body follows
// the head's path instead of swinging around it.
void PFDispatchCursorClient::UpdateSteered( float timeDelta )
{
  const NWorld::PFDispatchCursor* pWorldObject = WorldObject();
  const float maxSpeed = pWorldObject->GetMaxSpeed();
  if ( !clientDirInited )
  {
    clientDir = pWorldObject->GetHeading();
    clientDirInited = true;
    lastLogicPos = pWorldObject->GetCurPos();
    lastLogicAge = 0.0f;
    curYaw = SteeredYaw( clientDir );
    path.clear();
    path.push_back( currentPos.AsVec2D() );
  }

  CVec2 want = dest.AsVec2D() - currentPos.AsVec2D();
  bool reachable = true;
  {
    float const omega = pWorldObject->GetDB().turnRate * ( FP_PI / 180.0f );
    float const radius = maxSpeed / omega;
    float const side = ( clientDir.x * want.y - clientDir.y * want.x ) >= 0.0f ? radius : -radius;
    CVec2 const center = currentPos.AsVec2D() + CVec2( -clientDir.y, clientDir.x ) * side;
    reachable = fabs2( dest.AsVec2D() - center ) >= radius * radius;
  }
  if ( reachable && fabs2( want ) > EPS_VALUE )
  {
    float const cur = atan2( clientDir.y, clientDir.x );
    float diff = atan2( want.y, want.x ) - cur;
    while ( diff > FP_PI ) diff -= FP_2PI;
    while ( diff < -FP_PI ) diff += FP_2PI;
    float const maxTurn = pWorldObject->GetDB().turnRate * ( FP_PI / 180.0f ) * timeDelta;
    diff = Clamp( diff, -maxTurn, maxTurn );
    clientDir = CVec2( cos( cur + diff ), sin( cur + diff ) );
  }

  CVec2 const logicPos = pWorldObject->GetCurPos();
  if ( fabs2( logicPos - lastLogicPos ) > EPS_VALUE )
  {
    lastLogicPos = logicPos;
    lastLogicAge = 0.0f;
  }
  else
    lastLogicAge += timeDelta;
  CVec2 const predicted = logicPos + pWorldObject->GetHeading() * ( maxSpeed * Min( lastLogicAge, 0.3f ) );

  CVec2 newPos = currentPos.AsVec2D() + clientDir * ( maxSpeed * timeDelta );
  newPos += ( predicted - newPos ) * Min( 1.0f, timeDelta * 1.5f );

  currentPos = CVec3( newPos, 0.0f );
  pScene->GetHeightsController().GetHeight( currentPos.x, currentPos.y, 1, &currentPos.z, 0 );
  currentPos.z += pWorldObject->GetDB().height;

  // remember the path and find the point one body length back along it
  if ( path.empty() || fabs2( newPos - path.back() ) > s_steeredPathStep * s_steeredPathStep )
  {
    path.push_back( newPos );
    if ( path.size() > 400 )
      path.erase( path.begin() );
  }
  CVec2 tail = newPos;
  float left = s_steeredBodyLength;
  CVec2 prev = newPos;
  for ( int i = path.size() - 1; i >= 0 && left > 0.0f; --i )
  {
    float const seg = fabs( path[i] - prev );
    if ( seg >= left )
    {
      tail = prev + ( path[i] - prev ) * ( left / seg );
      left = 0.0f;
      break;
    }
    left -= seg;
    prev = path[i];
    tail = prev;
  }
  CVec2 bodyDir = newPos - tail;
  if ( left > 0.0f || fabs2( bodyDir ) < 0.01f )
    bodyDir = clientDir;   // path is still shorter than the body: just the movement direction

  float diff = SteeredYaw( bodyDir ) - curYaw;
  while ( diff > FP_PI ) diff -= FP_2PI;
  while ( diff < -FP_PI ) diff += FP_2PI;
  float const yawStep = diff * Min( 1.0f, timeDelta * s_steeredYawSpeed );
  curYaw += yawStep;
  if ( curYaw > FP_PI ) curYaw -= FP_2PI;
  if ( curYaw < -FP_PI ) curYaw += FP_2PI;

  ApplyBend( timeDelta, timeDelta > EPS_VALUE ? yawStep / timeDelta : 0.0f );

  Placement placement;
  placement.pos = currentPos;
  placement.rot.FromEulerAngles( curYaw, 0.0f, 0.0f );
  pEffect->SetPosition( placement );

  UpdateVisibility( GetSceneObject(), this );
}

// Procedural bend of the whale body while turning: head and tail groups of bones are turned about the body centre
// into the turn (the body lies along the arc of the path). Bend = half of the arc angle of the body: 0.5 * L * yawRate / v.
void PFDispatchCursorClient::ApplyBend( float timeDelta, float yawRate )
{
  if ( !bendInited )
  {
    NScene::SceneObject* pSO = GetSceneObject();
    if ( !pSO )
      return;   // try again next frame
    bendInited = true;
    struct Finder : public NScene::FullTraverser
    {
      NScene::AnimatedSceneComponent* asc;
      Finder() : asc( 0 ) {}
      void operator()( NScene::SceneComponent *pSC )
      {
        if ( !asc )
          asc = dynamic_cast<NScene::AnimatedSceneComponent*>( pSC );
      }
    } finder;
    pSO->Traverse( finder );
    if ( !finder.asc || !finder.asc->GetGraphApplicator() || !finder.asc->GetGraphApplicator()->GetModel() )
      return;
    Render::SkeletonWrapper* pSkel = finder.asc->GetGraphApplicator()->GetModel()->GetSkeletonWrapper();
    if ( !pSkel )
      return;
    // weight: + head (turns into the turn), - tail (behind the pivot, so also into the turn). The bend is split over
    // several pivots along the body (each bone group turns a little about several points) so the body makes a smooth arc
    struct Part { const char* name; const char* pivot; float weight; };
    const Part parts[] = {
      { "Spine04_M", "Chest_M", 0.5f }, { "Spine05_M", "Spine04_M", 0.5f },
      { "BackA_M", "Chest_M", -0.4f }, { "BackA_M", "BackB_M", -0.4f },
      { "CTail00_R", "Chest_M", -0.4f }, { "CTail00_R", "BackB_M", -0.4f }, { "CTail00_R", "root", -0.4f },
      { "CTail00_L", "Chest_M", -0.4f }, { "CTail00_L", "BackB_M", -0.4f }, { "CTail00_L", "root", -0.4f } };
    for ( int i = 0; i < sizeof( parts ) / sizeof( parts[0] ); ++i )
    {
      unsigned int bone = 0, pivot = 0;
      CVec3 pivotPos;
      if ( !pSkel->GetJointIndexByName( parts[i].name, bone ) || !pSkel->GetJointIndexByName( parts[i].pivot, pivot ) ||
           !pSkel->GetBindPosition( pivot, pivotPos ) )
        continue;
      Render::SkeletonWrapper::ExtraYaw e;
      e.bone = bone;
      e.angle = parts[i].weight;   // weight is kept here, angle = weight * bend is set every frame
      e.pivot = pivotPos;
      bendYaw.push_back( e );
    }
    if ( bendYaw.empty() )
      return;   // not the whale skeleton: no bend
    pBendSkeleton = pSkel;
  }
  if ( !pBendSkeleton )
    return;

  const float bodyLength = 7.0f;
  const float maxBend = 0.15f;
  float const speed = Max( WorldObject()->GetMaxSpeed(), 0.1f );
  float const target = Clamp( 0.5f * bodyLength * yawRate / speed, -maxBend, maxBend );
  bend += ( target - bend ) * Min( 1.0f, timeDelta * 3.0f );

  nstl::vector<Render::SkeletonWrapper::ExtraYaw> yaw = bendYaw;
  for ( int i = 0; i < yaw.size(); ++i )
    yaw[i].angle = bendYaw[i].angle * bend;
  pBendSkeleton->SetExtraYaw( yaw );
}

bool PFDispatchCursorClient::GetStartPos( CVec3& result, Matrix43& matrix ) const
{
  const NWorld::PFDispatchCursor* pWorldObject = WorldObject();
  if( NULL == pWorldObject )
    return false;

  CPtr<NWorld::PFBaseUnit> const pSender = pWorldObject->GetSender();
  if( !IsValid( pSender ) )
    return false;

  if ( pWorldObject->GetSource().IsPosition() )
  {
    result = pWorldObject->GetSource().GetPosition();
    return true;
  }

  CPtr<NWorld::PFBaseUnit> const pSource = pWorldObject->GetSource().GetUnit();
  if( !IsValid( pSource ) )
    return false;

  PFClientLogicObject* pClient = pSource->ClientObject();
  if( NULL == pClient)
    return false;

  NScene::SceneObject *pSO = pClient->GetSceneObject();
  if ( NULL == pSO )
    return false;

  result = pSO->GetPosition().pos;
  if ( pSender == pSource )
  {
    if ( NScene::Locator const* pL = pSO->FindLocator( pWorldObject->GetDB().fromLocator.c_str() ) )
    {
      matrix = Matrix43(QNULL, result);
      pSO->CalculateLocatorWorldMatrix(*pL, matrix, pClient->IsVisible() );
      result.Set(matrix._14, matrix._24, matrix._34);
      const NWorld::Target& target = WorldObject()->GetTarget();
      if ( target.IsPosition() )
      {
        if ( fabs(pSO->GetPosition().pos - target.GetPosition()) < fabs(pSO->GetPosition().pos - result) )
        {
          result = pSO->GetPosition().pos;
        }
      }
    }
  }

  NI_ASSERT( result.IsFinite(), NStr::StrFmt("Invalid dispatch start pos. Retrieved from %s !", pSO->FormatDescription()) );
  NI_ASSERT( matrix.IsFinite(), NStr::StrFmt("Invalid dispatch start matrix. Retrieved from %s !", pSO->FormatDescription()) );

  return true;
}

void PFDispatchCursorClient::Start()
{
  UpdateCursor();
  const NWorld::PFDispatchCursor* pWorldObject = WorldObject();
  NWorld::PFBaseUnit const* pSender = pWorldObject->GetSender().GetPtr();
  NDb::EffectBase const *dispatchEffect = GetDispatchEffectDB(pWorldObject);

  if ( !dispatchEffect )
  {
    Subscribe();
    return;
  }

  GetStartPos(currentPos, startMatrix);
  RetrieveTargetCoords();
  Placement placement;
  placement.pos = currentPos;

  pEffect = RetrieveStandaloneEffect( dispatchEffect, pSender );
  if (!pEffect)
    return;

  pEffect->SetPosition(placement);
  pEffect->AddToScene(pScene);
  InitVisibility( GetSceneObject() );

  if (pEffect->GetSceneObject() &&
    IsValid(pSender->ClientObject()))
  {
    PF_Core::RecolorSceneObject(*pEffect->GetSceneObject(), pSender->ClientObject()->GetRecolor());
  }

  UpdateVisibility( GetSceneObject(), this );

  // Subscribe for updates
  Subscribe();
}

void PFDispatchCursorClient::RetrieveTargetCoords()
{
  NI_ASSERT( IsValid( WorldObject() ), "World dispatch should be valid" );
  dest = WorldObject()->GetTarget().AcquirePosition();
  pScene->GetHeightsController().GetHeight( dest.x, dest.y, 1, &dest.z, 0 );
  dest.z += WorldObject()->GetDB().height;
}

NScene::SceneObject * PFDispatchCursorClient::GetSceneObject() const
{
  if (!IsValid(pEffect))
  {
    return NULL;
  }
  return pEffect->GetSceneObject();
}

void PFDispatchCursorClient::UpdateCursor()
{
  if ( !needSendCommandUpdateCursor )
    return;

  NGameX::AdventureScreen* advScreen = NGameX::AdventureScreen::Instance();
  if ( advScreen )
  {
    NCore::WorldCommand * cmd = NWorld::CreateCmdUpdateCursorPos( const_cast<NWorld::PFDispatchCursor *>( WorldObject() ), advScreen->GetCurrentCursorPosOnTerrain().AsVec2D() );
    advScreen->SendGameCommand( cmd, true );
  }
}
}

