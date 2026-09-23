// Black Sector: Chernarus
// Mission-side bunker loot manager v3.1.
// V3.1 creates the 59 SFM containers itself without ECE_DYNAMIC_PERSISTENCY,
// persists the exact remaining inventory to JSON, and restores it after restart.

// Black Sector: Chernarus
// Server-side bunker loot manager.
//
// Reads:
//   $profile:BlackSector_BunkerLoot\BlackSector_BunkerContainerManifest.json
//   $profile:BlackSector_BunkerLoot\BlackSector_BunkerLootSettings.json
//   $profile:BlackSector_BunkerLoot\BlackSector_BunkerLoot.cfg
//
// Writes:
//   $profile:BlackSector_BunkerLoot\BlackSector_BunkerLootState.json
//
// The SFM container classes, SNAFU/FOG classes, vanilla classes, and Expansion
// classes are referenced by classname strings only. This keeps this PBO
// server-side and avoids a compile-time dependency on those mods.

class BSBunkerMoneyConfig
{
    string ClassName;
    int MinAmount;
    int MaxAmount;
};

class BSBunkerContainerConfig
{
    string Id;
    int MapLine;
    string ClassName;
    string LootProfile;
    string Room;
    ref array<float> Position;
    ref array<float> Orientation;
    bool Enabled;
};

class BSBunkerManifest
{
    int Version;
    string SourceMap;
    ref array<float> BunkerCenter;
    float ActivationRadiusMeters;
    float ResetRadiusMeters;
    int CooldownSeconds;
    float ContainerMatchRadiusMeters;
    ref BSBunkerMoneyConfig MoneyLoot;
    ref array<ref BSBunkerContainerConfig> Containers;
};

class BSBunkerLootEntry
{
    string ClassName;
    float Weight;
    bool FillMagazine;
};

class BSBunkerLootPool
{
    string Name;
    ref array<ref BSBunkerLootEntry> Entries;
};

class BSBunkerProfile
{
    string Name;
    int MinItems;
    int MaxItems;
    ref array<string> Pools;
    ref array<float> PoolWeights;
};

class BSBunkerLootSettings
{
    int Version;
    bool Enabled;
    int CheckIntervalMs;
    int ContainerResolveIntervalMs;
    bool MakeContainersUntakeable;
    bool MakeContainersInvulnerable;
    int ContainerLifetimeSeconds;
    bool ClearBeforeRefill;
    int MaximumSpawnAttemptsPerItem;
    float ItemHealthMin;
    float ItemHealthMax;
    bool LogSpawnFailures;
    int MinimumResolvedContainersToFill;
    ref array<ref BSBunkerProfile> Profiles;
    ref array<ref BSBunkerLootPool> Pools;
};

class BSAttachmentSlotPool
{
    string SlotName;
    ref array<string> ClassNames;

    void BSAttachmentSlotPool(string slotName)
    {
        SlotName = slotName;
        ClassNames = new array<string>;
    }
};

class BSBunkerRuntimeConfig
{
    int Version = 2;
    bool ResetTimerAndRerollOnServerRestart = false;

    // Complete every weapon spawned from the Weapons pool.
    bool SpawnWeaponsComplete = true;

    // For every attachment slot exposed by the weapon, randomly choose from
    // all loaded config classes that declare compatibility with that slot.
    bool RandomizeAllCompatibleAttachments = true;

    // Pick one random entry from the weapon's own magazines[] compatibility list.
    bool AttachCompatibleMagazine = true;

    // Fill attached detachable magazines to their maximum capacity.
    bool FillAttachedMagazines = true;

    // Also complete attachment slots on attached optics/lights/etc.
    bool CompleteNestedAttachmentSlots = true;

    // Prevent pathological recursive attachment trees from running forever.
    int MaximumNestedAttachmentDepth = 3;

    // Attachment blacklist. Matching is case-insensitive.
    // Exact classnames are checked first, then substring patterns.
    ref array<string> ExcludedAttachmentClassNames;
    ref array<string> ExcludedAttachmentClassNameContains;

    void BSBunkerRuntimeConfig()
    {
        ExcludedAttachmentClassNames = new array<string>;
        ExcludedAttachmentClassNameContains = new array<string>;
    }
};

class BSBunkerSavedItem
{
    string ClassName;
    float Health01 = 1.0;
    float Quantity = -1.0;
    int AmmoCount = -1;
    int AttachmentSlotId = -1;
    ref array<ref BSBunkerSavedItem> SavedAttachments;
    ref array<ref BSBunkerSavedItem> SavedCargo;

    void BSBunkerSavedItem()
    {
        SavedAttachments = new array<ref BSBunkerSavedItem>;
        SavedCargo = new array<ref BSBunkerSavedItem>;
    }
};

class BSBunkerContainerSnapshot
{
    string ContainerId;
    ref array<ref BSBunkerSavedItem> SavedAttachments;
    ref array<ref BSBunkerSavedItem> SavedCargo;

    void BSBunkerContainerSnapshot()
    {
        SavedAttachments = new array<ref BSBunkerSavedItem>;
        SavedCargo = new array<ref BSBunkerSavedItem>;
    }
};

class BSBunkerLootState
{
    int Version = 3;
    bool Armed = true;
    int LastFillUtcSeconds = 0;
    int FillSerial = 0;
    bool HasInventorySnapshot = false;
    ref array<ref BSBunkerContainerSnapshot> ContainerSnapshots;

    void BSBunkerLootState()
    {
        ContainerSnapshots = new array<ref BSBunkerContainerSnapshot>;
    }
};

class BSBunkerRuntimeContainer
{
    ref BSBunkerContainerConfig Config;
    EntityAI containerEntity;
    bool MissingLogged;

    void BSBunkerRuntimeContainer(BSBunkerContainerConfig cfg)
    {
        Config = cfg;
        containerEntity = null;
        MissingLogged = false;
    }
};

class BSBunkerLootManager
{
    static const string PROFILE_DIR = "$profile:BlackSector_BunkerLoot";
    static const string MANIFEST_PATH = "$profile:BlackSector_BunkerLoot\\BlackSector_BunkerContainerManifest.json";
    static const string SETTINGS_PATH = "$profile:BlackSector_BunkerLoot\\BlackSector_BunkerLootSettings.json";
    static const string RUNTIME_CFG_PATH = "$profile:BlackSector_BunkerLoot\\BlackSector_BunkerLoot.cfg";
    static const string STATE_PATH = "$profile:BlackSector_BunkerLoot\\BlackSector_BunkerLootState.json";

    protected ref BSBunkerManifest m_Manifest;
    protected ref BSBunkerLootSettings m_Settings;
    protected ref BSBunkerRuntimeConfig m_RuntimeConfig;
    protected ref BSBunkerLootState m_State;
    protected ref array<ref BSBunkerRuntimeContainer> m_Runtime;

    // Runtime index: attachment slot name -> every loaded class that declares
    // that slot in inventorySlot / inventorySlot[].
    protected ref array<ref BSAttachmentSlotPool> m_AttachmentSlotPools;
    protected bool m_AttachmentCompatibilityIndexBuilt;

