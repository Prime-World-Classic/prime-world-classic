#include "stdafx.h"
#include "SkeletonWrapper.h"

#include "../MeshConverter/SkeletonHeader.h"

// Spring bones tuning from the console: a value >= 0 overrides the chain tables below for all chains
static bool  s_springEnable    = true;
static float s_springStiffness = -1.0f;
static float s_springStiffTip  = -1.0f;
static float s_springDamping   = -1.0f;
static float s_springGravity   = -1.0f;
static float s_springMaxAngle  = -1.0f;
REGISTER_VAR( "anim_spring_enable",    s_springEnable,    STORAGE_NONE );
REGISTER_VAR( "anim_spring_stiffness", s_springStiffness, STORAGE_NONE );
REGISTER_VAR( "anim_spring_stiff_tip", s_springStiffTip,  STORAGE_NONE );
REGISTER_VAR( "anim_spring_damping",   s_springDamping,   STORAGE_NONE );
REGISTER_VAR( "anim_spring_gravity",   s_springGravity,   STORAGE_NONE );
REGISTER_VAR( "anim_spring_max_angle", s_springMaxAngle,  STORAGE_NONE );

namespace
{
  // Spring chains by bone name prefix: every bone of a chain matches, the chain root is the first matching bone.
  // Stiffness and damping are per 1/60 s step, gravity in world units / s^2 along -Z, max angle in degrees from the animated pose.
  struct SpringChainParams
  {
    const char* prefix;
    float stiffness;  // pull to the animated pose at the chain root
    float stiffTip;   // ... and at the chain tip
    float damping;
    float gravity;
    float maxAngle;
  };
  const SpringChainParams s_springChains[] =
  {
    { "CTail", 0.1f, 0.3f, 1.0f, 1.0f, 25.0f },   // Laguna tentacles, tuned in game 2026-10-21
  };
  const int springChainsCount = sizeof( s_springChains ) / sizeof( s_springChains[0] );

  inline float SpringParam( float consoleValue, float chainValue ) { return consoleValue >= 0.0f ? consoleValue : chainValue; }

  const float springStepTime = 1.0f / 60.0f;
  const int   springMaxSteps = 6;
  const float springTeleportDistance = 3.0f;

  inline CVec3 GetTranslation( const Matrix43& m ) { return CVec3( m._14, m._24, m._34 ); }
  inline float SpringDot( const CVec3& a, const CVec3& b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  inline CVec3 SpringCross( const CVec3& a, const CVec3& b ) { return CVec3( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x ); }
  inline float SpringLength( const CVec3& a ) { return sqrtf( SpringDot( a, a ) ); }

  int FindSpringChain( const char* name )
  {
    if ( !name )
      return -1;
    for ( int i = 0; i < springChainsCount; ++i )
      if ( strncmp( name, s_springChains[i].prefix, strlen( s_springChains[i].prefix ) ) == 0 )
        return i;
    return -1;
  }

  // Rotation by angle about a unit axis (Rodrigues), applied to the orientation part of a world matrix around a pivot
  void RotateAround( Matrix43& m, const CVec3& axis, float angle, const CVec3& pivot )
  {
    const float c = cosf( angle ), sn = sinf( angle ), t = 1.0f - c;
    const float x = axis.x, y = axis.y, z = axis.z;
    const float r[3][3] = {
      { t * x * x + c,      t * x * y - sn * z, t * x * z + sn * y },
      { t * x * y + sn * z, t * y * y + c,      t * y * z - sn * x },
      { t * x * z - sn * y, t * y * z + sn * x, t * z * z + c      } };
    const Matrix43 src( m );
    const float in[3][4] = {
      { src._11, src._12, src._13, src._14 - pivot.x },
      { src._21, src._22, src._23, src._24 - pivot.y },
      { src._31, src._32, src._33, src._34 - pivot.z } };
    float out[3][4];
    for ( int i = 0; i < 3; ++i )
      for ( int j = 0; j < 4; ++j )
        out[i][j] = r[i][0] * in[0][j] + r[i][1] * in[1][j] + r[i][2] * in[2][j];
    m._11 = out[0][0]; m._12 = out[0][1]; m._13 = out[0][2]; m._14 = out[0][3] + pivot.x;
    m._21 = out[1][0]; m._22 = out[1][1]; m._23 = out[1][2]; m._24 = out[1][3] + pivot.y;
    m._31 = out[2][0]; m._32 = out[2][1]; m._33 = out[2][2]; m._34 = out[2][3] + pivot.z;
  }
}

namespace Render
{

