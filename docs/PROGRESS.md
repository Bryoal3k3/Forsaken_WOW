# RandomBot AI Development Progress

## Project Status: Phase 4 COMPLETE + Combat System Refactor

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 0 | ✅ Complete | Auto-generate bot accounts and characters |
| Phase 1 | ✅ Complete | Combat AI - bots fight when attacked |
| Phase 2 | ✅ Complete | Grinding - find mobs, kill, loot, rest |
| Phase 3 | ✅ Complete | Death handling and respawn |
| Phase 4 | ✅ Complete | Vendoring - sell items, repair gear |
| Phase 4.5 | ✅ Complete | Combat system refactor - class-appropriate engagement |
| Phase 5 | 📋 Planned | Movement - exploration, travel |

---

## 2025-01-11 - COMBAT SYSTEM REFACTOR

### Problem Solved
All bots (including casters) were initiating combat with melee auto-attack. Mages would run up and swing at mobs before casting. Hunters would run into melee instead of using Auto Shot.

### Solution: Modular Combat Handlers
Created a new combat architecture with per-class handlers:

| Class Type | Engagement Behavior |
|------------|---------------------|
| **Melee** (Warrior, Rogue, Paladin, Shaman, Druid) | `Attack()` + `MoveChase()` - run in and melee |
| **Caster** (Mage, Priest, Warlock) | `SetTargetGuid()` only - first rotation spell pulls |
| **Hunter** | `SetTargetGuid()` + `MoveChase(25.0f)` + Auto Shot at range |

### New Architecture

```
src/game/PlayerBots/
├── Combat/
│   ├── IClassCombat.h              ← Interface for class handlers
│   ├── BotCombatMgr.h/cpp          ← Combat coordinator
│   │
│   └── Classes/
│       ├── WarriorCombat.h/cpp     ← Melee engagement
│       ├── PaladinCombat.h/cpp     ← Melee engagement
│       ├── HunterCombat.h/cpp      ← Ranged, Auto Shot at 25 yards
│       ├── MageCombat.h/cpp        ← Caster, spell pull
│       ├── PriestCombat.h/cpp      ← Caster, spell pull
│       ├── WarlockCombat.h/cpp     ← Caster, spell pull
│       ├── RogueCombat.h/cpp       ← Melee engagement
│       ├── ShamanCombat.h/cpp      ← Melee engagement
│       └── DruidCombat.h/cpp       ← Melee engagement
```

### How It Works

1. **RandomBotAI** creates a `BotCombatMgr` on initialization
2. **BotCombatMgr** creates the appropriate class handler based on `GetClass()`
3. **GrindingStrategy** calls `GetCombatMgr()->Engage()` instead of generic `Attack()`
4. Each class handler implements `IClassCombat` interface:
   - `Engage()` - How to initiate combat (class-specific)
   - `UpdateCombat()` - Combat rotation
   - `UpdateOutOfCombat()` - Buffs, pet management

### Files Created (21 new files)

| File | Purpose |
|------|---------|
| `Combat/IClassCombat.h` | Interface for class handlers |
| `Combat/BotCombatMgr.h` | Combat coordinator header |
| `Combat/BotCombatMgr.cpp` | Combat coordinator implementation |
| `Combat/Classes/*Combat.h/cpp` | 9 class handlers (18 files) |

### Files Modified

| File | Changes |
|------|---------|
| `RandomBotAI.h` | Added `BotCombatMgr` member and accessor |
| `RandomBotAI.cpp` | Initialize combat manager, delegate to handlers |
| `GrindingStrategy.cpp` | Use `GetCombatMgr()->Engage()` for engagement |
| `CMakeLists.txt` | Added new source files and include directories |

---

## 2025-01-11 - CRITICAL BUG FIXES

### Issues Fixed

**1. Horde Bots Not Attacking (Orc, Tauren, Troll)**
- **Problem**: Horde bots (Orc, Tauren, Troll) would spawn but stand idle, never attacking mobs. Alliance bots and Night Elf worked fine.
- **Root Cause**: The mob search range (50 yards) was too small. Horde starting areas (Valley of Trials, Red Cloud Mesa) have hostile mobs spawned further from the spawn point than Alliance areas. The search only found friendly NPCs (trainers, quest givers).
- **Fix**: Increased `SEARCH_RANGE` from 50.0f to 150.0f in `GrindingStrategy.h`.

