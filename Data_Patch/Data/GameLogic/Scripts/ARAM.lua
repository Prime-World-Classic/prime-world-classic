include ("GameLogic/Scripts/Debug.lua")
include ("GameLogic/Scripts/StatesManager.lua")
include ("GameLogic/Scripts/Common.lua")
include ("GameLogic/Scripts/Consts.lua")

ZOMBIE_MODE = false
ZombieSpawnDelay = 3
KilledHeroProc = 100
KilledBySummonProc = 0
KilledByMeleeAAProc = 100
KilledByRangedAAProc = 100
KilledByAbilityProc = 0
-- by ifst
QUEST_MODE = true
PRICE_HERO = 50 -- 50
PRICE_CREEP = 5 -- 5
LIMIT_SCORE = 1000 -- 1000
MAX_PLAYER_TEAM = 5 -- 5
LOCAL_FACTION = 0

COMPANION_DATA = {}

function Init( reconnecting )
	
	if not reconnecting then
		
		local QUEST_DRAGON = {}
		
		QUEST_DRAGON[1] = { name = "Q_A", state = 0, start = 0, spawn = { x = 43, y = 130 } }
		
		QUEST_DRAGON[2] = { name = "Q_B", state = 0, start = 0, spawn = { x = 214, y = 125 } }
		
		SetGlobalVar( "QUEST_DRAGON", QUEST_DRAGON )
		
		local QUEST_TOWER = {}
		
		QUEST_TOWER[1] = { name = "Q_A2", state = 0, repair = { "TowerA1_m2", "TowerA2_m2" } }
		
		QUEST_TOWER[2] = { name = "Q_B2", state = 0, repair = { "TowerB1_m2", "TowerB2_m2" } }
		
		SetGlobalVar( "QUEST_TOWER", QUEST_TOWER )
		
		local CAPTAIN_GLYPH = {}
		
		CAPTAIN_GLYPH[1] = { name = "CaptainGlyph_A", state = false, hero = 0, spawn = { x = 37, y = 115 } }
		
		CAPTAIN_GLYPH[2] = { name = "CaptainGlyph_B", state = false, hero = 0, spawn = { x = 223, y = 142 } }
		
		SetGlobalVar( "CAPTAIN_GLYPH", CAPTAIN_GLYPH )
		
	end
	
	-- InitCaptainGlyph()
	
	LOCAL_FACTION = LuaGetUnitFaction( "local" )
	
	AddTriggerTop( DelayInit )
	
end

function DelayInit()
	
	WaitState( 15 )
	
	-- SpawnCaptainGlyph()
	
	LuaPlaceAttachedEffect( "WithLoveIfstLocalId", "WithLoveIfst", "local" )
	
	LuaSetHintLine( "welcome", "LeftClick" )
	
	if LOCAL_FACTION == 1 then 
		
		PlaySound( 2 )
	
	else 
		
		PlaySound( 3 )
		
	end
	
	WaitState( 15 )
	
	LuaSetHintLine( "", "None" )
	
	if QUEST_MODE then 
	
		WaitState( 7 )
		
		StartTrigger( QuestDragon )
		
		PlaySound( 1 )
		
		WaitState( 5 )
		
		StartTrigger( QuestTower )
	
	end
	
end

function InitCaptainGlyph()
	
	for team = 0, 1 do
	
		for hero = 0, 4 do
			
			local heroNameId = tostring( team ) .. tostring( hero )
			
			LuaSubscribeUnitEvent( heroNameId, EventPickup, "EventPickupGlyph" )
			
		end
		
	end
	
end

function SpawnCaptainGlyph()
	
	for faction, data in ipairs( GetGlobalVar( "CAPTAIN_GLYPH" ) ) do
		
		if not data.state then 
			
			LuaCreateGlyph( data.name, "CaptainGlyph", data.spawn.x, data.spawn.y )
			
			data.state = true
			
			--if LOCAL_FACTION ~= faction then 
			
				--LuaEnableGlyph( data.name, false )
			
			--end;
		
		end
	
	end
	
end

