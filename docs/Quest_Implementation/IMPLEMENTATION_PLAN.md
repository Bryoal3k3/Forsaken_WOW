# Questing System Implementation Plan

**Status**: Core questing functional. 5 features remaining.
**Created**: 2026-03-14
**Last Updated**: 2026-03-15

---

## Completed Phases

| # | Phase | Description | Status |
|---|-------|-------------|--------|
| 1 | R1 | Activity tier system refactor (IBotActivity + GrindingActivity) | Done |
| 2 | Q1 | Quest caches (givers, turn-ins, item drops) | Done |
| 3 | Q2 | QuestingActivity skeleton (accept/turn-in 10-state machine) | Done |
| 4 | Q3 | Kill quest objectives (GrindingStrategy target filter, mob spawn travel) | Done |
| 5 | Q4 | Collect quest objectives (item drops, reverse cache, closest spawn) | Done |
| 6 | Q5 | Object interaction (BotObjectInteraction, GO quests, ReqSourceId) | Done |
| 7 | Q6 | Exploration quests (areatrigger travel, auto-completion) | Done (untested) |
| 8 | Q8 | Quest log management (abandon grey/stale, 15-min soft timeout) | Done (untested) |
| 9 | Q11 | Class-based reward selection (STR/AGI/INT/STA weights) | Done (untested) |
| 10 | Q12 | Configurable activity ratio (`RandomBot.QuestingPercent`) | Done |
| 11 | -- | GO item looting fix (LOOT_CORPSE, CanInteractWith gate, relocate) | Done |
| 12 | -- | Quest giver bouncing fix (exhausted giver tracking) | Done |
| 13 | -- | Vendor repair loop fix (free repairs) | Done |

---

## Remaining Features

| # | Phase | Description | Priority | Notes |
|---|-------|-------------|----------|-------|
| 1 | Q7 | **Item usage quests** — use quest item on target/self/location | High | Needs `BotItemUsage` utility. Examples: use quest item on mob corpse, use item at location. Common in level 10+ zones. |
| 2 | Q10a | **Talk-to-NPC quests** — complete quests that only require speaking to an NPC | High | Currently bots skip these entirely (fall to NO_QUESTS_AVAILABLE). Many early quest chains include delivery/talk steps. Need to detect "no objectives" quests and auto-complete or travel to the target NPC. |
| 3 | Q10b | **Opportunistic quest pickup** — detect nearby quest givers while traveling/grinding | Medium | Bots currently only pick up quests when in FINDING_QUEST_GIVER state. Could grab quests from givers they pass by during other activities. |
| 4 | Q10c | **Item-started quests** — auto-accept quests from looted items with `StartQuest` | Medium | Hook in LootingBehavior. When a quest-starting item is looted, accept the quest automatically. |
| 5 | -- | **Quest blacklist** — skip specific broken/problematic quest IDs | Low | Simple database table (`bot_quest_blacklist`) or config list. Useful for quests that softlock bots. |

**Excluded**: Escort quests and dungeon/group/elite quests require a party system and are deferred indefinitely.

---

## Untested Features (code exists, needs longer server runs)

| Feature | How to Verify |
|---------|--------------|
| Exploration quests (Q6) | Bot picks up "The Fargodeep Mine" (quest 62) or "Frostmane Hold" (quest 287) and travels to areatrigger |
| Grey quest abandonment (Q8) | Bots outlevel early quests → "abandoning grey quest" log |
| Soft timeout abandonment (Q8) | Bot stuck on quest 15+ min → "abandoning stale quest" log |
| Class-based reward selection (Q11) | Bot turns in quest with multiple rewards → picks class-appropriate item |
| Source item awareness (Q5) | Undead bot does quest 6395, kills Samuel Fipps first, then interacts with Marla's Grave |

These will naturally trigger with longer server sessions (1+ hours) as bots level past the starting zones.

---

*Last Updated: 2026-03-15*
