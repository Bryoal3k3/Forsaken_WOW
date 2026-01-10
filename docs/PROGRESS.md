# RandomBot AI Development Progress

## Project Status: Phase 3 COMPLETE

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 0 | ✅ Complete | Auto-generate bot accounts and characters |
| Phase 1 | ✅ Complete | Combat AI - bots fight when attacked |
| Phase 2 | ✅ Complete | Grinding - find mobs, kill, loot, rest |
| Phase 3 | ✅ Complete | Death handling and respawn |
| Phase 4 | 📋 Planned | Vendoring - sell junk, repair |
| Phase 5 | 📋 Planned | Movement - exploration, travel |

---

## Current Bot Capabilities

**What bots CAN do:**
1. Auto-generate on first server launch (accounts + characters)
2. Spawn in the world
3. Apply self-buffs when out of combat
4. Fight back when attacked (class-appropriate combat rotations)
5. Autonomously find and attack mobs (including neutral/yellow mobs)
6. Skip mobs already tapped by other players/bots
7. Loot corpses after combat (gold + items)
8. Rest when low HP/mana (sit + cheat regen, no consumables needed)
9. **NEW:** Handle death - release spirit, ghost walk to corpse, resurrect
10. **NEW:** Death loop detection - use spirit healer if dying too often

**What bots CANNOT do yet:**
- Sell junk or repair

---

## Phase 3: Death Handling - ✅ COMPLETE

### What Works
- Bot dies → releases spirit → teleports to graveyard as ghost
- Ghost walks back to corpse location
- Resurrects on top of corpse with 50% HP/mana
- Death loop detection: 3 deaths in 10 minutes = use spirit healer
- Spirit healer resurrection applies resurrection sickness

### Architecture
```
src/game/PlayerBots/Strategies/
└── GhostWalkingStrategy.h/cpp  ← Death handling strategy
```

### Key Technical Solutions

**Death State Flow:**
- `JUST_DIED` (1) → transient, wait for next tick
- `CORPSE` (2) → call `BuildPlayerRepop()` + `RepopAtGraveyard()` to become ghost
- `DEAD` (3) → already a ghost, walk to corpse

**Ghost Walking:**
- `GetCorpse()` returns corpse location
- `MovePoint()` moves ghost toward corpse
- Within 5 yards → `ResurrectPlayer(0.5f)` + `SpawnCorpseBones()`

**Death Loop Detection:**
- Tracks timestamps of recent deaths
- 3+ deaths within 10 minutes = death loop
- Resurrect at spirit healer with `ResurrectPlayer(0.5f, true)` (sickness)

### Files Created
| File | Purpose |
|------|---------|
| `Strategies/GhostWalkingStrategy.h/cpp` | Ghost walking and resurrection |

### Files Modified
| File | Change |
|------|--------|
| `RandomBotAI.h` | Added GhostWalkingStrategy member |
| `RandomBotAI.cpp` | Call ghost strategy when dead |
| `CMakeLists.txt` | Added GhostWalkingStrategy files |

---

## Phase 2: Grinding Behavior - ✅ COMPLETE

### What Works
- Strategy layer architecture implemented
- Bots find mobs within 50 yards using custom grid search
- Bots attack neutral (yellow) mobs (solved the hostile-only limitation)
- Level filtering: only attacks mobs within ±2 levels
- Skips elites, critters, totems, evading mobs
- Skips mobs already tapped by others
- Loots corpses after combat ends (universal behavior)
- **NEW:** Rests when HP < 35% or Mana < 45% (BotCheats system)

### Future Enhancements (not blockers)
- [ ] Roaming/exploration between mobs

### Architecture
```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Combat rotations + universal behaviors
├── BotCheats.h/cpp             ← Cheat utilities (resting, future: reagents, ammo)
└── Strategies/
    ├── IBotStrategy.h          ← Strategy interface
    ├── GrindingStrategy.h/cpp  ← High-level behavior (find mob, attack)
    └── LootingBehavior.h/cpp   ← Looting corpses (universal, not strategy-specific)
```

### Key Technical Solutions

**Neutral Mob Targeting:**
Standard `GetEnemyListInRadiusAround()` only finds hostile mobs. We solved this by:
1. Using `Cell::VisitGridObjects()` with custom `AllCreaturesInRange` checker
2. Manually filtering by `GetReactionTo()` to accept `REP_NEUTRAL` (yellow) mobs
3. This bypasses the `IsValidAttackTarget()` filter that blocks neutrals