	SkeletonWrapper::SkeletonWrapper( const SkeletonDataWrapper* _pSkeletonData ) :
    pSkeletonData(_pSkeletonData),
    pSampledMatrix(0),
    pSkinWorldMatrix(0),
    activeBonesCount(0),
    allocatedBonesCount(0),
    springsInitialized(false),
    timeDelta(0.0f),
    springPrevStep(0.0f)
	{
		const unsigned int bonesCount = pSkeletonData->GetData()->jointsCount;

		pSampledMatrix = static_cast<Matrix43*>(Aligned_MAlloc(sizeof(Matrix43)*bonesCount*2, 16));
		pBoneWorldMatrix = pSampledMatrix + bonesCount;

    InitUpdateOrder();
    InitSpringBones();
	}

  void SkeletonWrapper::InitUpdateOrder()
  {
    const unsigned int bonesCount = pSkeletonData->GetData()->jointsCount;
    const unsigned int* pParentIndicies = pSkeletonData->GetData()->parentsIDs.at(0);

    // Bone 0 is the root (see Update); the rest go strictly after their parents
    updateOrder.clear();
    updateOrder.reserve( bonesCount );
    nstl::vector<bool> placed( bonesCount, false );
    if ( bonesCount > 0 )
    {
      updateOrder.push_back( 0 );
      placed[0] = true;
    }
    bool progress = true;
    while ( (unsigned int)updateOrder.size() < bonesCount && progress )
    {
      progress = false;
      for ( unsigned int i = 1; i < bonesCount; ++i )
      {
        if ( placed[i] )
          continue;
        const unsigned int parent = pParentIndicies[i];
        if ( parent >= bonesCount || placed[parent] )
        {
          updateOrder.push_back( i );
          placed[i] = true;
          progress = true;
        }
      }
    }
    // Cycles in the hierarchy: keep the old index order for whatever is left
    for ( unsigned int i = 1; i < bonesCount; ++i )
      if ( !placed[i] )
        updateOrder.push_back( i );
  }

  void SkeletonWrapper::InitSpringBones()
  {
    const unsigned int bonesCount = pSkeletonData->GetData()->jointsCount;
    const unsigned int* pParentIndicies = pSkeletonData->GetData()->parentsIDs.at(0);

    springByBone.clear();
    springByBone.resize( bonesCount, -1 );
    springBones.clear();

    nstl::vector<int> chainOfBone( bonesCount, -1 );
    nstl::vector<bool> isSpring( bonesCount, false );
    for ( unsigned int i = 1; i < bonesCount; ++i )
    {
      chainOfBone[i] = FindSpringChain( GetJointNameByIndex( i ) );
      isSpring[i] = chainOfBone[i] >= 0;
    }

    for ( unsigned int i = 1; i < bonesCount; ++i )
    {
      if ( !isSpring[i] )
        continue;

      SpringBone s;
      s.bone = i;
      s.chain = chainOfBone[i];
      s.child = -1;
      for ( unsigned int c = 1; c < bonesCount; ++c )
        if ( pParentIndicies[c] == i && isSpring[c] )
        {
          s.child = (int)c;
          break;
        }

      // Position in the chain: depth from the chain root and the number of bones below
      int depth = 0;
      for ( unsigned int p = pParentIndicies[i]; p < bonesCount && isSpring[p]; p = pParentIndicies[p] )
        ++depth;
      int below = 0;
      for ( int c = s.child; c >= 0; )
      {
        ++below;
        int next = -1;
        for ( unsigned int k = 1; k < bonesCount; ++k )
          if ( pParentIndicies[k] == (unsigned int)c && isSpring[k] )
          {
            next = (int)k;
            break;
          }
        c = next;
      }
      s.stiffnessCoeff = ( depth + below > 0 ) ? float( depth ) / float( depth + below ) : 0.0f;
      s.tail = s.prevTail = s.lastHead = VNULL3;

      springByBone[i] = springBones.size();
      springBones.push_back( s );
    }
  }

