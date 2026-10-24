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
    bool GetBindPosition( unsigned int index, CVec3& pos ) const;   // model-space bone position in the bind pose

    //void SetSkeletonData( const SkeletonDataWrapper* pNewSkeletonData );

	};

};