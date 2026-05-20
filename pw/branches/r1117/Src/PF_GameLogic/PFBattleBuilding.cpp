#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

namespace NWorld
{
class PFAbilityData;
class PFAbilityInstance;
class PFBaseApplicator;
class PFBaseBehaviour;
class PFApplStatus;
class PFApplTaunt;
class PFBehaviourGroup;
class PFDispatchUniformLinearMove;
}

#define PW_LINUX_INLINE_NULL_USER_CAST(TypeName) \
template<> inline TypeName* CastToUserObjectImpl<TypeName>(CObjectBase*, TypeName*, CObjectBase*) { return 0; }
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFAbilityData)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFAbilityInstance)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFBaseApplicator)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFBaseBehaviour)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFApplStatus)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFApplTaunt)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFBehaviourGroup)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFDispatchUniformLinearMove)
#undef PW_LINUX_INLINE_NULL_USER_CAST

#include "PFBattleBuilding.h"

float NormalizeAngle(float angle)
{
  while (angle < 0.0f)
    angle += FP_2PI;
  while (FP_2PI < angle)
    angle -= FP_2PI;
  return angle;
}

float DetectMinAngleDelta(float currentAngle, float targetAngle)
{
  targetAngle = NormalizeAngle(targetAngle);
  float deltaAngle = targetAngle - currentAngle;
  if (fabs(FP_2PI + deltaAngle) < fabs(deltaAngle))
    deltaAngle += FP_2PI;
  if (fabs(deltaAngle - FP_2PI) < fabs(deltaAngle))
    deltaAngle -= FP_2PI;
  return deltaAngle;
}

namespace NWorld
{

PFBattleBuilding::PFBattleBuilding(PFWorld* pWorld, NDb::AdvMapObject const& dbObject)
  : PFBuilding(pWorld, dbObject)
  , baseAngle(0.0f)
  , currentAngle(0.0f)
  , angleSpeed(0.0f)
  , maxAngleSpeed(0.0f)
  , angleAcceleration(0.0f)
  , deltaAngle(0.0f)
  , targetingTolerance(0.0f)
  , lastAngleResetDelay(-1.0f)
  , lastAngleResetDelayTime(0.0f)
  , accelerate(true)
  , controlTurret(false)
  , forceTargetAngle(false)
  , forcedAngle(0.0f)
{
}

void PFBattleBuilding::SetTurretParams(NDb::TurretParams const& params)
{
  maxAngleSpeed = ToRadian(params.turretAngleSpeed);
  targetingTolerance = fabs(ToRadian(params.turretRotaionTolerance));
  angleAcceleration = ToRadian(params.turretAngleAcceleration);
  lastAngleResetDelayTime = params.turretLastAngleResetDelay;
}

void PFBattleBuilding::SetBaseAngle(float angle)
{
  baseAngle = ToRadian(angle);
  currentAngle = NormalizeAngle(baseAngle);
}

void PFBattleBuilding::OnAfterReset() { PFBuilding::OnAfterReset(); }

float PFBattleBuilding::GetTargetAngle() const
{
  if (forceTargetAngle)
    return forcedAngle;

  CPtr<PFBaseUnit> pTarget = GetCurrentTarget();
  if (IsUnitValid(pTarget))
  {
    CVec3 direction = pTarget->GetPosition() - GetPosition();
    direction.z = 0.0f;
    return atan2(direction.x, -direction.y);
  }

  return lastAngleResetDelay < 0.0f ? baseAngle : currentAngle;
}

bool PFBattleBuilding::IsReadyToAttack() const { return PFBuilding::IsReadyToAttack() && IsInTargetPosition(); }
bool PFBattleBuilding::IsInTargetPosition() const { return fabs(deltaAngle) <= targetingTolerance; }
bool PFBattleBuilding::IsInBasePosition() const { return fabs(DetectMinAngleDelta(currentAngle, baseAngle)) < EPS_VALUE; }

bool PFBattleBuilding::Step(float dtInSeconds)
{
  if (0.0f <= lastAngleResetDelay)
    lastAngleResetDelay -= dtInSeconds;

  if (controlTurret)
  {
    const float targetAngle = NormalizeAngle(GetTargetAngle());
    deltaAngle = DetectMinAngleDelta(currentAngle, targetAngle);
    const float maxStep = Max(0.0f, maxAngleSpeed) * dtInSeconds;
    if (fabs(deltaAngle) <= Max(targetingTolerance, maxStep))
    {
      currentAngle = targetAngle;
      deltaAngle = 0.0f;
      angleSpeed = 0.0f;
    }
    else
    {
      const float step = Min(maxStep, fabs(deltaAngle)) * (deltaAngle < 0.0f ? -1.0f : 1.0f);
      currentAngle = NormalizeAngle(currentAngle + step);
      deltaAngle -= step;
    }
  }

  return PFBuilding::Step(dtInSeconds);
}

void PFBattleBuilding::OnTarget(const CPtr<PFBaseUnit>& pTarget, bool bStrongTarget) { AssignTarget(pTarget, bStrongTarget); }
void PFBattleBuilding::OnTargetAssigned() { deltaAngle = DetectMinAngleDelta(currentAngle, GetTargetAngle()); }
void PFBattleBuilding::ForceTargetAngle(bool force, float angle) { forceTargetAngle = force; forcedAngle = angle; OnTargetAssigned(); }

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFBattleBuilding, NWorld)