function EventPickupGlyph( hero, glyph )
	
	for faction, data in ipairs( GetGlobalVar( "CAPTAIN_GLYPH" ) ) do 
		
		if glyph == data.name and faction == LuaGetUnitFaction( hero ) then 
			
			LuaPlaceAttachedEffect( "CaptainStatus_" .. LuaGetObjectId( hero ), "CaptainStatus", hero )
			
			data.hero = LuaGetObjectId( hero )
			
		end
		
	end
	
end

function CheckDeadCaptain( victimId )
	
	for faction, data in ipairs( GetGlobalVar( "CAPTAIN_GLYPH" ) ) do 
		
		if data.hero ~= 0 and LuaGetUnitTypeById( victimId ) == UnitTypeHeroMale and data.hero == victimId and not LuaHeroIsCloneById( victimId )  then
			
			data.state = false
			
			LuaRemoveStandaloneEffect( "CaptainStatus_" .. victimId )
			
			data.hero = 0
		
		end
	
	
	end
	
	SpawnCaptainGlyph()

end

function QuestDragon()

	local Score = GetScore()
	
	for faction, data in ipairs( GetGlobalVar( "QUEST_DRAGON" ) ) do 
		
		if data.state == 0 then 
			
			LuaAddSessionQuest( data.name )
			
			data.start = Score[faction].total
			
			data.state = 1
		
		elseif data.state == 1 then 
		
			local total = ( Score[faction].total - data.start )
			
			if total > LIMIT_SCORE then
				
				total = LIMIT_SCORE
				
			end
			
			LuaUpdateSessionQuest( data.name, total )
			
			if total == LIMIT_SCORE then
				
				Spawn( "Dragon", data.spawn.x, data.spawn.y, faction )
				
				WaitState( 1 )
				
				data.state = 2
				
				LuaRemoveSessionQuest( data.name )
				
				PlaySound( 4 )
				
			end
			
		end
	
	end

end

function Spawn( unit, x, y, faction )
	
	local name = unit .. "_" .. faction

	LuaCreateCreep( name, unit, x, y, faction, faction )
	
	local objectId = LuaGetObjectId( name )
	
	LuaCreateZombieById( objectId, unit, faction )
	
	LuaKillUnit( name )
	
end

function QuestTower()

	local Score = GetScore()
	
	for faction, data in ipairs( GetGlobalVar( "QUEST_TOWER" ) ) do 
	
		local target = 1
		
		if faction == 1 then 
		
			target = 2
		
		end
		
		if data.state == 1 then 
		
			LuaUpdateSessionQuest( data.name, Score[target].dead )
		
		end
		
		if data.state == 0 and Score[target].dead == 0 then 
		
			data.state = 1
			
			LuaAddSessionQuest( data.name )
		
		elseif data.state == 1 and Score[target].dead == MAX_PLAYER_TEAM then 
			
			data.state = 0
			
			LuaRemoveSessionQuest( data.name )
			
			HealTower( data )
			
			if LOCAL_FACTION == faction then 
				
				PlaySound( 5 )
				
			end
			
		end
	
	end
	
end

function HealTower( data )
	
	for index, tower in ipairs( data.repair ) do
	
		local dead, found = LuaUnitIsDead( tower )
		
		if found and not dead then 
			
			--local health, total = LuaUnitGetHealth( tower )
			
			--local increase = health + ( total / 4 )
			
			--if increase > total then 
				
				--increase = total
				
			--end
			
			--LuaSetUnitHealth( tower, increase )
			
			LuaUnitApplyApplicator( tower, "AddLife" )
			
			AddTriggerTop( HealTowerEffect, tower )
			
			return
		
		end
	
	end
	
	data.state = 3

end

function HealTowerEffect( TowerName )

	local effectId = "HealBuilding_" .. TowerName;

	LuaPlaceAttachedEffect( effectId, "HealBuilding", TowerName )
	
	WaitState( 3 )
	
	LuaRemoveStandaloneEffect( effectId )

end

function PlaySound( id )

	LuaStartDialog ( "Sound_" .. id )
	
end