  void SkeletonWrapper::ApplySpringBone( SpringBone& s, Matrix43& boneWorld, const Matrix43& parentWorld, int steps, float stepTime, bool reset )
  {
    const CVec3 head = GetTranslation( boneWorld );

    // Tail of the bone in the animated pose
    CVec3 target;
    if ( s.child >= 0 )
    {
      __declspec(align(16)) Matrix43 childWorld;
      SEEMatrixMultiply( &childWorld, &boneWorld, &pSampledMatrix[s.child] );
      target = GetTranslation( childWorld );
    }
    else
    {
      // Chain tip: continue the direction from the parent bone
      target = head + ( head - GetTranslation( parentWorld ) );
    }

    const CVec3 animDir = target - head;
    const float boneLength = SpringLength( animDir );
    if ( boneLength < 1e-4f )
      return;

    if ( reset || SpringLength( head - s.lastHead ) > springTeleportDistance )
    {
      s.tail = s.prevTail = target;
      s.lastHead = head;
      return;
    }
    s.lastHead = head;

    const SpringChainParams& chain = s_springChains[s.chain];
    const float rootStiffness = SpringParam( s_springStiffness, chain.stiffness );
    const float tipStiffness = SpringParam( s_springStiffTip, chain.stiffTip );
    const float damping = SpringParam( s_springDamping, chain.damping );
    const float gravity = SpringParam( s_springGravity, chain.gravity );
    const float maxAngleDeg = SpringParam( s_springMaxAngle, chain.maxAngle );

    // Coefficients are tuned for 1/60 s steps, convert them to the actual step length
    const float stepRatio = stepTime / springStepTime;
    const float stiffness60 = Clamp( rootStiffness + ( tipStiffness - rootStiffness ) * s.stiffnessCoeff, 0.0f, 1.0f );
    const float stiffness = 1.0f - powf( 1.0f - stiffness60, stepRatio );
    const float keepVelocity = powf( 1.0f - Clamp( damping, 0.0f, 1.0f ), stepRatio );
    // Verlet velocity is a per-step displacement: rescale it when the step length changes (limited against hitches)
    const float velocityScale = ( springPrevStep > 0.0f ) ? Clamp( stepTime / springPrevStep, 0.5f, 2.0f ) : 1.0f;
    const CVec3 gravityStep( 0.0f, 0.0f, -gravity * stepTime * stepTime );
    for ( int k = 0; k < steps; ++k )
    {
      const float scale = ( k == 0 ) ? velocityScale : 1.0f;
      const CVec3 velocity = ( s.tail - s.prevTail ) * ( keepVelocity * scale );
      s.prevTail = s.tail;
      s.tail = s.tail + velocity + ( target - s.tail ) * stiffness + gravityStep;

      // Keep the bone length
      const CVec3 d = s.tail - head;
      const float len = SpringLength( d );
      s.tail = ( len > 1e-4f ) ? head + d * ( boneLength / len ) : target;
    }

    // Rotation from the animated direction to the simulated one, limited by the max angle
    const CVec3 from = animDir * ( 1.0f / boneLength );
    CVec3 to = s.tail - head;
    const float toLength = SpringLength( to );
    if ( toLength < 1e-4f )
      return;
    to = to * ( 1.0f / toLength );

    CVec3 axis = SpringCross( from, to );
    const float sinA = SpringLength( axis );
    const float cosA = SpringDot( from, to );
    if ( sinA < 1e-5f )
      return;
    axis = axis * ( 1.0f / sinA );
    float angle = atan2f( sinA, cosA );
    const float maxAngle = maxAngleDeg * FP_PI / 180.0f;
    if ( angle > maxAngle )
    {
      angle = maxAngle;
      // Pull the simulated tail back inside the cone so it does not stick to the limit
      const float c = cosf( angle ), sn = sinf( angle );
      const CVec3 limited = from * c + SpringCross( axis, from ) * sn;
      s.tail = head + limited * boneLength;
    }

    // Rodrigues rotation matrix, applied to the bone orientation around its head
    const float c = cosf( angle ), sn = sinf( angle ), t = 1.0f - c;
    const float x = axis.x, y = axis.y, z = axis.z;
    const float r11 = t * x * x + c,     r12 = t * x * y - sn * z, r13 = t * x * z + sn * y;
    const float r21 = t * x * y + sn * z, r22 = t * y * y + c,     r23 = t * y * z - sn * x;
    const float r31 = t * x * z - sn * y, r32 = t * y * z + sn * x, r33 = t * z * z + c;

    const Matrix43 m( boneWorld );
    boneWorld._11 = r11 * m._11 + r12 * m._21 + r13 * m._31;
    boneWorld._12 = r11 * m._12 + r12 * m._22 + r13 * m._32;
    boneWorld._13 = r11 * m._13 + r12 * m._23 + r13 * m._33;
    boneWorld._21 = r21 * m._11 + r22 * m._21 + r23 * m._31;
    boneWorld._22 = r21 * m._12 + r22 * m._22 + r23 * m._32;
    boneWorld._23 = r21 * m._13 + r22 * m._23 + r23 * m._33;
    boneWorld._31 = r31 * m._11 + r32 * m._21 + r33 * m._31;
    boneWorld._32 = r31 * m._12 + r32 * m._22 + r33 * m._32;
    boneWorld._33 = r31 * m._13 + r32 * m._23 + r33 * m._33;
  }

