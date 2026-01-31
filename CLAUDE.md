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
| `docs/PROGRESS.md` | Session log, what changed, architecture details |
| `docs/CURRENT_BUG.md` | Bug tracking and fix history |
| `docs/BOT_DECISION_FLOW.md` | State machine, constants, thresholds |
| `docs/RANDOMBOT_SYSTEM.md` | Comprehensive system documentation |
| `docs/VMANGOS_BOT_FRAMEWORK_ANALYSIS.md` | vMangos bot framework reference |

**After EACH session:** Update `docs/PROGRESS.md` with what was completed.

---

## Project Goal

Autonomous RandomBot AI: auto-generate bots that grind, loot, rest, vendor, travel, and level up naturally.

---

## Architecture

```
src/game/PlayerBots/
├── RandomBotAI.h/cpp           ← Main brain, coordinates all systems
├── BotMovementManager.h/cpp    ← Centralized movement (all strategies use this)
├── RandomBotGenerator.h/cpp    ← Auto-generation on first launch
├── PlayerBotMgr.h/cpp          ← Bot lifecycle management
├── BotCheats.h/cpp             ← Resting (cheat regen, no consumables)
├── DangerZoneCache.h/cpp       ← Danger zone cache (disabled, future use)
│
├── Combat/
│   ├── IClassCombat.h          ← Interface for class handlers
│   ├── BotCombatMgr.h/cpp      ← Combat coordinator
│   ├── CombatHelpers.h         ← Shared helpers (uses BotMovementManager)
│   └── Classes/                ← 9 class-specific handlers
│       └── *Combat.h/cpp       ← Warrior, Mage, Hunter, etc.
│
└── Strategies/                 ← High-level behaviors (all use BotMovementManager)
    ├── IBotStrategy.h          ← Strategy interface
    ├── GrindingStrategy.h/cpp  ← Find and kill mobs
    ├── LootingBehavior.h/cpp   ← Loot corpses after combat
    ├── GhostWalkingStrategy.h/cpp ← Death handling
    ├── VendoringStrategy.h/cpp ← Sell items, repair gear
    └── TravelingStrategy.h/cpp ← Travel to new grind spots
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

*Last Updated: 2026-01-31*