function GetScore()

	local score = {}

	for team = 0, 1 do
	
		for hero = 0, 4 do
			
			local heroNameId = tostring( team ) .. tostring( hero )
			
			local dead, found = LuaUnitIsDead(heroNameId)
			
			if found then
				
				local TotalNumHeroKills = LuaStatisticsGetTotalNumHeroKills( heroNameId )
				
				local KillsTotal = LuaHeroGetKillsTotal( heroNameId )
				
				local UnitFaction = LuaGetUnitFaction( heroNameId )
				
				if not score[UnitFaction] then 
					
					score[UnitFaction] = { total = 0, dead = 0 }
				
				end
				
				if UnitFaction == 1 then 
					
					score[UnitFaction].total = ( score[UnitFaction].total + ( TotalNumHeroKills * PRICE_HERO ) + ( KillsTotal * PRICE_CREEP ) )
					
					if dead then 
					
						score[UnitFaction].dead = ( score[UnitFaction].dead + 1 )
						
					end
					
				else
					
					score[UnitFaction].total = ( score[UnitFaction].total + ( TotalNumHeroKills * PRICE_HERO ) + ( KillsTotal * PRICE_CREEP ) )
					
					if dead then 
					
						score[UnitFaction].dead = ( score[UnitFaction].dead + 1 )
						
					end
					
				end
			
			end
		
		end
	
	end
	
	return score
	
end

function PointReceived( victimId, killerId )

	if LuaGetUnitTypeById( killerId ) == UnitTypeHeroMale and not LuaHeroIsCloneById( killerId ) then 
	
		local killerName = LuaGetUnitObjectNameById( killerId )
	
		local victimName = LuaGetUnitObjectNameById( victimId )
		
		LuaUnitApplyApplicator( killerName, "AddLife10" )
		
		local effectId = "PointReceived_" .. victimName;
		
		LuaPlaceAttachedEffect( effectId, "PointReceived", killerName )
		
		WaitState( 2 )
		
		LuaRemoveStandaloneEffect( effectId )
		
	end

end

function DeadEffect( victimId )

	if LuaGetUnitTypeById( victimId ) == UnitTypeHeroMale then
	
		local victimName = LuaGetUnitObjectNameById( victimId )
		
		local effectId = "deadEffect_"..victimName;
		
		LuaPlaceAttachedEffect( effectId, "DeadEffect", victimName )
		
		WaitState( 5 )
		
		LuaRemoveStandaloneEffect( effectId )
	
	end

end

function OnUnitDie( victimId, killerId, lastHitterId, deathParamsInfo )
	
	AddTriggerTop( DeadEffect, victimId )
	
	if LuaGetUnitTypeById( victimId ) == 3 and LuaGetUnitObjectNameById( victimId ) == "" then
		
		local faction = LuaGetUnitFactionById( victimId )
		
		local QUEST_DRAGON = GetGlobalVar( "QUEST_DRAGON" )
		
		if QUEST_DRAGON[faction].state == 2 then 
			
			QUEST_DRAGON[faction].state = 0
		
		end
		
	end
	
	if killerId == -1 then
	
		return
		
	end
	
	-- AddTriggerTop( CheckDeadCaptain, victimId )
	
	if LuaGetUnitTypeById( victimId ) ~= UnitTypeHeroMale then 
	
		AddTriggerTop( PointReceived, victimId, killerId )
	
	end
	
	if not ZOMBIE_MODE then 
	
		return
	
	end
	
	if LuaGetUnitVariableById( victimId, "InventorSpecial" ) ~= 0 then
		local dbid = "SuperInventor"
		local faction = LuaGetUnitFactionById( killerId )
		LuaCreateZombieById( victimId, dbid, faction )
		return
	end

	local victimType = LuaGetUnitTypeById( victimId )
	local killerType = LuaGetUnitTypeById( killerId )
	
	if killerType ~= UnitTypeHeroMale or LuaHeroIsCloneById( killerId ) then
		return
	end
		
	local heroIsKilled = victimType == UnitTypeHeroMale and not LuaHeroIsCloneById( victimId )
	
	if victimType ~= UnitTypeCreep and victimType ~= UnitTypeSiegeCreep and not heroIsKilled then
		return
	end
	
	if LuaGetUnitVariableById( victimId, "ImZombie" ) ~= 0 then
		return
	end
	
	local lastHitterType = LuaGetUnitTypeById( lastHitterId )
	
	local killedBySummon = lastHitterType == UnitTypeSummon or lastHitterType == UnitTypeDummyUnit
	local killedByAutoAttack = deathParamsInfo.isAutoAttack
	local killedByMelee = deathParamsInfo.isMelee
	
	local check = false
	local roll = LuaRandom( 0, 100 )
	if heroIsKilled then -- если был убит герой (как угодно)
		LuaDebugTrace( "Killed hero" )
		check = roll < KilledHeroProc
	elseif killedBySummon then -- если был убит крип суммоном
		LuaDebugTrace( "Killed by summon" )
		check = roll < KilledBySummonProc
	elseif killedByMelee then -- если  был убит крип милишной автоатакой героя
		LuaDebugTrace( "Killed by melee" )
		check = roll < KilledByMeleeAAProc
	elseif killedByAutoAttack then -- если был убит крип ренджовой автоатакой героя
		LuaDebugTrace( "Killed by ranged" )
		check = roll < KilledByRangedAAProc
	else -- если был убит крип абилкой героя
		LuaDebugTrace( "Killed by ability" )
		check = roll < KilledByAbilityProc
	end
	
	if not check then
		return
	end
	
	local dbid = "Zombie"
	
	local faction = LuaGetUnitFactionById( killerId )
	if faction == 1 then
		dbid = dbid .. "A"
	else
		dbid = dbid .. "B"
	end
	
	AddTriggerTop( SpawnZombie, victimId, dbid, faction, heroIsKilled)
	