  SkeletonWrapper::~SkeletonWrapper()
  {
    Aligned_Free(pSampledMatrix);
    Aligned_Free(pSkinWorldMatrix);
  }

  void SkeletonWrapper::SetActiveBones( unsigned int count )
  {
    matrixReindex.resize( count );

    for ( unsigned int i = 0; i < count; ++i )
    {
      matrixReindex[i] = i;
    }

    activeBonesCount = count;

    if ( count > allocatedBonesCount )
    {
      if ( pSkinWorldMatrix )
      {
        Aligned_Free( pSkinWorldMatrix );
      }

      allocatedBonesCount = count;

      pSkinWorldMatrix = static_cast<Matrix43*>(Aligned_MAlloc(sizeof(Matrix43)*allocatedBonesCount, 16));
    }
  }


	void SkeletonWrapper::Update( const Matrix43& _worldMatrix )
	{
		const unsigned int bonesCount = pSkeletonData->GetData()->jointsCount;
		const unsigned int* pParentIndicies = pSkeletonData->GetData()->parentsIDs.at(0);
		const Matrix43* pBindMatricies = pSkeletonData->GetData()->invertedBindPoseBones.at(0);

		__declspec(align(16)) Matrix43 worldMatrix(_worldMatrix);

		SEEMatrixMultiply( &pBoneWorldMatrix[0], &worldMatrix, &pSampledMatrix[0] );

    // Extra yaw: world' = World * (T(pivot) * Rz * T(-pivot)) * World^-1 * world, children inherit it through the parent
    const int maxExtraYaw = 16;
    __declspec(align(16)) Matrix43 extraYawMatrices[maxExtraYaw];
    const int extraYawCount = Min( (int)extraYaw.size(), maxExtraYaw );
    if ( extraYawCount > 0 )
    {
      __declspec(align(16)) Matrix43 worldInv( worldMatrix.GetInversed() );
      for ( int k = 0; k < extraYawCount; ++k )
      {
        const ExtraYaw& e = extraYaw[k];
        if ( e.bone >= bonesCount || e.angle == 0.0f )
          continue;
        __declspec(align(16)) Matrix43 rot( CQuat( e.angle, CVec3( 0.0f, 0.0f, 1.0f ) ), VNULL3 );
        const CVec3& p = e.pivot;
        rot._14 = p.x - ( rot._11 * p.x + rot._12 * p.y + rot._13 * p.z );
        rot._24 = p.y - ( rot._21 * p.x + rot._22 * p.y + rot._23 * p.z );
        rot._34 = p.z - ( rot._31 * p.x + rot._32 * p.y + rot._33 * p.z );
        __declspec(align(16)) Matrix43 k1, k2;
        SEEMatrixMultiply( &k1, &rot, &worldInv );
        SEEMatrixMultiply( &k2, &worldMatrix, &k1 );
        extraYawMatrices[k] = k2;
      }
    }

    // Spring bones time step
    const float dt = timeDelta;
    timeDelta = 0.0f;
    const bool springsActive = s_springEnable && !springBones.empty();
    const bool springReset = !springsInitialized || dt > 0.25f;
    int springSteps = 0;
    float springStep = 0.0f;
    if ( springsActive && dt > 0.0f && !springReset )
    {
      springSteps = Min( (int)ceilf( dt / springStepTime - 0.01f ), springMaxSteps );
      springSteps = Max( springSteps, 1 );
      springStep = dt / springSteps;
    }
    springsInitialized = springsActive;

    // Hierarchical bone placements, parents first
		for	( unsigned int n = 1; n < (unsigned int)updateOrder.size(); ++n )
    {
      const unsigned int i = updateOrder[n];
      const unsigned int parent = pParentIndicies[i];
      if ( parent < bonesCount )
  		  SEEMatrixMultiply( &pBoneWorldMatrix[i], &pBoneWorldMatrix[ parent ], &pSampledMatrix[i] );
      else
        SEEMatrixMultiply( &pBoneWorldMatrix[i], &worldMatrix, &pSampledMatrix[i] );
      for ( int k = 0; k < extraYawCount; ++k )
        if ( extraYaw[k].bone == i && extraYaw[k].angle != 0.0f )
        {
          __declspec(align(16)) Matrix43 tmp( pBoneWorldMatrix[i] );
          SEEMatrixMultiply( &pBoneWorldMatrix[i], &extraYawMatrices[k], &tmp );
        }
      if ( springsActive && springByBone[i] >= 0 )
        ApplySpringBone( springBones[ springByBone[i] ], pBoneWorldMatrix[i],
          parent < bonesCount ? pBoneWorldMatrix[ parent ] : worldMatrix, springSteps, springStep, springReset );
    }
    if ( springSteps > 0 )
      springPrevStep = springStep;
    else if ( springReset )
      springPrevStep = 0.0f;

    // Absolute bone placements
    //for	( unsigned int i = 1; i < bonesCount; ++i )
    // SEEMatrixMultiply( &pBoneWorldMatrix[i], &worldMatrix, &pSampledMatrix[i] );
    
		for	( unsigned int i = 0; i < activeBonesCount; ++i ) 
      SEEMatrixMultiply( &pSkinWorldMatrix[i], &pBoneWorldMatrix[ matrixReindex[i] ], &pBindMatricies[ matrixReindex[i] ] );
	}

