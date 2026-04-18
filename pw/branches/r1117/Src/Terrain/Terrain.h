#pragma once

#if defined(PW_LINUX_DB_BOOTSTRAP) && !defined(PW_LINUX_TERRAIN_RUNTIME_PROBE)

#include "GridConstants.h"
#include "NatureMap.h"
#include "TerrainTextureCache.h"
#include "../System/2DArray.h"
#if defined(PW_LINUX_NULL_RENDER)
#include "TerrainElement.h"
#include "TerrainElementManager.h"
#include "TerrainGeometryManager.h"
#endif

namespace NScene
{
  class LightingScene;
}

namespace Terrain
{
#if !defined(PW_LINUX_NULL_RENDER)
  typedef long TerrainElementId;
  static const TerrainElementId TERRAINELEMENTID_BAD = -1;

  enum
  {
    INVALID_GEOMETRY = 0x0001,
    INVALID_LIGHTING = 0x0002,
    INVALID_MASKS = 0x0004
  };

  struct TerrainElementInfo
  {
    TerrainElementId id;

    TerrainElementInfo()
      : id(TERRAINELEMENTID_BAD)
    {
    }
  };

  class BootstrapTerrainElementManager
  {
  public:
    template <class FUNCTOR> void ForAllElementInfos(FUNCTOR& /*func*/) {}
    template <class FUNCTOR> void ForAllElementInfosAABB(FUNCTOR& /*func*/, Render::AABB const& /*bounds*/) {}
  };

  class BootstrapTerrainGeometryManager
  {
  public:
    void SetLightingScene(NScene::LightingScene* /*lightingScene*/) {}
    void InvalidateElement(TerrainElementId /*id*/, int /*flags*/) {}
  };
#endif

  class BootstrapTerrainHeightGrid
  {
  public:
    int GetSizeX() const { return 0; }
    int GetSizeY() const { return 0; }
  };

  class BootstrapTerrainHeightManager
  {
    BootstrapTerrainHeightGrid heights;
    CArray2D<float> heightsAsFloat;

  public:
    BootstrapTerrainHeightGrid const& GetHeights() const { return heights; }
    CArray2D<float> const& GetHeightsAsFloat() const { return heightsAsFloat; }
    unsigned int GetHeightsVersion() const { return 0; }
  };

  class BootstrapNatureMap : public NatureMap
  {
  public:
    BootstrapNatureMap()
      : NatureMap(false)
    {
    }

  protected:
    virtual bool OnLoadAtRuntime(Stream* pStream, int fraction, bool fromRecconect)
    {
      (void)pStream;
      (void)fraction;
      (void)fromRecconect;
      return false;
    }

    virtual bool OnSaveAtRuntime(Stream* pStream, bool fromRecconect) const
    {
      (void)pStream;
      (void)fromRecconect;
      return false;
    }
  };

  class Terrain : public CObjectBase
  {
    OBJECT_BASIC_METHODS(Terrain);

  private:
    BootstrapNatureMap natureMap;
    GridConstants gridConstants;
    BootstrapTerrainHeightManager heightManager;
    bool editable;
#if defined(PW_LINUX_NULL_RENDER)
    TerrainElementManager elemManager;
    TerrainGeometryManager geometryManager;
#else
    BootstrapTerrainElementManager elemManager;
    BootstrapTerrainGeometryManager geometryManager;
#endif

    Terrain()
      : natureMap()
      , gridConstants()
      , heightManager()
      , editable(false)
      , elemManager()
      , geometryManager()
    {
    }

  public:
    explicit Terrain(bool editable_)
      : natureMap()
      , gridConstants()
      , heightManager()
      , editable(editable_)
      , elemManager()
      , geometryManager()
    {
    }

    virtual ~Terrain() {}

    NatureMap& GetNatureMap() { return natureMap; }
    GridConstants const& GetGridConstants() const { return gridConstants; }
    BootstrapTerrainHeightManager const& GetHeightManager() const { return heightManager; }
    uint GetHeightsCounter() const { return heightManager.GetHeightsVersion(); }
    bool IsEditable() const { return editable; }
    TerrainTextureCache* GetTextureCache() const { return 0; }

    bool GetHeight(float x, float y, float* height, CVec3* normal = NULL ) const
    {
      (void)x;
      (void)y;
      if (height)
        *height = 0.0f;
      if (normal)
        *normal = CVec3(0.0f, 0.0f, 1.0f);
      return false;
    }
#if defined(PW_LINUX_NULL_RENDER)
    TerrainElementManager& GetElementManager() { return elemManager; }
    TerrainGeometryManager& GetGeometryManager() { return geometryManager; }
    TerrainElementId AddTerrainElement(const NDb::TerrainElementInstance& descriptor, const string& fileName) { return geometryManager.AddTerrainElement(descriptor, fileName); }
    bool DeleteTerrainElement(TerrainElementId id) { return geometryManager.DeleteTerrainElement(id); }
#else
    BootstrapTerrainElementManager& GetElementManager() { return elemManager; }
    BootstrapTerrainGeometryManager& GetGeometryManager() { return geometryManager; }
    TerrainElementId AddTerrainElement(const NDb::TerrainElementInstance& /*descriptor*/, const string& /*fileName*/) { return TERRAINELEMENTID_BAD; }
    bool DeleteTerrainElement(TerrainElementId /*id*/) { return false; }
#endif

