# vMangos RandomBot AI Project

## COLLABORATION RULES (ALWAYS FOLLOW)

### Before Writing Code
1. **STOP and explain** your plan before implementing
2. **ASK** when you encounter design decisions with multiple valid approaches
3. **CONFIRM** before modifying any existing files
4. **NEVER** make large changes without user approval

### Work Style
- Work in SMALL chunks (one function/feature at a time)
- After each chunk, STOP and ask: "Does this look right?"
- If unsure about ANYTHING, ask - don't assume
- **Build incrementally** - only create files when needed NOW

---

## Documentation

| Doc | Purpose |
|-----|---------|
| `docs/PROGRESS.md` | Recent session log (last 3-5 sessions) |
| `docs/CURRENT_BUG.md` | Active bugs + future enhancements |
| `docs/SYSTEM_REFERENCE.md` | Architecture, constants, thresholds, debugging |
| `docs/Quest_Implementation/BRAINSTORM.md` | Questing system design decisions |
| `docs/Quest_Implementation/IMPLEMENTATION_PLAN.md` | Phase status and build order |
| `docs/Quest_Implementation/KNOWN_ISSUES.md` | Questing bugs, limitations, untested features |

**Archive:** `docs/archive/` contains historical material (old sessions, fixed bugs, audits).

**After EACH session:** Update `docs/PROGRESS.md` with what was completed.

---

## Project Goal

Autonomous RandomBot AI: auto-generate bots that grind, loot, rest, vendor, travel, train spells, quest, and level up naturally.

---

## Architecture

Three-tier system: Survival (always runs) → Activity (weighted selection) → Behaviors (reusable)

```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Main brain, coordinates tiers
├── IBotActivity.h              ← Activity interface (Tier 1)
├── BotMovementManager.h/cpp    ← Centralized movement (all behaviors use this)
├── RandomBotGenerator.h/cpp    ← Auto-generation on first launch
├── PlayerBotMgr.h/cpp          ← Bot lifecycle management
├── BotCheats.h/cpp             ← Resting (spell-based regen)
├── DangerZoneCache.h/cpp       ← Danger zone cache (disabled, future use)
│
├── Activities/                 ← Tier 1: What is the bot doing? (one active at a time)
│   ├── GrindingActivity.h/cpp ← Grinding + traveling coordinator
│   └── QuestingActivity.h/cpp ← Quest lifecycle coordinator (10-state machine)
│
├── Utilities/                  ← Shared tools (any activity can use)
│   ├── BotQuestCache.h/cpp    ← Quest giver/turn-in/item drop caches
│   └── BotObjectInteraction.h/cpp ← Gameobject interaction
│
├── Combat/
│   ├── IClassCombat.h          ← Interface for class handlers
│   ├── BotCombatMgr.h/cpp      ← Combat coordinator
│   ├── CombatHelpers.h         ← Shared helpers (uses BotMovementManager)
│   └── Classes/                ← 9 class-specific handlers
│       └── *Combat.h/cpp       ← Warrior, Mage, Hunter, etc.
│
└── Strategies/                 ← Tier 2: Reusable behaviors
    ├── IBotStrategy.h          ← Strategy/behavior interface
    ├── GrindingStrategy.h/cpp  ← Find and kill mobs (+ quest target filter)
    ├── LootingBehavior.h/cpp   ← Loot corpses after combat
    ├── GhostWalkingStrategy.h/cpp ← Death handling
    ├── VendoringStrategy.h/cpp ← Sell items, repair gear
    ├── TravelingStrategy.h/cpp ← Travel to new grind spots
    └── TrainingStrategy.h/cpp  ← Learn spells at class trainers
```

### Key Files to Study
- `RandomBotAI.cpp` - Main brain, coordinates everything
- `BotMovementManager.cpp` - All movement goes through here
- `PartyBotAI.cpp` - Reference implementation for patterns
- `PlayerBotAI.cpp` - Register new AI types here

---

## Build & Database

```bash
# Build
cd ~/Desktop/Forsaken_WOW/core/build && make -j$(nproc) && make install

# Run
cd ~/Desktop/Forsaken_WOW/run/bin && ./realmd   # Terminal 1
cd ~/Desktop/Forsaken_WOW/run/bin && ./mangosd  # Terminal 2

# Database
mysql -u mangos -pmangos characters  # playerbot table, grind_spots
mysql -u mangos -pmangos realmd      # RNDBOT accounts
```

---

## Debug Tools

- **`.bot status`** - Target a bot: shows level, class, HP/mana, action, strategy, victim, motion type

---

## Key Gotcha

If bots don't spawn: check `characters.playerbot` table. Empty table + generation disabled = nothing to spawn.

---

*Last Updated: 2026-03-15*
