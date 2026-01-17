# Current Bug Tracker

## Status: RESOLVED

---

## Problem Summary
Bots spawn naked (no starting gear) and sometimes have graphical issues (wrong race features).

## Symptoms
- Bots spawn without armor, clothes, or weapons
- Some bots have wrong visual features (e.g., gnome with night elf face markings)
- Issue started after setting `played_time_total=1` to fix cinematic bug

## Investigation

### Hypothesis
The `played_time_total=1` hack bypassed the starting item initialization check.

### Code Flow
```
Player::LoadFromDB() [Player.cpp:15113-15116]
    └─> hasItems = _LoadInventory()  // returns false for new bot
    └─> if (!hasItems && m_playedTime[PLAYED_TIME_TOTAL] == 0)
            AddStartingItems();      // SKIPPED because played_time=1!
```

### Key Files
| File | Relevance |
|------|-----------|
| `Player.cpp:15115` | Starting items only added when played_time=0 |
| `CharacterHandler.cpp:610` | Cinematic triggered when played_time=0 |
| `RandomBotGenerator.cpp:271` | Was setting played_time=1 |

### Debug Logging Added
| File | Location | Tag | Purpose |
|------|----------|-----|---------|
| (none needed) | | | |

## Findings

**The smoking gun (Player.cpp:15115):**
```cpp
if (!hasItems && m_playedTime[PLAYED_TIME_TOTAL] == 0)
    AddStartingItems();
```

Setting `played_time_total=1` to skip cinematic also skipped starting item initialization.

### Root Cause
**Location:** `RandomBotGenerator.cpp:271`
**Problem:** `played_time_total=1` bypasses `AddStartingItems()` check
**Why:** The previous cinematic fix (setting played_time=1) had unintended side effects on character initialization

## Fix

### Solution
1. Revert `played_time_total` back to 0 in RandomBotGenerator (allows proper initialization)
2. Add `&& !pCurrChar->IsBot()` check to cinematic trigger (skips cinematic for bots only)

### Files Modified
- `src/game/Handlers/CharacterHandler.cpp` - Added IsBot() check to cinematic condition
- `src/game/PlayerBots/RandomBotGenerator.cpp` - Reverted played_time_total to 0

### Verification
1. Build and run server
2. Purge existing bots (`RandomBot.Purge = 1`)
3. Generate new bots
4. Verify bots spawn with starting gear
5. Verify bots are targetable by mobs (no cinematic issue)
6. Verify real players still get intro cinematic

## Notes
- The backup `CinematicEnd()` call in RandomBotAI::UpdateAI() is retained as a safety fallback
- Graphical issues may be a separate bug related to invalid face/hair values per race

---
*Started: 2025-01-16*
*Resolved: 2025-01-16*