    protected int m_LastResolveTime;
    protected int m_LastSnapshotTime;
    protected string m_LastInventoryFingerprint;
    protected bool m_Running;

    void BSBunkerLootManager()
    {
        m_Runtime = new array<ref BSBunkerRuntimeContainer>;
        m_AttachmentSlotPools = new array<ref BSAttachmentSlotPool>;
        m_AttachmentCompatibilityIndexBuilt = false;

        m_LastResolveTime = 0;
        m_LastSnapshotTime = 0;
        m_LastInventoryFingerprint = "";
        m_Running = false;
    }

    void Start()
    {
        if (!GetGame() || !GetGame().IsServer())
            return;

        if (!FileExist(PROFILE_DIR))
            MakeDirectory(PROFILE_DIR);

        if (!LoadConfiguration())
            return;

        if (!m_Settings.Enabled)
        {
            Print("[BlackSector:BunkerLoot] Disabled by settings.");
            return;
        }

        LoadState();
        ApplyRestartPolicy();
        BuildRuntimeList();

        if (m_RuntimeConfig && m_RuntimeConfig.SpawnWeaponsComplete && m_RuntimeConfig.RandomizeAllCompatibleAttachments)
            BuildAttachmentCompatibilityIndex();

        SpawnManagedContainers();

        if (m_State.HasInventorySnapshot)
        {
            RestoreAllContainersFromState();
            m_LastInventoryFingerprint = BuildInventoryFingerprint();
        }

        int interval = m_Settings.CheckIntervalMs;
        if (interval < 1000)
            interval = 1000;

        m_Running = true;
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(Check, interval, true);

        Print("[BlackSector:BunkerLoot] V3 started. Managed containers: " + m_Runtime.Count());
    }

    void Stop()
    {
        if (!m_Running)
            return;

        // Final authoritative save for a graceful restart/shutdown.
        if (m_State && !m_State.Armed && m_State.HasInventorySnapshot)
        {
            CaptureInventorySnapshot();
            SaveState();
            Print("[BlackSector:BunkerLoot] Final inventory snapshot saved.");
        }

        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(Check);
        m_Running = false;
    }

    protected bool LoadConfiguration()
    {
        string error;

        if (!JsonFileLoader<BSBunkerManifest>.LoadFile(MANIFEST_PATH, m_Manifest, error))
        {
            Print("[BlackSector:BunkerLoot] ERROR loading manifest: " + error);
            Print("[BlackSector:BunkerLoot] Expected: " + MANIFEST_PATH);
            return false;
        }

        error = "";
        if (!JsonFileLoader<BSBunkerLootSettings>.LoadFile(SETTINGS_PATH, m_Settings, error))
        {
            Print("[BlackSector:BunkerLoot] ERROR loading settings: " + error);
            Print("[BlackSector:BunkerLoot] Expected: " + SETTINGS_PATH);
            return false;
        }

        if (!FileExist(RUNTIME_CFG_PATH))
        {
            m_RuntimeConfig = new BSBunkerRuntimeConfig;
            error = "";
            JsonFileLoader<BSBunkerRuntimeConfig>.SaveFile(RUNTIME_CFG_PATH, m_RuntimeConfig, error);
            Print("[BlackSector:BunkerLoot] Created default runtime cfg: " + RUNTIME_CFG_PATH);
        }
        else
        {
            error = "";
            if (!JsonFileLoader<BSBunkerRuntimeConfig>.LoadFile(RUNTIME_CFG_PATH, m_RuntimeConfig, error))
            {
                Print("[BlackSector:BunkerLoot] ERROR loading runtime cfg: " + error);
                Print("[BlackSector:BunkerLoot] Expected: " + RUNTIME_CFG_PATH);
                return false;
            }
        }

        if (!m_RuntimeConfig)
            m_RuntimeConfig = new BSBunkerRuntimeConfig;

        EnsureDefaultAttachmentBlacklist();

        if (!m_Manifest || !m_Manifest.Containers || !m_Settings || !m_Settings.Profiles || !m_Settings.Pools)
        {
            Print("[BlackSector:BunkerLoot] ERROR: manifest/settings deserialized with missing required data.");
            return false;
        }

        if (!m_Manifest.BunkerCenter || m_Manifest.BunkerCenter.Count() < 3)
        {
            Print("[BlackSector:BunkerLoot] ERROR: BunkerCenter must contain three numbers.");
            return false;
        }

        return true;
    }

    protected void LoadState()
    {
        string error;
        if (!JsonFileLoader<BSBunkerLootState>.LoadFile(STATE_PATH, m_State, error))
        {
            m_State = new BSBunkerLootState;
            SaveState();
            Print("[BlackSector:BunkerLoot] Created new v3 state file.");
        }

        if (!m_State)
            m_State = new BSBunkerLootState;

        if (!m_State.ContainerSnapshots)
            m_State.ContainerSnapshots = new array<ref BSBunkerContainerSnapshot>;

        m_State.Version = 3;
    }

    protected void SaveState()
    {
        if (!m_State)
            return;

        m_State.Version = 3;

        string error;
        if (!JsonFileLoader<BSBunkerLootState>.SaveFile(STATE_PATH, m_State, error))
            Print("[BlackSector:BunkerLoot] ERROR saving state: " + error);
    }

    protected void ApplyRestartPolicy()
    {
        if (!m_State)
            return;

        if (m_RuntimeConfig && m_RuntimeConfig.ResetTimerAndRerollOnServerRestart)
        {
            m_State.Armed = true;
            m_State.LastFillUtcSeconds = 0;
            m_State.HasInventorySnapshot = false;

            if (!m_State.ContainerSnapshots)
                m_State.ContainerSnapshots = new array<ref BSBunkerContainerSnapshot>;
            else
                m_State.ContainerSnapshots.Clear();

            SaveState();
            Print("[BlackSector:BunkerLoot] Restart policy: timer reset; fresh reroll armed.");
            return;
        }

        // V2 state had no inventory snapshot. Do not strand a migrated server
        // in an empty cooldown: arm one fresh cycle on the first V3 startup.
        if (!m_State.Armed && !m_State.HasInventorySnapshot)
        {
            m_State.Armed = true;
            m_State.LastFillUtcSeconds = 0;
            SaveState();
            Print("[BlackSector:BunkerLoot] Legacy state detected; fresh V3 cycle armed.");
        }
    }

    protected void BuildRuntimeList()
    {
        m_Runtime.Clear();

        foreach (BSBunkerContainerConfig cfg : m_Manifest.Containers)
        {
            if (cfg && cfg.Enabled)
                m_Runtime.Insert(new BSBunkerRuntimeContainer(cfg));
        }
    }

    protected vector ArrayToVector(array<float> values)
    {
        if (!values || values.Count() < 3)
            return "0 0 0";

        return Vector(values[0], values[1], values[2]);
    }

    protected vector GetBunkerCenter()
    {
        return ArrayToVector(m_Manifest.BunkerCenter);
    }