  bool SkeletonWrapper::GetBindPosition( unsigned int index, CVec3& pos ) const
  {
    if ( index >= pSkeletonData->GetData()->jointsCount )
      return false;
    const Matrix43 bind = pSkeletonData->GetData()->invertedBindPoseBones.at(0)[index].GetInversed();
    pos = CVec3( bind._14, bind._24, bind._34 );
    return true;
  }

	bool SkeletonWrapper::GetJointIndexByName( const char* name, unsigned int& index ) const
	{
		//crap!!!
		//nstl::find_if( pSkeletonData->boneNames.begin(), pSkeletonData->boneNames.end(), JointPredicate );

		for ( unsigned int i = 0; i < pSkeletonData->GetData()->jointsCount; i++ )
		{
			if ( strcmp( pSkeletonData->GetData()->jointsNames[i].c_str(), name ) == 0 )
			{
				index = pSkeletonData->GetData()->namesOrder[i];
				return true;
			}
		}
		return false;
	}

  bool SkeletonWrapper::GetJointOrderByName( const char* name, unsigned int& index ) const
  {
    //crap!!!
    //nstl::find_if( pSkeletonData->boneNames.begin(), pSkeletonData->boneNames.end(), JointPredicate );

    for ( unsigned int i = 0; i < pSkeletonData->GetData()->jointsCount; i++ )
    {
      if ( strcmp( pSkeletonData->GetData()->jointsNames[i].c_str(), name ) == 0 )
      {
        index = i;
        return true;
      }
    }
    return false;
  }

  void SkeletonWrapper::GetBoneChildren(unsigned int index, vector<unsigned int> &childrenIdxes) const
  {
    for ( unsigned int i = 0; i < pSkeletonData->GetData()->jointsCount; i++ )
    {
      if(pSkeletonData->GetData()->parentsIDs[i] == index)
      {
        childrenIdxes.push_back(i);
      }
    }  
    return;
  }

  char const * SkeletonWrapper::GetJointNameByIndex(unsigned int index ) const
  {
    for(unsigned int i = 0; i < pSkeletonData->GetData()->jointsCount; i++)
    {
      if(pSkeletonData->GetData()->namesOrder[i] == index)
      {
        return pSkeletonData->GetData()->jointsNames[i].c_str();
      }
    }
    return NULL;
  }

	int SkeletonWrapper::GetBonesCount() const
	{
		return pSkeletonData->GetData()->jointsCount;
	}

  vector<string> SkeletonWrapper::GetJointsList()
  {
    vector<string> res;
    for ( unsigned int i = 0; i < pSkeletonData->GetData()->jointsCount; i++ )
    {
      res.push_back( pSkeletonData->GetData()->jointsNames[i].c_str() );
    }
    return res;
  }

  void SkeletonWrapper::SetReindex( unsigned int from, unsigned int to )
  {
    NI_ASSERT( from >= 0 && from < pSkeletonData->GetData()->jointsCount, "Wrong index" );
    NI_ASSERT( to >= 0 && to < (unsigned int)matrixReindex.size(), "Wrong index" );
    matrixReindex[to] = from;
  }

  //void SkeletonWrapper::SetSkeletonData( const SkeletonDataWrapper* pNewSkeletonData )
  //{
  //  pSkeletonData = pNewSkeletonData;
  //}

};