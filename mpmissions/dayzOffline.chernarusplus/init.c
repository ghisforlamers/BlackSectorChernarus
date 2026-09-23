void SpawnObject(string objectName, vector position, vector orientation)
{
	Object obj;
	obj = Object.Cast(GetGame().CreateObject(objectName, "0 0 0"));
	obj.SetPosition(position);
	obj.SetOrientation(orientation);

	// Force update collisions
	if (obj.CanAffectPathgraph())
	{
		obj.SetAffectPathgraph(true, false);
		GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(GetGame().UpdatePathgraphRegionByObject, 100, false, obj);
	}
}

#include "$CurrentDir:\\mpmissions\\dayzOffline.chernarusplus\\caves.c"
#include "$CurrentDir:\\mpmissions\\dayzOffline.chernarusplus\\BlackSector_BunkerLoot.c"

void main()
{
	//INIT ECONOMY--------------------------------------
	Hive ce = CreateHive();
	if ( ce )
		ce.InitOffline();

	//DATE RESET AFTER ECONOMY INIT-------------------------
	int year, month, day, hour, minute;
	int reset_month = 9, reset_day = 20;
	GetGame().GetWorld().GetDate(year, month, day, hour, minute);

	if ((month == reset_month) && (day < reset_day))
	{
		GetGame().GetWorld().SetDate(year, reset_month, reset_day, hour, minute);
	}
	else
	{
		if ((month == reset_month + 1) && (day > reset_day))
		{
			GetGame().GetWorld().SetDate(year, reset_month, reset_day, hour, minute);
		}
		else
		{
			if ((month < reset_month) || (month > reset_month + 1))
			{
				GetGame().GetWorld().SetDate(year, reset_month, reset_day, hour, minute);
			}
		}
	}

	caves();

	// Start Black Sector bunker loot manager after the mission/bunker mapping is loaded.
	GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(BS_StartBunkerLootManager, 15000, false);
}


class BlackSectorBunkerAmbush
{
	protected bool m_HordeActive;
	protected bool m_EncounterArmed;

	protected float m_SpawnRadius = 25.0;
	protected float m_DespawnRadius = 300.0;

	// Punched-card entrance / door area.
	protected vector m_TriggerCenter = "5284.168 470.462 14866.214";

	protected ref array<DayZInfected> m_SpawnedInfected;
	protected ref array<string> m_InfectedTypes;
	protected ref array<vector> m_SpawnPositions;

	void BlackSectorBunkerAmbush()
	{
		m_HordeActive = false;
		m_EncounterArmed = true;

		m_SpawnedInfected = new array<DayZInfected>;
		m_InfectedTypes = new array<string>;
		m_SpawnPositions = new array<vector>;

		// Current Chernarus military infected pool.
		m_InfectedTypes.Insert("ZmbM_PatrolNormal_Autumn");
		m_InfectedTypes.Insert("ZmbM_PatrolNormal_Flat");
		m_InfectedTypes.Insert("ZmbM_PatrolNormal_PautRev");
		m_InfectedTypes.Insert("ZmbM_PatrolNormal_Summer");
		m_InfectedTypes.Insert("ZmbM_SoldierNormal");
		m_InfectedTypes.Insert("ZmbM_usSoldier_normal_Woodland");

		// Approximate walkable positions immediately inside / behind the
		// punched-card entrance. Exact XYZ values are re-applied after creation.
		// Tune individual XYZ values if any infected appear inside geometry.
		m_SpawnPositions.Insert("5298.0 460.2 14875.0");
		m_SpawnPositions.Insert("5300.0 460.2 14874.0");
		m_SpawnPositions.Insert("5302.0 460.2 14873.0");
		m_SpawnPositions.Insert("5304.0 460.2 14872.0");
		m_SpawnPositions.Insert("5306.0 460.2 14871.0");

		m_SpawnPositions.Insert("5298.5 460.2 14878.0");
		m_SpawnPositions.Insert("5300.5 460.2 14878.5");
		m_SpawnPositions.Insert("5302.5 460.2 14878.0");
		m_SpawnPositions.Insert("5304.5 460.2 14877.5");
		m_SpawnPositions.Insert("5306.5 460.2 14877.0");

		m_SpawnPositions.Insert("5299.0 460.2 14881.0");
		m_SpawnPositions.Insert("5301.0 460.2 14881.0");
		m_SpawnPositions.Insert("5303.0 460.2 14880.5");
		m_SpawnPositions.Insert("5305.0 460.2 14880.0");

		m_SpawnPositions.Insert("5307.0 460.2 14875.0");
		m_SpawnPositions.Insert("5308.0 460.2 14873.0");
		m_SpawnPositions.Insert("5308.5 460.2 14877.0");
		m_SpawnPositions.Insert("5307.5 460.2 14880.0");

		m_SpawnPositions.Insert("5303.5 460.2 14884.0");
		m_SpawnPositions.Insert("5306.0 460.2 14883.0");

		GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(CheckPlayers, 2000, true);
		Print("[BlackSector] Bunker ambush manager initialized.");
	}