    protected float Distance2D(vector a, vector b)
    {
        float dx = a[0] - b[0];
        float dz = a[2] - b[2];
        return Math.Sqrt((dx * dx) + (dz * dz));
    }

    protected bool IsEntityAlreadyAssigned(EntityAI entity, BSBunkerRuntimeContainer exceptRuntime)
    {
        if (!entity)
            return false;

        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (!runtime || runtime == exceptRuntime)
                continue;

            if (runtime.containerEntity == entity)
                return true;
        }

        return false;
    }

    protected void SpawnManagedContainers()
    {
        int spawnedCount = 0;

        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (!runtime || !runtime.Config)
                continue;

            RemoveLegacyContainerCopies(runtime.Config);

            EntityAI spawnedContainer = SpawnSingleManagedContainer(runtime.Config);
            runtime.containerEntity = spawnedContainer;

            if (spawnedContainer)
                spawnedCount++;
        }

        Print("[BlackSector:BunkerLoot] Spawned managed containers: " + spawnedCount + "/" + m_Runtime.Count());
    }

    protected void EnsureManagedContainers()
    {
        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (!runtime || !runtime.Config)
                continue;

            if (runtime.containerEntity)
            {
                ApplyContainerProtection(runtime.containerEntity);
                continue;
            }

            runtime.containerEntity = SpawnSingleManagedContainer(runtime.Config);

            // If a managed container was recreated during an active cycle,
            // restore only its saved contents.
            if (runtime.containerEntity && m_State && m_State.HasInventorySnapshot)
            {
                BSBunkerContainerSnapshot snapshot = FindContainerSnapshot(runtime.Config.Id);
                if (snapshot)
                    RestoreContainerSnapshot(runtime.containerEntity, snapshot);
            }
        }
    }

    protected EntityAI SpawnSingleManagedContainer(BSBunkerContainerConfig cfg)
    {
        if (!cfg)
            return null;

        vector position = ArrayToVector(cfg.Position);
        vector orientation = ArrayToVector(cfg.Orientation);

        // Intentionally omit ECE_DYNAMIC_PERSISTENCY. These placement entities
        // are recreated by V3 each mission start; their inventory is persisted
        // by BlackSector_BunkerLootState.json instead of storage_1.
        int spawnFlags = ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_KEEPHEIGHT;

        Object spawnedObject = GetGame().CreateObjectEx(cfg.ClassName, position, spawnFlags);
        EntityAI spawnedContainer = EntityAI.Cast(spawnedObject);

        if (!spawnedContainer)
        {
            if (spawnedObject)
                GetGame().ObjectDelete(spawnedObject);

            Print("[BlackSector:BunkerLoot] ERROR: failed to create managed container " + cfg.Id + " / " + cfg.ClassName);
            return null;
        }

        spawnedContainer.SetPosition(position);
        spawnedContainer.SetOrientation(orientation);
        spawnedContainer.SetFlags(EntityFlags.STATIC, false);
        spawnedContainer.Update();

        ApplyContainerProtection(spawnedContainer);
        return spawnedContainer;
    }

    protected void RemoveLegacyContainerCopies(BSBunkerContainerConfig cfg)
    {
        if (!cfg)
            return;

        vector expectedPos = ArrayToVector(cfg.Position);
        array<Object> objects = new array<Object>;
        array<CargoBase> proxyCargos = new array<CargoBase>;

        // 0.15 m is deliberately tighter than the closest pair of intended
        // bunker containers. It targets only copies at the exact same placement.
        GetGame().GetObjectsAtPosition3D(expectedPos, 0.15, objects, proxyCargos);

        int removed = 0;
        foreach (Object obj : objects)
        {
            EntityAI candidate = EntityAI.Cast(obj);
            if (!candidate)
                continue;

            if (candidate.GetType() != cfg.ClassName)
                continue;

            GetGame().ObjectDelete(candidate);
            removed++;
        }

        if (removed > 0)
            Print("[BlackSector:BunkerLoot] Removed " + removed + " legacy/duplicate copy for " + cfg.Id);
    }

    protected void ResolveContainers(bool force)
    {
        int gameTime = GetGame().GetTime();

        if (!force && (gameTime - m_LastResolveTime) < m_Settings.ContainerResolveIntervalMs)
            return;

        m_LastResolveTime = gameTime;

        int resolved = 0;

        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (!runtime || !runtime.Config)
                continue;

            if (runtime.containerEntity)
            {
                resolved++;
                ApplyContainerProtection(runtime.containerEntity);
                continue;
            }

            vector expectedPos = ArrayToVector(runtime.Config.Position);

            array<Object> objects = new array<Object>;
            array<CargoBase> proxyCargos = new array<CargoBase>;
            GetGame().GetObjectsAtPosition3D(expectedPos, m_Manifest.ContainerMatchRadiusMeters, objects, proxyCargos);

            EntityAI best;
            float bestDistance = 1000000.0;

            foreach (Object obj : objects)
            {
                EntityAI candidate = EntityAI.Cast(obj);
                if (!candidate)
                    continue;

                if (candidate.GetType() != runtime.Config.ClassName)
                    continue;

                if (IsEntityAlreadyAssigned(candidate, runtime))
                    continue;

                float distance = vector.Distance(candidate.GetPosition(), expectedPos);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = candidate;
                }
            }

            if (best)
            {
                runtime.containerEntity = best;
                runtime.MissingLogged = false;
                ApplyContainerProtection(best);
                resolved++;
                Print("[BlackSector:BunkerLoot] Bound " + runtime.Config.Id + " -> " + best.GetType());
            }
            else if (!runtime.MissingLogged)
            {
                runtime.MissingLogged = true;
                Print("[BlackSector:BunkerLoot] WARNING: container not found: " + runtime.Config.Id + " / " + runtime.Config.ClassName);
            }
        }

        Print("[BlackSector:BunkerLoot] Resolved containers: " + resolved + "/" + m_Runtime.Count());
    }

    protected void ApplyContainerProtection(EntityAI container)
    {
        if (!container)
            return;

        if (m_Settings.MakeContainersUntakeable)
            container.SetTakeable(false);

        if (m_Settings.MakeContainersInvulnerable)
            container.SetAllowDamage(false);

        if (m_Settings.ContainerLifetimeSeconds > 0)
        {
            container.SetLifetimeMax(m_Settings.ContainerLifetimeSeconds);
            container.SetLifetime(m_Settings.ContainerLifetimeSeconds);
        }
    }

    protected bool AnyLivingPlayerWithin(float radius)
    {
        array<Man> players = new array<Man>;
        GetGame().GetPlayers(players);

        vector center = GetBunkerCenter();

        foreach (Man man : players)
        {
            PlayerBase player = PlayerBase.Cast(man);
            if (!player || !player.IsAlive())
                continue;

            if (Distance2D(player.GetPosition(), center) <= radius)
                return true;
        }

        return false;
    }

    void Check()
    {
        if (!GetGame() || !GetGame().IsServer() || !m_Settings || !m_Settings.Enabled)
            return;

        EnsureManagedContainers();

        bool insideActivation = AnyLivingPlayerWithin(m_Manifest.ActivationRadiusMeters);
        bool insideReset = AnyLivingPlayerWithin(m_Manifest.ResetRadiusMeters);
        int now = GetUtcSeconds();

        if (!m_State.Armed)
        {
            SnapshotIfChanged();

            // A used bunker rearms only after every player has left the reset
            // radius and the configured cooldown has elapsed.
            if (!insideReset && (now - m_State.LastFillUtcSeconds) >= m_Manifest.CooldownSeconds)
            {
                ClearAllManagedContainers();

                m_State.Armed = true;
                m_State.HasInventorySnapshot = false;

                if (!m_State.ContainerSnapshots)
                    m_State.ContainerSnapshots = new array<ref BSBunkerContainerSnapshot>;
                else
                    m_State.ContainerSnapshots.Clear();

                m_LastInventoryFingerprint = "";
                SaveState();
                Print("[BlackSector:BunkerLoot] Bunker loot rearmed; previous container state cleared.");
            }

            return;
        }

        if (!insideActivation)
            return;

        int resolved = CountResolvedContainers();
        int minimumResolved = m_Settings.MinimumResolvedContainersToFill;
        if (minimumResolved < 1)
            minimumResolved = 1;

        if (resolved < minimumResolved)
        {
            Print("[BlackSector:BunkerLoot] WARNING: activation detected with only " + resolved + "/" + m_Runtime.Count() + " containers available. Fill postponed.");
            return;
        }

        int spawned = RefillAllContainers();

        if (spawned > 0)
        {
            m_State.Armed = false;
            m_State.LastFillUtcSeconds = now;
            m_State.FillSerial++;

            CaptureInventorySnapshot();
            SaveState();

            m_LastInventoryFingerprint = BuildInventoryFingerprint();
            m_LastSnapshotTime = GetGame().GetTime();

            Print("[BlackSector:BunkerLoot] Fill cycle " + m_State.FillSerial + " completed. Spawned items: " + spawned);
        }
        else
        {
            Print("[BlackSector:BunkerLoot] WARNING: fill cycle produced no items; bunker remains armed.");
        }
    }

    protected int CountResolvedContainers()
    {
        int count = 0;

        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (runtime && runtime.containerEntity)
                count++;
        }

        return count;
    }

    protected void ClearAllManagedContainers()
    {
        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (runtime && runtime.containerEntity)
                ClearContainer(runtime.containerEntity);
        }
    }

    protected int RefillAllContainers()
    {
        int totalSpawned = 0;

        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (!runtime || !runtime.Config || !runtime.containerEntity)
                continue;

            ApplyContainerProtection(runtime.containerEntity);

            if (m_Settings.ClearBeforeRefill)
                ClearContainer(runtime.containerEntity);

            if (runtime.Config.LootProfile == "Money")
                totalSpawned += FillMoneyContainer(runtime.containerEntity, runtime.Config);
            else
                totalSpawned += FillProfileContainer(runtime.containerEntity, runtime.Config);
        }

        return totalSpawned;
    }

    protected void ClearContainer(EntityAI container)
    {
        if (!container)
            return;

        GameInventory inventory = container.GetInventory();
        if (!inventory)
            return;

        CargoBase cargo = inventory.GetCargo();
        if (cargo)
        {
            for (int i = cargo.GetItemCount() - 1; i >= 0; i--)
            {
                EntityAI item = cargo.GetItem(i);
                if (item)
                    item.Delete();
            }
        }

        for (int a = inventory.AttachmentCount() - 1; a >= 0; a--)
        {
            EntityAI attachment = inventory.GetAttachmentFromIndex(a);
            if (attachment)
                attachment.Delete();
        }
    }

    protected int FillMoneyContainer(EntityAI container, BSBunkerContainerConfig cfg)
    {
        if (!m_Manifest.MoneyLoot || m_Manifest.MoneyLoot.ClassName == "")
            return 0;

        EntityAI entity = container.GetInventory().CreateEntityInCargo(m_Manifest.MoneyLoot.ClassName);
        ItemBase money = ItemBase.Cast(entity);

        if (!money)
        {
            if (entity)
                entity.Delete();

            LogSpawnFailure(cfg, m_Manifest.MoneyLoot.ClassName);
            return 0;
        }

        int amount = Math.RandomIntInclusive(m_Manifest.MoneyLoot.MinAmount, m_Manifest.MoneyLoot.MaxAmount);
        money.SetQuantity(amount);
        money.SetHealth01("", "", 1.0);

        return 1;
    }

    protected int FillProfileContainer(EntityAI container, BSBunkerContainerConfig cfg)
    {
        BSBunkerProfile profile = FindProfile(cfg.LootProfile);
        if (!profile)
        {
            Print("[BlackSector:BunkerLoot] WARNING: unknown loot profile '" + cfg.LootProfile + "' for " + cfg.Id);
            return 0;
        }

        int desired = Math.RandomIntInclusive(profile.MinItems, profile.MaxItems);
        int spawned = 0;
        int attempts = 0;
        int maxAttempts = desired * m_Settings.MaximumSpawnAttemptsPerItem;

        if (maxAttempts < desired)
            maxAttempts = desired;

        while (spawned < desired && attempts < maxAttempts)
        {
            attempts++;

            BSBunkerLootPool pool = PickPool(profile);
            if (!pool)
                continue;

            BSBunkerLootEntry entry = PickEntry(pool);
            if (!entry || entry.ClassName == "")
                continue;

            if (SpawnLootEntry(container, entry, cfg, pool.Name))
                spawned++;
        }

        if (spawned < desired)
        {
            Print("[BlackSector:BunkerLoot] " + cfg.Id + " filled " + spawned + "/" + desired + " items.");
        }

        return spawned;
    }

    protected BSBunkerProfile FindProfile(string name)
    {
        foreach (BSBunkerProfile profile : m_Settings.Profiles)
        {
            if (profile && profile.Name == name)
                return profile;
        }

        return null;
    }

    protected BSBunkerLootPool FindPool(string name)
    {
        foreach (BSBunkerLootPool pool : m_Settings.Pools)
        {
            if (pool && pool.Name == name)
                return pool;
        }

        return null;
    }

    protected BSBunkerLootPool PickPool(BSBunkerProfile profile)
    {
        if (!profile || !profile.Pools || profile.Pools.Count() == 0)
            return null;

        if (!profile.PoolWeights || profile.PoolWeights.Count() != profile.Pools.Count())
        {
            int index = Math.RandomInt(0, profile.Pools.Count());
            return FindPool(profile.Pools[index]);
        }

        float total = 0.0;
        foreach (float weight : profile.PoolWeights)
        {
            if (weight > 0.0)
                total += weight;
        }

        if (total <= 0.0)
            return FindPool(profile.Pools[0]);

        float roll = Math.RandomFloatInclusive(0.0, total);
        float running = 0.0;

        for (int i = 0; i < profile.Pools.Count(); i++)
        {
            float w = profile.PoolWeights[i];
            if (w <= 0.0)
                continue;

            running += w;
            if (roll <= running)
                return FindPool(profile.Pools[i]);
        }

        return FindPool(profile.Pools[profile.Pools.Count() - 1]);
    }

    protected BSBunkerLootEntry PickEntry(BSBunkerLootPool pool)
    {
        if (!pool || !pool.Entries || pool.Entries.Count() == 0)
            return null;

        float total = 0.0;
        foreach (BSBunkerLootEntry entry : pool.Entries)
        {
            if (entry && entry.Weight > 0.0)
                total += entry.Weight;
        }

        if (total <= 0.0)
            return pool.Entries[Math.RandomInt(0, pool.Entries.Count())];

        float roll = Math.RandomFloatInclusive(0.0, total);
        float running = 0.0;

        foreach (BSBunkerLootEntry candidate : pool.Entries)
        {
            if (!candidate || candidate.Weight <= 0.0)
                continue;

            running += candidate.Weight;
            if (roll <= running)
                return candidate;
        }

        return pool.Entries[pool.Entries.Count() - 1];
    }

    protected bool SpawnLootEntry(EntityAI container, BSBunkerLootEntry entry, BSBunkerContainerConfig cfg, string sourcePoolName)
    {
        EntityAI item = container.GetInventory().CreateEntityInCargo(entry.ClassName);

        if (!item)
        {
            LogSpawnFailure(cfg, entry.ClassName);
            return false;
        }

        ApplyRandomItemHealth(item);

        if (entry.FillMagazine)
        {
            Magazine standaloneMagazine = Magazine.Cast(item);
            if (standaloneMagazine)
                standaloneMagazine.ServerSetAmmoCount(standaloneMagazine.GetAmmoMax());
        }

        // Only entries selected from the Weapons pool are treated as weapon
        // packages. This avoids accidentally trying to "complete" loose optics,
        // magazines, clothing, or other cargo.
        if (sourcePoolName == "Weapons" && m_RuntimeConfig && m_RuntimeConfig.SpawnWeaponsComplete)
            CompleteWeaponPackage(item);

        return true;
    }

    protected void ApplyRandomItemHealth(EntityAI item)
    {
        if (!item)
            return;

        float minHealth = m_Settings.ItemHealthMin;
        float maxHealth = m_Settings.ItemHealthMax;

        if (minHealth < 0.0)
            minHealth = 0.0;
        if (maxHealth > 1.0)
            maxHealth = 1.0;
        if (maxHealth < minHealth)
            maxHealth = minHealth;

        item.SetHealth01("", "", Math.RandomFloatInclusive(minHealth, maxHealth));
    }

    protected void CompleteWeaponPackage(EntityAI weapon)
    {
        if (!weapon || !m_RuntimeConfig)
            return;

        // Magazine first so the detachable-magazine slot is guaranteed to be
        // occupied before the generic attachment-slot pass.
        if (m_RuntimeConfig.AttachCompatibleMagazine)
            AttachRandomCompatibleMagazine(weapon);

        if (m_RuntimeConfig.RandomizeAllCompatibleAttachments)
            FillAllCompatibleAttachmentSlots(weapon, 0);

        // Retry magazine after the attachment pass. Some heavily modded weapons
        // expose inventory state only after support parts are attached.
        if (m_RuntimeConfig.AttachCompatibleMagazine)
            AttachRandomCompatibleMagazine(weapon);
    }

    protected void EnsureDefaultAttachmentBlacklist()
    {
        if (!m_RuntimeConfig)
            return;

        if (!m_RuntimeConfig.ExcludedAttachmentClassNames)
            m_RuntimeConfig.ExcludedAttachmentClassNames = new array<string>;

        if (!m_RuntimeConfig.ExcludedAttachmentClassNameContains)
            m_RuntimeConfig.ExcludedAttachmentClassNameContains = new array<string>;

        AddUniqueString(m_RuntimeConfig.ExcludedAttachmentClassNames, "ImprovisedSuppressor");

        // Defensive aliases/patterns for mods that rename the bottle suppressor
        // or expose admin-only suppressors under custom classnames.
        AddUniqueString(m_RuntimeConfig.ExcludedAttachmentClassNameContains, "improvisedsuppressor");
        AddUniqueString(m_RuntimeConfig.ExcludedAttachmentClassNameContains, "bottlesuppressor");
        AddUniqueString(m_RuntimeConfig.ExcludedAttachmentClassNameContains, "plasticbottle");
        AddUniqueString(m_RuntimeConfig.ExcludedAttachmentClassNameContains, "admin");

        // Persist newly-added defaults so the live cfg documents the active policy.
        string error;
        JsonFileLoader<BSBunkerRuntimeConfig>.SaveFile(RUNTIME_CFG_PATH, m_RuntimeConfig, error);
        if (error != "")
            Print("[BlackSector:BunkerLoot] WARNING saving runtime cfg blacklist defaults: " + error);
    }

    protected void AddUniqueString(array<string> values, string value)
    {
        if (!values || value == "")
            return;

        string normalizedValue = value;
        normalizedValue.ToLower();

        foreach (string existingValue : values)
        {
            string normalizedExisting = existingValue;
            normalizedExisting.ToLower();

            if (normalizedExisting == normalizedValue)
                return;
        }

        values.Insert(value);
    }

    protected bool IsAttachmentClassExcluded(string className)
    {
        if (className == "")
            return true;

        if (!m_RuntimeConfig)
            return false;

        string normalizedClass = className;
        normalizedClass.ToLower();

        if (m_RuntimeConfig.ExcludedAttachmentClassNames)
        {
            foreach (string exactName : m_RuntimeConfig.ExcludedAttachmentClassNames)
            {
                string normalizedExact = exactName;
                normalizedExact.ToLower();

                if (normalizedExact != "" && normalizedClass == normalizedExact)
                    return true;
            }
        }

        if (m_RuntimeConfig.ExcludedAttachmentClassNameContains)
        {
            foreach (string pattern : m_RuntimeConfig.ExcludedAttachmentClassNameContains)
            {
                string normalizedPattern = pattern;
                normalizedPattern.ToLower();

                if (normalizedPattern != "" && normalizedClass.IndexOf(normalizedPattern) >= 0)
                    return true;
            }
        }

        return false;
    }

    protected void BuildAttachmentCompatibilityIndex()
    {
        if (m_AttachmentCompatibilityIndexBuilt)
            return;

        if (!m_AttachmentSlotPools)
            m_AttachmentSlotPools = new array<ref BSAttachmentSlotPool>;
        else
            m_AttachmentSlotPools.Clear();

        // Most DayZ inventory attachments are CfgVehicles entities. A small
        // number of mods expose inventory-capable classes through CfgWeapons,
        // so index both roots and deduplicate by classname within each slot.
        IndexAttachmentClassesFromConfigRoot("CfgVehicles");
        IndexAttachmentClassesFromConfigRoot("CfgWeapons");

        m_AttachmentCompatibilityIndexBuilt = true;

        int classRefs = 0;
        foreach (BSAttachmentSlotPool slotPool : m_AttachmentSlotPools)
        {
            if (slotPool && slotPool.ClassNames)
                classRefs += slotPool.ClassNames.Count();
        }

        Print("[BlackSector:BunkerLoot] Attachment compatibility index built: " + m_AttachmentSlotPools.Count() + " slots / " + classRefs + " compatible class references.");
    }

    protected void IndexAttachmentClassesFromConfigRoot(string configRoot)
    {
        if (!GetGame().ConfigIsExisting(configRoot))
            return;

        int childCount = GetGame().ConfigGetChildrenCount(configRoot);

        for (int i = 0; i < childCount; i++)
        {
            string className;
            if (!GetGame().ConfigGetChildName(configRoot, i, className))
                continue;

            if (className == "")
                continue;

            if (IsAttachmentClassExcluded(className))
                continue;

            string classPath = configRoot + " " + className;

            // scope=2 is the normal public/spawnable config scope. Hidden base
            // classes and editor-only/static definitions are intentionally skipped.
            if (GetGame().ConfigGetInt(classPath + " scope") != 2)
                continue;

            array<string> compatibleSlots = new array<string>;
            ReadConfigStringList(classPath + " inventorySlot", compatibleSlots);

            foreach (string slotName : compatibleSlots)
            {
                if (slotName == "")
                    continue;

                BSAttachmentSlotPool slotPool = GetOrCreateAttachmentSlotPool(slotName);
                if (!slotPool || !slotPool.ClassNames)
                    continue;

                if (slotPool.ClassNames.Find(className) == -1)
                    slotPool.ClassNames.Insert(className);
            }
        }
    }

    protected BSAttachmentSlotPool GetOrCreateAttachmentSlotPool(string slotName)
    {
        string normalized = NormalizeSlotName(slotName);

        foreach (BSAttachmentSlotPool existing : m_AttachmentSlotPools)
        {
            if (existing && existing.SlotName == normalized)
                return existing;
        }

        BSAttachmentSlotPool created = new BSAttachmentSlotPool(normalized);
        m_AttachmentSlotPools.Insert(created);
        return created;
    }

    protected BSAttachmentSlotPool FindAttachmentSlotPool(string slotName)
    {
        if (!m_AttachmentCompatibilityIndexBuilt)
            BuildAttachmentCompatibilityIndex();

        string normalized = NormalizeSlotName(slotName);

        foreach (BSAttachmentSlotPool slotPool : m_AttachmentSlotPools)
        {
            if (slotPool && slotPool.SlotName == normalized)
                return slotPool;
        }

        return null;
    }

    protected string NormalizeSlotName(string slotName)
    {
        string normalized = slotName;
        normalized.ToLower();
        return normalized;
    }

    protected string GetEntityConfigRoot(string className)
    {
        if (GetGame().ConfigIsExisting("CfgWeapons " + className))
            return "CfgWeapons";

        if (GetGame().ConfigIsExisting("CfgVehicles " + className))
            return "CfgVehicles";

        return "";
    }

    protected void ReadConfigStringList(string configPath, array<string> values)
    {
        if (!values)
            return;

        values.Clear();

        if (!GetGame().ConfigIsExisting(configPath))
            return;

        int configType = GetGame().ConfigGetType(configPath);

        if (configType == CT_ARRAY)
        {
            GetGame().ConfigGetTextArray(configPath, values);
            return;
        }

        string singleValue;
        GetGame().ConfigGetText(configPath, singleValue);

        if (singleValue != "")
            values.Insert(singleValue);
    }

    protected bool HasMagazineAttached(EntityAI weapon)
    {
        if (!weapon)
            return false;

        GameInventory inventory = weapon.GetInventory();
        if (!inventory)
            return false;

        for (int i = 0; i < inventory.AttachmentCount(); i++)
        {
            EntityAI attachment = inventory.GetAttachmentFromIndex(i);
            if (Magazine.Cast(attachment))
                return true;
        }

        return false;
    }

    protected bool AttachRandomCompatibleMagazine(EntityAI weapon)
    {
        if (!weapon)
            return false;

        if (HasMagazineAttached(weapon))
            return true;

        string configRoot = GetEntityConfigRoot(weapon.GetType());
        if (configRoot == "")
            return false;

        array<string> compatibleMagazines = new array<string>;
        ReadConfigStringList(configRoot + " " + weapon.GetType() + " magazines", compatibleMagazines);

        if (compatibleMagazines.Count() == 0)
            return false;

        // Random without replacement. Because this list comes directly from the
        // weapon's magazines[] config, every entry is nominally compatible; if
        // a mod adds an extra runtime restriction, failed candidates are skipped.
        array<string> remaining = new array<string>;
        remaining.Copy(compatibleMagazines);

        while (remaining.Count() > 0)
        {
            int index = Math.RandomInt(0, remaining.Count());
            string magazineClass = remaining[index];
            remaining.RemoveOrdered(index);

            if (magazineClass == "")
                continue;

            EntityAI created = weapon.GetInventory().CreateAttachment(magazineClass);
            Magazine magazine = Magazine.Cast(created);

            if (!magazine)
            {
                if (created)
                    created.Delete();

                continue;
            }

            ApplyRandomItemHealth(magazine);

            if (m_RuntimeConfig.FillAttachedMagazines)
                magazine.ServerSetAmmoCount(magazine.GetAmmoMax());

            return true;
        }

        return false;
    }

    protected void FillAllCompatibleAttachmentSlots(EntityAI parent, int depth)
    {
        if (!parent || !m_RuntimeConfig)
            return;

        int maxDepth = m_RuntimeConfig.MaximumNestedAttachmentDepth;
        if (maxDepth < 0)
            maxDepth = 0;

        if (depth > maxDepth)
            return;

        string configRoot = GetEntityConfigRoot(parent.GetType());
        if (configRoot == "")
            return;

        array<string> attachmentSlots = new array<string>;
        ReadConfigStringList(configRoot + " " + parent.GetType() + " attachments", attachmentSlots);

        if (attachmentSlots.Count() == 0)
            return;

        // Multiple passes handle modded dependency chains where, for example,
        // a rail/handguard must exist before another attachment becomes valid.
        for (int pass = 0; pass < 4; pass++)
        {
            bool addedThisPass = false;

            foreach (string slotName : attachmentSlots)
            {
                if (slotName == "")
                    continue;

                if (parent.FindAttachmentBySlotName(slotName))
                    continue;

                EntityAI attached = AttachRandomCompatibleItemToSlot(parent, slotName, depth);
                if (attached)
                    addedThisPass = true;
            }

            if (!addedThisPass)
                break;
        }

        if (!m_RuntimeConfig.CompleteNestedAttachmentSlots)
            return;

        GameInventory inventory = parent.GetInventory();
        if (!inventory)
            return;

        // Also recurse into attachments that may have existed by default rather
        // than being created in the loop above.
        for (int i = 0; i < inventory.AttachmentCount(); i++)
        {
            EntityAI child = inventory.GetAttachmentFromIndex(i);
            if (!child)
                continue;

            // A detachable magazine does not need attachment recursion.
            if (Magazine.Cast(child))
                continue;

            FillAllCompatibleAttachmentSlots(child, depth + 1);
        }
    }

    protected EntityAI AttachRandomCompatibleItemToSlot(EntityAI parent, string slotName, int depth)
    {
        if (!parent || slotName == "")
            return null;

        int slotId = InventorySlots.GetSlotIdFromString(slotName);
        if (!InventorySlots.IsSlotIdValid(slotId))
            return null;

        if (parent.FindAttachmentBySlotName(slotName))
            return null;

        BSAttachmentSlotPool slotPool = FindAttachmentSlotPool(slotName);
        if (!slotPool || !slotPool.ClassNames || slotPool.ClassNames.Count() == 0)
            return null;

        array<string> remaining = new array<string>;
        remaining.Copy(slotPool.ClassNames);

        // Random without replacement means that, among classes the engine
        // actually accepts for this exact parent+slot combination, the first
        // accepted item is randomly selected.
        while (remaining.Count() > 0)
        {
            int index = Math.RandomInt(0, remaining.Count());
            string candidateClass = remaining[index];
            remaining.RemoveOrdered(index);

            if (candidateClass == "")
                continue;

            if (IsAttachmentClassExcluded(candidateClass))
                continue;

            EntityAI attached = parent.GetInventory().CreateAttachmentEx(candidateClass, slotId);
            if (!attached)
                continue;

            ApplyRandomItemHealth(attached);

            Magazine attachedMagazine = Magazine.Cast(attached);
            if (attachedMagazine && m_RuntimeConfig.FillAttachedMagazines)
                attachedMagazine.ServerSetAmmoCount(attachedMagazine.GetAmmoMax());

            if (m_RuntimeConfig.CompleteNestedAttachmentSlots && !attachedMagazine)
                FillAllCompatibleAttachmentSlots(attached, depth + 1);

            return attached;
        }

        return null;
    }

    protected void LogSpawnFailure(BSBunkerContainerConfig cfg, string className)
    {
        if (!m_Settings.LogSpawnFailures)
            return;

        string id = "<unknown>";
        string profile = "<unknown>";

        if (cfg)
        {
            id = cfg.Id;
            profile = cfg.LootProfile;
        }

        Print("[BlackSector:BunkerLoot] Spawn rejected/failed: " + className + " -> " + id + " (" + profile + ")");
    }

    protected void SnapshotIfChanged()
    {
        if (!m_State || m_State.Armed || !m_State.HasInventorySnapshot)
            return;

        int nowMs = GetGame().GetTime();
        int interval = m_Settings.CheckIntervalMs;
        if (interval < 1000)
            interval = 1000;

        if ((nowMs - m_LastSnapshotTime) < interval)
            return;

        m_LastSnapshotTime = nowMs;

        string fingerprint = BuildInventoryFingerprint();
        if (fingerprint == m_LastInventoryFingerprint)
            return;

        CaptureInventorySnapshot();
        SaveState();
        m_LastInventoryFingerprint = fingerprint;

        Print("[BlackSector:BunkerLoot] Remaining bunker inventory changed; snapshot saved.");
    }

    protected void CaptureInventorySnapshot()
    {
        if (!m_State)
            return;

        if (!m_State.ContainerSnapshots)
            m_State.ContainerSnapshots = new array<ref BSBunkerContainerSnapshot>;
        else
            m_State.ContainerSnapshots.Clear();

        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (!runtime || !runtime.Config || !runtime.containerEntity)
                continue;

            BSBunkerContainerSnapshot snapshot = SnapshotContainer(runtime.containerEntity, runtime.Config.Id);
            if (snapshot)
                m_State.ContainerSnapshots.Insert(snapshot);
        }

        m_State.HasInventorySnapshot = true;
    }

    protected BSBunkerContainerSnapshot SnapshotContainer(EntityAI container, string containerId)
    {
        if (!container)
            return null;

        BSBunkerContainerSnapshot snapshot = new BSBunkerContainerSnapshot;
        snapshot.ContainerId = containerId;

        GameInventory inventory = container.GetInventory();
        if (!inventory)
            return snapshot;

        for (int a = 0; a < inventory.AttachmentCount(); a++)
        {
            EntityAI attachment = inventory.GetAttachmentFromIndex(a);
            BSBunkerSavedItem savedAttachment = SnapshotItemRecursive(attachment);

            if (savedAttachment)
                snapshot.SavedAttachments.Insert(savedAttachment);
        }

        CargoBase cargo = inventory.GetCargo();
        if (cargo)
        {
            for (int c = 0; c < cargo.GetItemCount(); c++)
            {
                EntityAI cargoItem = cargo.GetItem(c);
                BSBunkerSavedItem savedCargo = SnapshotItemRecursive(cargoItem);

                if (savedCargo)
                    snapshot.SavedCargo.Insert(savedCargo);
            }
        }

        return snapshot;
    }

    protected BSBunkerSavedItem SnapshotItemRecursive(EntityAI entity)
    {
        if (!entity)
            return null;

        BSBunkerSavedItem saved = new BSBunkerSavedItem;
        saved.ClassName = entity.GetType();
        saved.Health01 = entity.GetHealth01("", "");

        InventoryLocation currentLocation = new InventoryLocation;
        if (entity.GetInventory() && entity.GetInventory().GetCurrentInventoryLocation(currentLocation))
        {
            if (currentLocation.GetType() == InventoryLocationType.ATTACHMENT)
                saved.AttachmentSlotId = currentLocation.GetSlot();
        }

        Magazine magazine = Magazine.Cast(entity);
        if (magazine)
        {
            saved.AmmoCount = magazine.GetAmmoCount();
        }
        else
        {
            ItemBase itemBase = ItemBase.Cast(entity);
            if (itemBase && itemBase.HasQuantity())
                saved.Quantity = itemBase.GetQuantity();
        }

        GameInventory inventory = entity.GetInventory();
        if (!inventory)
            return saved;

        for (int a = 0; a < inventory.AttachmentCount(); a++)
        {
            EntityAI attachment = inventory.GetAttachmentFromIndex(a);
            BSBunkerSavedItem savedAttachment = SnapshotItemRecursive(attachment);

            if (savedAttachment)
                saved.SavedAttachments.Insert(savedAttachment);
        }

        CargoBase cargo = inventory.GetCargo();
        if (cargo)
        {
            for (int c = 0; c < cargo.GetItemCount(); c++)
            {
                EntityAI cargoItem = cargo.GetItem(c);
                BSBunkerSavedItem savedCargo = SnapshotItemRecursive(cargoItem);

                if (savedCargo)
                    saved.SavedCargo.Insert(savedCargo);
            }
        }

        return saved;
    }

    protected void RestoreAllContainersFromState()
    {
        if (!m_State || !m_State.HasInventorySnapshot || !m_State.ContainerSnapshots)
            return;

        int restoredContainers = 0;
        int restoredItems = 0;

        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (!runtime || !runtime.Config || !runtime.containerEntity)
                continue;

            ClearContainer(runtime.containerEntity);

            BSBunkerContainerSnapshot snapshot = FindContainerSnapshot(runtime.Config.Id);
            if (!snapshot)
                continue;

            restoredItems += RestoreContainerSnapshot(runtime.containerEntity, snapshot);
            restoredContainers++;
        }

        Print("[BlackSector:BunkerLoot] Restored saved bunker inventory: " + restoredContainers + " containers, " + restoredItems + " top-level items.");
    }

    protected BSBunkerContainerSnapshot FindContainerSnapshot(string containerId)
    {
        if (!m_State || !m_State.ContainerSnapshots)
            return null;

        foreach (BSBunkerContainerSnapshot snapshot : m_State.ContainerSnapshots)
        {
            if (snapshot && snapshot.ContainerId == containerId)
                return snapshot;
        }

        return null;
    }

    protected int RestoreContainerSnapshot(EntityAI container, BSBunkerContainerSnapshot snapshot)
    {
        if (!container || !snapshot)
            return 0;

        int restored = 0;

        if (snapshot.SavedAttachments)
        {
            foreach (BSBunkerSavedItem savedAttachment : snapshot.SavedAttachments)
            {
                if (RestoreSavedItem(container, savedAttachment, true))
                    restored++;
            }
        }

        if (snapshot.SavedCargo)
        {
            foreach (BSBunkerSavedItem savedCargo : snapshot.SavedCargo)
            {
                if (RestoreSavedItem(container, savedCargo, false))
                    restored++;
            }
        }

        return restored;
    }

    protected EntityAI RestoreSavedItem(EntityAI parent, BSBunkerSavedItem saved, bool asAttachment)
    {
        if (!parent || !saved || saved.ClassName == "")
            return null;

        GameInventory parentInventory = parent.GetInventory();
        if (!parentInventory)
            return null;

        EntityAI restored;

        if (asAttachment)
        {
            if (saved.AttachmentSlotId >= 0)
                restored = parentInventory.CreateAttachmentEx(saved.ClassName, saved.AttachmentSlotId);
            else
                restored = parentInventory.CreateAttachment(saved.ClassName);
        }
        else
        {
            restored = parentInventory.CreateEntityInCargo(saved.ClassName);
        }

        if (!restored)
        {
            Print("[BlackSector:BunkerLoot] WARNING: failed to restore saved item " + saved.ClassName);
            return null;
        }

        restored.SetHealth01("", "", saved.Health01);

        Magazine magazine = Magazine.Cast(restored);
        if (magazine && saved.AmmoCount >= 0)
        {
            magazine.ServerSetAmmoCount(saved.AmmoCount);
        }
        else
        {
            ItemBase itemBase = ItemBase.Cast(restored);
            if (itemBase && itemBase.HasQuantity() && saved.Quantity >= 0.0)
                itemBase.SetQuantity(saved.Quantity, false);
        }

        if (saved.SavedAttachments)
        {
            foreach (BSBunkerSavedItem childAttachment : saved.SavedAttachments)
                RestoreSavedItem(restored, childAttachment, true);
        }

        if (saved.SavedCargo)
        {
            foreach (BSBunkerSavedItem childCargo : saved.SavedCargo)
                RestoreSavedItem(restored, childCargo, false);
        }

        return restored;
    }

    protected string BuildInventoryFingerprint()
    {
        string fingerprint = "";

        foreach (BSBunkerRuntimeContainer runtime : m_Runtime)
        {
            if (!runtime || !runtime.Config)
                continue;

            fingerprint += runtime.Config.Id + "=";

            if (runtime.containerEntity)
                fingerprint += BuildEntityInventoryFingerprint(runtime.containerEntity);

            fingerprint += ";";
        }

        return fingerprint;
    }

    protected string BuildEntityInventoryFingerprint(EntityAI entity)
    {
        if (!entity)
            return "";

        string result = entity.GetType() + "@" + entity.GetHealth01("", "");

        Magazine magazine = Magazine.Cast(entity);
        if (magazine)
        {
            result += "#A" + magazine.GetAmmoCount();
        }
        else
        {
            ItemBase itemBase = ItemBase.Cast(entity);
            if (itemBase && itemBase.HasQuantity())
                result += "#Q" + itemBase.GetQuantity();
        }

        GameInventory inventory = entity.GetInventory();
        if (!inventory)
            return result;

        result += "[A";

        for (int a = 0; a < inventory.AttachmentCount(); a++)
        {
            EntityAI attachment = inventory.GetAttachmentFromIndex(a);
            result += BuildEntityInventoryFingerprint(attachment) + ",";
        }

        result += "][C";

        CargoBase cargo = inventory.GetCargo();
        if (cargo)
        {
            for (int c = 0; c < cargo.GetItemCount(); c++)
            {
                EntityAI cargoItem = cargo.GetItem(c);
                result += BuildEntityInventoryFingerprint(cargoItem) + ",";
            }
        }

        result += "]";
        return result;
    }

    protected bool IsLeapYear(int year)
    {
        if ((year % 400) == 0)
            return true;
        if ((year % 100) == 0)
            return false;
        return (year % 4) == 0;
    }

    protected int DaysInMonth(int year, int month)
    {
        switch (month)
        {
            case 1:  return 31;
            case 2:
            {
                if (IsLeapYear(year))
                    return 29;

                return 28;
            }
            case 3:  return 31;
            case 4:  return 30;
            case 5:  return 31;
            case 6:  return 30;
            case 7:  return 31;
            case 8:  return 31;
            case 9:  return 30;
            case 10: return 31;
            case 11: return 30;
            case 12: return 31;
        }

        return 30;
    }

    protected int GetUtcSeconds()
    {
        int year;
        int month;
        int day;
        int hour;
        int minute;
        int second;

        GetYearMonthDayUTC(year, month, day);
        GetHourMinuteSecondUTC(hour, minute, second);

        int days = 0;

        for (int y = 1970; y < year; y++)
        {
            if (IsLeapYear(y))
                days += 366;
            else
                days += 365;
        }

        for (int m = 1; m < month; m++)
        {
            days += DaysInMonth(year, m);
        }

        days += day - 1;

        return (days * 86400) + (hour * 3600) + (minute * 60) + second;
    }
};

ref BSBunkerLootManager g_BlackSectorBunkerLootManager;

void BS_StartBunkerLootManager()
{
    if (!GetGame() || !GetGame().IsServer())
        return;

    if (g_BlackSectorBunkerLootManager)
        return;

    g_BlackSectorBunkerLootManager = new BSBunkerLootManager;
    g_BlackSectorBunkerLootManager.Start();
}

void BS_StopBunkerLootManager()
{
    if (!g_BlackSectorBunkerLootManager)
        return;

    g_BlackSectorBunkerLootManager.Stop();
    g_BlackSectorBunkerLootManager = null;
}
