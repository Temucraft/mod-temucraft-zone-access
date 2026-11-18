/*
 * mod-temucraft-zone-access: closes maps, zones and areas by a "virtual" expansion, and keeps players off maps the
 * server has no data for.
 *
 * Rows in mod_temucraft_zone_access (world DB) give an area (Type 0), zone (1) or map (2) an expansion of your own
 * choosing. Checked in that order (area > zone > map): the most specific row decides. A row is closed while its
 * Expansion is higher than Temucraft.ZoneAccess.Expansion, unless its OpenDate has passed. Accounts at the row's
 * MinSecurity or higher go in anyway (logged).
 *
 * Closed places:
 *   - can't be teleported into (any teleport: .tele, portals, summons, hearthstones...);
 *   - anyone found in one (login, arriving, changing zone or area, coming back to life) is sent to the row's fallback
 *     spot for their faction (mod_temucraft_zone_fallback), else the row's Fallback, else their hearthstone point;
 *     if none of those is open, they're told "No spawn available" (and the server log says why) and stay put;
 *   - players on a flight path or a boat/zeppelin are left alone until they land or step off;
 *   - ghosts may go to any graveyard; they're moved once alive again;
 *   - new characters whose start is closed start at the fallback spot instead, with their hearthstone set there
 *     (Temucraft.ZoneAccess.NewCharacters).
 *
 * Maps with no data (missing from Map.dbc, or no map tiles and no vmap tree in DataDir): the client crashes loading
 * them, so with Temucraft.ZoneAccess.CloseMapsWithoutData nobody, GMs included, is teleported there, and at server
 * startup characters saved on one are moved to their hearthstone, or their faction's city if that's unusable too.
 *
 *   .zoneaccess reload   re-reads both tables
 *   .zoneaccess check    says whether where you stand is open, and which row decides it
 *
 * Idea after 55Honey's Acore_Zonecheck (Eluna) and Ryan's rpSpawn zone and area lists.
 */

#include "Chat.h"
#include "TemucraftModuleText.h"
#include "ChatCommand.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <unordered_set>

using namespace Acore::ChatCommands;

namespace
{
    enum AccessType : uint8 { TYPE_AREA = 0, TYPE_ZONE = 1, TYPE_MAP = 2 }; // in the order they're checked

    struct AccessRow
    {
        uint8 expansion = 255;
        uint32 minSecurity = 1;
        time_t openDate = 0;
        uint32 fallback = 0, fallbackAlliance = 0, fallbackHorde = 0;
        std::string message, comment;
    };

    struct Spot
    {
        uint32 map = 0;
        float x = 0, y = 0, z = 0, o = 0;
        std::string comment;
    };

    struct Settings
    {
        bool enable = true;
        uint8 expansion = 2;
        bool announce = true;
        bool closeNoData = true;
        bool rescue = true;
        bool newCharacters = true;
        bool logBypass = true;
    } settings;

    std::map<std::pair<uint8, uint32>, AccessRow> sRows;
    std::unordered_map<uint32, Spot> sFallbacks;
    std::unordered_set<uint32> sMapsWithData;

    Spot const SPOT_STORMWIND = { 0, -8865.09f, 671.793f, 97.9034f, 2.16098f, "Stormwind" };
    Spot const SPOT_ORGRIMMAR = { 1, 1633.8f, -4440.47f, 15.634f, 3.08343f, "Orgrimmar" };
    Spot const SPOT_BOOTY_BAY = { 0, -14468.104f, 483.00665f, 26.776731f, 0.0f, "Booty Bay" };

