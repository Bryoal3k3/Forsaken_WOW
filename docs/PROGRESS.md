# RandomBot AI Development Progress

## Project Status: Phase 2 IN PROGRESS

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 0 | ✅ Complete | Auto-generate bot accounts and characters |
| Phase 1 | ✅ Complete | Combat AI - bots fight when attacked |
| Phase 2 | 🔄 In Progress | Grinding - find mobs, kill, loot, rest |
| Phase 3 | 📋 Planned | Death handling and respawn |
| Phase 4 | 📋 Planned | Vendoring - sell junk, repair |
| Phase 5 | 📋 Planned | Movement - exploration, travel |

---

## Current Bot Capabilities

**What bots CAN do:**
1. Auto-generate on first server launch (accounts + characters)
2. Spawn in the world
3. Apply self-buffs when out of combat
4. Fight back when attacked (class-appropriate combat rotations)
5. **NEW:** Autonomously find and attack mobs (including neutral/yellow mobs)

**What bots CANNOT do yet:**
- Loot corpses
- Avoid already-tapped mobs
- Rest/eat/drink
- Handle death/respawn
- Sell junk or repair

---

## Phase 2: Grinding Behavior - 🔄 IN PROGRESS

### What Works
- Strategy layer architecture implemented
- Bots find mobs within 50 yards using custom grid search
- Bots attack neutral (yellow) mobs (solved the hostile-only limitation)
- Level filtering: only attacks mobs within ±2 levels
- Skips elites, critters, totems, evading mobs

### What's Missing
- [ ] Loot corpses after killing
- [ ] Skip already-tapped mobs
- [ ] Rest when low HP/mana
- [ ] (Future) Roaming/exploration

### Architecture
```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Combat rotations (how to fight)
└── Strategies/
    ├── IBotStrategy.h          ← Strategy interface
    └── GrindingStrategy.h/cpp  ← High-level behavior (what to do)
```

### Key Technical Solution
Standard `GetEnemyListInRadiusAround()` only finds hostile mobs. We solved this by:
1. Using `Cell::VisitGridObjects()` with custom `AllCreaturesInRange` checker
2. Manually filtering by `GetReactionTo()` to accept `REP_NEUTRAL` (yellow) mobs
3. This bypasses the `IsValidAttackTarget()` filter that blocks neutrals

### Files Created
| File | Purpose |
|------|---------|
| `Strategies/IBotStrategy.h` | Strategy interface |
| `Strategies/GrindingStrategy.h` | Grinding strategy declaration |
| `Strategies/GrindingStrategy.cpp` | Mob finding logic (150 lines) |

### Files Modified
| File | Change |
|------|--------|
| `RandomBotAI.h` | Added `std::unique_ptr<IBotStrategy>` member |
| `RandomBotAI.cpp` | Delegates to strategy in `UpdateOutOfCombatAI()` |
| `CMakeLists.txt` | Added Strategy files and include path |

---

## Phase 1: Combat AI - ✅ COMPLETE

### What Works
- Bots load from database on server start
- Bots initialize (role, spells, pets) when spawned
- Bots fight back when attacked with class-appropriate abilities
- Bots maintain self-buffs out of combat

### Files Created
| File | Purpose |
|------|---------|
| `src/game/PlayerBots/RandomBotAI.h` | AI class declaration |
| `src/game/PlayerBots/RandomBotAI.cpp` | Full implementation (~810 lines) |

### Combat Abilities by Class
| Class | Key Abilities |
|-------|--------------|
| Warrior | Execute, Overpower, Mortal Strike, Bloodthirst, Heroic Strike, Battle Shout |
| Paladin | Judgement, Hammer of Wrath, Consecration, Holy Shield, self-healing |
| Hunter | Hunter's Mark, Aimed Shot, Multi-Shot, Arcane Shot, Serpent Sting |
| Mage | Frost Nova, Fire Blast, Frostbolt, Fireball, Ice Armor, Arcane Intellect |
| Priest | Power Word: Shield, Shadow Word: Pain, Mind Blast, Smite, self-healing |
| Warlock | Corruption, Curse of Agony, Immolate, Shadow Bolt, Demon Armor |
| Rogue | Slice and Dice, Eviscerate, Sinister Strike |
| Shaman | Earth Shock, Flame Shock, Stormstrike, Lightning Bolt, self-healing |
| Druid | Moonfire, Wrath, Starfire, Mark of the Wild, Thorns, self-healing |

---

## Phase 0: Auto-Generation - ✅ COMPLETE

### What Works
On first launch with `RandomBot.Enable=1` and empty playerbot table:
1. System detects no bots exist
2. Creates RNDBOT### accounts in `realmd.account`
3. Creates bot characters (all 9 classes, levels 1-60)
4. Inserts entries in `characters.playerbot` with `ai='RandomBotAI'`
5. Normal Load() picks them up and spawns them

### Files Created
| File | Purpose |
|------|---------|
| `src/game/PlayerBots/RandomBotGenerator.h` | Generator class declaration |
| `src/game/PlayerBots/RandomBotGenerator.cpp` | SQL-based generation logic |

---

## Session Log

### 2025-01-10 - Phase 2 Mob Targeting Working
- Implemented Strategy layer architecture (IBotStrategy, GrindingStrategy)
- Solved neutral mob targeting using custom grid search
- Refactored grinding code from RandomBotAI into GrindingStrategy
- **TESTED**: Bots autonomously find and attack mobs
- Next: Add tapped-mob check, then looting

### 2025-01-10 - Documentation Correction
- Corrected PROGRESS.md to reflect actual state
- Phase 2 has NO code - previous attempts were reverted

### 2025-01-09 - Phase 1 Complete
- Created RandomBotAI class with all 9 class rotations
- Registered in AI factory
- **TESTED**: Bots enter combat and cast spells when attacked

---

## Key Code Locations

### Current Bot System
| Location | Purpose |
|----------|---------|
| `RandomBotAI::UpdateAI()` | Main update loop |
| `RandomBotAI::UpdateOutOfCombatAI()` | Delegates to strategy |
| `GrindingStrategy::Update()` | Find and attack mobs |
| `GrindingStrategy::FindGrindTarget()` | Custom grid search |
| `RandomBotAI::UpdateInCombatAI_<Class>()` | Class combat rotations |

### AI Factory
| Location | Purpose |
|----------|---------|
| `PlayerBotAI.cpp:362-377` | `CreatePlayerBotAI()` - type registration |

### Configuration
| Setting | Default | Purpose |
|---------|---------|---------|
| `RandomBot.Enable` | 1 | Enable system |
| `RandomBot.MaxBots` | 50 | Bots to generate |
| `RandomBot.MinBots` | 3 | Minimum online |
| `RandomBot.Refresh` | 60000 | Add/remove interval (ms) |

---

## Testing Commands

```sql
-- Count bots
SELECT COUNT(*) FROM characters.playerbot WHERE ai = 'RandomBotAI';

-- Check bot characters
SELECT c.guid, c.name, c.class, c.level, c.position_x, c.position_y, c.map
FROM characters.characters c
JOIN characters.playerbot p ON c.guid = p.char_guid
WHERE p.ai = 'RandomBotAI' LIMIT 10;
```

```
# In-game GM commands
.pinfo RNDBOT001          # Check bot account
.appear <BotName>         # Teleport to bot
.modify hp 1              # Damage bot to test combat
```

---

*Last Updated: 2025-01-10*
*Current State: Phase 2 in progress. Bots autonomously find and attack mobs. Missing: looting, tapped-mob check, resting.*
