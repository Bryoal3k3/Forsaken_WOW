# vMangos RandomBot AI Project

## COLLABORATION RULES (ALWAYS FOLLOW)

### Before Writing Code
1. **STOP and explain** your plan before implementing
2. **ASK** when you encounter design decisions with multiple valid approaches
3. **CONFIRM** before modifying any existing files
4. **NEVER** make large changes without user approval
5. **NEVER** delete working code - refactor incrementally instead

### Work Style
- Work in SMALL chunks (one function/feature at a time)
- After each chunk, STOP and ask: "Does this look right? Should I continue?"
- If unsure about ANYTHING, ask - don't assume
- Present OPTIONS when there are multiple ways to do something
- **Build incrementally** - only create files when needed NOW

### What NOT to Do
- Don't implement entire phases without checking in
- Don't refactor existing code without asking
- Don't make architectural decisions alone
- Don't assume you know what the user wants
- Don't build files "for later"

---

## Documentation Protocol

1. **Before work:** Read `docs/PROGRESS.md` for current state and next steps
2. **Reference:** See `docs/VMANGOS_BOT_FRAMEWORK_ANALYSIS.md` for framework details
3. **Reference:** See `docs/BOT_DECISION_FLOW.md` for state machine and strategy coordination
4. **After work:** Update `docs/PROGRESS.md` with what changed

### Checkpoint System (MANDATORY)
After EACH work session, update `docs/PROGRESS.md` with:
- What was completed
- Current blockers/issues
- Next steps
- Files created/modified

---

## Project Goal

Create autonomous RandomBot AI system where bots:
- Auto-generate on first server launch
- Spawn in the world
- Fight, grind mobs, loot, rest, sell junk, and level up naturally

---

## Architecture

### Design Principle
**Incremental growth** - build only what the current phase needs. Don't over-engineer.

### Structure
```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Bot AI (main brain, coordinates all systems)
├── RandomBotGenerator.h/cpp    ← Auto-generation on first launch
├── PlayerBotMgr.h/cpp          ← Bot lifecycle management
├── BotCheats.h/cpp             ← Cheat utilities (resting)
│
├── Combat/                     ← Combat system (class-specific handlers)
│   ├── IClassCombat.h          ← Interface for class handlers
│   ├── BotCombatMgr.h/cpp      ← Combat coordinator
│   ├── CombatHelpers.h         ← Shared helpers (EngageCaster, EngageMelee, HandleRangedMovement)
│   └── Classes/                ← Per-class combat handlers (9 classes)
│       ├── WarriorCombat.h/cpp
│       ├── MageCombat.h/cpp
│       ├── HunterCombat.h/cpp
│       └── ... (all 9 classes)
│
└── Strategies/                 ← High-level behavior
    ├── IBotStrategy.h          ← Strategy interface
    ├── GrindingStrategy.h/cpp  ← Find mob → kill
    ├── LootingBehavior.h/cpp   ← Loot corpses after combat
    ├── GhostWalkingStrategy.h/cpp ← Death handling
    ├── VendoringStrategy.h/cpp ← Sell items, repair gear
    └── TravelingStrategy.h/cpp ← Travel to new grind spots
```

### Layer Responsibilities
| Layer | Responsibility |
|-------|---------------|
| **RandomBotAI** | Main brain - coordinates strategies, combat manager, behaviors |
| **BotCombatMgr** | Combat coordinator - creates/delegates to class handlers |
| **IClassCombat** | Class-specific combat - engagement, rotations, buffs |
| **Strategies** | High-level goals - what to do next (find mob, rest, sell) |

### Class Engagement Types
| Class Type | Engagement Behavior |
|------------|---------------------|
| **Melee** (Warrior, Rogue, Paladin, Shaman, Druid) | `Attack()` + `MoveChase()` |
| **Caster** (Mage, Priest, Warlock) | `SetTargetGuid()` only - first spell pulls |
| **Hunter** | Auto Shot at 25 yard range |

