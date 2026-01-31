# RandomBot AI v0.1 - Patch Notes

**Release Date:** 2026-01-31
**Stability:** 3+ hours runtime with no crashes (ASAN-verified)

---

## Overview

RandomBot AI is an autonomous bot system for vMangos that generates and controls player bots. Bots grind mobs, loot corpses, rest when low on resources, and travel to new areas as they level. They require no human input after server startup.

This is the first public release (v0.1) establishing the core grinding loop.

---

## Features (What Bots CAN Do)

### Auto-Generation
- **First-launch generation**: On first server startup with `RandomBot.Enable = 1`, the system automatically creates bot accounts (RNDBOT001-006) and characters
- **Race-appropriate names**: Bots have lore-friendly names using syllable-based generation (Orc names are guttural, Elf names are melodic, etc.)
- **All 9 classes supported**: Warrior, Paladin, Hunter, Rogue, Priest, Mage, Warlock, Shaman, Druid
- **Both factions**: Alliance and Horde bots generated
- **Configurable count**: Default 50 bots, adjustable via `RandomBot.MaxBots`

### Combat System
- **Class-appropriate engagement**:
  - Melee classes (Warrior, Rogue, Paladin, Shaman, Druid) charge in and auto-attack
  - Casters (Mage, Priest, Warlock) stand at range and cast spells to pull
  - Hunters use Auto Shot at range, switch to melee when mobs close in
- **Full combat rotations**: Each class has implemented spell rotations
- **Self-buffing**: Bots maintain class buffs (Ice Armor, Demon Skin, Seals, etc.)
- **Proper facing**: Bots face their targets before attacking
- **Combat reactivity**: If attacked by a different mob while fighting, switches to the new attacker
- **Target switching**: Bots respond to being attacked while out of combat

### Grinding
- **Autonomous mob finding**: Scans 75-yard radius for valid targets
- **Smart target selection**:
  - Only attacks same level or up to 2 levels below (no suicide pulls)
  - Validates path exists before committing to target
- **Random target selection**: Picks randomly from valid mobs (natural spread, no "trains")
- **Path validation**: Won't target mobs on unreachable terrain
- **30-second approach timeout**: Gives up on unreachable targets

### Movement
- **Centralized movement system**: All movement through BotMovementManager
- **Navmesh pathfinding**: Uses Detour navmesh for proper pathing
- **Steep slope avoidance**: Bots path around hills like real players
- **Cave/building entry**: Bots properly path through entrances to reach mobs inside
- **Stuck detection**: 5-second detection with micro-recovery, emergency teleport if needed
- **Multi-Z search**: Handles caves and multi-story buildings

### Looting
- **Auto-loot after combat**: Bots loot corpses they have rights to
- **Gold collection**: Automatic
- **Item pickup**: Items go to bags

### Resting
- **HP/Mana regeneration**: Bots sit and regenerate when low
- **Thresholds**: Start resting at HP < 35% or Mana < 45%, stop at 90%
- **No consumables needed**: Uses "cheat" regeneration (5% per 2 seconds)
- **Visual feedback**: Bots visually sit while resting

### Death Handling
- **Ghost walking**: Bots release spirit and walk to their corpse
- **Resurrection**: Bots resurrect at their corpse
- **Death loop detection**: If dying repeatedly, uses spirit healer instead
- **State reset**: All strategies properly reset after resurrection

### Travel System
- **Out-leveled area detection**: Recognizes when no appropriate mobs exist
- **Database-driven grind spots**: 2,684 auto-generated spots covering levels 1-60
- **Faction-aware**: Only travels to faction-appropriate areas
- **Waypoint navigation**: Long journeys split into ~200 yard segments
- **Stuck timeout**: 30-second timeout resets travel if stuck
- **Anti-thrashing**: 90-second cooldown prevents constant spot-hopping

### Vendoring (Partial)
- **Trigger detection**: Knows when bags are full or gear is broken
- **Vendor finding**: Locates nearest faction-appropriate vendor
- **Navigation**: Walks to vendor location using pathfinding