	void Stop()
	{
		GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).Remove(CheckPlayers);
		DespawnHorde();
	}

	protected float Distance2D(vector a, vector b)
	{
		float dx = a[0] - b[0];
		float dz = a[2] - b[2];
		return Math.Sqrt((dx * dx) + (dz * dz));
	}

	void CheckPlayers()
	{
		if (!GetGame().IsServer())
			return;

		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);

		bool playerInsideSpawnRadius = false;
		bool playerInsideDespawnRadius = false;

		foreach (Man man : players)
		{
			PlayerBase player = PlayerBase.Cast(man);
			if (!player || !player.IsAlive())
				continue;

			float distance = Distance2D(player.GetPosition(), m_TriggerCenter);

			if (distance <= m_DespawnRadius)
				playerInsideDespawnRadius = true;

			if (distance <= m_SpawnRadius)
				playerInsideSpawnRadius = true;
		}

		// Once every player has moved well away from the bunker, clean up
		// the encounter and arm it for the next expedition.
		if (!playerInsideDespawnRadius)
		{
			if (m_SpawnedInfected.Count() > 0)
				DespawnHorde();

			m_HordeActive = false;
			m_EncounterArmed = true;
			return;
		}

		// Spawn once when a player comes within 25 m of the card door.
		if (m_EncounterArmed && !m_HordeActive && playerInsideSpawnRadius)
		{
			SpawnHorde();
			m_HordeActive = true;
			m_EncounterArmed = false;
			return;
		}

		// If the group has been killed, do not respawn it while players
		// remain within the 300 m encounter area. Leaving the area rearms it.
		if (m_HordeActive && CountLivingInfected() == 0)
		{
			m_HordeActive = false;
			Print("[BlackSector] Bunker entrance horde eliminated.");
		}
	}

	protected int CountLivingInfected()
	{
		int living = 0;

		foreach (DayZInfected infected : m_SpawnedInfected)
		{
			if (infected && infected.IsAlive())
				living++;
		}

		return living;
	}

	protected void SpawnHorde()
	{
		m_SpawnedInfected.Clear();

		foreach (vector pos : m_SpawnPositions)
		{
			string infectedType = m_InfectedTypes.Get(Math.RandomInt(0, m_InfectedTypes.Count()));

			// Use the basic CreateObject API here instead of CreateObjectEx/ECE flags.
			// false = server/global object, true = initialize AI, true = create physics.
			Object spawnedObject = GetGame().CreateObject(infectedType, pos, false, true, true);
			DayZInfected infected = DayZInfected.Cast(spawnedObject);

			if (infected)
			{
				// Re-apply the exact XYZ so the infected remains at the intended
				// underground spawn position.
				infected.SetPosition(pos);
				m_SpawnedInfected.Insert(infected);
			}
			else if (spawnedObject)
			{
				// Defensive cleanup if the requested class did not create as DayZInfected.
				GetGame().ObjectDelete(spawnedObject);
			}
		}

		Print("[BlackSector] Bunker entrance horde spawned: " + m_SpawnedInfected.Count());
	}

	protected void DespawnHorde()
	{
		foreach (DayZInfected infected : m_SpawnedInfected)
		{
			if (infected)
				GetGame().ObjectDelete(infected);
		}

		m_SpawnedInfected.Clear();
		Print("[BlackSector] Bunker entrance horde despawned/reset.");
	}
}

class CustomMission: MissionServer
{
	protected ref BlackSectorBunkerAmbush m_BlackSectorBunkerAmbush;

	override void OnInit()
	{
		super.OnInit();

		m_BlackSectorBunkerAmbush = new BlackSectorBunkerAmbush();
	}

	override void OnMissionFinish()
	{
		// Save the final bunker inventory snapshot before mission shutdown.
		BS_StopBunkerLootManager();

		if (m_BlackSectorBunkerAmbush)
			m_BlackSectorBunkerAmbush.Stop();

		super.OnMissionFinish();
	}

	void SetRandomHealth(EntityAI itemEnt)
	{
		if ( itemEnt )
		{
			float rndHlt = Math.RandomFloat( 0.45, 0.65 );
			itemEnt.SetHealth01( "", "", rndHlt );
		}
	}

	override PlayerBase CreateCharacter(PlayerIdentity identity, vector pos, ParamsReadContext ctx, string characterName)
	{
		Entity playerEnt;
		playerEnt = GetGame().CreatePlayer( identity, characterName, pos, 0, "NONE" );
		Class.CastTo( m_player, playerEnt );

		GetGame().SelectPlayer( identity, m_player );

		return m_player;
	}

	override void StartingEquipSetup(PlayerBase player, bool clothesChosen)
	{
		EntityAI itemClothing;
		EntityAI itemEnt;
		ItemBase itemBs;
		float rand;

		itemClothing = player.FindAttachmentBySlotName( "Body" );
		if ( itemClothing )
		{
			SetRandomHealth( itemClothing );
			
			itemEnt = itemClothing.GetInventory().CreateInInventory( "BandageDressing" );
			player.SetQuickBarEntityShortcut(itemEnt, 2);
			
			string chemlightArray[] = { "Chemlight_White", "Chemlight_Yellow", "Chemlight_Green", "Chemlight_Red" };
			int rndIndex = Math.RandomInt( 0, 4 );
			itemEnt = itemClothing.GetInventory().CreateInInventory( chemlightArray[rndIndex] );
			SetRandomHealth( itemEnt );
			player.SetQuickBarEntityShortcut(itemEnt, 1);

			rand = Math.RandomFloatInclusive( 0.0, 1.0 );
			if ( rand < 0.35 )
				itemEnt = player.GetInventory().CreateInInventory( "Apple" );
			else if ( rand > 0.65 )
				itemEnt = player.GetInventory().CreateInInventory( "Pear" );
			else
				itemEnt = player.GetInventory().CreateInInventory( "Plum" );
			player.SetQuickBarEntityShortcut(itemEnt, 3);
			SetRandomHealth( itemEnt );
		}
		
		itemClothing = player.FindAttachmentBySlotName( "Legs" );
		if ( itemClothing )
			SetRandomHealth( itemClothing );
		
		itemClothing = player.FindAttachmentBySlotName( "Feet" );
	}
};

Mission CreateCustomMission(string path)
{
	return new CustomMission();
}