    // DataDir/maps/<map id><yy><xx>.map and DataDir/vmaps/<map id>.vmtree; map IDs of 1000 and up have 4 digits
    void ScanMapData()
    {
        sMapsWithData.clear();
        std::error_code ec;
        for (auto const& file : std::filesystem::directory_iterator(sWorld->GetDataPath() + "maps", ec))
        {
            std::string name = file.path().filename().string();
            if (name.size() >= 11 && name.ends_with(".map") && std::all_of(name.begin(), name.end() - 4, ::isdigit))
                sMapsWithData.insert(std::stoul(name.substr(0, name.size() - 8)));
        }
        for (auto const& file : std::filesystem::directory_iterator(sWorld->GetDataPath() + "vmaps", ec))
        {
            std::string name = file.path().filename().string();
            if (name.size() >= 10 && name.ends_with(".vmtree") && std::all_of(name.begin(), name.end() - 7, ::isdigit))
                sMapsWithData.insert(std::stoul(name.substr(0, name.size() - 7)));
        }
    }

    bool HasData(uint32 map)
    {
        return sMapStore.LookupEntry(map) && (sMapsWithData.contains(map) || sObjectMgr->IsTransportMap(map));
    }

    void Load()
    {
        sRows.clear();
        sFallbacks.clear();
        if (QueryResult result = WorldDatabase.Query("SELECT `Type`, `ID`, `Expansion`, `MinSecurity`, "
            "IFNULL(UNIX_TIMESTAMP(`OpenDate`), 0), `Fallback`, `FallbackAlliance`, `FallbackHorde`, `Message`, `Comment` "
            "FROM `mod_temucraft_zone_access`"))
        {
            do
            {
                Field* f = result->Fetch();
                AccessRow row;
                row.expansion = f[2].Get<uint8>();
                row.minSecurity = f[3].Get<uint8>();
                row.openDate = time_t(f[4].Get<uint64>());
                row.fallback = f[5].Get<uint32>();
                row.fallbackAlliance = f[6].Get<uint32>();
                row.fallbackHorde = f[7].Get<uint32>();
                row.message = f[8].Get<std::string>();
                row.comment = f[9].Get<std::string>();
                sRows[{ f[0].Get<uint8>(), f[1].Get<uint32>() }] = row;
            } while (result->NextRow());
        }
        if (QueryResult result = WorldDatabase.Query("SELECT `ID`, `MapID`, `X`, `Y`, `Z`, `Orientation`, `Comment` FROM `mod_temucraft_zone_fallback`"))
        {
            do
            {
                Field* f = result->Fetch();
                sFallbacks[f[0].Get<uint32>()] = { f[1].Get<uint32>(), f[2].Get<float>(), f[3].Get<float>(), f[4].Get<float>(),
                    f[5].Get<float>(), f[6].Get<std::string>() };
            } while (result->NextRow());
        }
        LOG_INFO("module", "mod-temucraft-zone-access: {} rows, {} fallback spots, virtual expansion {}, {} maps with data",
            sRows.size(), sFallbacks.size(), settings.expansion, sMapsWithData.size());
    }

    std::pair<uint8, AccessRow const*> Decide(uint32 map, uint32 zone, uint32 area)
    {
        for (auto [type, id] : { std::pair<uint8, uint32>{ TYPE_AREA, area }, { TYPE_ZONE, zone }, { TYPE_MAP, map } })
        {
            if (!id && type != TYPE_MAP)
                continue;
            auto itr = sRows.find({ type, id });
            if (itr != sRows.end())
                return { type, &itr->second };
        }
        return { 0, nullptr };
    }

    bool Closed(AccessRow const* row)
    {
        if (!row)
            return false;
        if (row->openDate && GameTime::GetGameTime().count() >= row->openDate)
            return false;
        // 255 = closed for good (only an OpenDate opens it), whatever Temucraft.ZoneAccess.Expansion is set to
        return row->expansion == 255 || row->expansion > settings.expansion;
    }

    bool Bypasses(Player* player, AccessRow const* row)
    {
        return player->GetSession()->GetSecurity() >= row->minSecurity;
    }

