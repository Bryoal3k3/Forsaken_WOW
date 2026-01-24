-- RandomBot Grind Spots Table
-- Run this against the characters database

DROP TABLE IF EXISTS grind_spots;

CREATE TABLE grind_spots (
    id INT PRIMARY KEY AUTO_INCREMENT,
    map_id INT NOT NULL,                    -- 0=Eastern Kingdoms, 1=Kalimdor
    zone_id INT NOT NULL DEFAULT 0,         -- For reference/debugging
    x FLOAT NOT NULL,
    y FLOAT NOT NULL,
    z FLOAT NOT NULL,
    min_level TINYINT NOT NULL,
    max_level TINYINT NOT NULL,
    faction TINYINT NOT NULL DEFAULT 0,     -- 0=both, 1=alliance, 2=horde
    radius FLOAT NOT NULL DEFAULT 100.0,    -- Roaming radius from center point
    priority TINYINT NOT NULL DEFAULT 50,   -- Higher = better spot (1-100)
    name VARCHAR(64)                        -- Debug name
);

CREATE INDEX idx_grind_level ON grind_spots(min_level, max_level);
CREATE INDEX idx_grind_faction ON grind_spots(faction);
CREATE INDEX idx_grind_map ON grind_spots(map_id);

-- Initial Test Data: Starting Zones
-- Coordinates verified against vMangos spawn data

-- ============================================================================
-- ALLIANCE Starting Zones
-- ============================================================================

-- Northshire Valley (Human starting area)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 12, -8913, -136, 82, 1, 6, 1, 80.0, 70, 'Northshire Valley');

-- Elwynn Forest (Human 6-12)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 12, -9464, 62, 56, 6, 12, 1, 120.0, 60, 'Elwynn Forest');

-- Coldridge Valley (Dwarf/Gnome starting area)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 1, -6240, 331, 383, 1, 6, 1, 80.0, 70, 'Coldridge Valley');

-- Dun Morogh (Dwarf/Gnome 6-12)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 1, -5603, -482, 396, 6, 12, 1, 120.0, 60, 'Dun Morogh');

-- Shadowglen (Night Elf starting area)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 141, 10311, 832, 1326, 1, 6, 1, 80.0, 70, 'Shadowglen');

-- Teldrassil (Night Elf 6-12)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 141, 9947, 1012, 1306, 6, 12, 1, 120.0, 60, 'Teldrassil');

-- ============================================================================
-- HORDE Starting Zones
-- ============================================================================

-- Valley of Trials (Orc/Troll starting area)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 14, -618, -4251, 38, 1, 6, 2, 80.0, 70, 'Valley of Trials');

-- Durotar (Orc/Troll 6-12)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 14, 296, -4706, 16, 6, 12, 2, 120.0, 60, 'Durotar');

-- Camp Narache (Tauren starting area)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 215, -2917, -257, 52, 1, 6, 2, 80.0, 70, 'Camp Narache');

-- Mulgore (Tauren 6-12)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 215, -2371, -382, -5, 6, 12, 2, 120.0, 60, 'Mulgore');

-- Deathknell (Undead starting area)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 85, 1676, 1678, 121, 1, 6, 2, 80.0, 70, 'Deathknell');

-- Tirisfal Glades (Undead 6-12)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 85, 2270, 323, 34, 6, 12, 2, 120.0, 60, 'Tirisfal Glades');

-- ============================================================================
-- Shared Low-Level Zones (Both Factions)
-- ============================================================================

-- Westfall (Alliance contested, level 10-20)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 40, -10453, 1049, 42, 10, 15, 1, 150.0, 55, 'Westfall - South'),
(0, 40, -10905, 988, 32, 15, 20, 1, 150.0, 55, 'Westfall - North');

-- Loch Modan (Alliance, level 10-20)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 38, -5372, -2919, 322, 10, 15, 1, 150.0, 55, 'Loch Modan - South'),
(0, 38, -5095, -3468, 299, 15, 20, 1, 150.0, 55, 'Loch Modan - North');

-- Darkshore (Alliance, level 10-20)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 148, 6437, 526, 7, 10, 15, 1, 150.0, 55, 'Darkshore - South'),
(1, 148, 6754, 112, 6, 15, 20, 1, 150.0, 55, 'Darkshore - North');

-- The Barrens (Horde, level 10-25)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(1, 17, -434, -2640, 95, 10, 15, 2, 150.0, 55, 'The Barrens - Northern'),
(1, 17, -1167, -3477, 91, 15, 20, 2, 150.0, 55, 'The Barrens - Central'),
(1, 17, -2117, -3661, 5, 20, 25, 2, 150.0, 55, 'The Barrens - Southern');

-- Silverpine Forest (Horde, level 10-20)
INSERT INTO grind_spots (map_id, zone_id, x, y, z, min_level, max_level, faction, radius, priority, name) VALUES
(0, 130, 505, 1532, 32, 10, 15, 2, 150.0, 55, 'Silverpine Forest - North'),
(0, 130, -116, 967, 66, 15, 20, 2, 150.0, 55, 'Silverpine Forest - South');
