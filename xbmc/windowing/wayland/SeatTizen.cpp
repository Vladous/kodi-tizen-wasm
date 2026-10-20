/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SeatTizen.h"

#include <algorithm>
#include <array>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

#include <xkbcommon/xkbcommon.h>

namespace KODI::WINDOWING::WAYLAND
{

namespace
{
// Keysyms of the TV's keymap that xkbcommon does not know, which would make their keys
// send nothing, and the standard ones that Kodi maps to the same function.
constexpr std::array<std::pair<const char*, const char*>, 10> TIZEN_KEYSYMS = {{
    {"XF86RaiseChannel", "XF86ChannelUp"},
    {"XF86LowerChannel", "XF86ChannelDown"},
    {"XF86ChannelGuide", "XF86MediaSelectProgramGuide"},
    {"XF86SimpleMenu", "XF86MenuKB"},
    {"XF86AudioPlayPause", "XF86AudioPause"},
    {"XF86PlayBack", "XF86AudioPlay"},
    {"XF86PreviousChapter", "XF86AudioPrev"},
    {"XF86NextChapter", "XF86AudioNext"},
    {"XF86MTS", "XF86MediaLanguageMenu"},
    {"XF86Caption", "XF86Subtitle"},
}};

// The TV sets its volume itself, so Kodi must not change its own as well.
constexpr std::array<std::string_view, 3> TV_KEYSYMS = {
    "XF86AudioRaiseVolume",
    "XF86AudioLowerVolume",
    "XF86AudioMute",
};
} // namespace

void CSeatTizen::AdjustKeymap(std::string& keymap)
{
  for (const auto& [tizen, standard] : TIZEN_KEYSYMS)
    keymap = std::regex_replace(keymap, std::regex(std::string("\\b") + tizen + "\\b"), standard);

  // The keymap has hundreds of other keysyms of Samsung's own, for keys Kodi has no use
  // for, and xkbcommon would warn about each of them. Keys the TV handles itself send
  // nothing to Kodi either.
  std::string known;
  known.reserve(keymap.size());
  const std::regex keysym(R"(\bXF86[A-Za-z0-9_]+)");
  auto last = keymap.cbegin();
  for (std::sregex_iterator it(keymap.begin(), keymap.end(), keysym), end; it != end; ++it)
  {
    known.append(last, (*it)[0].first);
    const std::string name = it->str();
    const bool tvKey = std::ranges::find(TV_KEYSYMS, name) != TV_KEYSYMS.end();
    known += !tvKey && xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_NO_FLAGS) != XKB_KEY_NoSymbol
                 ? name
                 : "NoSymbol";
    last = (*it)[0].second;
  }
  known.append(last, keymap.cend());
  keymap = std::move(known);
}

} // namespace KODI::WINDOWING::WAYLAND