    // Closed for this player at this spot; *rowOut gets the deciding row
    bool ClosedFor(Player* player, uint32 map, float x, float y, float z, AccessRow const** rowOut = nullptr)
    {
        if (settings.closeNoData && !HasData(map))
            return true;
        uint32 zone = 0, area = 0;
        sMapMgr->GetZoneAndAreaId(player->GetPhaseMask(), zone, area, map, x, y, z);
        AccessRow const* row = Decide(map, zone, area).second;
        if (rowOut)
            *rowOut = row;
        return Closed(row) && !Bypasses(player, row);
    }

    void Tell(Player* player, std::string const& text)
    {
        if (settings.announce)
            ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-zone-access", player->GetSession(), 1, "|cffff6060{}|r"), text);
    }

    void NoSpawn(Player* player, std::string const& why)
    {
        LOG_WARN("module", "mod-temucraft-zone-access: no spawn available for {} (map {}, zone {}, area {}): {}",
            player->GetName(), player->GetMapId(), player->GetZoneId(), player->GetAreaId(), why);
        ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-zone-access", player->GetSession(), 2, "|cffff6060[Zone access] No spawn available: {}|r"), why);
    }

    // Where a player found in this row's place should go: faction spot, then Fallback, then hearthstone
    bool Destination(Player* player, AccessRow const* row, Spot& out)
    {
        uint32 faction = player->GetTeamId() == TEAM_ALLIANCE ? row->fallbackAlliance : row->fallbackHorde;
        for (uint32 id : { faction, row->fallback })
        {
            if (!id)
                continue;
            auto itr = sFallbacks.find(id);
            if (itr == sFallbacks.end())
            {
                NoSpawn(player, Acore::StringFormat("fallback spot {} isn't in mod_temucraft_zone_fallback.", id));
                continue;
            }
            if (ClosedFor(player, itr->second.map, itr->second.x, itr->second.y, itr->second.z))
            {
                NoSpawn(player, Acore::StringFormat("fallback spot {} ({}) is closed too.", id, itr->second.comment));
                continue;
            }
            out = itr->second;
            return true;
        }
        Spot home = { player->m_homebindMapId, player->m_homebindX, player->m_homebindY, player->m_homebindZ, player->GetOrientation(), "hearthstone" };
        if (!ClosedFor(player, home.map, home.x, home.y, home.z))
        {
            out = home;
            return true;
        }
        NoSpawn(player, "the hearthstone point is closed as well, so you stay here. Add a fallback spot to this row.");
        return false;
    }

    void Check(Player* player)
    {
        if (!settings.enable || player->IsBeingTeleported() || player->IsInFlight() || player->GetTransport() || !player->IsAlive())
            return;
        auto [type, row] = Decide(player->GetMapId(), player->GetZoneId(), player->GetAreaId());
        if (!Closed(row))
            return;
        if (Bypasses(player, row))
        {
            if (settings.logBypass)
                LOG_INFO("module", "mod-temucraft-zone-access: {} (security {}) is in closed {} ({}) by GM bypass", player->GetName(),
                    player->GetSession()->GetSecurity(), row->comment, player->GetAreaId());
            return;
        }
        Tell(player, row->message.empty() ? "This place isn't open yet." : row->message);
        Spot spot;
        if (Destination(player, row, spot))
            player->TeleportTo(spot.map, spot.x, spot.y, spot.z, spot.o);
    }

