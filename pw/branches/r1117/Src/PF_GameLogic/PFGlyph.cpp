#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFGlyph.h"
#include "PFWorld.h"
#include "DBAdvMap.h"
#include "DBSessionRoots.h"
#include "PFAdvMapObject.h"
#include "PFBaseUnit.h"
#include "PFBaseUnitEvent.h"
#include "PFAIContainer.h"
#include "PFAIWorld.h"
#include "PFMaleHero.h"
#include "TileMap.h"
#include "WarFog.h"

namespace NWorld
{

static void SetupLinuxGlyphTileState(PFWorld* pWorld, NDb::GameObject const* gameObject, const CVec3& position, PFPickupableObjectBase* pObject)
{
  if (!pObject)
    return;

  float objectSourceSize = gameObject && gameObject->lockMask.tileSize > 0.0f ? gameObject->lockMask.tileSize : 1.0f;
  float objectSize = objectSourceSize;
  int objectTileSize = static_cast<int>(ceil(objectSourceSize));
  int objectDynTileSize = static_cast<int>(ceil(objectSourceSize));

  if (pWorld && pWorld->GetTileMap() && gameObject)
  {
    NDb::AdvMapObject dbObject;
    dbObject.gameObject = gameObject;
    dbObject.offset = CPlacement(position, QNULL, CVec3(1.f, 1.f, 1.f));

    vector<SVector> occupiedTiles;
    MarkObject(pWorld->GetTileMap(), dbObject, occupiedTiles);
    pWorld->GetTileMap()->MarkObject(occupiedTiles, true, MAP_MODE_BUILDING);

    const float tileSize = pWorld->GetTileMap()->GetTileSize();
    if (tileSize > 0.0f)
    {
      objectTileSize = static_cast<int>(ceil(objectSourceSize / tileSize));
      objectDynTileSize = static_cast<int>(objectSourceSize);
      objectSize = objectSourceSize * tileSize;
    }
  }

  pObject->SetObjectSizes(objectSize, objectTileSize, objectDynTileSize);
}

static int ResolveLinuxGlyphNumber(PFWorld* pWorld, const NDb::Glyph* pGlyphDesc)
{
  if (!pGlyphDesc)
    return -1;

  const NDb::AdvMapSettings* mapSettings = 0;
  if (pWorld && pWorld->GetMapDescription())
  {
    if (IsValid(pWorld->GetMapDescription()->mapSettings))
      mapSettings = pWorld->GetMapDescription()->mapSettings.GetPtr();
    else if (pWorld->GetMapDescription()->map && IsValid(pWorld->GetMapDescription()->map->mapSettings))
      mapSettings = pWorld->GetMapDescription()->map->mapSettings.GetPtr();
  }

  if (mapSettings && IsValid(mapSettings->overrideGlyphSettings) && IsValid(mapSettings->overrideGlyphSettings->glyphs))
  {
    for (int i = 0; i < mapSettings->overrideGlyphSettings->glyphs->glyphs.size(); ++i)
    {
      if (mapSettings->overrideGlyphSettings->glyphs->glyphs[i].glyph &&
          pGlyphDesc->GetDBID() == mapSettings->overrideGlyphSettings->glyphs->glyphs[i].glyph->GetDBID())
        return i;
    }
  }

  NDb::Ptr<NDb::SessionRoot> pRoot = NDb::SessionRoot::GetRoot();
  if (pRoot && pRoot->logicRoot && pRoot->logicRoot->glyphsDB)
  {
    for (int i = 0; i < pRoot->logicRoot->glyphsDB->glyphs.size(); ++i)
    {
      if (pRoot->logicRoot->glyphsDB->glyphs[i].glyph &&
          pGlyphDesc->GetDBID() == pRoot->logicRoot->glyphsDB->glyphs[i].glyph->GetDBID())
        return i;
    }
  }

  return -1;
}

PFGlyphSpawner::PFGlyphSpawner( const CPtr<PFWorld>& pWorld, const NDb::AdvMapObject &dbObject )
: PFWorldObjectBase( pWorld, 1 )
, position( dbObject.offset.GetPlace().pos )
, spawnOffset(-1.0f)
, hidden(false)
, lastGlyph(0)
{
  pDesc = dynamic_cast<const NDb::GlyphSpawner*>( dbObject.gameObject.GetPtr() );
  scriptName = dbObject.scriptName;
  pSpawnedGlyph = CreateGlyph(position);
}

const NDb::GlyphSettings& PFGlyphSpawner::GetSettings() const
{
  return pDesc->settings;
}

CPtr<NWorld::PFGlyph> PFGlyphSpawner::CreateGlyph( CVec3 const& position )
{
  if (!pDesc || !pDesc->glyphs || pDesc->glyphs->glyphs.empty())
    return 0;

  const int glyphIndex = lastGlyph % pDesc->glyphs->glyphs.size();
  const NDb::GlyphEntry& glyphEntry = pDesc->glyphs->glyphs[glyphIndex];
  lastGlyph = (lastGlyph + 1) % pDesc->glyphs->glyphs.size();
  if (!glyphEntry.glyph)
    return 0;

  CPtr<NWorld::PFGlyph> glyph = new PFGlyph(GetWorld(), glyphEntry.glyph, position);
  glyph->SetMapObject(true);
  glyph->Hide(hidden);
  if (!scriptName.empty())
  {
    string name = scriptName + "_glyph";
    glyph->SetScriptName(name);
    if (GetWorld() && GetWorld()->GetAIContainer())
      GetWorld()->GetAIContainer()->RegisterObject(glyph, name, "");
  }
  return glyph;
}

void PFGlyphSpawner::Hide(bool hide)
{
  if (hidden == hide)
    return;

  hidden = hide;
  if (IsValid(pSpawnedGlyph))
    pSpawnedGlyph->Hide(hidden);
}

bool PFGlyphSpawner::Step(float dtInSeconds)
{
  (void)dtInSeconds;
  return PFWorldObjectBase::Step(dtInSeconds);
}

NAMEMAP_BEGIN(PFGlyph)
NAMEMAP_FUNC_RO(name, &PFGlyph::GetName);
NAMEMAP_END

PFGlyph::PFGlyph( const CPtr<PFWorld>& pWorld, const NDb::Ptr<NDb::Glyph>& pGlyphDesc, const CVec3& position )
: PFPickupableObjectBase( pWorld, position, pGlyphDesc && pGlyphDesc->gameObject ? pGlyphDesc->gameObject.GetPtr() : 0 )
, visUnitID1(-1)
, visUnitID2(-1)
, pDesc(pGlyphDesc)
, enabled(true)
, hidden(false)
, glyphNumber(-1)
{
  if (pDesc && pDesc->gameObject)
    SetupLinuxGlyphTileState(GetWorld(), pDesc->gameObject.GetPtr(), position, this);

  glyphNumber = ResolveLinuxGlyphNumber(GetWorld(), pDesc.GetPtr());
  OpenWarFog();
}

void PFGlyph::Reset()
{
  CloseWarFog();
  PFPickupableObjectBase::Reset();
  visUnitID1 = -1;
  visUnitID2 = -1;
  if (!hidden)
    OpenWarFog();
}

void PFGlyph::Hide(bool hide)
{
  if (hidden == hide)
    return;

  hidden = hide;
  if (hide)
    CloseWarFog();
  else
    OpenWarFog();

  UpdateHiddenState(!hidden);
}

void PFGlyph::OpenWarFog()
{
  if (!pDesc || pDesc->openWarFogRadius <= 0.0f)
    return;

  PFWorld* pWorld = GetWorld();
  if (!pWorld || !pWorld->GetTileMap() || !pWorld->GetFogOfWar())
    return;

  SVector pos = pWorld->GetTileMap()->GetTile(GetPosition().AsVec2D());
  int tileRadius = pWorld->GetTileMap()->GetLenghtInTiles(pDesc->openWarFogRadius);

  if (visUnitID1 < 0)
    visUnitID1 = pWorld->GetFogOfWar()->AddObject(pos, NDb::FACTION_FREEZE, tileRadius);
  if (visUnitID2 < 0)
    visUnitID2 = pWorld->GetFogOfWar()->AddObject(pos, NDb::FACTION_BURN, tileRadius);
}

void PFGlyph::CloseWarFog()
{
  PFWorld* pWorld = GetWorld();
  FogOfWar* pFog = pWorld ? pWorld->GetFogOfWar() : 0;

  if (pFog && visUnitID1 >= 0)
    pFog->RemoveObject(visUnitID1);
  if (pFog && visUnitID2 >= 0)
    pFog->RemoveObject(visUnitID2);

  visUnitID1 = -1;
  visUnitID2 = -1;
}

void PFGlyph::OnPickedUp( PFBaseUnit* pPicker )
{
  if (!pPicker)
    return;

  pPicker->OnGlyphPickUp(this);
  PFBaseUnitPickupEvent ev(NDb::BASEUNITEVENT_PICKUP, true, glyphNumber, scriptName);
  pPicker->EventHappened(ev);
  if (ev.NeedActivate() && pDesc)
    pPicker->UseExternalAbility(pDesc.GetPtr(), Target(pPicker));
}

void PFGlyph::OnDie()
{
  CloseWarFog();
  PFLogicObject::OnDie();
}

bool PFGlyph::CanBePickedUpBy( const PFBaseUnit* pPicker ) const
{
  return enabled && PFPickupableObjectBase::CanBePickedUpBy(pPicker);
}

NAMEMAP_BEGIN(PFNatureGlyph)
NAMEMAP_FUNC_RO(name, &PFNatureGlyph::GetName);
NAMEMAP_END

PFNatureGlyph::PFNatureGlyph( const CPtr<PFBaseMaleHero>& targetHero_, NDb::ERoute routeID_, const CPtr<PFWorld>& pWorld, const NDb::Ptr<NDb::GameObject> _gameObject, const CVec3& position )
: PFPickupableObjectBase(pWorld, position, _gameObject.GetPtr())
, bVisible(true)
, targetHero(targetHero_)
, routeID(routeID_)
, visUnitID(-1)
, gameObject(_gameObject)
{
  if (gameObject)
    SetupLinuxGlyphTileState(GetWorld(), gameObject.GetPtr(), position, this);

  if (IsValid(targetHero))
  {
    ChangeFaction(targetHero->GetFaction());
    if (targetHero->IsLocal())
    {
      PFWorld* pWorld = GetWorld();
      if (pWorld && pWorld->GetTileMap() && pWorld->GetFogOfWar() && pWorld->GetAIWorld())
      {
        SVector tile = pWorld->GetTileMap()->GetTile(GetPosition().AsVec2D());
        int tileRadius = pWorld->GetTileMap()->GetLenghtInTiles(pWorld->GetAIWorld()->GetAIParameters().expandNatureGlyphsVisibilityRadius);
        visUnitID = pWorld->GetFogOfWar()->AddObject(tile, targetHero->GetFaction(), tileRadius);
      }
    }
  }
}

void PFNatureGlyph::SetPosition( const CVec3 newPosition )
{
  position = newPosition;
  PFWorld* pWorld = GetWorld();
  if (visUnitID >= 0 && pWorld && pWorld->GetFogOfWar() && pWorld->GetTileMap())
    pWorld->GetFogOfWar()->MoveObject(visUnitID, pWorld->GetTileMap()->GetTile(position.AsVec2D()));
}

void PFNatureGlyph::SetVisible( bool newVisible )
{
  if (bVisible == newVisible)
    return;

  bVisible = newVisible;
  UpdateHiddenState(bVisible);
}

void PFNatureGlyph::Destroy()
{
  PFWorld* pWorld = GetWorld();
  if (visUnitID >= 0 && pWorld && pWorld->GetFogOfWar())
    pWorld->GetFogOfWar()->RemoveObject(visUnitID);
  visUnitID = -1;
  Die();
}

void PFNatureGlyph::Reset()
{
  PFWorld* pWorld = GetWorld();
  if (visUnitID >= 0 && pWorld && pWorld->GetFogOfWar())
    pWorld->GetFogOfWar()->RemoveObject(visUnitID);

  PFPickupableObjectBase::Reset();
  visUnitID = -1;
  if (IsValid(targetHero) && targetHero->IsLocal())
  {
    pWorld = GetWorld();
    if (pWorld && pWorld->GetTileMap() && pWorld->GetFogOfWar() && pWorld->GetAIWorld())
    {
      SVector tile = pWorld->GetTileMap()->GetTile(GetPosition().AsVec2D());
      int tileRadius = pWorld->GetTileMap()->GetLenghtInTiles(pWorld->GetAIWorld()->GetAIParameters().expandNatureGlyphsVisibilityRadius);
      visUnitID = pWorld->GetFogOfWar()->AddObject(tile, targetHero->GetFaction(), tileRadius);
    }
  }
}

void PFNatureGlyph::OnPickedUp( const CPtr<PFBaseHero>& pPicker )
{
  (void)pPicker;
}

} //namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFGlyph,        NWorld);
REGISTER_WORLD_OBJECT_NM(PFNatureGlyph,  NWorld);
REGISTER_WORLD_OBJECT_NM(PFGlyphSpawner,  NWorld);

