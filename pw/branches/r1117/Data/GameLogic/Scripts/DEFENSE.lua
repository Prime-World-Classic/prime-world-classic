include ("GameLogic/Scripts/Debug.lua")
include ("GameLogic/Scripts/StatesManager.lua")
include ("GameLogic/Scripts/Common.lua")
include ("GameLogic/Scripts/Consts.lua")

function Init( reconnecting )
	
	if not reconnecting then
		
		
		
	end
	
	ClearBridgeEvent()
	
	StartTrigger( StartEventBridge )
	
end

function StartEventBridge()
	
	local file = io.open( "bridge", "r" )
	
	if file then
		
		local content = file:read( "*a" )
		
		file:close()
		
		if content and #content > 0 then
			
			local values = {}
			
			for n in content:gmatch( "%d+" ) do
				
				values[ #values + 1 ] = tonumber( n )
				
			end
			
			if #values ~= 0 then 
				
				LuaMessageToChat( content )
				
				if values[1] == 1 then 
					
					AddTriggerTop( WaveSpawn, values[2], values[3] )
					
				end
				
				ClearBridgeEvent()
				
			end
			
		end
		
	end
	
	WaitState( 10 )
	
end

function ClearBridgeEvent()

	io.open( "bridge", "w" ):close()
	
end

function WaveSpawn( total, direction )

	for i = 1, total do
		
		if direction == 1 then 
			
			Spawn( "Ghost", 19, 120, 2 )
		
		else 
			
			Spawn( "Ghost", 235, 130, 2 )
		
		end
		
		WaitState( 1 )
	
	end
	
end

function Spawn( unit, x, y, faction )
	
	local name = unit .. "_" .. faction .. "_"

	LuaCreateCreep( name, unit, x, y, faction, faction )
	
	local objectId = LuaGetObjectId( name )
	
	LuaCreateZombieById( objectId, unit, faction )
	
	LuaKillUnit( name )
	
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

function OnUnitDie( victimId, killerId, lastHitterId, deathParamsInfo )
	
	if killerId == -1 then
	
		return
		
	end
	
	if LuaGetUnitTypeById( victimId ) ~= UnitTypeHeroMale then 
	
		AddTriggerTop( PointReceived, victimId, killerId )
	
	end
	
end