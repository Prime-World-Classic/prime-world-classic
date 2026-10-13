#include "stdafx.h"

#include "PFFlagpole.h"
#include "PFWorldNatureMap.h"
#include "PFAIWorld.h"
#include "../Terrain/Terrain.h"
#include "../System/InlineProfiler.h"

#if defined( PW_LINUX_NULL_RENDER ) && !defined( PW_LINUX_TERRAIN_RUNTIME_PROBE )
#define PW_LINUX_NATUREMAP_NULL_RENDER 1
#include <cmath>
#endif

#ifndef VISUAL_CUTTED
#include "PFClientNatureMap.h"
#include "AdventureScreen.h"
#include "../Terrain/NatureMap.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif

namespace NWorld
{
#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
namespace
{
	/** Reuse the original non-editable loader; missing files never generate factions. */
	class LinuxNatureQuery : public Terrain::NatureMap
	{
		Terrain::NatureMap::NatureMapElementIdList modified;
	public:
		LinuxNatureQuery() : Terrain::NatureMap(false) {}
		bool Load(const NDb::DBNatureMap& db, const CVec2& size)
		{
			const Terrain::NatureMapParams params = {&db, CVec3(size, 50.f)};
			const bool loaded = Initialize(params, &modified);
			modified.clear();
			return loaded;
		}
		bool Advance(float dt)
		{
			const bool changed = OnStep(dt);
			// Headless worlds have no visual consumer to drain this transient list.
			modified.clear();
			return changed;
		}
	protected:
		bool OnLoadAtRuntime(Stream*, int, bool) override { return false; }
		bool OnSaveAtRuntime(Stream*, bool) const override { return false; }
	};
}

PFWorldNatureMap::~PFWorldNatureMap()
{
	// NatureMap has a non-virtual destructor; destroy the actual owned type.
	delete static_cast<LinuxNatureQuery*>(pMap);
}
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PFWorldNatureMap::PFWorldNatureMap(PFWorld *pWorld)
: PFWorldObjectBase( pWorld, 1 )
, pMap(0)
, customNaturePercents(VNULL2)
#ifndef _SHIPPING
, overrideFaction( NDb::FACTION_NEUTRAL )
, overridePoint( VNULL2 )
, overrideRadius( 0.0f )
, overrideTime( -1.0f )
#endif
{
  CreateClientObject<NGameX::PFClientNatureMap>();
}

void PFWorldNatureMap::Reset()
{
	PFWorldObjectBase::Reset();
	CreateClientObject<NGameX::PFClientNatureMap>();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PFWorldNatureMap::OnLoaded(const NDb::Ptr<NDb::Terrain>& pDBTerrain_)
{
  NI_PROFILE_FUNCTION

#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
	delete static_cast<LinuxNatureQuery*>(pMap);
	pMap = 0;
#endif
  pDBTerrain = pDBTerrain_;
  NI_VERIFY( pDBTerrain && pDBTerrain->natureMap.attackSpaceRoads.size(), "DBNatureMap not defined properly!", return; );
  NI_VERIFY( pDBTerrain && pDBTerrain->natureMap.logicRoads.size(), "DBNatureMap not defined properly!", return; );

#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
	if (GetWorld())
	{
		LinuxNatureQuery* candidate = new LinuxNatureQuery;
		if (candidate->Load(pDBTerrain->natureMap, GetWorld()->GetMapSize()))
			pMap = candidate;
		else
			delete candidate;
	}
#else
  if (PFWorld * pWorld = GetWorld())
    if (NScene::IScene* pScene = pWorld->GetScene())
      if (Terrain::Terrain* pTerrain = pScene->GetTerrain())
        pMap = &pTerrain->GetNatureMap();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int PFWorldNatureMap::LocateElement(float x, float y) const
{
#if defined( PW_LINUX_NATUREMAP_NULL_RENDER )
	if (!pMap || !std::isfinite(x) || !std::isfinite(y)) return -1;
	// The original hex locator converts floor(float) to int. Reject far outside
	// the grid first, retaining its boundary-cell selection without clamping.
	const double gx = double(x) / pMap->GetElementSize();
	const double gy = double(y) / pMap->GetElementSize();
	if (gx < -2 || gy < -2 || gx > pMap->GetSizeX() + 2.0 || gy > pMap->GetSizeY() + 2.0) return -1;
	return pMap->LocateElement(x, y);
#else
  return pMap ? pMap->LocateElement(x, y) : Terrain::NATUREMAPELEMENTID_BAD;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int PFWorldNatureMap::GetNatureInPoint(float x, float y) const
{
#if defined( PW_LINUX_NATUREMAP_NULL_RENDER )
	return GetNatureInPoint(LocateElement(x, y));
#else
  return pMap ? pMap->GetNature(x, y) : 0;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int PFWorldNatureMap::GetNatureInPoint(int id) const
{
#if defined( PW_LINUX_NATUREMAP_NULL_RENDER )
	return pMap && id >= 0 && id < pMap->GetSizeX() * pMap->GetSizeY() ? pMap->GetNature(id) : 0;
#else
  return pMap ? pMap->GetNature(id) : 0;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool PFWorldNatureMap::IsAtBase(float x, float y) const
{
#if defined( PW_LINUX_NATUREMAP_NULL_RENDER )
	// Reuse the guarded hex locator before the original stored attack-space test.
	return LocateElement(x, y) >= 0 && pMap->IsAtBase(x, y);
#else
  return pMap ? pMap->IsAtBase(x, y) : false;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float PFWorldNatureMap::GetNaturePercent(NDb::EFaction faction) const
{
  NI_STATIC_ASSERT(NDb::KnownEnum<NDb::EFaction>::sizeOf == NDb::KnownEnum<NDb::ENatureType>::sizeOf, ASSUME_FACTIONS_EQ_NATURES);
#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
	if (faction < NDb::FACTION_NEUTRAL || faction > NDb::FACTION_BURN) return 0.f;
#endif
  if ( pDBTerrain && pDBTerrain->natureMap.editableNatureMap )
  {
#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
		// Editable maps retain their original custom A/B/neutral query policy.
		const float a = customNaturePercents.m[0], b = customNaturePercents.m[1];
		if (!std::isfinite(a) || !std::isfinite(b) || a < 0.f || b < 0.f || a > 1.f || b > 1.f || a + b > 1.f) return 0.f;
#endif
    return faction == NDb::FACTION_FREEZE ? customNaturePercents.m[0] : 
      ( faction == NDb::FACTION_BURN ? customNaturePercents.m[1] : 1.f - customNaturePercents.m[0] - customNaturePercents.m[1]  );
  }

#if defined( PW_LINUX_NATUREMAP_NULL_RENDER )
	if (!pMap || faction == NDb::FACTION_NEUTRAL) return 0.f;
	const NDb::DBNatureMap& db = pMap->GetDB();
	if (!std::isfinite(db.baseAPercent) || !std::isfinite(db.baseBPercent)
		|| db.baseAPercent < 0.f || db.baseBPercent < 0.f || db.baseAPercent > 100.f || db.baseBPercent > 100.f
		|| db.baseAPercent + db.baseBPercent > 100.f) return 0.f;
	const float result = pMap->GetNaturePercent((NDb::ENatureType)faction);
	return std::isfinite(result) ? result : 0.f;
#else
  return pMap ? pMap->GetNaturePercent((NDb::ENatureType)faction) : 0.0f;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float PFWorldNatureMap::GetNaturePercent(NDb::EFaction faction, NDb::ENatureRoad roadIndex) const
{
  NI_STATIC_ASSERT(NDb::KnownEnum<NDb::EFaction>::sizeOf == NDb::KnownEnum<NDb::ENatureType>::sizeOf, ASSUME_FACTIONS_EQ_NATURES);
  NI_STATIC_ASSERT(NDb::KnownEnum<NDb::ENatureRoad>::sizeOf == 3, THERE_MUST_BE_THREE_ROADS);
#if defined( PW_LINUX_NATUREMAP_NULL_RENDER )
	if (!pMap || (faction != NDb::FACTION_FREEZE && faction != NDb::FACTION_BURN)
		|| roadIndex < NDb::NATUREROAD_TOP || roadIndex > NDb::NATUREROAD_BOTTOM) return 0.f;
	const float result = pMap->GetNaturePercent((NDb::ENatureType)faction, roadIndex);
	return std::isfinite(result) ? result : 0.f;
#else
  return pMap ? pMap->GetNaturePercent((NDb::ENatureType)faction, roadIndex) : 0.0f;
#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorldNatureMap::SetCustomNaturePercents( float freeze, float burn )
{
  NI_VERIFY( pDBTerrain->natureMap.editableNatureMap, "Custom nature percent can be used on map with editable terrain", return );
  NI_VERIFY( freeze + burn <= 1.f, "Invalid custom nature percent: should be <1 total", freeze /= freeze + burn; burn /= freeze + burn );
  customNaturePercents.m[0] = freeze;
  customNaturePercents.m[1] = burn;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int PFWorldNatureMap::GetNumSegments() const
{
#if defined( PW_LINUX_NATUREMAP_NULL_RENDER )
	return pMap ? pMap->GetNumSegments() : 0;
#else
  return pMap ? pMap->GetNumSegments() : 0;
#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PFWorldNatureMap::GetNatureBorders(NDb::EFaction faction, CVec2 roadPoints[]) const
{
#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
	if (!pMap || !roadPoints || (faction != NDb::FACTION_FREEZE && faction != NDb::FACTION_BURN)) return;
	CVec2 candidate[3];
	for (int road = 0; road < 3; ++road)
	{
		candidate[road] = pMap->GetNatureBound((NDb::ENatureType)faction, (NDb::ENatureRoad)road);
		if (!std::isfinite(candidate[road].x) || !std::isfinite(candidate[road].y)) return;
	}
	// Invalid geometry must not partially replace the caller's three road points.
	for (int road = 0; road < 3; ++road) roadPoints[road] = candidate[road];
#else
  if (pMap)
  {
    NI_STATIC_ASSERT(NDb::KnownEnum<NDb::EFaction>::sizeOf == NDb::KnownEnum<NDb::ENatureType>::sizeOf, ASSUME_FACTIONS_EQ_NATURES);
    NI_STATIC_ASSERT(NDb::KnownEnum<NDb::ENatureRoad>::sizeOf == 3, THERE_MUST_BE_THREE_ROADS);
    roadPoints[0] = pMap->GetNatureBound((NDb::ENatureType)faction, NDb::NATUREROAD_TOP);
    roadPoints[1] = pMap->GetNatureBound((NDb::ENatureType)faction, NDb::NATUREROAD_CENTER);
    roadPoints[2] = pMap->GetNatureBound((NDb::ENatureType)faction, NDb::NATUREROAD_BOTTOM);
  }
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorldNatureMap::GetNatureSegment(CVec2 const &pos, NDb::ENatureRoad &road, int &segment) const
{
#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
	if (pMap && std::isfinite(pos.x) && std::isfinite(pos.y))
		pMap->GetNatureSegment(pos, road, segment);
#else
  if (pMap)
    pMap->GetNatureSegment(pos, road, segment);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorldNatureMap::SetDesiredPosition(NDb::EFaction faction, NDb::ENatureRoad road, int segment)
{
  NI_VERIFY(faction == 1 || faction == 2, "Invalid faction index", return;); 
  NI_VERIFY(road >= 0 && road <= 2, "Invalid road", return;);  
  NI_STATIC_ASSERT(NDb::KnownEnum<NDb::EFaction>::sizeOf == NDb::KnownEnum<NDb::ENatureType>::sizeOf, ASSUME_FACTIONS_EQ_NATURES);
  
#if !defined( PW_LINUX_NATUREMAP_NULL_RENDER )
  if (pMap)
    pMap->SetDesiredPosition((NDb::ENatureType)faction, road, segment);
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PFWorldNatureMap::Expand(NDb::EFaction faction, NDb::ENatureRoad roadIndex, int numSegments)
{
  NI_ALWAYS_ASSERT("Obsolete method PFWorldNatureMap::Expand is called. The caller must be refactored according to new design.");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PFWorldNatureMap::Override(NDb::EFaction faction, const CCircle & circle, float lifeTime, unsigned ownerData)
{
  NI_STATIC_ASSERT(NDb::KnownEnum<NDb::EFaction>::sizeOf == NDb::KnownEnum<NDb::ENatureType>::sizeOf, ASSUME_FACTIONS_EQ_NATURES);
#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
	if (pMap) pMap->Override((NDb::ENatureType)faction, circle, lifeTime, ownerData);
#else
  if (pMap)
    pMap->Override((NDb::ENatureType)faction, circle, lifeTime, ownerData);
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorldNatureMap::DebugOverride(NDb::EFaction faction, CVec2 const &center, float radius, float lifeTime)
{
#ifndef _SHIPPING
  overrideFaction = faction;
  overridePoint = center;
  overrideRadius = radius;
  overrideTime = lifeTime;
#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace
{
#if !defined( PW_LINUX_NATUREMAP_NULL_RENDER )
  struct NatureChnager : public NonCopyable
  {
  public :
    NatureChnager(Terrain::NatureMap& nmap, NDb::ENatureType from, NDb::ENatureType to )
      : nmap(nmap), from(from), to(to) {}
    void operator()(const CVec2& elemPos, Terrain::NatureMapElementId id, Terrain::NatureMapElementInfo const& info)
    {  
      if ( (int)from >= 0 && info.natureBase != (uint)from )
        return;

      nmap.EditNature( id, to );
    }

    Terrain::NatureMap& nmap;
    NDb::ENatureType from;
    NDb::ENatureType to;
  }; 
#endif

}
void PFWorldNatureMap::ChangeNature( CVec2 const &pos, float radius, NDb::ENatureType from, NDb::ENatureType to )
{
#if !defined( PW_LINUX_NATUREMAP_NULL_RENDER )
  if (!pMap)
    return;

  if ( radius <= 0 )
    return;

  NatureChnager func( *pMap, from, to );
  pMap->ForAllElementsInCircle( func, pos, radius );
#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorldNatureMap::ChangeNature( const vector<CVec2>& polygon, NDb::ENatureType from, NDb::ENatureType to )
{
#if !defined( PW_LINUX_NATUREMAP_NULL_RENDER )
  if (!pMap)
    return;
  if( polygon.empty() )
    return;

  NatureChnager func( *pMap, from, to );
  pMap->ForAllElementsInPolygon( func, polygon );
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFWorldNatureMap::IsOverrideActive(unsigned int ownerData) const
{
#if defined( PW_LINUX_NATUREMAP_NULL_RENDER )
	return pMap && pMap->IsOverrideActive(ownerData);
#else
  return pMap ? pMap->IsOverrideActive(ownerData) : false;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFWorldNatureMap::OnStep(float dt)
{
  NI_PROFILE_FUNCTION

#if defined(PW_LINUX_NATUREMAP_NULL_RENDER)
	if (!pMap || !std::isfinite(dt) || dt < 0.f) return;
	if (static_cast<LinuxNatureQuery*>(pMap)->Advance(dt) && GetWorld() && GetWorld()->GetAIWorld())
	{
		struct UpdateNature
		{
			void operator()(PFLogicObject& object) { object.UpdateNatureType(); }
		} update;
		GetWorld()->GetAIWorld()->ForAllObjects(update);
	}
#else
  if (pMap)                                                                                                                                        
  {
#ifndef _SHIPPING
    // execute postponed debug command
    if (overrideTime > 0.0f)
    {
      static unsigned int overrideId = 0x000FFFFF; // Just a big number so we won't get ids collision
      NI_STATIC_ASSERT(NDb::KnownEnum<NDb::EFaction>::sizeOf == NDb::KnownEnum<NDb::ENatureType>::sizeOf, ASSUME_FACTIONS_EQ_NATURES);
      pMap->Override((NDb::ENatureType)overrideFaction, CCircle(overridePoint, overrideRadius), overrideTime, overrideId);
      ++overrideId;
      overrideTime = -1.0f;
    }
#endif

    bool changed = pMap->OnStep(dt);
    if (changed)
    {
      struct 
      {
        void operator()(PFLogicObject& o) { o.UpdateNatureType(); }
      } updateNatureType;

      if (PFWorld * pWorld = GetWorld())
      {
        pWorld->GetAIWorld()->ForAllObjects(updateNatureType);
      }
    }
  }
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int PFWorldNatureMap::operator&( IBinSaver &f )
{
  f.Add(1,(PFWorldObjectBase*)this); 
#if !defined( PW_LINUX_NATUREMAP_NULL_RENDER )
  f.Add(2,pMap);  
#endif
  f.Add(3, &customNaturePercents);
  return 0;
}


} // namespace

REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFWorldNatureMap, NWorld)