#else

#include "PFGlyph.h"
#include "PFMaleHero.h"
#include "TileMap.h"
#include "PFAbilityData.h"
#include "PFAIWorld.h"
#include "PFAdvMapObject.h"
#include "DBSessionRoots.h"
#include "PFGlyphManager.h"
#include "PFAIContainer.h"

#include "WarFog.h"
#include "../Scene/DiAnGr.h"

#ifndef VISUAL_CUTTED
#include "PFClientChest.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif

#include "../System/RandomGen.h"

namespace NWorld
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFGlyphSpawner::PFGlyphSpawner( const CPtr<PFWorld>& pWorld, const NDb::AdvMapObject &dbObject )
: PFWorldObjectBase( pWorld, 1 )
, position( dbObject.offset.GetPlace().pos )
, spawnOffset(-1.0f)
, hidden(false)
, lastGlyph(0)
{ 
  pDesc = dynamic_cast<const NDb::GlyphSpawner*>( dbObject.gameObject.GetPtr() );

  if(PFAIWorld const* pAIWorld = pWorld ? pWorld->GetAIWorld() : NULL)
    pManager = pAIWorld->GetGlyphsManager();
  if( !IsValid(pManager) )
  {
    NI_ALWAYS_ASSERT("Failed to retreive glyphs manager!");
    return;
  }

  scriptName = dbObject.scriptName;
  NDb::GlyphSettings const& settings = GetSettings();
  spawnOffset = pWorld->GetRndGen()->Next(settings.minNormalSpawnDelay, settings.maxNormalSpawnDelay);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const NDb::GlyphSettings& NWorld::PFGlyphSpawner::GetSettings() const
{
  if ( pDesc->useGlyphManager )
    return pManager->GetSettings();
  return pDesc->settings;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CPtr<NWorld::PFGlyph> PFGlyphSpawner::CreateGlyph( CVec3 const& position )
{
  CPtr<NWorld::PFGlyph> glyph;

  if ( pDesc->useGlyphManager )
  {
    glyph = pManager->CreateGlyph( position );
  }
  else 
  {
    const NDb::GlyphsDB* glyphs = pDesc->glyphs;
    if ( !glyphs )
      glyphs = pManager->GetGlyphsDB();

    const NDb::GlyphEntry* pGlyphEntry = 0;

    if ( pDesc->settings.spawnMode == NDb::GLYPHSPAWNMODE_RANDOMWEIGHT )
    {
      struct WeightGetter
      {
        int operator()( const NDb::GlyphEntry& item ) const
        {
          return item.weight;
        }
      } wg;

      pGlyphEntry = &(GetWorld()->GetRndGen()->RollFromContainerByWeight( glyphs->glyphs, wg ));
    }
    else if ( pDesc->settings.spawnMode == NDb::GLYPHSPAWNMODE_ORDERED )
    {
      pGlyphEntry = &(glyphs->glyphs[lastGlyph]);
      lastGlyph = ( lastGlyph + 1 ) % glyphs->glyphs.size();
    }

    NI_DATA_VERIFY( pGlyphEntry && pGlyphEntry->glyph, "Invalid glyph entry", return 0 );

    glyph = new PFGlyph( GetWorld(), pGlyphEntry->glyph, position );
  }

  if ( !glyph )
    return glyph;

  glyph->Hide( hidden );

  if ( !scriptName.empty() )
  {
    string name = scriptName + "_glyph";
    glyph->SetScriptName( name );
    GetWorld()->GetAIContainer()->RegisterObject( glyph, name, "" );
  }

  return glyph;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFGlyphSpawner::Hide(bool hide)
{
  if ( hidden == hide )
    return;
  hidden = hide;
  if ( IsValid(pSpawnedGlyph) )
    pSpawnedGlyph->Hide( hidden );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFGlyphSpawner::Step(float dtInSeconds)
{
  NI_PROFILE_FUNCTION;

  if(IsValid(pManager) && !IsValid(pSpawnedGlyph) )
  {
    NDb::GlyphSettings const& settings = GetSettings();

    if( spawnOffset < 0.0f )
    {
      spawnOffset = pWorld->GetRndGen()->Next(settings.minAfterPickupSpawnDelay, settings.maxAfterPickupSpawnDelay);
    }
    else
    {
      spawnOffset -= dtInSeconds;
      if( spawnOffset < EPS_VALUE)
      {
        if( pSpawnedGlyph = CreateGlyph( position ) )
        {
          spawnOffset = -1.0f;
        }
        else
        {
          spawnOffset = pWorld->GetRndGen()->Next(settings.minNormalSpawnDelay, settings.maxNormalSpawnDelay);
          spawnOffset = max(settings.minNormalSpawnDelay, spawnOffset - settings.spawnAttemptDelayDecrease);   
        }
      } 
    }
  }

  return PFWorldObjectBase::Step(dtInSeconds);
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NAMEMAP_BEGIN(PFGlyph)
NAMEMAP_FUNC_RO(name, &PFGlyph::GetName);
NAMEMAP_END

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFGlyph::PFGlyph( const CPtr<PFWorld>& pWorld, const NDb::Ptr<NDb::Glyph>& pGlyphDesc, const CVec3& position )
: PFPickupableObjectBase( pWorld, position, pGlyphDesc->gameObject )
, pDesc(pGlyphDesc)
, visUnitID1( -1 )
, visUnitID2( -1 )
, enabled( true )
, hidden( false )
, glyphNumber(-1)
{
  NI_VERIFY( pGlyphDesc->gameObject, "Need gameObject to create Glyph!", return; );
  CreateClientObject<NGameX::PFClientGlyph>( NGameX::PFClientLogicObject::CreatePars(pWorld->GetScene(), BADNODENAME, pGlyphDesc->gameObject.GetPtr()), pGlyphDesc->color );

  const NDb::AdvMapSettings* desc = IsValid(GetWorld()->GetMapDescription()->mapSettings) ? GetWorld()->GetMapDescription()->mapSettings 
                                                                                          : GetWorld()->GetMapDescription()->map->mapSettings;
  if ( desc && IsValid(desc->overrideGlyphSettings) )
  {
    for ( int i = 0; i < desc->overrideGlyphSettings->glyphs->glyphs.size(); i++ )
    {
      if ( pDesc->GetDBID() == desc->overrideGlyphSettings->glyphs->glyphs[i].glyph->GetDBID() )
      {
        glyphNumber = i;
        break;
      }
    }
  }
  else
  {
    NDb::Ptr<NDb::SessionRoot> pRoot = NDb::SessionRoot::GetRoot();
    NDb::Ptr<NDb::GlyphsDB> pDBGlyphs = pRoot->logicRoot->glyphsDB;
    for ( int i = 0; i < pDBGlyphs->glyphs.size(); i++ )
    {
      if ( pDesc->GetDBID() == pDBGlyphs->glyphs[i].glyph->GetDBID() )
      {
        glyphNumber = i;
        break;
      }
    }
  }

  NDb::AdvMapObject dbObject;
  dbObject.gameObject = pGlyphDesc->gameObject;
  dbObject.offset = CPlacement( position, QNULL, CVec3(1.f, 1.f, 1.f));

  vector<SVector> occupiedTiles;

  TileMap * tileMap = GetWorld()->GetTileMap();

  MarkObject(tileMap, dbObject, occupiedTiles);
  tileMap->MarkObject(occupiedTiles, true, MAP_MODE_BUILDING);

  float fObjectSize = pGlyphDesc->gameObject->lockMask.tileSize;

  if (fObjectSize <= 0)
  {
    NDb::SingleStateObject const * pSingleStateObject; 
    pSingleStateObject = dynamic_cast<NDb::SingleStateObject const *>(pGlyphDesc->gameObject.GetPtr());
    NI_VERIFY( pSingleStateObject, "Failed to downcast GameObject of Glyph to SingleStateObject!", return; );

    NI_DATA_VERIFY( pSingleStateObject->sceneObject && 
      pSingleStateObject->sceneObject->attached.size() > 0 &&
      pSingleStateObject->sceneObject->attached[0].component &&
      pSingleStateObject->sceneObject->attached[0].component->attached.size() > 0 &&
      pSingleStateObject->sceneObject->attached[0].component->attached[0].component, 
      "Failed to access attached object in SceneObject of Glyph!", return; );

    NDb::DBStaticSceneComponent const * pdbStaticSceneComponent;
    pdbStaticSceneComponent = dynamic_cast<NDb::DBStaticSceneComponent const *>(pSingleStateObject->sceneObject->attached[0].component->attached[0].component.GetPtr());
    NI_VERIFY( pdbStaticSceneComponent, "Failed to downcast attached component of Glyph to DBStaticSceneComponent!", return; );

    NDb::AABB const * pAABB = &pdbStaticSceneComponent->aabb;

    if (pAABB->maxX != -666 && pAABB->minX != 666 && pAABB->maxY != -666 && pAABB->minY != 666)
    {
      fObjectSize = sqrt( fabs2(pAABB->maxX - pAABB->minX) + fabs2(pAABB->maxY - pAABB->minY) );
    }
  }

  const float fTileSize   = tileMap->GetTileSize();

  int objectTileSize    = (fTileSize == 0.0f) ? (0) : (ceil( fObjectSize / fTileSize ));
  int objectDynTileSize = fObjectSize;
  float objectSize      = fObjectSize * fTileSize; // rounded up to tile size

  SetObjectSizes(objectSize, objectTileSize, objectDynTileSize);

  OpenWarFog();
  // Warfog
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFGlyph::Reset()
{
  PFPickupableObjectBase::Reset();
  CreateClientObject<NGameX::PFClientGlyph>( NGameX::PFClientLogicObject::CreatePars(GetWorld()->GetScene(), BADNODENAME, pDesc->gameObject.GetPtr()), pDesc->color );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFGlyph::Hide(bool hide)
{
  if ( hidden == hide )
    return;

  hidden = hide;
  if ( hide )
    CloseWarFog();
  else
    OpenWarFog();

  UpdateHiddenState(!hidden);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFGlyph::OpenWarFog()
{
  if ( pDesc->openWarFogRadius > 0.0f )
  {
    TileMap * tileMap = GetWorld()->GetTileMap();

    SVector pos = tileMap->GetTile(GetPosition().AsVec2D());
    int tileRadius = tileMap->GetLenghtInTiles( pDesc->openWarFogRadius );

    visUnitID1 = GetWorld()->GetFogOfWar()->AddObject(pos, FCTN_A, tileRadius);
    visUnitID2 = GetWorld()->GetFogOfWar()->AddObject(pos, FCTN_B, tileRadius);
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFGlyph::CloseWarFog()
{
  if ( visUnitID1 >= 0 )
    GetWorld()->GetFogOfWar()->RemoveObject(visUnitID1);
  if ( visUnitID2 >= 0 )
    GetWorld()->GetFogOfWar()->RemoveObject(visUnitID2);
  visUnitID1 = visUnitID2 = -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFGlyph::OnPickedUp( PFBaseUnit* pPicker )
{
  pPicker->OnGlyphPickUp( this );
  PFBaseUnitPickupEvent ev( NDb::BASEUNITEVENT_PICKUP, true, glyphNumber, scriptName );
  pPicker->EventHappened( ev );
  if ( ev.NeedActivate() )
    pPicker->UseExternalAbility( pDesc.GetPtr(), Target(pPicker) );
}

void PFGlyph::OnDie()
{
  CloseWarFog();

  PFLogicObject::OnDie();
}


bool PFGlyph::CanBePickedUpBy( const PFBaseUnit* pPicker ) const
{
  if ( !enabled )
    return false;

  return PFPickupableObjectBase::CanBePickedUpBy( pPicker );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
NAMEMAP_BEGIN( PFNatureGlyph )
NAMEMAP_FUNC_RO( name, &PFNatureGlyph::GetName );
NAMEMAP_END

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFNatureGlyph::PFNatureGlyph( const CPtr<PFBaseMaleHero>& targetHero, NDb::ERoute routeID, const CPtr<PFWorld>& pWorld, const NDb::Ptr<NDb::GameObject> _gameObject, const CVec3& position ) 
: PFPickupableObjectBase(pWorld, position, _gameObject)
, targetHero(targetHero)
, routeID(routeID)
, bVisible(true)
, gameObject(_gameObject)
{
  NI_VERIFY( gameObject && dynamic_cast<const NDb::SingleStateObject*>(gameObject.GetPtr()), "Game object must be a SingleStateObject", return; );

  NDb::AdvMapObject dbObject;
  dbObject.gameObject = gameObject;
  dbObject.offset     = CPlacement( position, QNULL, CVec3(1.f, 1.f, 1.f));

  vector<SVector> occupiedTiles;
  MarkObject(pWorld->GetTileMap(), dbObject, occupiedTiles);
  pWorld->GetTileMap()->MarkObject(occupiedTiles, true, MAP_MODE_BUILDING);


  if( !IsValid(targetHero) )
  {
    NI_ALWAYS_ASSERT( "Target hero must be valid!");
    return;
  }

  ChangeFaction(targetHero->GetFaction());

  if ( targetHero->IsLocal() )
  {
    NI_VERIFY( gameObject, "Need gameObject to create Glyph!", return; );
    CreateClientObject<NGameX::PFClientChest>( NGameX::PFClientLogicObject::CreatePars(pWorld->GetScene(), BADNODENAME, gameObject) );

    TileMap * tileMap = GetWorld()->GetTileMap();
    SVector pos = tileMap->GetTile(GetPosition().AsVec2D());
    int tileRadius = tileMap->GetLenghtInTiles(GetWorld()->GetAIWorld()->GetAIParameters().expandNatureGlyphsVisibilityRadius);

    visUnitID = GetWorld()->GetFogOfWar()->AddObject(pos, targetHero->GetFaction(), tileRadius);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFNatureGlyph::OnPickedUp( const CPtr<PFBaseHero>& pPicker )
{
  NI_ASSERT( targetHero == pPicker, "Picker is not a target hero!" );
  GetWorld()->GetAIWorld()->OnNatureGlyphUsed( routeID, targetHero );
}

void PFNatureGlyph::Destroy()
{
  GetWorld()->GetFogOfWar()->RemoveObject(visUnitID);
  visUnitID = -1;
  Die();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFNatureGlyph::SetPosition( const CVec3 newPosition )
{
  position = newPosition;
  if ( IsValid(ClientObject()) ) // client object may be not valid, if targetHero is not local!
    CALL_CLIENT_1ARGS(OnMove, position);

  GetWorld()->GetFogOfWar()->MoveObject(visUnitID, GetWorld()->GetTileMap()->GetTile(position.AsVec2D()));
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFNatureGlyph::SetVisible( bool newVisible )
{
  if ( bVisible != newVisible )
  {
    bVisible = newVisible;
    UpdateHiddenState(bVisible);
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFNatureGlyph::Reset()
{
  PFPickupableObjectBase::Reset();
  if( !IsValid(targetHero) )
    return;
  if ( targetHero->IsLocal() )
  {
    CreateClientObject<NGameX::PFClientChest>( NGameX::PFClientLogicObject::CreatePars(GetWorld()->GetScene(), BADNODENAME, gameObject) );

    TileMap * tileMap = GetWorld()->GetTileMap();
    SVector pos = tileMap->GetTile(GetPosition().AsVec2D());
    int tileRadius = tileMap->GetLenghtInTiles(GetWorld()->GetAIWorld()->GetAIParameters().expandNatureGlyphsVisibilityRadius);

    if ( visUnitID > -1 )
      visUnitID = GetWorld()->GetFogOfWar()->AddObject(pos, targetHero->GetFaction(), tileRadius);
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
} //namespace NWorld

REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFGlyph,           NWorld);
REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFNatureGlyph,     NWorld);
REGISTER_WORLD_OBJECT_NM(PFGlyphSpawner,  NWorld);
#endif
