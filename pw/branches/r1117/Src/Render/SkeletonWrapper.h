#pragma once


struct SkeletonDataWrapper;

namespace Render
{
	class SkeletonWrapper
	{
		const SkeletonDataWrapper* pSkeletonData;

    Matrix43* pSampledMatrix;
		Matrix43* pBoneWorldMatrix;
    Matrix43* pSkinWorldMatrix;

    typedef nstl::vector<unsigned int> MatrixReindex; 
    MatrixReindex matrixReindex;

    unsigned int allocatedBonesCount;
    unsigned int activeBonesCount;

  public:
    // Extra rotation of a bone (and so of all its children) about the model up axis (Z) around a model-space pivot,
    // applied on top of the animation: procedural body bend (Laguna's whale turning).
    struct ExtraYaw
    {
      unsigned int bone;
      float angle;
      CVec3 pivot;
    };
  private:
    nstl::vector<ExtraYaw> extraYaw;

    // Bones are evaluated parent-first (exported skeletons are not always sorted by index)
    nstl::vector<unsigned int> updateOrder;

    // Spring (jiggle) bones: secondary motion on top of the animation, client-side visual only
    struct SpringBone
    {
      unsigned int bone;
      int chain;            // index of the chain parameters
      int child;           // child in the chain whose position is the bone tail, -1 for the chain tip
      float stiffnessCoeff; // 0 at the chain root .. 1 at the tip
      CVec3 tail;
      CVec3 prevTail;
      CVec3 lastHead;
    };
    nstl::vector<SpringBone> springBones;
    nstl::vector<int> springByBone;
    bool springsInitialized;
    float timeDelta;
    float springPrevStep;

    void InitUpdateOrder();
    void InitSpringBones();
    void ApplySpringBone( SpringBone& s, Matrix43& boneWorld, const Matrix43& parentWorld, int steps, float stepTime, bool reset );


	public:
		SkeletonWrapper(const SkeletonDataWrapper* _pSkeletonData);
		~SkeletonWrapper();

		int GetBonesCount() const; 
		void Update( const Matrix43& _worldMatrix );

		const Matrix43* GetSkinWorldMatrices() const { return pSkinWorldMatrix; } 
		const Matrix43* GetBoneWorldMatrices() const { return pBoneWorldMatrix; } 
		Matrix43* GetSampledMatrix() { return pSampledMatrix; } 

		bool GetJointIndexByName( const char* name, unsigned int& index ) const;
    bool GetJointOrderByName( const char* name, unsigned int& index ) const;
    char const * GetJointNameByIndex(unsigned int index ) const;
    void GetBoneChildren(unsigned int index, vector<unsigned int> &childrenIdxes) const;

    void SetReindex( unsigned int from, unsigned int to );
    void SetActiveBones( unsigned int count );

    vector<string> GetJointsList();

    void SetExtraYaw( const nstl::vector<ExtraYaw>& yaw ) { extraYaw = yaw; }
    void ClearExtraYaw() { extraYaw.clear(); }
    // Time passed since the previous Update, drives spring bones (0 = keep them as they are)
    void SetTimeDelta( float dt ) { timeDelta = dt; }
    bool GetBindPosition( unsigned int index, CVec3& pos ) const;   // model-space bone position in the bind pose

    //void SetSkeletonData( const SkeletonDataWrapper* pNewSkeletonData );

	};

};