#else


#include "PFBattleBuilding.h"

#ifndef VISUAL_CUTTED
#include "PFClientBuilding.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif


float NormalizeAngle( float angle )
{
  while( angle < 0 )
    angle += FP_2PI;
  while( FP_2PI < angle )
    angle -= FP_2PI;

  return angle;
}

float DetectMinAngleDelta( float currentAngle, float targetAngle )
{
  targetAngle       = NormalizeAngle( targetAngle );
  float deltaAngle  = targetAngle - currentAngle;

  // (2Pi - d`) -> d`` transfer
  if( fabs( FP_2PI + deltaAngle) < fabs(deltaAngle) )
    deltaAngle += FP_2PI;

  // d`` -> (2Pi - d`) transfer
  if( fabs( deltaAngle - FP_2PI ) < fabs(deltaAngle) )
    deltaAngle -= FP_2PI;

  return deltaAngle;
}

namespace NWorld
{

  PFBattleBuilding::PFBattleBuilding(PFWorld* pWorld, NDb::AdvMapObject const& dbObject)
    : PFBuilding(pWorld, dbObject)
    , currentAngle(0.0f)
    , angleSpeed(0.0f)
    , maxAngleSpeed(0.0f)
    , angleAcceleration(0.0f)
    , deltaAngle(0.0f)
    , targetingTolerance(0.0f)
    , lastAngleResetDelay(-1.0f)
    , accelerate( true )
    , baseAngle(0.0f)
    , controlTurret(false)
    , forceTargetAngle(false)
    , forcedAngle(0.0f)
  {
  }

  void PFBattleBuilding::SetTurretParams(NDb::TurretParams const& params)
  {
    maxAngleSpeed           = ToRadian(params.turretAngleSpeed);
    targetingTolerance      = fabs( ToRadian(params.turretRotaionTolerance) );
    angleAcceleration       = ToRadian(params.turretAngleAcceleration);
    lastAngleResetDelayTime = params.turretLastAngleResetDelay;

    CALL_CLIENT_2ARGS(OnRotationChanged ,currentAngle, true);
  }

  void PFBattleBuilding::SetBaseAngle(float angle)
  {
    baseAngle    = ToRadian(angle);
    currentAngle = baseAngle;

    CALL_CLIENT_2ARGS(OnRotationChanged, currentAngle, true);
  }

