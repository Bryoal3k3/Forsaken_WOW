# vMangos Native Bot Framework Analysis

## Overview

vMangos has a **production-ready bot framework** with 15,725 lines of code spanning:
- PartyBot (group/dungeon content)
- BattleBot (PvP battlegrounds)
- RandomBot (world population)

All 9 classes are fully implemented with class-specific AI routines.

---

## 1. Bot Code Location

**Main Directory:** `src/game/PlayerBots/`

| File | Lines | Purpose |
|------|-------|---------|
| `PlayerBotAI.h/cpp` | 473 | Base bot class |
| `PlayerBotMgr.h/cpp` | 2,165 | Central manager (singleton) |
| `CombatBotBaseAI.h/cpp` | 3,815 | Combat AI base with spell management |
| `PartyBotAI.h/cpp` | 3,487 | Party-based dungeon bot |
| `BattleBotAI.h/cpp` | 3,530 | PvP battleground bot |
| `BattleBotWaypoints.h/cpp` | 2,255 | Battleground pathfinding |

**Base Class:** `src/game/AI/PlayerAI.h`

---

## 2. Bot Update Loop

### Call Stack
```
World Update
└─→ PlayerBotMgr::Update(diff)           [Manages bot lifecycle]
    └─→ Player::UpdateAI(diff)           [Called on bot player objects]
        └─→ PlayerBotAI::UpdateAI(diff)  [Base bot update]
            └─→ PartyBotAI::UpdateAI()   [Specialized update]
                ├─→ UpdateInCombatAI()   [Combat state]
                └─→ UpdateOutOfCombatAI() [Non-combat state]
```

### Update Intervals
- **PlayerBotMgr:** Configurable, default 10000ms
- **PartyBot/BattleBot:** 1000ms (`#define PB_UPDATE_INTERVAL 1000`)

### Key Files for Update Loop
- `PlayerBotMgr.cpp:189` - Manager update entry
- `PartyBotAI.cpp:580-887` - PartyBot main update
- `BattleBotAI.cpp:651-930` - BattleBot main update

---

## 3. Class Hierarchy

```
PlayerAI (src/game/AI/PlayerAI.h)
└── PlayerBotAI (base bot class)
    └── CombatBotBaseAI (combat capabilities)
        ├── PartyBotAI (party/dungeon bots)
        └── BattleBotAI (PvP bots)
```

---

## 4. Existing Bot Features

### PartyBot Capabilities
- **Role-based combat:** Tank, Melee DPS, Range DPS, Healer
- **Party following:** Stays with leader
- **Intelligent healing:** Target selection based on health %
- **Spell rotations:** Class-specific combat & out-of-combat routines
- **Crowd control:** Marks targets, applies CC spells
- **Resurrection:** Revives fallen party members
- **Buff management:** Applies buffs to party
- **Positioning:** Maintains proper range for role

### BattleBot Capabilities
- **BG joining:** Queues and enters battlegrounds
- **Waypoint following:** Pre-recorded paths per BG
- **Flag mechanics:** WSG flag carrying AI
- **Mount usage:** Uses mounts for movement
- **Objective seeking:** Moves toward BG objectives
- **Auto-resurrection:** Handles BG death/respawn

### Combat Features (CombatBotBaseAI)
- **45+ spells per class** tracked and managed
- **Dynamic spell population** at initialization
- **Auto-equip system** with gear templates
- **Talent system** per class
- **Pet management** (Warlock/Hunter/Druid)

---

## 5. Bot AI API

### Spell Casting
```cpp
// CombatBotBaseAI.cpp:2803-2836
SpellCastResult DoCastSpell(Unit* pTarget, SpellEntry const* pSpellEntry)
```
Handles facing, mounting, movement stop, reagents.

### Spell Validation
```cpp
// CombatBotBaseAI.cpp:2748-2801
bool CanTryToCastSpell(Unit const* pTarget, SpellEntry const* pSpellEntry) const
```
Validates: cooldowns, GCD, power costs, range, immunity, auras.

### Target Selection
```cpp
// CombatBotBaseAI.cpp:1988-2044
Unit* SelectHealTarget(float selfHealPercent, float groupHealPercent) const

// PartyBotAI.cpp:~100
Unit* SelectAttackTarget(Player* pLeader)
Unit* SelectPartyAttackTarget()
```

### Healing
```cpp
// CombatBotBaseAI.cpp:1873-1879
bool FindAndHealInjuredAlly(float selfHealPercent, float groupHealPercent)

// CombatBotBaseAI.cpp:1941-1952
bool HealInjuredTarget(Unit* pTarget)

// CombatBotBaseAI.cpp:1882-1930
SpellEntry const* SelectMostEfficientHealingSpell(Unit const*, std::set<SpellEntry const*>&)
```

### Movement
```cpp
// PartyBotAI.cpp:144-170
Unit* GetDistancingTarget(Unit* pEnemy)

// PartyBotAI.cpp:172-181
bool RunAwayFromTarget(Unit* pEnemy)

// BattleBotAI.cpp:240-264
bool AttackStart(Unit* pVictim)
```

