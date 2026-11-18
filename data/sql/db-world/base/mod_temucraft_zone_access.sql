-- mod-temucraft-zone-access: which areas, zones and maps are open, by a "virtual" expansion of your choosing.
--
-- mod_temucraft_zone_access: one row per area, zone or map you want to control.
--   Type              0 = area, 1 = zone (AreaTable.dbc IDs; .gps shows both), 2 = map (Map.dbc ID).
--                     Checked in that order: area > zone > map, the most specific row decides.
--   Expansion         closed while it's higher than Temucraft.ZoneAccess.Expansion in the config. Any number you like
--                     (3 = Cataclysm, 4 = Pandaria, ...): nothing to do with the client's 0-2.
--                     255 = closed for good, whatever the config says; leave OpenDate NULL to keep it closed forever.
--   MinSecurity       accounts at this security level or higher go in anyway, like a realm's allowed security level
--                     (0 player, 1 moderator, 2 GM, 3 administrator). Default 1.
--   OpenDate          optional: from this date and time the row is open, whatever its Expansion. NULL = no date.
--   FallbackAlliance  mod_temucraft_zone_fallback.ID for Alliance players found there (0 = use Fallback)
--   FallbackHorde     the same for the Horde
--   Fallback          for everyone else, or when the faction spot is 0; 0 = their hearthstone point
--   Message           what players are told when stopped or moved ('' = "This place isn't open yet.")
--
-- mod_temucraft_zone_fallback: the spots players are sent to.
--
-- The tables are only created here (never emptied), so your rows survive updates. Fill them in with your own SQL;
-- examples are at the bottom. After changing them in game: .zoneaccess reload

CREATE TABLE IF NOT EXISTS `mod_temucraft_zone_access` (
    `Type`             TINYINT UNSIGNED NOT NULL COMMENT '0 area, 1 zone, 2 map',
    `ID`               INT UNSIGNED NOT NULL,
    `Expansion`        TINYINT UNSIGNED NOT NULL DEFAULT 255 COMMENT 'closed while higher than Temucraft.ZoneAccess.Expansion',
    `MinSecurity`      TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'this security level and up go in anyway',
    `OpenDate`         DATETIME NULL DEFAULT NULL COMMENT 'open from this date, whatever the Expansion',
    `FallbackAlliance` INT UNSIGNED NOT NULL DEFAULT 0,
    `FallbackHorde`    INT UNSIGNED NOT NULL DEFAULT 0,
    `Fallback`         INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = hearthstone point',
    `Message`          VARCHAR(255) NOT NULL DEFAULT '',
    `Comment`          VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`Type`, `ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `mod_temucraft_zone_fallback` (
    `ID`          INT UNSIGNED NOT NULL,
    `MapID`       INT UNSIGNED NOT NULL,
    `X`           FLOAT NOT NULL,
    `Y`           FLOAT NOT NULL,
    `Z`           FLOAT NOT NULL,
    `Orientation` FLOAT NOT NULL DEFAULT 0,
    `Comment`     VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Examples (not run): close Kalimdor (Alliance to Stormwind, Horde to Booty Bay), keep Moonglade open, open Pandaria at
-- virtual expansion 4 or on 1 January 2027, Draenor at 5.
--
-- SET @Stormwind := 1, @BootyBay := 2;
-- REPLACE INTO `mod_temucraft_zone_fallback` (`ID`, `MapID`, `X`, `Y`, `Z`, `Orientation`, `Comment`) VALUES
-- (@Stormwind, 0, -8865.09, 671.793, 97.9034, 2.16098, 'Stormwind, Trade District'),
-- (@BootyBay,  0, -14468.1, 483.007, 26.7767, 0,       'Booty Bay');
-- REPLACE INTO `mod_temucraft_zone_access` (`Type`, `ID`, `Expansion`, `MinSecurity`, `OpenDate`, `FallbackAlliance`, `FallbackHorde`, `Fallback`, `Message`, `Comment`) VALUES
-- (2, 1,    255, 1, NULL,                  @Stormwind, @BootyBay, 0, 'Kalimdor is being rebuilt.', 'Kalimdor closed'),
-- (1, 493,  0,   1, NULL,                  0,          0,         0, '',                           'Moonglade stays open inside closed Kalimdor'),
-- (2, 870,  4,   1, '2027-01-01 00:00:00', 0,          0,         0, 'Pandaria opens on 1 January.', 'Pandaria'),
-- (2, 1116, 5,   1, NULL,                  0,          0,         0, '',                           'Draenor');