**Looting:**
- LootingBehavior runs in RandomBotAI main loop (universal, not strategy-specific)
- Triggers when combat ends (tracks `m_wasInCombat` state)
- Scans for dead creatures where `IsTappedBy(bot)` returns true
- Uses `Player::SendLoot()`, `AutoStoreLoot()`, `DoLootRelease()`
- 40 yard max range, 12 second timeout to prevent getting stuck

**Resting (BotCheats):**
- BotCheats static utility class bypasses item-based mechanics
- Bot sits down when HP < 35% or Mana < 45%
- Regenerates 5% HP/mana every 2 seconds (no consumables needed)
- Stands up when both HP >= 90% and Mana >= 90%
- Combat-aware: immediately stands if bot or group member enters combat
- Prevents sitting during raid pulls (checks group combat state)

### Files Created
| File | Purpose |
|------|---------|
| `Strategies/IBotStrategy.h` | Strategy interface |
| `Strategies/GrindingStrategy.h/cpp` | Mob finding logic |
| `Strategies/LootingBehavior.h/cpp` | Looting corpses after combat |
| `BotCheats.h/cpp` | Cheat utilities (resting without consumables) |

### Files Modified
| File | Change |
|------|--------|
| `RandomBotAI.h` | Added LootingBehavior, resting state tracking |
| `RandomBotAI.cpp` | Integrated looting and resting into UpdateAI() |
| `CMakeLists.txt` | Added Strategy, LootingBehavior, and BotCheats files |

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
| `src/game/PlayerBots/RandomBotAI.cpp` | Full implementation (~850 lines) |

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

### 2025-01-10 - Phase 3 COMPLETE (Death Handling)
- Created GhostWalkingStrategy for death handling
- Bot releases spirit, teleports to graveyard, walks back to corpse
- Resurrects on top of corpse with 50% HP/mana
- Death loop detection: 3 deaths in 10 min = spirit healer res with sickness
- **TESTED**: Bots die, release, walk back, resurrect correctly
- Phase 3 complete! Next: Phase 4 (vendoring)

### 2025-01-10 - Phase 2 COMPLETE (Resting Added)
- Created BotCheats utility class for "cheat" mechanics
- Implemented resting: bots sit and regen 5% HP/mana per 2 sec tick
- Thresholds: Start at 35% HP or 45% mana, stop at 90%
- Combat-aware: stands up if bot or group member enters combat
- **TESTED**: Bots sit down when low, regen without consumables
- Phase 2 complete! Next: Phase 3 (death handling)

### 2025-01-10 - Phase 2 Looting Complete
- Added tapped mob check to GrindingStrategy (skip mobs tapped by others)
- Created LootingBehavior class for looting corpses after combat
- Integrated looting into RandomBotAI main loop (universal behavior)
- **TESTED**: Bots loot gold and items after killing mobs

### 2025-01-10 - Phase 2 Mob Targeting Working
- Implemented Strategy layer architecture (IBotStrategy, GrindingStrategy)
- Solved neutral mob targeting using custom grid search
- Refactored grinding code from RandomBotAI into GrindingStrategy
- **TESTED**: Bots autonomously find and attack mobs

### 2025-01-09 - Phase 1 Complete
- Created RandomBotAI class with all 9 class rotations
- Registered in AI factory
- **TESTED**: Bots enter combat and cast spells when attacked

---

## Key Code Locations

### Current Bot System
| Location | Purpose |
|----------|---------|
| `RandomBotAI::UpdateAI()` | Main update loop (death, resting, combat, looting) |
| `RandomBotAI::UpdateOutOfCombatAI()` | Delegates to strategy |
| `GhostWalkingStrategy::Update()` | Ghost walk to corpse and resurrect |
| `GhostWalkingStrategy::OnDeath()` | Release spirit, handle death loop |
| `BotCheats::HandleResting()` | Sit and regen HP/mana (cheat) |
| `BotCheats::CanRest()` | Check if safe to rest (no combat) |
| `LootingBehavior::Update()` | Find and loot corpses |
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
*Current State: Phase 3 complete! Bots grind, loot, rest, and handle death autonomously. Next: Phase 4 (vendoring).*
