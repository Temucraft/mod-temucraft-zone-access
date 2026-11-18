-- mod-temucraft-zone-access: player-facing texts. Translations go in module_string_locale (same module and id, plus the locale).
DELETE FROM `module_string` WHERE `module` = 'mod-temucraft-zone-access';
INSERT INTO `module_string` (`module`, `id`, `string`) VALUES
('mod-temucraft-zone-access', 1, '|cffff6060{}|r'),
('mod-temucraft-zone-access', 2, '|cffff6060[Zone access] No spawn available: {}|r'),
('mod-temucraft-zone-access', 3, '|cffff4c4cMap {} has no map data on this server: the client would crash loading it. Not teleporting.|r'),
('mod-temucraft-zone-access', 4, 'Zone access: {} rows, {} fallback spots, {} maps with data.'),
('mod-temucraft-zone-access', 5, 'Map {} / zone {} / area {}: no row, open.'),
('mod-temucraft-zone-access', 6, 'Map {} / zone {} / area {}: decided by the {} row "{}" (expansion {}, virtual expansion {}, min security {}{}): {}{}.');
