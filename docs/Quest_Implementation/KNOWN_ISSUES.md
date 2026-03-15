# Questing System - Known Issues & Untested Features

**Last Updated**: 2026-03-15

---

## Known Bugs

### Bug 1: GO Item Looting Not Working
**Severity**: High
**Affects**: Any quest requiring items from gameobjects (Cactus Apples, herb quest items, etc.)
**Symptom**: Bot travels to gameobject, calls LootObject, log says "looted quest item from Cactus Apple" but item count never increases. Bot keeps cycling to same GO.
**Root Cause**: `SendLoot` + `AutoStoreLoot` approach isn't giving quest items. Possibly wrong loot type passed to SendLoot, or loot generation requires a different flow for gameobjects vs creatures.
**Files**: `BotObjectInteraction.cpp` → `LootObject()`

### Bug 2: Quest Giver Bouncing (Non-Actionable Quests)
**Severity**: Medium
**Affects**: Bots with quests that have no kill/collect/GO/explore objectives
**Symptom**: Bots cycle between "traveling to quest giver" and "arrived" every 2 seconds. Examples: Dwarves at entry 713, Night Elves at entry 3514, Tauren at entry 3209.
**Root Cause**: `HandleSelectingQuest` finds no actionable objectives but the bot has incomplete quests AND free quest log slots, so it goes to `FINDING_QUEST_GIVER` instead of `NO_QUESTS_AVAILABLE`. The fix for this was applied but doesn't cover all cases — some quest types (talk-to-NPC, escort, etc.) still have no handler.
**Files**: `QuestingActivity.cpp` → `HandleSelectingQuest()`

### Bug 3: Grumdu-Type Permanent Stuck
**Severity**: Low (not questing-specific)
**Symptom**: Bot repeatedly stuck at invalid position, teleports home, immediately gets stuck again. Seen at coords around (-5957, 269, 387) in Dun Morogh.
**Root Cause**: Map/navmesh issue at specific coordinates. Bot teleports to hearthstone which is near the bad spot.

---

## Known Limitations (Not Bugs)

### Limitation 1: "Talk to NPC" Quests Not Handled
Quests that only require talking to an NPC (no kill/collect/GO/explore objectives) are not actionable. Bot either bounces or falls to NO_QUESTS_AVAILABLE.

### Limitation 2: Item Usage Quests (Q7) Not Implemented
Quests requiring using an item on a target/self/location. `BotItemUsage` utility class was planned but not built.

### Limitation 3: No Quest Blacklist
No mechanism to blacklist specific quest IDs that cause problems. Planned as a simple database table or config list.

### Limitation 4: Opportunistic Features (Q10) Not Implemented
- Item-started quests: items with `StartQuest` field looted during combat don't auto-accept quests
- Nearby quest pickup: bots don't detect quest givers while traveling/grinding

### Limitation 5: Escort Quests Not Handled
Escort quests require reactive behavior. Deferred to future.

### Limitation 6: Dungeon/Group/Elite Quests Not Handled
Requires party system. Deferred to future.

---

## Not Fully Tested

### Test 1: Exploration Quests (Q6)
**Status**: Code written, never confirmed in-game
**How to test**: Wait for a bot to pick up "The Fargodeep Mine" (quest 62, Elwynn) or "Frostmane Hold" (quest 287, Dun Morogh). Should see "traveling to explore area" log and auto-completion on arrival.

### Test 2: Source Item Awareness (ReqSourceId)
**Status**: Code written for Marla's Last Wish pattern, untested
**How to test**: Undead bot needs to reach quest 6395. Bot should kill Samuel Fipps for Samuel's Remains first, then interact with Marla's Grave.

### Test 3: GO Quest Objectives (ReqCreatureOrGOId < 0)
**Status**: Interaction works (bot travels and calls Use()) but completion unconfirmed
**How to test**: Quest 786 "Thwarting Kolkar Aggression" (level 8, Durotar) has GO objectives.

### Test 4: Grey Quest Abandonment
**Status**: Code written, bots haven't leveled enough during testing
**How to test**: Run server long enough for bots to outlevel early quests. Should see "abandoning grey quest" log.

### Test 5: Soft Timeout Abandonment
**Status**: Code written, 15 min timeout never observed firing
**How to test**: Bot needs to be stuck on a quest making no progress for 15+ minutes. Should see "abandoning stale quest" log.

### Test 6: Class-Based Reward Selection (Q11)
**Status**: Code written, never verified correct item picked
**How to test**: Watch a bot turn in a quest with multiple reward choices. Check if the class-appropriate item was selected.

### Test 7: Mob Area Relocation
**Status**: 10-second timeout implemented
**How to test**: Watch bots clear all mobs in an area. After 10 seconds of NO_TARGETS, should see "traveling to quest mob area" with a different location.

### Test 8: Multi-Quest Overlap
**Status**: `BuildKillTargetList` combines all quest objectives
**How to test**: Bot with two kill quests targeting different mobs in same area should progress both simultaneously.

### Test 9: GO Spawn Relocation
**Status**: Immediate relocate on "GO not found at destination" implemented
**How to test**: Depends on Bug 1 being fixed first. Bot should move to different Cactus Apple spawn when current one is despawned.

### Test 10: NO_QUESTS_AVAILABLE Re-check
**Status**: 30-second re-check timer implemented
**How to test**: Bot in NO_QUESTS_AVAILABLE should re-check after 30 seconds. If it leveled up or quests became available, should resume questing.

---

## Priority for Next Session

1. **Fix Bug 1** (GO item looting) — blocks all gameobject-item quests
2. **Fix Bug 2** (bouncing) — affects many Dwarf/NE/Tauren bots
3. **Test items 1-3** (exploration, source items, GO objectives)
4. **Implement Limitation 1** (talk-to-NPC quests) — would fix most of Bug 2

---

*Last Updated: 2026-03-15*
