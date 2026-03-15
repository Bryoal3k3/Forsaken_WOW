/*
 * BotObjectInteraction.h
 *
 * Shared utility for bot interaction with gameobjects.
 * Used by questing (quest objects, wanted posters), gathering (herbs, mines),
 * and any future activity that needs gameobject interaction.
 *
 * Part of the vMangos RandomBot AI Project.
 */

#ifndef MANGOS_BOTOBJECTINTERACTION_H
#define MANGOS_BOTOBJECTINTERACTION_H

#include "Common.h"

class Player;
class GameObject;

class BotObjectInteraction
{
public:
    // Find nearest gameobject by entry ID within range
    static GameObject* FindNearbyObject(Player* pBot, uint32 entry, float range = 30.0f);

    // Find nearest gameobject by type (e.g., GAMEOBJECT_TYPE_CHEST) within range
    static GameObject* FindNearbyObjectByType(Player* pBot, uint32 type, float range = 30.0f);

    // Check if bot can interact with this object (alive, in range, object usable)
    static bool CanInteractWith(Player* pBot, GameObject* pObject);

    // Interact with a gameobject (calls GameObject::Use)
    // Handles quest givers, chests, goobers, etc.
    static bool InteractWith(Player* pBot, GameObject* pObject);

    // Loot a gameobject (chest, quest container, etc.)
    // SendLoot → AutoStoreLoot → DoLootRelease
    static bool LootObject(Player* pBot, GameObject* pObject);
};

#endif // MANGOS_BOTOBJECTINTERACTION_H