  void  PFBattleBuilding::OnAfterReset()
  {
    PFBuilding::OnAfterReset();
    CALL_CLIENT_2ARGS(OnRotationChanged, currentAngle, true);
  }

  float PFBattleBuilding::GetTargetAngle()  const
  {
    if( forceTargetAngle )  
      return forcedAngle;

    CPtr<PFBaseUnit> pTarget = GetCurrentTarget();
    if( IsUnitValid( pTarget ) )
    {
      CVec3 direction = pTarget->GetPosition() - GetPosition();
      direction.z     = 0.0f;

      return atan2(direction.x, -direction.y);
    }

    return lastAngleResetDelay < 0 ? baseAngle : currentAngle;
  }

  bool  PFBattleBuilding::IsReadyToAttack() const
  {
    return PFBuilding::IsReadyToAttack() && IsInTargetPosition();
  }

  bool  PFBattleBuilding::IsInTargetPosition() const
  {
    return fabs(deltaAngle) < targetingTolerance;
  }

  bool  PFBattleBuilding::IsInBasePosition() const
  {
    const float angle = NormalizeAngle( baseAngle );
    const float delta = DetectMinAngleDelta(currentAngle, angle );

    return fabs(delta) < EPS_VALUE;
  }

  bool  PFBattleBuilding::Step(float dtInSeconds)
  {
    NI_PROFILE_FUNCTION;
    
    if( 0 <= lastAngleResetDelay )
      lastAngleResetDelay -= dtInSeconds;

    if( controlTurret )
    {
      const float targetAngle = NormalizeAngle( GetTargetAngle() );
      const float newDelta    = DetectMinAngleDelta(currentAngle, targetAngle );

      const float fullDecelerateTime = EPS_VALUE < angleAcceleration ? angleSpeed / angleAcceleration : 0.0f;
      const float fullDistance       = angleSpeed * fullDecelerateTime - 0.5f * angleAcceleration * pow(fullDecelerateTime, 2.0f) ;

      const float targetDisplacement = fullDecelerateTime * fabs(deltaAngle) / dtInSeconds;

      accelerate = fullDistance < ( fabs(deltaAngle) +  targetDisplacement);     
      deltaAngle = newDelta;

      angleSpeed = Clamp( angleSpeed + angleAcceleration * dtInSeconds * ( accelerate ? 1.0f: -1.0f ), 0.0f,maxAngleSpeed );

      if( fabs(deltaAngle) < EPS_VALUE )
      {
        currentAngle = targetAngle;
        deltaAngle   = 0.0f;
        angleSpeed   = 0.0f;
      }
      else  
      {
        float delta = min( dtInSeconds * angleSpeed, fabs(deltaAngle) ) * ( 0 < deltaAngle ? 1.0f : -1.0f );
        currentAngle += delta;
        deltaAngle   += delta;
      }

      currentAngle = NormalizeAngle(currentAngle);
      CALL_CLIENT_1ARGS(OnRotationChanged, currentAngle);
    }

    return PFBuilding::Step(dtInSeconds);
  }

  void PFBattleBuilding::OnTarget( const CPtr<PFBaseUnit>& pTarget, bool bStrongTarget )
  {
    if ( IsTargetInAttackRange( pTarget ) )
    {
      AssignTarget( pTarget, bStrongTarget );
    }
    PFBuilding::OnTarget( pTarget, bStrongTarget );
  }

  void PFBattleBuilding::OnTargetAssigned()
  {
    if( controlTurret )
    {
      const float targetAngle = NormalizeAngle( GetTargetAngle() );
      const float newDelta    = DetectMinAngleDelta(currentAngle, targetAngle );
      deltaAngle = newDelta;
    }
  }

  void PFBattleBuilding::ForceTargetAngle( bool force, float angle)
  {
    forcedAngle      = angle;
    forceTargetAngle = force;

    OnTargetAssigned();
  }

};

REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFBattleBuilding, NWorld);

#endif
