#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFAIContainer.h"
#include "PFAIController.h"
#include "PFAIWorld.h"
#include "PFBuildings.h"
#include "PFCreep.h"
#include "PFHero.h"
#include "PFMainBuilding.h"
#include "PFMaleHero.h"
#include "PFPlayer.h"
#include "PFWorld.h"
namespace NWorld
{

Weak<PFScript> PFAIContainer::s_luaScript;

int PFScriptSerializer::operator&( IBinSaver &f )
{
  (void)f;
  return 0;
}

PFAIContainer::PFAIContainer( PFWorld* pWorld, NCore::ITransceiver *pTransceiver )
: PFWorldObjectBase( pWorld, 0 )
, transceiver( pTransceiver )
, currentLine( 0 )
, waitStep( 0 )
, logScriptEvents( false )
{
}

bool PFAIContainer::Step( float timeDelta )
{
  for( vector<CObj<IPFAIController>>::iterator it = controllers.begin(); it != controllers.end(); ++it )
  {
    if ( IsValid(*it) )
      (*it)->Step( timeDelta );
  }
  return true;
}

IPFAIController* PFAIContainer::Find( const PFBaseHero* pUnit ) const
{
  for ( int i = 0; i < controllers.size(); ++i )
  {
    if ( IsValid(controllers[i]) && static_cast<const PFBaseHero*>(controllers[i]->GetHero()) == pUnit )
      return controllers[i];
  }
  return 0;
}

IPFAIController* PFAIContainer::Add( PFBaseHero* pUnit, int lineNumber )
{
  if ( !IsValid(pUnit) )
    return 0;

  IPFAIController* ctl = Find( pUnit );
  if ( ctl )
    return ctl;

  int shift = 0;
  for ( int i = 0; i < controllers.size(); ++i )
  {
    if ( IsValid(controllers[i]) &&
         controllers[i]->GetLineNumber() == lineNumber &&
         controllers[i]->GetHero() &&
         pUnit->GetFaction() == controllers[i]->GetHero()->GetFaction() )
    {
      shift = ((shift + 2) % 3) - 1;
    }
  }

  ctl = new PFAIController( pUnit, transceiver, lineNumber, shift );
  controllers.push_back( ctl );
  return ctl;
}

IPFSeriesAIController* PFAIContainer::AddSeriesController( PFBaseHero* pUnit, bool lvlUpAvailable )
{
  (void)pUnit;
  (void)lvlUpAvailable;
  return 0;
}

void PFAIContainer::OnMinimapSignal( PFBaseHero* pSender, PFBaseUnit* pSelected, const Target& target )
{
  (void)pSender;
  (void)pSelected;
  (void)target;
}

bool PFAIContainer::Remove( PFBaseHero* pUnit )
{
  for ( int i = 0; i < controllers.size(); ++i )
  {
    if ( IsValid(controllers[i]) && static_cast<PFBaseHero*>(controllers[i]->GetHero()) == pUnit )
    {
      controllers[i]->StopHero();
      controllers.eraseByIndex(i);
      return true;
    }
  }
  return false;
}

void PFAIContainer::RemoveAll()
{
  controllers.clear();
}

void PFAIContainer::LoadScript( const vector<string>& _script )
{
  script = _script;
}

bool PFAIContainer::LoadScript( const string & scriptName, const vector<NDb::ResourceDesc> & res, bool isReconnecting )
{
  (void)scriptName;
  (void)isReconnecting;

  for ( int i = 0; i < res.size(); ++i )
  {
    if ( !res[i].key.empty() && IsValid(res[i].resource) )
      resources[res[i].key] = res[i].resource->GetDBID().GetFileName();
  }

  return false;
}

const NDb::ScriptArea* PFAIContainer::GetScriptArea( const char* name )
{
  for ( TScriptAreas::const_iterator it = scriptAreas.begin(); it != scriptAreas.end(); ++it )
  {
    if ( it->name == name )
      return &( *it );
  }
  return 0;
}

void PFAIContainer::GetScriptAreasByName( const string& name, vector<const NDb::ScriptArea*>& _scriptAreas )
{
  for ( TScriptAreas::const_iterator it = scriptAreas.begin(); it != scriptAreas.end(); ++it )
  {
    if ( it->name == name )
      _scriptAreas.push_back( &( *it ) );
  }
}

void PFAIContainer::RegisterScriptPath( const string &scriptName, const NDb::ScriptPath* gameObject )
{
  if ( gameObject )
    scriptPaths[scriptName] = gameObject->path;
}

const vector<CVec2>* PFAIContainer::GetScriptPath( const string &scriptName )
{
  TScriptPaths::const_iterator it = scriptPaths.find(scriptName);
  return it == scriptPaths.end() ? 0 : &(it->second);
}

void PFAIContainer::RegisterPolygonArea( string scriptName, const NDb::ScriptPolygonArea* area )
{
  if ( area )
    scriptPolyAreas[scriptName] = area;
}

PFAIContainer::MinimapIcon::MinimapIcon( NDb::EMinimapIcons _icon, float _x, float _y, NDb::EUnitType _unitType, NDb::EFaction _faction )
: PF_Core::IUpdateable( true )
, icon( _icon )
, x( _x )
, y( _y )
, unitType( _unitType )
, faction( _faction )
{
}

void PFAIContainer::MinimapIcon::Update( float timeDelta )
{
  (void)timeDelta;
}

void PFAIContainer::PlaceMinimapIcon( const char* iconName, NDb::EMinimapIcons icon, float x, float y, NDb::EUnitType unitType )
{
  if ( iconName )
    minimapIcons[iconName] = new MinimapIcon( icon, x, y, unitType, NDb::FACTION_NEUTRAL );
}

void PFAIContainer::RemoveMinimapIcon( const char* iconName )
{
  TMinimapIcons::iterator it = minimapIcons.find( iconName );
  if ( it != minimapIcons.end() )
    minimapIcons.erase( it );
}

bool PFAIContainer::CaptureTheFlag(const char *unitScripName, NDb::EFaction faction, bool pickup)
{
  (void)unitScripName;
  (void)faction;
  (void)pickup;
  return false;
}

void PFAIContainer::StepScript()
{
}

void PFAIContainer::RemoveInvalidUnits()
{
  for( TCreeps::iterator it = creeps.begin(); it != creeps.end(); )
  {
    if ( IsUnitValid( it->second ) )
      ++it;
    else
    {
      deadObjectNames.insert( it->first );
      it = creeps.erase( it );
    }
  }

  for( TUnits::iterator it = units.begin(); it != units.end(); )
  {
    if ( IsUnitValid( it->second ) )
      ++it;
    else
    {
      deadObjectNames.insert( it->first );
      it = units.erase( it );
    }
  }

  for( TObjects::iterator it = objects.begin(); it != objects.end(); )
  {
    if ( IsValid( it->second ) )
      ++it;
    else
    {
      deadObjectNames.insert( it->first );
      it = objects.erase( it );
    }
  }

  for( TObjectGroups::iterator it = objectGroups.begin(); it != objectGroups.end(); )
  {
    for( TObjectGroup::iterator itList = it->second.begin(); itList != it->second.end(); )
    {
      if ( IsValid( *itList ) )
        ++itList;
      else
        itList = it->second.erase( itList );
    }

    if ( it->second.size() > 0 )
      ++it;
    else
      it = objectGroups.erase( it );
  }

  const PFWorld* world = GetWorld();
  if ( !world )
    return;

  for ( TObjectNames::iterator it = objectIdToName.begin(); it != objectIdToName.end(); )
  {
    if ( IsValid( world->GetObjectById( it->first ) ) )
      ++it;
    else
      it = objectIdToName.erase( it );
  }
}

void PFAIContainer::BuildIdNameMap()
{
  objectIdToName.clear();

  for ( TCreeps::const_iterator it = creeps.begin(); it != creeps.end(); ++it )
  {
    PFBaseCreep* object = it->second.GetPtr();
    if ( object )
      objectIdToName.insert( make_pair( object->GetWOID(), it->first ) );
  }

  for ( TUnits::const_iterator it = units.begin(); it != units.end(); ++it )
  {
    PFBaseUnit* object = it->second.GetPtr();
    if ( object )
      objectIdToName.insert( make_pair( object->GetWOID(), it->first ) );
  }

  for ( TObjects::const_iterator it = objects.begin(); it != objects.end(); ++it )
  {
    PF_Core::WorldObjectBase* object = it->second.GetPtr();
    if ( object )
      objectIdToName.insert( make_pair( object->GetObjectId(), it->first ) );
  }
}

PFBaseHero* PFAIContainer::GetLocalHero() const
{
  const PFWorld* world = GetWorld();
  if ( !world )
    return 0;

  for ( int i = 0; i < world->GetPlayersCount(); ++i )
  {
    PFPlayer* player = world->GetPlayer( i );
    if ( player && player->IsLocal() )
      return player->GetHero();
  }

  return 0;
}

PFBaseHero* PFAIContainer::FindHero( const char* hero, bool aliasEnabled ) const
{
  if ( !hero )
    return 0;

  if ( strcmp( "local", hero ) == 0 )
    return GetLocalHero();

  if ( aliasEnabled )
  {
    for ( vector<HeroAlias>::const_iterator iter = heroesAliases.begin(); iter != heroesAliases.end(); ++iter )
    {
      if ( iter->alias == string( hero ) )
        return FindHero( iter->hero.c_str() );
    }
  }

  if ( strlen( hero ) != 2 || !NStr::IsDecDigit( hero[0] ) || !NStr::IsDecDigit( hero[1] ) )
    return 0;

  const PFWorld* world = GetWorld();
  if ( !world )
    return 0;

  int n = 0;
  const int team = hero[0] - '0';
  const int id = hero[1] - '0';
  for ( int i = 0; i < world->GetPlayersCount(); ++i )
  {
    PFPlayer* player = world->GetPlayer( i );
    if ( player && player->GetTeamID() == team )
    {
      if ( n == id )
        return player->GetHero();
      ++n;
    }
  }

  return 0;
}

PFCreature* PFAIContainer::FindCreature( const char* creature ) const
{
  if ( !creature )
    return 0;

  TCreeps::const_iterator pos = creeps.find( creature );
  if ( pos != creeps.end() )
    return pos->second;

  vector<string> parts;
  NStr::SplitString( creature, &parts, '_' );
  if ( parts.size() == 3 && parts[0] == "summon" && NStr::IsDecNumber( parts[2] ) )
  {
    PFBaseHero* hero = FindHero( parts[1].c_str() );
    if ( !IsValid( hero ) )
      return 0;

    int number = NStr::ToInt( parts[2] );
    PFBehaviourGroup* group = hero->GetSummonedGroup( NDb::SUMMONTYPE_PRIMARY );
    if ( group && number >= group->GetSize() )
    {
      number -= group->GetSize();
      group = hero->GetSummonedGroup( NDb::SUMMONTYPE_SECONDARY );
    }

    if ( group && number < group->GetSize() )
    {
      struct GetNthFromRingFunctor
      {
        int number;
        PFSummonBehaviour* behaviour;
        GetNthFromRingFunctor( int _number ) : number( _number ), behaviour( 0 ) {}
        void operator()( PFSummonBehaviour* _behaviour )
        {
          if ( number == 0 )
            behaviour = _behaviour;
          --number;
        }
      } f( number );
      group->ForAllBehaviour( f );

      if ( IsValid( f.behaviour ) )
        return dynamic_cast<PFCreature*>( f.behaviour->GetUnit().GetPtr() );
    }
  }

  return FindHero( creature );
}
PFFlagpole* PFAIContainer::FindFlag(int roadIndex, int flagIndex) const { (void)roadIndex; (void)flagIndex; return 0; }
PFBaseUnit* PFAIContainer::FindUnit( const char* unit ) const
{
  if ( !unit )
    return 0;

  TUnits::const_iterator pos = units.find( unit );
  if ( pos != units.end() )
    return pos->second;

  return FindCreature( unit );
}

PF_Core::WorldObjectBase* PFAIContainer::FindObject( const char* obj ) const
{
  if ( !obj )
    return 0;

  TObjects::const_iterator it = objects.find( obj );
  if ( it != objects.end() )
    return it->second;

  return FindUnit( obj );
}
bool PFAIContainer::FindTalent( PFBaseHero* hero, const char* persistentId, int* level, int* slot ) const { (void)hero; (void)persistentId; (void)level; (void)slot; return false; }
PFAIContainer::TObjectGroup* PFAIContainer::FindGroup( const char * group )
{
  if ( !group )
    return 0;

  TObjectGroups::iterator it = objectGroups.find( group );
  return it != objectGroups.end() ? &it->second : 0;
}

bool PFAIContainer::FindDeadObjectName(const char * objectName) const
{
  return objectName && deadObjectNames.find( objectName ) != deadObjectNames.end();
}

const char* PFAIContainer::FindObjectGroupName( const PF_Core::WorldObjectBase* object ) const
{
  if ( !object )
    return 0;

  for ( TObjectGroups::const_iterator it = objectGroups.begin(); it != objectGroups.end(); ++it )
  {
    for ( TObjectGroup::const_iterator itList = it->second.begin(); itList != it->second.end(); ++itList )
    {
      if ( *itList == object )
        return it->first.c_str();
    }
  }

  return 0;
}

void PFAIContainer::GetCreepNames( const hash_set<int>& objectIds, vector<const char*>* pObjectNames )
{
  if ( !pObjectNames )
    return;

  for ( TCreeps::const_iterator it = creeps.begin(); it != creeps.end(); ++it )
  {
    PFBaseCreep* object = it->second.GetPtr();
    if ( object && objectIds.find( object->GetObjectId() ) != objectIds.end() )
      pObjectNames->push_back( it->first.c_str() );
  }
}

bool PFAIContainer::FindObjectName( string& name, PF_Core::WorldObjectBase* object )
{
  name.clear();
  if ( !object )
    return false;

  if ( PFBaseCreep* creep = dynamic_cast<PFBaseCreep*>( object ) )
  {
    if ( IsValid( creep->GetMasterUnit() ) )
    {
      object = creep->GetMasterUnit();
      name = "summon_";
    }
  }

  if ( PFBaseHero* hero = dynamic_cast<PFBaseHero*>( object ) )
  {
    if ( hero->IsClone() && IsValid( hero->GetMasterUnit() ) && hero->GetMasterUnit()->IsTrueHero() )
      object = hero->GetMasterUnit();
  }

  if ( PFBaseHero* hero = dynamic_cast<PFBaseHero*>( object ) )
  {
    PFWorld* world = GetWorld();
    if ( !world )
      return false;

    char pid[2] = {0, 0};
    for ( int i = 0; i < world->GetPlayersCount(); ++i )
    {
      PFPlayer* player = world->GetPlayer( i );
      if ( !player )
        continue;

      const int team = player->GetTeamID();
      if ( team < 0 || team >= 2 )
        continue;

      if ( player->GetHero() == hero )
      {
        name.resize( 2, '\0' );
        name[0] = '0' + team;
        name[1] = '0' + pid[team];
        return true;
      }

      ++pid[team];
    }

    return false;
  }

  TObjectNames::iterator it = objectIdToName.find( object->GetObjectId() );
  if ( it != objectIdToName.end() )
  {
    name += it->second;
    return true;
  }

  return false;
}

void PFAIContainer::GetHeroName( PFBaseHero* pHero, string &name )
{
  if ( !FindObjectName( name, pHero ) )
    name.clear();
}
void PFAIContainer::RemoveStandaloneEffect( const char* effectName ) { (void)effectName; }
void PFAIContainer::PlaceStandaloneEffect( const char* effectName, const char* dbid, float x, float y ) { (void)effectName; (void)dbid; (void)x; (void)y; }
void PFAIContainer::PlaceAttachedEffect( const char* name, const char* dbid, const char* parentName ) { (void)name; (void)dbid; (void)parentName; }
void PFAIContainer::PlaceClientEffect( const char* dbid, float x, float y ) { (void)dbid; (void)x; (void)y; }
void PFAIContainer::PlaceSimpleObject( const char* name, const char* dbid, float x, float y, float z ) { (void)name; (void)dbid; (void)x; (void)y; (void)z; }
void PFAIContainer::PlaceSimpleObject( const char* name, const char* dbid, float x, float y, float z, float roll, float pitch, float yaw ) { (void)name; (void)dbid; (void)x; (void)y; (void)z; (void)roll; (void)pitch; (void)yaw; }
void PFAIContainer::RemoveSimpleObject( const char* name ) { (void)name; }
void PFAIContainer::ShowSimpleObject( const char* name ) { (void)name; }
void PFAIContainer::HideSimpleObject( const char* name ) { (void)name; }
bool PFAIContainer::UseConsumable( const char* hero, const char* item, const char* unit, float x, float y ) { (void)hero; (void)item; (void)unit; (void)x; (void)y; return false; }
void PFAIContainer::OnReconnect() {}
void PFAIContainer::RegisterEventScriptHandler( const char* name, NDb::EBaseUnitEvent eventType, const char* callbackFunctionName ) { (void)name; (void)eventType; (void)callbackFunctionName; }
void PFAIContainer::UnregisterEventScriptHandler( const char* name, NDb::EBaseUnitEvent eventType ) { (void)name; (void)eventType; }
void PFAIContainer::InvokeEventCallback( const string& name, const string& callbackFunctionName, const PFBaseUnitEvent *pEvent ) { (void)name; (void)callbackFunctionName; (void)pEvent; }
bool PFAIContainer::CreateZombie( PFCreature const* pCreature, const char* dbid, const NDb::EFaction faction ) { (void)pCreature; (void)dbid; (void)faction; return false; }
const char* PFAIContainer::GetFileNameByKey(const char* key)
{
  if ( !key )
    return 0;

  ScriptResources::iterator it = resources.find( key );
  return it == resources.end() ? key : it->second.c_str();
}
bool PFAIContainer::ChangeNatureMap( const float x, const float y, const float radius, NDb::ENatureType from, NDb::ENatureType to) { (void)x; (void)y; (void)radius; (void)from; (void)to; return false; }
bool PFAIContainer::HeroRaiseFlag( const char* _hero, const char* _flag ) { (void)_hero; (void)_flag; return false; }
void PFAIContainer::RegisterObject(PF_Core::WorldObjectBase* pObject, nstl::string scriptName, nstl::string scriptGroupName )
{
  if ( !pObject )
    return;

  if ( !scriptName.empty() )
  {
    objectIdToName[pObject->GetObjectId()] = scriptName;

    if ( PFBaseCreep* pCreep = dynamic_cast<PFBaseCreep*>( pObject ) )
    {
      NI_VERIFY( creeps.find( scriptName ) == creeps.end(),
        NStr::StrFmt( "Creep with script name \"%s\" already registered!", scriptName.c_str() ), return );
      creeps[scriptName] = pCreep;
    }
    else if ( PFBaseUnit* pUnit = dynamic_cast<PFBaseUnit*>( pObject ) )
    {
      NI_VERIFY( units.find( scriptName ) == units.end(),
        NStr::StrFmt( "Unit with script name \"%s\" already registered!", scriptName.c_str() ), return );
      units[scriptName] = pUnit;
    }
    else
    {
      NI_VERIFY( objects.find( scriptName ) == objects.end(),
        NStr::StrFmt( "Object with script name \"%s\" already registered!", scriptName.c_str() ), return );
      objects[scriptName] = pObject;
    }
  }

  if ( !scriptGroupName.empty() )
    objectGroups[scriptGroupName].push_back( pObject );
}

bool PFAIContainer::ScriptEffect::Create( NScene::IScene* pScene ) { (void)pScene; return false; }
void PFAIContainer::ScriptEffect::Remove() {}

template<class UnitType>
struct UnitCollectorFaction : NonCopyable
{
  UnitCollectorFaction( vector<UnitType*>& _objects, NDb::EFaction _faction )
    : objects(_objects)
    , faction(_faction)
  {
  }

