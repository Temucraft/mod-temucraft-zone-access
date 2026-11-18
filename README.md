# mod-temucraft-zone-access

Close areas, zones and maps on a "virtual" expansion of your own choosing, send anyone found there to a spot you pick, and keep everyone off maps the server has no data for.

The 3.3.5a client only knows expansions 0-2 (it reads a map's expansion only for the flight map), and the core refuses maps whose Map.dbc expansion is higher than the account's. This module keeps that out of the DBCs: give any area, zone or map an expansion number in a table, set the server's virtual expansion in the config, and everything above it is closed. Raise the number to open the next "expansion".

## Rules

- **Area > zone > map**: the most specific row decides (Type 0 = area, 1 = zone, 2 = map). An area row can open a spot inside a closed zone.
- A row is closed while its `Expansion` is higher than `Temucraft.ZoneAccess.Expansion`, unless its `OpenDate` has passed.
- Accounts at the row's `MinSecurity` or higher go in anyway (like a realm's allowed security level; default 1 = moderators and up). Logged with `LogBypass`.
- Closed places can't be teleported into (`.tele`, portals, summons, hearthstones...).
- Anyone found in one (on login, on arriving, on changing zone or area, on coming back to life) is sent to the row's spot for their faction, else its `Fallback`, else their hearthstone point.
- If there's nowhere to send them (fallback ID missing from the fallback table, fallback spot closed too, hearthstone point closed as well), they're told "No spawn available" with the reason, the server log gets a warning, and they stay where they are.
- Players on a flight path or a boat/zeppelin are left alone until they land or step off. Ghosts may reach any graveyard; they're moved once alive.
- New characters whose start is closed start at the row's fallback spot, with their hearthstone set there (`NewCharacters`).
- **Maps with no data** (missing from Map.dbc, or no map tiles and no vmap tree in DataDir): the client crashes loading them, so nobody, GMs included, is teleported there, and at startup characters saved on one are moved to their hearthstone or faction city (`CloseMapsWithoutData`, `RescueFromMapsWithoutData`).
- `.zoneaccess check` says whether where you stand is open and which row decides it; `.zoneaccess reload` re-reads the tables and the map data list.

## Install

1. Put this folder in `modules/` of your AzerothCore source, re-run CMake and build.
2. The worldserver creates the two tables in the world database on its next start (`data/sql/db-world/base/`). They are never emptied, so your rows survive updates.
3. Copy `conf/mod-temucraft-zone-access.conf.dist` to `configs/modules/mod-temucraft-zone-access.conf` to change the settings.
4. No client changes.

## The tables (world database)

`mod_temucraft_zone_access`

| Column | Default | Meaning |
|---|---|---|
| Type | | 0 = area, 1 = zone, 2 = map (the order they're checked in) |
| ID | | AreaTable.dbc ID for areas and zones, Map.dbc ID for maps (`.gps` shows them) |
| Expansion | 255 | closed while higher than `Temucraft.ZoneAccess.Expansion`; 255 = closed for good, whatever the config says (only an OpenDate can open it) |
| MinSecurity | 1 | this security level and up go in anyway (0 player, 1 moderator, 2 GM, 3 administrator) |
| OpenDate | NULL | open from this date and time, whatever the Expansion |
| FallbackAlliance | 0 | spot for Alliance players (0 = use Fallback) |
| FallbackHorde | 0 | spot for Horde players (0 = use Fallback) |
| Fallback | 0 | spot for everyone else; 0 = their hearthstone point |
| Message | '' | what players are told ('' = "This place isn't open yet.") |
| Comment | '' | for you |

`mod_temucraft_zone_fallback`: `ID`, `MapID`, `X`, `Y`, `Z`, `Orientation`, `Comment`.

Examples are at the bottom of the SQL file.

## Works with other modules

Doesn't need any other module. [`mod-ryans-debug`](https://github.com/TheSCREWEDSoftware/mod-ryans-debug) only reports maps with no data and leaves the stopping and moving to this module; its startup log says whether this module is doing it.

Idea after 55Honey's Acore_Zonecheck (Eluna) and Ryan's rpSpawn zone and area lists.

## Texts and translations

The 6 player-facing texts (chat messages, gossip options) are in the `module_string` table under the module name
`mod-temucraft-zone-access`, so they can be changed without rebuilding. Translations go in `module_string_locale` with the same
module and id plus the locale (`deDE`, `esES`, `frFR`, ...). If a row is missing the English text in the code is used.

Part of the Temucraft modules for AzerothCore 3.3.5a.