**2. GUID Conflict Bug**
- **Problem**: When user created characters, they would overwrite bot characters because both used GUID 1, 2, 3, etc.
- **Root Cause**: `RandomBotGenerator::GetNextFreeCharacterGuid()` queried DB directly but didn't update `ObjectMgr.m_CharGuids` counter. When player created character, `GeneratePlayerLowGuid()` returned conflicting GUID.
- **Fix**: Changed `RandomBotGenerator::GenerateRandomBots()` to use `sObjectMgr.GeneratePlayerLowGuid()` instead of manual GUID calculation. This keeps the GUID counter in sync.

**2. Account ID Corruption Bug**
- **Problem**: Bot characters' account IDs in DB would change from real values (1-6) to fake session IDs (10000+) after server restart.
- **Root Cause**: Bots use generated session account IDs (10000+) to allow multiple bots from same account to be online. But `Player::SaveToDB()` saved the session account ID instead of the real DB account ID.
- **Fix**: Modified `Player::SaveToDB()` to detect bots (`IsBot()`) and query the DB for the real account ID before saving. Only uses the DB value if it's valid (< 10000).

**3. Vendor Cache Timing**
- **Problem**: Vendor cache was built lazily on first use, causing delay message after bots started logging in.
- **Fix**: Moved `VendoringStrategy::BuildVendorCache()` call to `PlayerBotMgr::Load()`, before bots start. Added progress bar using `BarGoLink`.

### New Features

**RandomBot.Purge Config Option**
- Set `RandomBot.Purge = 1` in mangosd.conf
- On server startup, completely removes all RandomBot data:
  - Deletes all characters using `Player::DeleteFromDB()` (handles all related tables)
  - Deletes from `playerbot` table
  - Deletes RNDBOT accounts from realmd
- Logs reminder to set back to 0 after purge

### Files Modified

| File | Changes |
|------|---------|
| `GrindingStrategy.h` | Increased `SEARCH_RANGE` from 50.0f to 150.0f |
| `PlayerBotMgr.h` | Added `m_confPurgeRandomBots` member |
| `PlayerBotMgr.cpp` | Added purge config loading, purge call in Load(), vendor cache call, fixed bot loading to use real account IDs |
| `RandomBotGenerator.h` | Added `PurgeAllRandomBots()` declaration |
| `RandomBotGenerator.cpp` | Added `PurgeAllRandomBots()` implementation with progress bar, changed `GenerateRandomBots()` to use `sObjectMgr.GeneratePlayerLowGuid()` |
| `Player.cpp` | Modified `SaveToDB()` to preserve real account ID for bots |
| `VendoringStrategy.h` | Made `BuildVendorCache()` public |
| `VendoringStrategy.cpp` | Added progress bar to `BuildVendorCache()` |

### Technical Details

**Bot Session Account IDs (Why 10000+)**
- Multiple bot characters share real accounts (RNDBOT001 has 9 chars)
- Can't have multiple WorldSessions on same account
- Solution: Generate fake session account IDs (10000+) for each bot
- This allows all 50 bots to be online simultaneously
- The REAL account ID is preserved in the database via SaveToDB fix

**SaveToDB Fix Logic**
```cpp
if (IsBot())
{
    // Query DB for real account ID
    result = CharacterDatabase.PQuery("SELECT account FROM characters WHERE guid = %u", GetGUIDLow());
    if (result)
    {
        uint32 dbAccountId = result->Fetch()[0].GetUInt32();
        // Only use if valid (not a corrupted session ID)
        if (dbAccountId > 0 && dbAccountId < 10000)
            saveAccountId = dbAccountId;
    }
}
```

---

## Current Bot Capabilities

