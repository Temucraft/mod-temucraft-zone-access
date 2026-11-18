/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Player-facing text from the module_string / module_string_locale tables, in the reader's locale. The English text
// written in the code is used when the table has no row for that id, so the module works before its SQL is applied.
// (Same file in every Temucraft module.)
#pragma once

#include "Chat.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include <string>

inline char const* TemucraftModuleText(std::string const& module, WorldSession* session, uint32 id, char const* fallback)
{
    if (!sObjectMgr->GetModuleString(module, id))
        return fallback;
    std::string const* text = session ? session->GetModuleString(module, id) : sObjectMgr->GetModuleString(module, id, LOCALE_enUS);
    return text ? text->c_str() : fallback;
}
inline char const* TemucraftModuleText(std::string const& module, Player* player, uint32 id, char const* fallback)
{
    return TemucraftModuleText(module, player ? player->GetSession() : nullptr, id, fallback);
}
inline char const* TemucraftModuleText(std::string const& module, Player const* player, uint32 id, char const* fallback)
{
    return TemucraftModuleText(module, player ? player->GetSession() : nullptr, id, fallback);
}
inline char const* TemucraftModuleText(std::string const& module, ChatHandler* handler, uint32 id, char const* fallback)
{
    return TemucraftModuleText(module, handler ? handler->GetSession() : nullptr, id, fallback);
}
inline char const* TemucraftModuleText(std::string const& module, ChatHandler& handler, uint32 id, char const* fallback)
{
    return TemucraftModuleText(module, handler.GetSession(), id, fallback);
}