### Combat Evaluation
```cpp
// CombatBotBaseAI.cpp:1932-1939
int32 GetIncomingdamage(Unit const* pTarget) const

// CombatBotBaseAI.cpp:2009
bool AreOthersOnSameTarget(ObjectGuid guid, bool checkMelee, bool checkSpells) const
```

---

## 6. AI Decision Flow

### PartyBot Update Logic
```
1. Timer check (1000ms interval)
2. World state validation
3. First-time initialization:
   - Join party
   - Level up if needed
   - Learn spells
   - Equip gear
   - Populate spell data
   - Summon pets
4. Check party leader validity
5. Main AI dispatch:
   - UpdateInCombatAI_[ClassName]()
   - UpdateOutOfCombatAI_[ClassName]()
```

### Class-Specific Routines
Each class has two methods:
- `UpdateOutOfCombatAI_[Class]()` - Buffing, eating, drinking
- `UpdateInCombatAI_[Class]()` - Combat rotation

Example priority (Paladin):
1. Divine Shield if < 20% health
2. Apply buffs to party
3. Healing spells
4. Judgement on target
5. Hammer of Justice (CC)
6. Hammer of Wrath (execute)
7. Holy Shield / Consecration

---

## 7. Extension Points

### Creating Custom AI

1. **Create new class inheriting from CombatBotBaseAI:**
```cpp
class CustomBotAI : public CombatBotBaseAI
{
public:
    void UpdateAI(uint32 const diff) override;
    void UpdateInCombatAI();
    void UpdateOutOfCombatAI();
};
```

2. **Register in AI Factory** (`PlayerBotAI.cpp`):
```cpp
PlayerBotAI* CreatePlayerBotAI(std::string ainame)
{
    if (ainame == "CustomBot")
        return new CustomBotAI();
    // ...
}
```

3. **Add to database:**
```sql
INSERT INTO playerbot (char_guid, chance, ai) VALUES (guid, 100, 'CustomBot');
```

### Bot Manager Singleton
```cpp
#define sPlayerBotMgr MaNGOS::Singleton<PlayerBotMgr>::Instance()

// Usage
sPlayerBotMgr.AddBattleBot(queueType, team, level, temporary);
sPlayerBotMgr.AddTempBot(account, duration);
```

---

## 8. Spell Data Structure

Each class has a union in CombatBotBaseAI.h with 20-45 spell entries:

```cpp
// Example: Paladin spells (lines 284-305)
struct {
    SpellEntry const* pAura1;
    SpellEntry const* pAura2;
    SpellEntry const* pBlessingOfWisdom;
    SpellEntry const* pBlessingOfMight;
    SpellEntry const* pBlessingOfKings;
    SpellEntry const* pBlessingOfSalvation;
    SpellEntry const* pBlessingOfSanctuary;
    SpellEntry const* pBlessingOfLight;
    SpellEntry const* pBlessingOfFreedom;
    SpellEntry const* pBlessingOfProtection;
    SpellEntry const* pBlessingOfSacrifice;
    SpellEntry const* pRighteousFury;
    SpellEntry const* pDivineShield;
    SpellEntry const* pLayOnHands;
    SpellEntry const* pCleanse;
    SpellEntry const* pHammerOfWrath;
    SpellEntry const* pHammerOfJustice;
    SpellEntry const* pJudgement;
    SpellEntry const* pConsecration;
    SpellEntry const* pHolyShock;
    SpellEntry const* pHolyShield;
} paladin;
```

---

## 9. Key Insights

### What Works Well
- Complete class coverage with intelligent rotations
- Role-based behavior (tank/heal/dps)
- Efficient spell selection (prevents overheal)
- Safe AoE handling (respects CC marks)
- Modular design for extension

### Integration Points for Custom AI
1. **UpdateAI()** - Main entry, called every update tick
2. **UpdateInCombatAI()** - Combat decisions
3. **UpdateOutOfCombatAI()** - Non-combat decisions
4. **PopulateSpellData()** - Build spell references
5. **AutoAssignRole()** - Determine tank/heal/dps

### Useful Utilities
- `CanTryToCastSpell()` - Full spell validation
- `DoCastSpell()` - Execute spell with proper setup
- `SelectHealTarget()` - Intelligent heal targeting
- `SelectMostEfficientHealingSpell()` - Prevents overheal
- `GetIncomingdamage()` - Damage prediction

---

## 10. RandomBot Combat Handler System

RandomBotAI uses a modular combat handler architecture for class-specific behavior:

### Architecture
```
RandomBotAI
└── BotCombatMgr (combat coordinator)
    └── IClassCombat (interface)
        ├── WarriorCombat
        ├── PaladinCombat
        ├── HunterCombat
        ├── MageCombat
        ├── PriestCombat
        ├── WarlockCombat
        ├── RogueCombat
        ├── ShamanCombat
        └── DruidCombat
```