    template <class FUNCTOR> void ForAllElements(FUNCTOR& func) { elemManager.ForAllElements(func); }

    void LoadTerrain(const NDb::Terrain* pDBTerrain) { (void)pDBTerrain; }
    void CreateTerrain(const NDb::Terrain* pDBTerrain) { (void)pDBTerrain; }
    void Update() {}

    bool IntersectWithGrid( CVec2* pPoint, const CVec2& dir ) const
    {
      (void)pPoint;
      (void)dir;
      return false;
    }
  };
}

#else

#include "GridConstants.h"
#include "TerrainElement.h"
#include "TerrainElementManager.h"
#include "TerrainLayerManager.h"
#include "TerrainGeometryManager.h"
#include "TerrainHeightManager.h"
#include "TerrainMaterialCache.h"
#include "GrassLayersManager.h"
#include "GrassRenderManager.h"
#include "NatureMap.h"
#include "NatureMapVisual.h"
#include "TerrainTextureCache.h"

#include "../Render/batch.h"
#include "../Scene/RenderableScene.h"
#include "../Render/ShadowReceiverVolume.h"

namespace Terrain
{
//	static const float TERRAIN_TILE_SIZE = 2.5f; // smirnov [2008/12/4]: use GridConstants.metersPerTile instead

  class Terrain : public CObjectBase, public Render::IShadowReceiverVolume
	{
		OBJECT_BASIC_METHODS(Terrain);

		TerrainLayerManager layerManager;
		TerrainHeightManager heightManager;
		TerrainElementManager elemManager;
#ifndef VISUAL_CUTTED
		TerrainGeometryManager geometryManager;
		TerrainMaterialCache materialCache;

    int textureCacheSize;
    ScopedPtr<TerrainTextureCache> pTextureCache;
    ScopedPtr<class TEGetter> pTEGetter;
    Render::BatchQueueSorter batchQueueSorter;

		/** grass parameters */
    Grass::GrassRenderManager grassRenderManager;
		Grass::GrassLayersManager grassManager;
    ScopedPtr<Render::BaseMaterial> aoeMaterial;
    Render::DeviceLostWrapper<NatureMapVisual> natureMap;
    vector<TerrainElementId> nmTerrainElements;
    vector<NatureMapElementId> teNatureMapElements;
    vector<int> nmToTe, teToNm;

 		CArray1Bit natureMapDirty; // the size of tile map
#else
		NatureMap natureMap;
#endif

    NatureMapVisual::NatureMapElementIdList modifiedElems;

		GridConstants gridConstants;

		bool editable;

		bool  aoeEnabled;
		CVec3 aoeCenter;
		float aoeRadius;

		int layerDisplayMode;

    // dummy constructor for CObjectBase
    Terrain();

	public:
		explicit Terrain(bool editable_);
		virtual ~Terrain();

		bool IsEditable() const { return editable; }

		TerrainLayerManager& GetLayerManager() { return layerManager; }
		TerrainHeightManager& GetHeightManager() { return heightManager; }
		const TerrainHeightManager& GetHeightManager() const { return heightManager; }
		TerrainElementManager& GetElementManager() { return elemManager; }
#ifndef VISUAL_CUTTED
		TerrainGeometryManager& GetGeometryManager() { return geometryManager; }
		TerrainMaterialCache& GetMaterialCache() { return materialCache; }
    TerrainTextureCache* GetTextureCache() const { return ::Get(pTextureCache); }
#endif
    NatureMap& GetNatureMap() { return natureMap; }
		GridConstants const& GetGridConstants() const { return gridConstants; }

		int GetLayerDisplayMode() const { return layerDisplayMode; }
		void SetLayerDisplayMode(int mode) { layerDisplayMode = mode; }

    void LoadTerrain( const NDb::Terrain* pDBTerrain );   // in-game only
		void CreateTerrain( const NDb::Terrain* pDBTerrain ); // in-editor only

		void Update();

#ifndef VISUAL_CUTTED
    // next 2 methods are used for runtime reconfiguration
    void InvalidateRenderResources();
    void RestoreRenderResources();

    void InitNatureMapParams();
    void UpdateTerrainElements();

		void StartRendering();
		void StopRendering();

    void FillCache(Render::BatchQueue &_queue, class IRenderableScene *_pScene);
    void OnRender(const Render::BatchQueue &_queue, bool _doTerrainZPrepass);
		void RenderWithMaterial( Render::AABB const& bbox, Render::BatchQueue& queue, Render::BaseMaterial* pMaterial );
		void ForAllElements(IObjectFunctor &func)
		{
			elemManager.ForAllElements( func );
			grassRenderManager.ForAllElements( func ); 
		}