    void RescueFromNoDataMaps()
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT c.guid, c.name, c.race, c.map, h.mapId, h.posX, h.posY, h.posZ FROM characters c "
            "LEFT JOIN character_homebind h ON h.guid = c.guid");
        if (!result)
            return;
        uint32 moved = 0;
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        do
        {
            Field* f = result->Fetch();
            uint32 map = f[3].Get<uint16>();
            if (HasData(map))
                continue;
            Spot spot;
            if (!f[4].IsNull() && HasData(f[4].Get<uint16>()) && !sMapStore.LookupEntry(f[4].Get<uint16>())->Instanceable())
                spot = { f[4].Get<uint16>(), f[5].Get<float>(), f[6].Get<float>(), f[7].Get<float>(), 0, "hearthstone" };
            else
            {
                ChrRacesEntry const* race = sChrRacesStore.LookupEntry(f[2].Get<uint8>());
                spot = race && race->TeamID == 7 ? SPOT_STORMWIND : race && race->TeamID == 1 ? SPOT_ORGRIMMAR : SPOT_BOOTY_BAY;
            }
            trans->Append("UPDATE characters SET map = {}, instance_id = 0, position_x = {}, position_y = {}, position_z = {}, "
                "orientation = {}, trans_x = 0, trans_y = 0, trans_z = 0, trans_o = 0, transguid = 0, taxi_path = '' WHERE guid = {}",
                spot.map, spot.x, spot.y, spot.z, spot.o, f[0].Get<uint32>());
            LOG_INFO("module", "mod-temucraft-zone-access: {} was on map {} (no data): moved to {}", f[1].Get<std::string>(), map, spot.comment);
            ++moved;
        } while (result->NextRow());
        if (moved)
            CharacterDatabase.CommitTransaction(trans);
        LOG_INFO("module", "mod-temucraft-zone-access: {} character(s) moved off maps with no data", moved);
    }
}

class TemucraftZoneAccess_Player : public PlayerScript
{
public:
    TemucraftZoneAccess_Player() : PlayerScript("TemucraftZoneAccess_Player", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_FIRST_LOGIN,
        PLAYERHOOK_ON_UPDATE_ZONE, PLAYERHOOK_ON_UPDATE_AREA, PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_ON_BEFORE_TELEPORT,
        PLAYERHOOK_ON_PLAYER_RESURRECT }) { }

    void OnPlayerLogin(Player* player) override { Check(player); }
    void OnPlayerUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override { Check(player); }
    void OnPlayerUpdateArea(Player* player, uint32 /*oldArea*/, uint32 /*newArea*/) override { Check(player); }
    void OnPlayerMapChanged(Player* player) override { Check(player); }

    // Back to life (a spirit healer in a closed place, for example): moved once the resurrection is done
    void OnPlayerResurrect(Player* player, float /*restorePercent*/, bool& /*applySickness*/) override
    {
        player->m_Events.AddEventAtOffset([player]() { Check(player); }, 1s);
    }

    // A new character whose start is closed starts at the fallback spot, with the hearthstone set there
    void OnPlayerFirstLogin(Player* player) override
    {
        if (!settings.enable || !settings.newCharacters)
            return;
        auto [type, row] = Decide(player->GetMapId(), player->GetZoneId(), player->GetAreaId());
        if (!Closed(row) || Bypasses(player, row))
            return;
        Spot spot;
        if (!Destination(player, row, spot) || spot.comment == "hearthstone")
            return;
        uint32 zone = 0, area = 0;
        sMapMgr->GetZoneAndAreaId(player->GetPhaseMask(), zone, area, spot.map, spot.x, spot.y, spot.z);
        player->SetHomebind(WorldLocation(spot.map, spot.x, spot.y, spot.z, spot.o), area);
        player->TeleportTo(spot.map, spot.x, spot.y, spot.z, spot.o);
    }

    bool OnPlayerBeforeTeleport(Player* player, uint32 mapid, float x, float y, float z, float /*orientation*/,
        uint32 options, Unit* /*target*/) override
    {
        if (!settings.enable)
            return true;
        if (settings.closeNoData && !HasData(mapid))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(TemucraftModuleText("mod-temucraft-zone-access", player->GetSession(), 3, "|cffff4c4cMap {} has no map data on this server: the client would "
                "crash loading it. Not teleporting.|r"), mapid);
            LOG_WARN("module", "mod-temucraft-zone-access: stopped {} teleporting to map {} (no map data)", player->GetName(), mapid);
            return false;
        }
        // boats and zeppelins finish their trip; the player is moved after stepping off. Ghosts may reach any graveyard.
        if ((options & TELE_TO_NOT_LEAVE_TRANSPORT) || !player->IsAlive())
            return true;
        AccessRow const* row = nullptr;
        if (!ClosedFor(player, mapid, x, y, z, &row))
        {
            if (row && Closed(row) && settings.logBypass)
                LOG_INFO("module", "mod-temucraft-zone-access: {} (security {}) teleported into closed {} by GM bypass",
                    player->GetName(), player->GetSession()->GetSecurity(), row->comment);
            return true;
        }
        Tell(player, row && !row->message.empty() ? row->message : "That place isn't open yet.");
        return false;
    }
};