### IClassCombat Interface
```cpp
class IClassCombat
{
public:
    virtual ~IClassCombat() = default;
    virtual bool Engage(Player* pBot, Unit* pTarget) = 0;      // How to pull
    virtual void UpdateCombat(Player* pBot, Unit* pVictim) = 0; // Combat rotation
    virtual void UpdateOutOfCombat(Player* pBot) = 0;           // Buffs, pets
    virtual char const* GetName() const = 0;
};
```

### Engagement Types
| Class Type | Engage() Implementation |
|------------|------------------------|
| Melee | `Attack()` + `MoveChase()` |
| Caster | `SetTargetGuid()` only - first rotation spell pulls |
| Hunter | `SetTargetGuid()` + `MoveChase(25.0f)` + Auto Shot |

### Handler Access to Spells
Each handler receives a pointer to `CombatBotBaseAI` in constructor, giving access to:
- `m_spells` union (spell data for all classes)
- `CanTryToCastSpell()` - spell validation
- `DoCastSpell()` - spell execution
- `FindAndHealInjuredAlly()` - healing utilities

---

## 11. Files to Study

For custom AI implementation, study these in order:

1. `PlayerBotAI.h` - Base class interface
2. `CombatBotBaseAI.h` - Combat API and spell structures
3. `CombatBotBaseAI.cpp` - Implementation details
4. `Combat/BotCombatMgr.cpp` - Combat handler creation
5. `Combat/Classes/*Combat.cpp` - Class-specific handlers
6. `PartyBotAI.cpp` - Example class-specific routines
7. `PlayerBotMgr.cpp` - Bot lifecycle management
8. `Strategies/TravelingStrategy.cpp` - Travel and pathfinding
9. `src/game/Maps/PathFinder.cpp` - NavMesh pathfinding (core vMangos)

---

## 12. PathFinder and Navigation

### PathFinder Overview
vMangos uses Detour (from Recast Navigation) for navmesh-based pathfinding.

**Key Files:**
- `src/game/Maps/PathFinder.h/cpp` - Wrapper around Detour
- `src/game/Maps/MoveMap.h/cpp` - NavMesh tile management
- `dep/recastnavigation/Detour/` - Detour library

### PathFinder Usage
```cpp
PathFinder path(pUnit);
path.calculate(destX, destY, destZ);

PathType type = path.getPathType();
// PATHFIND_NORMAL (0x01) = complete path
// PATHFIND_INCOMPLETE (0x04) = partial path (truncated or can't reach end)
// PATHFIND_NOPATH (0x08) = no valid path

PointsArray const& points = path.getPath();
// Vector of Vector3 waypoints
```

### Key PathFinder Internals
1. **BuildPolyPath()** - Finds polygon path from A to B using `findPath()`
2. **BuildPointPath()** - Converts polygons to smooth waypoints using `findSmoothPath()`
3. **findSmoothPath()** - vMangos custom function that walks the polygon path

### Important Limits
| Constant | Value | Purpose |
|----------|-------|---------|
| `MAX_PATH_LENGTH` | 256 | Max polygons in path |
| `MAX_POINT_PATH_LENGTH` | 256 | Max smooth waypoints |

### PathFinder Fix Applied (2026-01-25)
Long paths (>256 waypoints) now correctly return `PATHFIND_INCOMPLETE` instead of `PATHFIND_NOPATH`. See PROGRESS.md for details.

### Debug Logging
Bot-only PathFinder logging added (filtered by `IsPlayerBot()` helper). All logs prefixed with `[BOT]`.

---

## 13. RandomBot Strategy System

### Strategy Architecture
```
RandomBotAI
├── GrindingStrategy      ← Default: find and kill mobs
├── VendoringStrategy     ← Sell items, repair gear
├── TravelingStrategy     ← Move to new grind spots
├── GhostWalkingStrategy  ← Handle death/resurrection
├── LootingBehavior       ← Universal: loot corpses
└── BotCombatMgr          ← Combat handler coordinator
```

### IBotStrategy Interface
```cpp
class IBotStrategy
{
public:
    virtual bool Update(Player* pBot, uint32 diff) = 0;
    virtual void OnEnterCombat(Player* pBot) = 0;
    virtual void OnLeaveCombat(Player* pBot) = 0;
    virtual char const* GetName() const = 0;
};
```

### TravelingStrategy Flow
1. GrindingStrategy reports NO_TARGETS for 5 consecutive ticks
2. TravelingStrategy queries cached grind spots (loaded at startup)
3. ValidatePath() checks if destination is reachable
4. GenerateWaypoints() splits journey into ~200 yard segments
5. MovementInform() callback chains segments together
6. Bot arrives, 90-second cooldown before traveling again

---

*Generated: 2026-01-08*
*Updated: 2026-01-25 - Added PathFinder/Navigation section, Strategy system*
*Purpose: Pre-implementation research for bot framework extension*