**What bots CAN do:**
1. Auto-generate on first server launch (accounts + characters)
2. Spawn in the world without GUID conflicts
3. Be whispered by players (properly registered in ObjectAccessor)
4. Apply self-buffs when out of combat
5. Fight back when attacked (class-appropriate combat rotations)
6. **Engage targets appropriately by class:**
   - Melee classes (Warrior, Rogue, Paladin, Shaman, Druid) charge in
   - Casters (Mage, Priest, Warlock) stand and cast - no melee run-up
   - Hunters use Auto Shot at 25 yard range
7. Autonomously find and attack mobs (including neutral/yellow mobs)
8. Skip mobs already tapped by other players/bots
9. Loot corpses after combat (gold + items)
10. Rest when low HP/mana (sit + cheat regen, no consumables needed)
11. Handle death - release spirit, ghost walk to corpse, resurrect
12. Death loop detection - use spirit healer if dying too often
13. Vendor when bags full or gear broken - walk to nearest vendor
14. Sell all items and repair gear at vendor
15. Persist correctly across server restarts (account IDs preserved)

**What bots CANNOT do yet:**
- Travel/explore to find new grinding areas (stuck at vendor location after vendoring)

---

## Configuration

| Setting | Default | Purpose |
|---------|---------|---------|
| `RandomBot.Enable` | 0 | Enable RandomBot system |
| `RandomBot.Purge` | 0 | Purge all bots on startup (set back to 0 after!) |
| `RandomBot.MaxBots` | 50 | Bots to generate |
| `RandomBot.MinBots` | 3 | Minimum bots online |
| `RandomBot.Refresh` | 60000 | Add/remove interval (ms) |

---

## Architecture

```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Bot AI (main loop, coordinates all systems)
├── RandomBotGenerator.h/cpp    ← Auto-generation + purge on first launch
├── PlayerBotMgr.h/cpp          ← Bot lifecycle management
├── BotCheats.h/cpp             ← Cheat utilities (resting)
│
├── Combat/                     ← Combat system (class-specific handlers)
│   ├── IClassCombat.h          ← Interface for class handlers
│   ├── BotCombatMgr.h/cpp      ← Combat coordinator
│   └── Classes/                ← Per-class combat handlers
│       ├── WarriorCombat.h/cpp
│       ├── PaladinCombat.h/cpp
│       ├── HunterCombat.h/cpp
│       ├── MageCombat.h/cpp
│       ├── PriestCombat.h/cpp
│       ├── WarlockCombat.h/cpp
│       ├── RogueCombat.h/cpp
│       ├── ShamanCombat.h/cpp
│       └── DruidCombat.h/cpp
│
└── Strategies/                 ← High-level behavior
    ├── IBotStrategy.h          ← Strategy interface
    ├── GrindingStrategy.h/cpp  ← Find mob → kill
    ├── LootingBehavior.h/cpp   ← Loot corpses after combat
    ├── GhostWalkingStrategy.h/cpp ← Death handling
    └── VendoringStrategy.h/cpp ← Sell items, repair gear
```

### Layer Responsibilities

| Layer | Responsibility |
|-------|---------------|
| **RandomBotAI** | Main brain - coordinates strategies, combat manager, behaviors |
| **BotCombatMgr** | Combat coordinator - creates/delegates to class handlers |
| **IClassCombat** | Class-specific combat - engagement, rotations, buffs |
| **Strategies** | High-level goals - what to do next (find mob, rest, sell) |

---

## Database Tables

| Table | Purpose |
|-------|---------|
| `realmd.account` | Bot accounts (RNDBOT001, RNDBOT002...) |
| `characters.characters` | Bot character data |
| `characters.playerbot` | Links char_guid to AI type ('RandomBotAI') |

---

## Build & Test

```bash
# Build
cd ~/Desktop/Forsaken_WOW/core/build && make -j$(nproc) && make install

# Run (two terminals)
cd ~/Desktop/Forsaken_WOW/run/bin && ./realmd   # Terminal 1
cd ~/Desktop/Forsaken_WOW/run/bin && ./mangosd  # Terminal 2
```

---

## Testing Commands