### Server Integration
- **Whisperable**: Players can whisper bots (bots don't respond, but messages arrive)
- **Targetable**: Bots appear in /who, can be targeted, inspected
- **Persistent**: Bot data survives server restarts
- **GUID safe**: No conflicts with player-created characters

### Debug Tools
- **`.bot status` command**: Shows bot state, target, movement, strategy

---

## Known Limitations (What Bots CANNOT Do)

### Not Implemented

| Feature | Status | Notes |
|---------|--------|-------|
| **Learn new spells/ranks** | Not implemented | Bots use spells they start with |
| **Train at class trainers** | Not implemented | No trainer interaction |
| **Complete vendor transactions** | Broken | Bots walk to vendor but don't sell/repair |
| **Upgrade gear** | Not implemented | No gear evaluation or purchasing |
| **Swimming** | Not tested | May work, may not - untested |
| **Form groups** | Not implemented | Bots don't group with each other |
| **Accept group invites** | Not implemented | Players cannot invite bots to party |
| **Questing** | Not implemented | No quest pickup, tracking, or completion |
| **Dungeons** | Not implemented | No dungeon AI or group coordination |
| **Chat/Speaking** | Not implemented | Bots are silent |
| **Emotes** | Not implemented | Bots don't emote |
| **Use flight paths** | Not implemented | Same-map travel only |
| **Use boats/zeppelins** | Not implemented | Cannot cross continents |
| **PvP combat** | Not implemented | No player-vs-player logic |
| **Professions** | Not implemented | No crafting or gathering |
| **Auction house** | Not implemented | No AH interaction |
| **Use consumables** | Not implemented | Relies on cheat regen instead |
| **Mount usage** | Not implemented | Bots walk everywhere |
| **Threat avoidance** | Disabled | Danger zone system exists but disabled |

### Known Bugs (Low Priority)

| Bug | Description |
|-----|-------------|
| Bug #12 | Hunter Auto Shot cooldown spam in console (cosmetic) |

### Edge Cases

- Bots may path through hostile mob camps when traveling long distances
- Very steep terrain can occasionally cause stuck issues
- Some complex cave/building layouts may be problematic

---

## Major Bug Fixes

### Bug #16: Server Crash (Use-After-Free) - CRITICAL
**Problem:** Random server crashes when bots reconnected after logout.
**Cause:** `BotMovementManager::m_bot` pointed to freed Player object after reconnect.
**Fix:** Added `SetBot()` method called in `OnPlayerLogin()` to sync pointers.
**Result:** 3+ hours stable runtime with ASAN enabled.

### Bug #15: Bots Stuck Forever on Unreachable Targets
**Problem:** Bots would infinitely try to reach mobs on mountains/steep terrain.
**Cause:** Timeout check was in wrong code path (out-of-combat, not in-combat).
**Fix:** Complete GrindingStrategy rewrite with state machine and 30-second timeout.

### Bug #13: Ranged Bots Freezing
**Problem:** Casters would freeze in place at 47+ yards from target.
**Cause:** `MoveChase(target, 28.0f)` offset caused pathfinding issues.
**Fix:** Removed offset from all ranged MoveChase calls.

### Bug #9: Bots Not Entering Caves/Buildings
**Problem:** Bots stood outside structures instead of entering.
**Cause:** Movement interrupted by LoS+incomplete path edge case.
**Fix:** Added movement persistence in combat handlers.

### Bug #2: Bots Walking Onto Steep Slopes
**Problem:** Bots walked onto no-navmesh terrain and got stuck.
**Cause:** ChaseMovementGenerator didn't exclude steep slopes for bots.
**Fix:** Added `ExcludeSteepSlopes()` for bot pathfinding.

### Long Path Bug
**Problem:** Travel paths over 256 waypoints returned NOPATH.
**Cause:** `findSmoothPath()` returned DT_FAILURE when buffer full.
**Fix:** Return `PATHFIND_INCOMPLETE` for truncated paths.

### Tree Collision Bug
**Problem:** Bots walked through trees.
**Cause:** Corrupted M2 data in private server client.
**Fix:** Re-extracted vmaps/mmaps from clean vanilla 1.12 client.

---

## Technical Architecture

### Core Files
```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Main AI brain
├── BotMovementManager.h/cpp    ← Centralized movement
├── RandomBotGenerator.h/cpp    ← Auto-generation
├── PlayerBotMgr.h/cpp          ← Bot lifecycle
├── BotCheats.h/cpp             ← Resting system
│
├── Combat/
│   ├── IClassCombat.h          ← Interface
│   ├── BotCombatMgr.h/cpp      ← Coordinator
│   ├── CombatHelpers.h         ← Shared helpers
│   └── Classes/                ← 9 class handlers
│
└── Strategies/
    ├── IBotStrategy.h          ← Interface
    ├── GrindingStrategy.h/cpp  ← Mob finding
    ├── LootingBehavior.h/cpp   ← Looting
    ├── GhostWalkingStrategy.h/cpp ← Death
    ├── VendoringStrategy.h/cpp ← Vendoring
    └── TravelingStrategy.h/cpp ← Travel
```

### Database Tables
- `realmd.account` - Bot accounts (RNDBOT001-006)
- `characters.characters` - Bot character data
- `characters.playerbot` - Links GUID to AI type
- `characters.grind_spots` - 2,684 grind locations

### Configuration (mangosd.conf)
```conf
RandomBot.Enable = 1        # Enable system
RandomBot.MinBots = 3       # Minimum bots online
RandomBot.MaxBots = 50      # Total bots to generate
RandomBot.Refresh = 60000   # Add/remove check interval (ms)
RandomBot.Purge = 0         # Delete all bots on startup
```

---

## Development Timeline

| Date | Milestone |
|------|-----------|
| 2025-01-09 | Phase 1: Combat AI (all 9 class rotations) |
| 2025-01-10 | Phase 2: Grinding + Resting |
| 2025-01-10 | Phase 3: Death handling |
| 2025-01-10 | Phase 4: Vendoring strategy |
| 2025-01-11 | Phase 4.5: Combat system refactor |
| 2025-01-11 | Critical bug fixes (GUID, account ID, Horde bots) |
| 2026-01-24 | Phase 5: Travel system |
| 2026-01-25 | PathFinder long path fix |
| 2026-01-26 | Major bug fix session (8 bugs) |
| 2026-01-28 | Phase 5.5: Auto-generated grind spots |
| 2026-01-29 | Bug #13: Ranged bot freeze fix |
| 2026-01-30 | Phase 6: BotMovementManager + GrindingStrategy rewrite |
| 2026-01-31 | Bug #16: Use-after-free crash fix |
| 2026-01-31 | **v0.1 Release** |

---

## What's Next (v0.2 Roadmap Ideas)

- Fix vendor transactions (actually sell items and repair)
- Spell learning at trainers
- Group formation between bots
- Player group invites
- Basic chat responses
- Swimming verification
- Gear upgrades

---

*RandomBot AI v0.1 - First stable release*