    void SetAOEMaterial_Deprecated(NDb::Material const *pMaterial);
    void SetAOESelection_Deprecated(CVec3 const& center, float radius);
		void ClearAOESelection_Deprecated() { aoeEnabled = false; }
		void RenderAOESelection_Deprecated(Render::BatchQueue& queue);

    void RecalculateLighting();

		/**
		 * get terrain height by projected x and y, return true if can do it
		 */
		bool GetHeight(float x, float y, float* height, CVec3* normal = NULL ) const { return heightManager.GetHeight(x, y, height, normal); }
		uint GetHeightsCounter() const { return heightManager.GetHeightsVersion(); }

		void NotifyNatureChange(int row, int column) { layerManager.NotifyNatureChange(elemManager.LocateElement(unsigned(row), unsigned(column))); }

		// Layer methods.
		void WriteLayerValue( int layerIndex, int row, int column, unsigned char val ) { if (val) layerManager.WriteLayerValue( layerIndex, row, column ); }
		unsigned char ReadLayerValue( int layerIndex, int row, int column ) { return layerManager.ReadLayerValue( layerIndex, row, column ); }
		void InvalidateLayers() { layerManager.InvalidateLayers(); }
		int AddLayer( const NDb::TerrainMaterial* pMaterial ) { return layerManager.AddLayer( pMaterial ); }
		void ReplaceLayer( int index, const NDb::TerrainMaterial* pMaterial ) { layerManager.ReplaceLayer( index, pMaterial ); }
		void RemoveLayer( int index ) { layerManager.RemoveLayer( index ); }
		void SaveLayerMask( const nstl::string& filename ) { layerManager.SaveLayerMask( filename ); }
		void LoadLayerMask( const nstl::string& filename ) { layerManager.LoadLayerMask( filename ); }

		// Geometry methods.
		TerrainElementId AddTerrainElement( const NDb::TerrainElementInstance& descriptor, const string& fileName ) { return geometryManager.AddTerrainElement( descriptor, fileName ); }
		bool DeleteTerrainElement( TerrainElementId id ) { return geometryManager.DeleteTerrainElement( id ); }
		void SetTCV( int column, int row, const Matrix43& basis ) { geometryManager.SetTCV( column, row, basis ); }
		const Matrix43& GetTCV( int column, int row ) const { return geometryManager.GetTCV( column, row ); }
		void SaveTCV( const string& fileName );
		void LoadTCV( const string& fileName, bool applyInstuntly );
		/** bake terrain elements in file, order of elements must be coincide, return number of baked elements */		
		int BakeTerrainGeometry( const vector<TerrainElementId>& elements, const string& backedFileName ) const;
    void WeldVertices() const { geometryManager.WeldVertices(true); }

		void SaveHeightmap(const nstl::string& filename) { heightManager.Save(filename); }
		void LoadHeightmap(const nstl::string& filename) { heightManager.Load(filename); }

    void SaveNatureMap(const nstl::string& filename) { natureMap.Save(filename); }

		/** grass methods */
		Grass::GrassLayersManager::LayerId AddGrassLayer( const NDb::GrassMaterial* pDescription, int blades ) { return grassManager.AddLayer( pDescription, blades ); }
		bool RemoveGrassLayer( Grass::GrassLayersManager::LayerId id ) { return grassManager.RemoveLayer( id ); }
		unsigned char GetLayerDensity( Grass::GrassLayersManager::LayerId id, int column, int row ) const { return grassManager.GetLayerDensity( id, column, row ); }
		void SetLayerDensity( Grass::GrassLayersManager::LayerId id, int column, int row, unsigned char density ) { grassManager.SetLayerDensity( id, column, row, density ); }
		bool ChangeGrassMaterial( Grass::GrassLayersManager::LayerId id, const NDb::GrassMaterial* pDescription) { return grassManager.ChangeMaterial( id, pDescription ); }
		bool ChangeBladesPerDensity( Grass::GrassLayersManager::LayerId id, unsigned int blades ) { return grassManager.ChangeBladesPerDensity( id, blades ); }
		bool SaveGrassLayer( const nstl::string& filename, Grass::GrassLayersManager::LayerId id ) { return grassManager.SaveLayer( filename, id ); }
		bool LoadGrassLayer( const nstl::string& filename, Grass::GrassLayersManager::LayerId id, unsigned char defaultValue ) { return grassManager.LoadLayer( filename, id, defaultValue ); }

    void ShowNatureAttackTexture();
#endif
    /** project to height map virtual grid */
    bool IntersectWithGrid( CVec2* pPoint, const CVec2& dir ) const;

    // from IShadowReceiverVolume
    virtual void GetBoundsInCamera( const Render::AABB& _cameraAABB, Render::AABB& _receiversAABB );

  private :
#ifndef VISUAL_CUTTED
    struct InitNatureMapParamsHelper;
#endif

	private:
    void Initialize( const NDb::Terrain* pDBTerrain );
	};
};

#endif