  void operator()( PFLogicObject &baseUnit )
  {
    CDynamicCast<UnitType> unit = &baseUnit;
    if ( IsValid(unit) && unit->GetFaction() == faction )
      objects.push_back(unit);
  }

  vector<UnitType*>& objects;
  NDb::EFaction faction;
};

template<class UnitType>
bool FindFactionObjects( PFWorld* pWorld, NDb::EFaction faction, vector<UnitType*>& objects )
{
  objects.clear();
  if ( !pWorld || !pWorld->GetAIWorld() )
    return false;

  UnitCollectorFaction<UnitType> collector(objects, faction);
  pWorld->GetAIWorld()->ForAllUnits(collector);
  return !objects.empty();
}

bool FindQuarters( PFWorld* pWorld, NDb::EFaction faction, vector<PFQuarters*>& objects ) { return FindFactionObjects(pWorld, faction, objects); }
bool FindMainBuildings( PFWorld* pWorld, NDb::EFaction faction, vector<PFMainBuilding*>& objects ) { return FindFactionObjects(pWorld, faction, objects); }
bool FindShop( PFWorld* pWorld, NDb::EFaction faction, vector<PFShop*>& objects ) { return FindFactionObjects(pWorld, faction, objects); }

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFAIContainer, NWorld)

#else