---

## Key Source Files

| File | Purpose |
|------|---------|
| `RandomBotAI.cpp` | Bot AI - main brain, coordinates all systems |
| `BotCombatMgr.cpp` | Combat coordinator - creates class handlers |
| `Combat/Classes/*Combat.cpp` | Per-class combat handlers (9 files) |
| `RandomBotGenerator.cpp` | Creates bot accounts/characters on first launch |
| `PlayerBotMgr.cpp` | Bot spawning, loading, lifecycle |
| `CombatBotBaseAI.cpp` | Combat utilities (spell casting, targeting, spell data) |
| `PartyBotAI.cpp` | Working bot example - study for patterns |
| `PlayerBotAI.cpp:361-377` | `CreatePlayerBotAI()` - register new AI types here |

---

## System Understanding

### Database Tables
```
realmd.account          ← Bot accounts (RNDBOT001, RNDBOT002...)
characters.characters   ← Bot character data (name, class, level, position)
characters.playerbot    ← Links char_guid to AI type string
characters.grind_spots  ← Level/faction-appropriate grind locations for travel
```

### Bot Generation Flow (First Launch)
```
PlayerBotMgr::Load()
├── LoadConfig()                           // Load RandomBot.* settings
├── sRandomBotGenerator.GenerateIfNeeded() // Check if generation needed
│   ├── IsPlayerbotTableEmpty()            // playerbot table empty?
│   ├── HasRandomBotAccounts()             // No RNDBOT accounts?
│   └── GenerateRandomBots(count)          // If both true:
│       ├── Create accounts in realmd.account
│       ├── Create characters in characters.characters
│       └── Insert into characters.playerbot with ai='RandomBotAI'
└── (continues to normal load)
```

### Bot Spawning Flow (Every Launch)
```
PlayerBotMgr::Load()
├── Query characters.playerbot table
├── For each row: create PlayerBotEntry in m_bots map
├── VendoringStrategy::BuildVendorCache()    // Cache all vendors
├── TravelingStrategy::BuildGrindSpotCache() // Cache all grind spots
├── AddRandomBot() picks from m_bots, spawns MinBots immediately
└── Periodic refresh adds/removes bots based on Min/Max settings

When spawning a bot:
├── CreatePlayerBotAI(ai_string) creates the AI instance
├── "RandomBotAI" → new RandomBotAI()
└── Bot player object created with AI attached
```

### Registered AI Types (PlayerBotAI.cpp:362-377)
```cpp
"RandomBotAI"              → RandomBotAI()              // Autonomous grinding bot
"MageOrgrimmarAttackerAI"  → MageOrgrimmarAttackerAI()
"IronforgePopulationAI"    → PopulateAreaBotAI(...)
"StormwindPopulationAI"    → PopulateAreaBotAI(...)
"OrgrimmarPopulationAI"    → PopulateAreaBotAI(...)
"PlayerBotFleeingAI"       → PlayerBotFleeingAI()
<unknown/empty>            → PlayerBotAI()              // Base class, no behavior
```

### Key Gotcha
If `m_bots` map is empty, `AddRandomBot()` has nothing to spawn. This happens when:
- playerbot table is empty AND generation is disabled
- Generation failed silently
- Check playerbot table first when debugging "no bots spawning"

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

## Database

```bash
mysql -u mangos -pmangos mangos      # World DB
mysql -u mangos -pmangos characters  # Characters DB (playerbot table)
mysql -u mangos -pmangos realmd      # Accounts DB (RNDBOT accounts)
```

---

## Configuration

| Setting | Purpose |
|---------|---------|
| `RandomBot.Enable` | Enable/disable system |
| `RandomBot.Purge` | Purge all bots on startup (set back to 0 after!) |
| `RandomBot.MaxBots` | Number of bots to generate |
| `RandomBot.MinBots` | Minimum bots online |
| `RandomBot.Refresh` | Add/remove interval (ms) |

---

*Last Updated: 2026-01-24*
