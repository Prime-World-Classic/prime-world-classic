#include "stdafx.h"
#include "PFGlyphManager.h"

#include "DBSessionRoots.h"
#include "PFWorld.h"
#include "PFGlyph.h"
#include "PFClientObjectHelpers.h"

#include "Scene/DiAnGr.h"
#if defined(PW_LINUX_NULL_RENDER)
#include <cmath>
#include <limits>
#endif

namespace NWorld
{
PFGlyphsManager::PFGlyphsManager(CPtr<PFWorld> const& pWorld)
: PFWorldObjectBase( pWorld, 0 )
, pWorld(pWorld)
, lastGlyph(0)
{
}

void NWorld::PFGlyphsManager::Init( const NDb::GlyphManagerParams* overrideGlyphSettings )
{
#if defined(PW_LINUX_NULL_RENDER)
	// Missing overrides remain authoritative; never borrow a different table's index space.
	glyphs.clear(); pDBGlyphs = nullptr; settings = NDb::GlyphSettings(); lastGlyph = 0;
	if (overrideGlyphSettings)
	{
		pDBGlyphs = overrideGlyphSettings->glyphs; settings = overrideGlyphSettings->settings;
	}
	else
	{
		const auto& root = NDb::SessionRoot::GetRoot();
		if (root && root->logicRoot)
		{
			pDBGlyphs = root->logicRoot->glyphsDB;
			if (root->logicRoot->aiLogic) settings = root->logicRoot->aiLogic->glyphSettings;
		}
	}
	if (pDBGlyphs) glyphs.resize(pDBGlyphs->glyphs.size());
#else
  if ( overrideGlyphSettings == 0 )
  {
    NDb::Ptr<NDb::SessionRoot> pRoot = NDb::SessionRoot::GetRoot();

    pDBGlyphs = pRoot->logicRoot->glyphsDB;
    settings  = pRoot->logicRoot->aiLogic->glyphSettings;
  }
  else 
  {
    pDBGlyphs = overrideGlyphSettings->glyphs;
    settings  = overrideGlyphSettings->settings;
  }

  NI_VERIFY( IsValid( pDBGlyphs ), "Cannot find glyphs DB!" , return );

  GetWorld()->GetIAdventureScreen()->PreloadEffectsInResourceTree( pDBGlyphs, BADNODENAME );

  glyphs.resize(pDBGlyphs->glyphs.size());
#endif
}


CPtr<NWorld::PFGlyph> PFGlyphsManager::CreateGlyph( CVec3 const& position)
{
#if defined(PW_LINUX_NULL_RENDER)
	// Keep invalid geometry out of the original voxel/tile conversions and reject stale tables.
	if (!IsValid(pWorld) || !pDBGlyphs || glyphs.size() != pDBGlyphs->glyphs.size() || settings.maxInstances <= 0
		|| !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)
		|| position.x < 0 || position.y < 0 || position.x >= pWorld->GetMapSize().x || position.y >= pWorld->GetMapSize().y) return nullptr;
#endif
  vector<int> emptyIndices;
  for(int i = 0, count = glyphs.size(); i < count; ++i)
  { 
#if defined(PW_LINUX_NULL_RENDER)
		if (!IsValid(glyphs[i]) || glyphs[i]->IsObjectDead())
#else
    if( !IsValid( glyphs[i] ) )
#endif
      emptyIndices.push_back(i);
  }
  
  if( settings.maxInstances <= ( glyphs.size() - emptyIndices.size() ) || emptyIndices.empty() )
    return NULL; // can`t create glyphs -> too much glyphs are already created

  int slotId = 0;

  if ( settings.spawnMode == NDb::GLYPHSPAWNMODE_RANDOMWEIGHT )
  {
    struct WeightGetter : NonCopyable
    {
      WeightGetter( const vector<NDb::GlyphEntry>& _container ) : container(_container) {}
      const vector<NDb::GlyphEntry>& container;
      int operator()( const int& item ) const
      {
        return container[item].weight;
      }
    } wg( pDBGlyphs->glyphs );

#if defined(PW_LINUX_NULL_RENDER)
		// The original helper sums signed ints and returns a borrowed default on an empty roll.
		int64 total = 0;
		for (int index : emptyIndices)
		{
			const auto& entry = pDBGlyphs->glyphs[index];
			if (entry.weight < 0 || (entry.weight > 0 && !entry.glyph)) return nullptr;
			total += entry.weight;
			if (total > std::numeric_limits<int>::max()) return nullptr;
		}
		if (total == 0) return nullptr;
#endif
    slotId = GetWorld()->GetRndGen()->RollFromContainerByWeight( emptyIndices, wg );
  }
  else if ( settings.spawnMode == NDb::GLYPHSPAWNMODE_ORDERED )
  {
#if defined(PW_LINUX_NULL_RENDER)
		// Do not overwrite an occupied ordered slot and lose its shared-capacity accounting.
		if (lastGlyph < 0 || lastGlyph >= glyphs.size() || !pDBGlyphs->glyphs[lastGlyph].glyph
			|| (IsValid(glyphs[lastGlyph]) && !glyphs[lastGlyph]->IsObjectDead())) return nullptr;
#endif
    slotId = lastGlyph;
    lastGlyph = ( lastGlyph + 1 ) % pDBGlyphs->glyphs.size();
  }
#if defined(PW_LINUX_NULL_RENDER)
	else return nullptr;
#endif

  const NDb::GlyphEntry& glyphEntry = pDBGlyphs->glyphs[slotId];

  NI_DATA_VERIFY( glyphEntry.glyph, "Invalid glyph entry", return 0 );
  
  glyphs[slotId] = new PFGlyph( pWorld, glyphEntry.glyph, position );
  
  return glyphs[slotId];
} 

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFGlyphsManager, NWorld);