end

function SpawnZombie( victimId, dbid, faction, heroIsKilled )

	if heroIsKilled then
	
		if faction == 1 then
			faction = 2
		else
			faction = 1
		end
	
		LuaCreateZombieById( victimId, "Ghost", faction )
	
	else
		
		WaitState( ZombieSpawnDelay )
		LuaCreateZombieById( victimId, dbid, faction )
		WaitState( ZombieSpawnDelay )
		LuaCreateZombieById( victimId, dbid, faction )
	
	end
	
end

function initCompanion()

	for team = 0, 1 do
	
		for hero = 0, 4 do
		
			local heroId = tostring( team ) .. tostring( hero )
			
			local x, y = LuaHeroGetPosition( heroId )
			
			local body = "CompanionA"
			
			if team == 1 then
			
				body = "CompanionB"
			
			end
			
			COMPANION_DATA[heroId] = { id = body .. heroId, attack = "", recovery = false, target = heroId, distance = 0, body = body, status = "", alive = false, targetX = 0, targetY = 0, notAction = 0, limitNotAction = LuaRandom( 15, 30 ), home = { x = x, y = y } }
			
			StartTrigger( spawnCompanion, heroId )
			
		end
		
	end
	
end

function spawnCompanion( heroId )

	local x1, y1 = LuaHeroGetPosition( heroId )
	
	local faction = LuaGetUnitFaction( heroId )
	
	COMPANION_DATA[heroId].distance = 75

	if not COMPANION_DATA[heroId].alive then
		
		local rx, ry = randomPosition( x1, y1, 3)
	
		LuaCreateCreep( COMPANION_DATA[heroId].id, COMPANION_DATA[heroId].body, rx, ry, faction, 1 )
		
		LuaUnitAddFlag( COMPANION_DATA[heroId].id, ForbidSelectTarget )
		
		LuaUnitAddFlag( COMPANION_DATA[heroId].id, ForbidAutoAttack )
		
		-- LuaUnitAddFlag( COMPANION_DATA[heroId].id, ForbidAutotargetMe ) -- чтобы крипы не брали цель
		
		LuaSetUnitStat( COMPANION_DATA[heroId].id , 0, 1000 ) -- хп
		
		COMPANION_DATA[heroId].alive = true
	
	end
	
	if LuaUnitIsDead(COMPANION_DATA[heroId].id) then 
	
		COMPANION_DATA[heroId].alive = false
		
		return
	
	end
	
	local heroMoveSpeed = LuaGetUnitStat( COMPANION_DATA[heroId].target, 3 )
	
	LuaSetUnitStat( COMPANION_DATA[heroId].id , 3, heroMoveSpeed )
	
	local kills = LuaHeroGetKillsTotal( COMPANION_DATA[heroId].target )
	
	LuaSetUnitStat( COMPANION_DATA[heroId].id , 15, kills * 64 )
	
	if COMPANION_DATA[heroId].recovery then 
	
		if HelthDownPercent( COMPANION_DATA[heroId].id, 100 ) then
		
			COMPANION_DATA[heroId].recovery = false
		
		else
			
			LuaUnitClearStates( COMPANION_DATA[heroId].id )
			
			return LuaCreatureMoveTo( COMPANION_DATA[heroId].id , COMPANION_DATA[heroId].home.x , COMPANION_DATA[heroId].home.y, 0 )
			
		end
	
	end
	
	local x2, y2 = LuaUnitGetPosition( COMPANION_DATA[heroId].id )
	
	-- LuaPlaceMinimapIcon( COMPANION_DATA[heroId].id, 1, x2, y2, 2 ) -- отображаем на миникарте
	
	if LuaIsUnitAttacked( COMPANION_DATA[heroId].id ) then 
		
		-- LuaCreateMinimapSignal(x2, y2) -- сигнал о помощи
	
	end
	
	if HelthDownPercent( COMPANION_DATA[heroId].id, 25 ) then
	
		LuaUnitClearStates( COMPANION_DATA[heroId].id )
		
		LuaSetUnitStat( COMPANION_DATA[heroId].id , 3, 100 )
		
		COMPANION_DATA[heroId].recovery = true
		
		return LuaCreatureMoveTo( COMPANION_DATA[heroId].id , COMPANION_DATA[heroId].home.x , COMPANION_DATA[heroId].home.y, 0 )
		
	end
	
	if HelthDownPercent( COMPANION_DATA[heroId].id, 50 ) then 
	
		COMPANION_DATA[heroId].distance = 30
		
	end
	
	local mask = 2 -- 2 крипы A, 4 крипы B
	
	if faction == 1 then
	
		mask = 4
	
	end
	
	local creepsInArea = LuaGetCreepsInArea( x1, y1, 256, mask )
	
	local minDistance = COMPANION_DATA[heroId].distance
	
	local targetAttack = ""
	
	for index, unitId in pairs(creepsInArea) do 
	
		local x2, y2 = LuaUnitGetPosition( unitId )
	
		local distance = DistanceSquared(x1,y1,x2,y2)
		
		if distance <= COMPANION_DATA[heroId].distance then 
		
			if distance < minDistance then 
		
				minDistance = distance
			
				targetAttack = unitId
		
			end
		
		end
		
	end
	
	if targetAttack ~= "" then 
	
		COMPANION_DATA[heroId].attack = targetAttack
	
	end
	
	local distance = DistanceSquared(x1,y1,x2,y2)
	
	if ( distance < COMPANION_DATA[heroId].distance ) then
	
		if COMPANION_DATA[heroId].attack ~= "" then 
			
			WaitState( 0.25 )
			
			if not LuaUnitIsDead(COMPANION_DATA[heroId].attack) then 
				
				return LuaUnitAttackUnit( COMPANION_DATA[heroId].id, COMPANION_DATA[heroId].attack )
			
			end
		
		else 
		
			if (x1 == COMPANION_DATA[heroId].targetX and y1 == COMPANION_DATA[heroId].targetY) then
		
				COMPANION_DATA[heroId].notAction = COMPANION_DATA[heroId].notAction + 1

			end
			
			if COMPANION_DATA[heroId].notAction > COMPANION_DATA[heroId].limitNotAction then
			
				COMPANION_DATA[heroId].notAction = 0
				
				COMPANION_DATA[heroId].limitNotAction = LuaRandom( 15, 30 )
		
				local rx, ry = randomPosition( x1, y1, 3 )
		
				LuaCreatureMoveTo( COMPANION_DATA[heroId].id, rx , ry, 0 )
				
				WaitState( 1 )
				
				return
		
			end
			
			COMPANION_DATA[heroId].targetX = x1
			
			COMPANION_DATA[heroId].targetY = y1
			
		end
		
	else
		
		LuaUnitClearStates( COMPANION_DATA[heroId].id )
		
	end
	
	LuaCreatureMoveTo( COMPANION_DATA[heroId].id , x1 + 5 , y1, 0 )
	
	-- WaitState( 0.25 )

end

function randomPosition( x, y, range )

	if LuaRandom(0,100) > 50 then 
		
		x = x + range;
		
	else 
	
		x = x - range;
		
	end
	
	if LuaRandom(0,100) > 50 then 
		
		y = y + range;
		
	else 
	
		y = y - range;
		
	end
	
	return x, y

end