#include "PFAIContainer.h"
#include "PFAIController.h"
#include "PFSeriesAIController.h"
#include "PFMaleHero.h"
#include "PFTalent.h"
#include "AdventureScreen.h"
#include "System/FileSystem/FileUtils.h"
#include "PFMainBuilding.h"
#include "Minimap.h"


namespace NWorld
{

  bool PFAIContainer::CaptureTheFlag(const char *flag, NDb::EFaction faction, bool pickup)
  {
    PFFlagpole* pFlagpole = dynamic_cast<PFFlagpole*>(FindUnit( flag ));
    NI_DATA_VERIFY( IsValid( pFlagpole ), NStr::StrFmt( "Flagpole \"%s\" not found", flag ), return true );

    pFlagpole->Reset();
    PFBaseUnit* pUnit = NULL;

    if(pickup)
    {
        pFlagpole->OnStartRaise( 0, 0.0f );
        pFlagpole->OnRaise(faction, pUnit);
    } 
    else
    {
      pFlagpole->OnCancelRaise();
      pFlagpole->OnDropFlag(pUnit);
    }

    return true;
  }

bool PFAIContainer::Step( float timeDelta )
{
  NI_PROFILE_FUNCTION;

  for( vector<CObj<IPFAIController>>::iterator it = controllers.begin(); it != controllers.end(); ++it )
  {
    (*it)->Step( timeDelta );
  }
  StepScript();
  return true;
}

IPFAIController* PFAIContainer::Find( const PFBaseHero* pUnit ) const
{
  for ( int i = 0; i < controllers.size(); i++ )
  {
    if ( controllers[i]->GetHero() == pUnit )
    {
      return controllers[i];
    }
  }
  return NULL;
}

IPFAIController* PFAIContainer::Add( PFBaseHero* pUnit, int lineNumber )
{
  IPFAIController* ctl = Find( pUnit );
  if ( ctl )
  {
    return ctl;
  }

  // Calculate road's waypoints shift for heroes to go on different parts of the road

  int shift = 0; // No shift by default (apply to first hero on the road and each third)

  // This loop checks if we already have heroes from the same faction on that road and make a shift if needed
  for (int i = 0; i < controllers.size(); i++)
  {
    if (controllers[i]->GetLineNumber() == lineNumber
      && pUnit->GetFaction() == controllers[i]->GetHero()->GetFaction())
    {
      // Calculate shift flag according to the following sequence: 0, 1, -1, 0, 1, -1 ...
      // Where each additional hero on the same road will get the next number from that sequence
      shift = ((shift + 2) % 3) - 1;
    }
  }

  ctl = new PFAIController( pUnit, transceiver, lineNumber, shift );
  controllers.push_back( ctl );
  return ctl;
}
///////////////////////////////////////////////////////////////////////////////
IPFSeriesAIController * PFAIContainer::AddSeriesController( PFBaseHero* pUnit, bool lvlUpAvailable )
{
  IPFAIController* ctl = Find( pUnit );
  if ( ctl )
  {
    if ( IPFSeriesAIController *sctl = dynamic_cast<IPFSeriesAIController*>(ctl) )
      return sctl;
    else 
      Remove( pUnit );
  }
  PFSeriesAIController *sctl = new PFSeriesAIController( pUnit, transceiver, lvlUpAvailable );
  controllers.push_back( sctl );
  return sctl;
}


void PFAIContainer::OnMinimapSignal( PFBaseHero* pSender, PFBaseUnit* pSelected, const Target& target )
{
  PFBaseHero* pSelectedHero = dynamic_cast<PFBaseHero*>(pSelected);
  if ( pSelectedHero && pSelectedHero->GetFaction() == pSender->GetFaction()  )
  {
    if ( IPFAIController* ctl = Find( pSelectedHero ) )
    {
      ctl->OnMinimapSignal( target, true );
      return;
    }
  }
  for ( int i = 0; i < controllers.size(); i++ )
  {
    if ( controllers[i]->GetHero()->GetFaction() != pSender->GetFaction() )
      continue;

    if ( controllers[i]->OnMinimapSignal( target, false ) )
      return;
  }
}


///////////////////////////////////////////////////////////////////////////////
//	PFAIContainer
///////////////////////////////////////////////////////////////////////////////

bool PFAIContainer::Remove( PFBaseHero* pUnit )
{
  for ( int i = 0; i < controllers.size(); i++ )
  {
    if ( controllers[i]->GetHero() == pUnit )
    {
      controllers[i]->StopHero();
      if ( PFFsm *fsm_ctl = dynamic_cast<PFFsm*>(controllers[i].GetPtr()) )
        fsm_ctl->Cleanup(true);

      controllers.eraseByIndex(i);
      return true;
    }
  }
  return false;
}

void PFAIContainer::RemoveAll()
{
  for ( int i = 0; i < controllers.size(); i++ )
  {
    controllers[i]->StopHero();
    
    if ( PFFsm *fsm_ctl = dynamic_cast<PFFsm*>(controllers[i].GetPtr()) )
      fsm_ctl->Cleanup(true);
  }

  controllers.clear();
}


void PFAIContainer::RegisterScriptPath( const string &scriptName, const NDb::ScriptPath* gameObject )
{
  NI_DATA_VERIFY(scriptPaths.find(scriptName) == scriptPaths.end(), NStr::StrFmt("Duplicate script path name found: %s", scriptName ), return );
    
  scriptPaths.insert( TScriptPaths::value_type(scriptName, gameObject->path) );
}
const vector<CVec2>* PFAIContainer::GetScriptPath( const string &scriptName )
{
  TScriptPaths::const_iterator it = scriptPaths.find(scriptName);
  return it == scriptPaths.end() ? NULL : &(it->second);

}

void PFAIContainer::RegisterPolygonArea( string scriptName, const NDb::ScriptPolygonArea* area )
{
  NI_DATA_VERIFY(scriptPolyAreas.find(scriptName) == scriptPolyAreas.end(), NStr::StrFmt("Duplicate script polygon area name found: %s", scriptName ), return );

  scriptPolyAreas.insert( TScriptPolyAreas::value_type(scriptName, area) );
}



PFAIContainer::MinimapIcon::MinimapIcon( NDb::EMinimapIcons _icon, float _x, float _y, NDb::EUnitType _unitType, NDb::EFaction _faction )
: PF_Core::IUpdateable( true ), icon(_icon), x(_x), y(_y), unitType(_unitType), faction(_faction )
{
}

void PFAIContainer::MinimapIcon::Update( float timeDelta )
{
  if (NGameX::AdventureScreen const * pAdvScreen = NGameX::AdventureScreen::Instance())
  {
    if (NGameX::Minimap *pMinimap = pAdvScreen->GetMinimap())
    {
      pMinimap->AddObject(unitType, faction, CVec3( x, y, 0.0f ), icon );
    }
  }
}

void PFAIContainer::PlaceMinimapIcon( const char* iconName, NDb::EMinimapIcons icon, float x, float y, NDb::EUnitType unitType )
{
  NI_VERIFY( minimapIcons.find( iconName ) == minimapIcons.end(), NStr::StrFmt("Minimap icon \"%s\" already exists", iconName ), return );
  
  minimapIcons[iconName] = new MinimapIcon( icon, x, y, unitType, NDb::FACTION_NEUTRAL );
}

void PFAIContainer::RemoveMinimapIcon( const char* iconName )
{
  TMinimapIcons::iterator it = minimapIcons.find( iconName );
  if ( it != minimapIcons.end() )
  {
    it->second->Unsubscribe();
    minimapIcons.erase( it );
  }
}

///////////////////////////////////////////////////////////////////////////////
//	Console commands
///////////////////////////////////////////////////////////////////////////////

// Helper function
static PFBaseHero *GetObject()
{
  PFBaseHero* selectedObject = NULL;
#ifndef VISUAL_CUTTED
  // based on CheatCommandProxy()
  if ( NGameX::AdventureScreen* pAdvScreen = NGameX::AdventureScreen::Instance() )
  {
    int localPlayerId = pAdvScreen->GetLocalPlayerID();
    selectedObject = dynamic_cast<NWorld::PFBaseHero*>( pAdvScreen->GetCurrentSelectedObject() );
    if ( !selectedObject )
    {
      NWorld::PFPlayer* pLocalPlayer = pAdvScreen->GetWorld()->GetPlayer( localPlayerId );
      selectedObject = pLocalPlayer ? pLocalPlayer->GetHero() : NULL;
    }
  }
#endif
  return selectedObject;
}

bool CommandAttachAI( const char *name, const vector<wstring> &params )
{
  // get unit
  PFBaseHero *pUnit = GetObject();
  if ( !pUnit )
  {
    DebugTrace("You must select unit first!");
    return false;
  }

  // get or create AI container
  PFWorld *pWorld = pUnit->GetWorld();
  PFAIContainer* pCont = pWorld->GetAIContainer();
  NI_ASSERT( pCont, "No AI container present" );

  // check: whether AI object attached to unit
  IPFAIController* pCtl = pCont->Find( pUnit );
  if ( pCtl )
  {
    DebugTrace("Unit already controlled by AI");
    return false;
  }

  // create controller
  int lineNumber = RANDOM_LINE;
  if ( params.size() > 0 )
  {
    lineNumber = (int) NGlobal::VariantValue( params[0] ).GetFloat();
  }
  pCont->Add( pUnit, lineNumber );

  return true;
}

bool CommandDetachAI( const char* name, const vector<wstring> &params )
{
  // get unit
  PFBaseHero* pUnit = GetObject();
  if ( !pUnit )
  {
    DebugTrace( "You must select unit first!" );
    return false;
  }

  // get AI container
  PFWorld* pWorld = pUnit->GetWorld();
  PFAIContainer* pCont = pWorld->GetAIContainer();
  NI_ASSERT( pCont, "No AI container present" );

  // remove controller
  if ( !pCont->Remove( pUnit ) )
  {
    DebugTrace( "Unit not controlled by AI" );
    return false;
  }

  return true;
}

bool GetTalentId( const char* name, const vector<wstring>& params)
{
  if ( params.size() < 1 || params.size() > 2 || params[0].size() != 2 || ( params.size() == 2 && params[1].size() != 2 ) )
  {
    systemLog( NLogg::LEVEL_MESSAGE ) << "Usage: " << name << "[<filename>] <leve><slot>" << endl;
    return true;
  }

  NGameX::AdventureScreen* pAdvScreen = NGameX::AdventureScreen::Instance();
  if ( !pAdvScreen )
  {
    return false;
  }
  PFWorld* pWorld = pAdvScreen->GetWorld();
  if ( !pWorld )
  {
    return false;
  }

  PFBaseHero* hero = 0;
  if ( params.size() == 2 )
  {
    int n = 0;
    const int team = params[0][0] - L'0';
    const int id = params[0][1] - L'0';
    for( int i = 0; i < pWorld->GetPlayersCount(); ++i )
    {
      PFPlayer* player = pWorld->GetPlayer( i );
      if ( player->GetTeamID() == team )
      {
        if ( n == id )
        {
          hero = player->GetHero();
          break;
        }
        else
          ++n;
      }
    }
    if ( !hero )
    {
      systemLog( NLogg::LEVEL_ASSERT ) << "\"" << params[0] << "\"" << " is not valid hero name" << endl;
      return true;
    }
  }
  else
  {
    hero = dynamic_cast<PFBaseHero*>( pAdvScreen->GetSelectedObject().GetPtr() );
    if ( !hero )
    {
      systemLog( NLogg::LEVEL_ASSERT ) << "Currently selected object should be hero" << endl;
      return true;
    }
  }

  const int level = params[params.size() - 1][0] - '0';
  const int slot = params[params.size() - 1][1] - '0';

  PFTalent* talent = hero->GetTalent( level, slot );
  if ( !talent )
  {
    systemLog( NLogg::LEVEL_ASSERT ) << "\"" << params[params.size() - 1] << "\"" << " is not valid talent name" << endl;
    return true;
  }

  systemLog( NLogg::LEVEL_MESSAGE ) << "\"" << talent->GetTalentDesc()->persistentId << "\"" << endl;

  return true;
}


bool RunScript( const char* name, const vector<wstring>& params)
{
  if ( params.size() != 1 )
  {
    systemLog( NLogg::LEVEL_MESSAGE ) << "Usage: " << name << "<filename>" << endl;
    return true;
  }

  NGameX::AdventureScreen* pAdvScreen = NGameX::AdventureScreen::Instance();
  if ( !pAdvScreen )
  {
    return false;
  }
  PFWorld* pWorld = pAdvScreen->GetWorld();
  if ( !pWorld )
  {
    return false;
  }
  PFAIContainer* pCont = pWorld->GetAIContainer();
  if ( !pCont )
  {
    return false;
  }

  string fileName = NFile::GetFullName( NStr::ToMBCS( params[0] ) );
  CObj<FileStream> stream = new FileStream( fileName, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING );
  NI_DATA_VERIFY( IsValid( stream ) && stream->IsOk(), NStr::StrFmt( "Cannot open file \"%s\"", fileName ), return true );
  NI_DATA_VERIFY( stream->GetSize() > 0, NStr::StrFmt( "Script file \"%s\" is empty", fileName ), return true );

  string content;
  content.resize( stream->GetSize() );
  stream->Read( &(content[0]), stream->GetSize() );

  vector<string> script;
  NStr::SplitString( content.c_str(), &script, '\n' );

  pCont->LoadScript( script );
  return true;
}


///////////////////////////////////////////////////////////////////////////////
//	Debugging
///////////////////////////////////////////////////////////////////////////////

#ifndef _SHIPPING

static int g_showLabels = 0;
// Values: 1=show all, 2=show selected
REGISTER_VAR( "ai_labels", g_showLabels, STORAGE_NONE );

//BEGIN_DEBUG_CB_TABLE(PFAIContainer)
//	DEBUG_CB_ON_VARIABLE_NOT_VALUE(&g_showLabels, 0, &PFAIContainer::ShowDebug)
//END_DEBUG_CB_TABLE()

void PFAIContainer::ShowDebug( Render::IDebugRender* pRender ) const
{
  PFBaseHero* pSelected = GetObject();
  for ( int i = 0; i < controllers.size(); i++ )
  {
    IPFAIController* ctl = controllers[i];
    if ( !IsValid( ctl->GetHero() ) )
    {
      continue;
    }

    const PFBaseHero* pUnit = ctl->GetHero();
    if ( g_showLabels == 2 && pUnit != pSelected )
    {
      continue;
    }

    char buf[256];
    strcpy_s( buf, pUnit->GetDebugDescription() );
    char *s = strrchr( buf, '/' );
    if ( s )
    {
      *s = 0;
    }
    s = strrchr( buf, '/' );
    s = s ? s+1 : buf;

    const char* stateName = ctl->GetCurrentStateName();
    pRender->DrawText3D( NStr::StrFmt(
      "%s (%s)\nstate[%d]=%s\nwarFront=%.1f", 
      s, NDb::EnumToString( pUnit->GetFaction() ),
      ctl->GetStackSize(),
      stateName ? stateName : "<unknown>",
      0 /*ctl->warFrontTimeDist*/ ),
      pUnit->GetVisualPosition3D() + CVec3(0,0,8), 14, Render::Color( 255,255,255 )
      );
  }
}
#endif // _SHIPPING

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// objects finders
template<class UnitType>
struct UnitCollectorFaction : NonCopyable
{
  UnitCollectorFaction( vector<UnitType*>& _objects, NDb::EFaction _faction )
    : objects( _objects )
    , faction( _faction )
  {}