class TemucraftZoneAccess_Commands : public CommandScript
{
public:
    TemucraftZoneAccess_Commands() : CommandScript("TemucraftZoneAccess_Commands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable sub =
        {
            { "reload", HandleReload, 101501 /* SEC_ADMINISTRATOR -> RBAC */, Console::Yes },
            { "check",  HandleCheck,  101502 /* SEC_GAMEMASTER -> RBAC */,    Console::No },
        };
        static ChatCommandTable root = { { "zoneaccess", sub } };
        return root;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        ScanMapData();
        Load();
        handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-zone-access", handler, 4, "Zone access: {} rows, {} fallback spots, {} maps with data."), sRows.size(), sFallbacks.size(),
            sMapsWithData.size());
        return true;
    }

    static bool HandleCheck(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        auto [type, row] = Decide(player->GetMapId(), player->GetZoneId(), player->GetAreaId());
        if (!row)
        {
            handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-zone-access", handler, 5, "Map {} / zone {} / area {}: no row, open."), player->GetMapId(), player->GetZoneId(), player->GetAreaId());
            return true;
        }
        static char const* const names[] = { "area", "zone", "map" };
        handler->PSendSysMessage(TemucraftModuleText("mod-temucraft-zone-access", handler, 6, "Map {} / zone {} / area {}: decided by the {} row \"{}\" (expansion {}, virtual expansion {}, "
            "min security {}{}): {}{}."), player->GetMapId(), player->GetZoneId(), player->GetAreaId(), names[type], row->comment,
            row->expansion, settings.expansion, row->minSecurity, row->openDate ? ", has an open date" : "",
            Closed(row) ? "closed" : "open", Closed(row) && Bypasses(player, row) ? " (you bypass it)" : "");
        return true;
    }
};

class TemucraftZoneAccess_World : public WorldScript
{
public:
    TemucraftZoneAccess_World() : WorldScript("TemucraftZoneAccess_World", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        settings.enable = sConfigMgr->GetOption<bool>("Temucraft.ZoneAccess.Enable", true);
        settings.expansion = sConfigMgr->GetOption<uint8>("Temucraft.ZoneAccess.Expansion", 2);
        settings.announce = sConfigMgr->GetOption<bool>("Temucraft.ZoneAccess.Announce", true);
        settings.closeNoData = sConfigMgr->GetOption<bool>("Temucraft.ZoneAccess.CloseMapsWithoutData", true);
        settings.rescue = sConfigMgr->GetOption<bool>("Temucraft.ZoneAccess.RescueFromMapsWithoutData", true);
        settings.newCharacters = sConfigMgr->GetOption<bool>("Temucraft.ZoneAccess.NewCharacters", true);
        settings.logBypass = sConfigMgr->GetOption<bool>("Temucraft.ZoneAccess.LogBypass", true);
        if (reload)
            Load();
    }

    void OnStartup() override
    {
        ScanMapData();
        Load();
        if (settings.enable && settings.closeNoData && settings.rescue)
            RescueFromNoDataMaps();
    }
};

void AddTemucraftZoneAccessScripts()
{
    new TemucraftZoneAccess_Player();
    new TemucraftZoneAccess_Commands();
    new TemucraftZoneAccess_World();
}