```sql
-- Check bot accounts
SELECT id, username FROM realmd.account WHERE username LIKE 'RNDBOT%';

-- Check bot characters and their accounts
SELECT guid, account, name, level FROM characters.characters
WHERE account IN (SELECT id FROM realmd.account WHERE username LIKE 'RNDBOT%') LIMIT 10;

-- Verify account IDs are correct (should be 1-6, NOT 10000+)
SELECT guid, account, name FROM characters.characters WHERE account >= 10000;
```

---

## Session Log

### 2025-01-12 - Purge GUID Counter Fix
- **Problem**: After purging bots with `RandomBot.Purge`, newly generated bots would get unnecessarily high GUIDs (e.g., 51+ instead of 1+)
- **Root Cause**: `ObjectMgr::m_CharGuids` counter is loaded from DB at startup via `SetHighestGuids()`. When purge deletes bots, the counter is NOT reset, so new bots continue from the old max GUID.
- **Fix**: Added `ObjectMgr::ReloadCharacterGuids()` method that reloads the GUID counter from the database. Called at end of `PurgeAllRandomBots()`.
- **Files Modified**:
  - `src/game/ObjectMgr.h` - Added `ReloadCharacterGuids()` declaration
  - `src/game/ObjectMgr.cpp` - Implemented `ReloadCharacterGuids()`
  - `src/game/PlayerBots/RandomBotGenerator.cpp` - Call `ReloadCharacterGuids()` after purge
- **Result**: After purge, new bots get GUIDs starting from the correct value (respecting any remaining player characters)

### 2025-01-12 - Bot Registration Fix (Whispers Now Work)
- **Problem**: Bots couldn't be whispered, and ObjectAccessor lookups by name failed
- **Root Cause**: `ObjectAccessor::AddObject()` stored names without normalization (e.g., "norosu"), but `FindPlayerByName()` and `FindMasterPlayer()` normalized search terms to title case (e.g., "Norosu")
- **Fix**: Modified `ObjectAccessor::AddObject()` and `RemoveObject()` to normalize names before adding/removing from name maps
- **Files Modified**: `src/game/ObjectAccessor.cpp`
- **Result**: Bots are now whisperable, all ObjectAccessor lookups work correctly

### 2025-01-11 - Combat System Refactor (Phase 4.5)
- Refactored combat engagement to be class-appropriate
- Created modular combat handler architecture (BotCombatMgr + IClassCombat)
- 21 new files: interface, manager, 9 class handlers
- Casters now stand and cast to pull (no melee run-up)
- Hunters use Auto Shot at 25 yard range
- Melee classes charge in as expected

### 2025-01-11 - Horde Bot Fix
- Fixed Orc, Tauren, Troll bots standing idle - increased mob search range from 50 to 150 yards
- Horde starting areas have mobs further from spawn point than Alliance areas

### 2025-01-11 - Critical Bug Fixes
- Fixed GUID conflict: bots now use `sObjectMgr.GeneratePlayerLowGuid()`
- Fixed account ID corruption: `SaveToDB()` preserves real account ID for bots
- Added `RandomBot.Purge` config option for clean bot removal
- Moved vendor cache build to startup with progress bar
- **TESTED**: Server restarts preserve correct account IDs, no GUID conflicts

### 2025-01-11 - Bug Fix: Bots not showing in /who list
- Fixed `world_phase_mask = 0` issue in RandomBotGenerator

### 2025-01-10 - Phase 4 COMPLETE (Vendoring)
- Created VendoringStrategy with state machine
- Vendor cache, faction-aware vendor finding
- Bot walks to vendor, sells all items, repairs gear

### 2025-01-10 - Phase 3 COMPLETE (Death Handling)
- Ghost walking, resurrection, death loop detection

### 2025-01-10 - Phase 2 COMPLETE (Grinding + Resting)
- Neutral mob targeting, looting, resting with BotCheats

### 2025-01-09 - Phase 1 COMPLETE (Combat AI)
- All 9 class rotations implemented

---

*Last Updated: 2025-01-12*
*Current State: Phase 4.5 complete. Fixed purge GUID counter bug. Next: Phase 5 (movement/exploration).*