  void operator()( PFLogicObject &baseUnit )
  {
    CDynamicCast<UnitType> unit = &baseUnit;
    if ( IsValid( unit ) && unit->GetFaction() == faction )	// and faction
    {
      objects.push_back( unit );
    }
  }

  NDb::EFaction      faction;
  vector<UnitType*>& objects;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool FindQuarters( PFWorld* pWorld, NDb::EFaction faction, vector<PFQuarters*>& objects )
{
  UnitCollectorFaction<PFQuarters> collector( objects, faction );
  pWorld->GetAIWorld()->ForAllUnits( collector );
  return objects.size() > 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool FindMainBuildings( PFWorld* pWorld, NDb::EFaction faction, vector<PFMainBuilding*>& objects )
{
  UnitCollectorFaction<PFMainBuilding> collector( objects, faction );
  pWorld->GetAIWorld()->ForAllUnits( collector );
  return objects.size() > 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool FindShop( PFWorld* pWorld, NDb::EFaction faction, vector<PFShop*>& objects )
{
  UnitCollectorFaction<PFShop> collector( objects, faction );
  pWorld->GetAIWorld()->ForAllUnits( collector );
  return objects.size() > 0;
}

} // namespace

REGISTER_WORLD_OBJECT_NM(PFAIContainer, NWorld)

REGISTER_DEV_CMD( run_script, NWorld::RunScript );
REGISTER_DEV_CMD( get_talentid, NWorld::GetTalentId );

REGISTER_DEV_CMD( attach_ai, NWorld::CommandAttachAI );
REGISTER_DEV_CMD( detach_ai, NWorld::CommandDetachAI );

#endif
