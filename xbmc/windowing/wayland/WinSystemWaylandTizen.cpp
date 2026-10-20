/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemWaylandTizen.h"

#include "Connection.h"
#include "Registry.h"
#include "SeatTizen.h"
#include "ServiceBroker.h"
#include "VideoRenderers/HwDecRender/RendererTizen.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "guilib/WindowIDs.h"
#include "input/actions/Action.h"
#include "input/actions/ActionIDs.h"
#include "messaging/ApplicationMessenger.h"
#include "utils/log.h"

#include "platform/wasm/tizen/TizenEMSS.h"

#include <algorithm>
#include <array>
#include <map>
#include <regex>
#include <string_view>

#include <csignal>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

namespace
{
int g_lifecycleFd = -1;

// Events of the app's lifecycle
constexpr char EVENT_BACKGROUND = 'b'; //!< Kodi's window cannot be seen any more
constexpr char EVENT_VISIBLE = 'v'; //!< Kodi's window can be seen again
constexpr char EVENT_ACTIVATE = 'a'; //!< the app was started again and is to come to the front

void SendLifecycleEvent(char event)
{
  if (g_lifecycleFd >= 0)
    [[maybe_unused]] auto ignored = write(g_lifecycleFd, &event, 1);
}

// SIGUSR1: the app went to the background, SIGUSR2: it was started or resumed again.
extern "C" void OnLifecycleSignal(int sig)
{
  SendLifecycleEvent(sig == SIGUSR1 ? EVENT_BACKGROUND : EVENT_ACTIVATE);
}

// Keys of the remote control that Kodi has functions for, which only reach a window that
// grabs them. The others, such as those of other apps or of the TV's sources, stay with
// the TV.
constexpr std::array<std::string_view, 20> REMOTE_KEYS = {
    "XF86Back",          "XF86Red",           "XF86Green",
    "XF86Yellow",        "XF86Blue",          "XF86AudioPlay",
    "XF86AudioPause",    "XF86AudioStop",     "XF86AudioRewind",
    "XF86AudioForward",  "XF86AudioNext",     "XF86AudioPrev",
    "XF86AudioRecord",   "XF86ChannelUp",     "XF86ChannelDown",
    "XF86Info",          "XF86MediaSelectProgramGuide",
    "XF86Subtitle",      "XF86MediaLanguageMenu",
    "XF86MenuKB"};

// tizen_keyrouter mode for a key the window has registered, as a web app's registerKey()
constexpr uint32_t KEYGRAB_REGISTERED = 5;

// Whether the TV shows HDR video, from the feature key that Samsung's web API
// webapis.avinfo.isHdrTvSupport() reads. The TV's compositor has no colour management
// protocol to tell it.
bool TvSupportsHdr()
{
  void* lib = dlopen("libcapi-system-info.so.0", RTLD_NOW);
  if (!lib)
    return false;
  using GetCustomBool = int (*)(const char*, bool*);
  auto getCustomBool = reinterpret_cast<GetCustomBool>(dlsym(lib, "system_info_get_custom_bool"));
  bool supported = false;
  const int ret =
      getCustomBool ? getCustomBool("com.samsung/featureconf/hdr_support", &supported) : -1;
  dlclose(lib);
  CLog::Log(LOGINFO, "Tizen: the TV {} HDR video", ret == 0 && supported ? "shows" : "does not show");
  return ret == 0 && supported;
}
} // namespace

namespace KODI::WINDOWING::WAYLAND
{

bool CWinSystemWaylandTizen::InitWindowSystem()
{
  if (!CWinSystemWayland::InitWindowSystem())
    return false;

  m_tizenRegistry = std::make_unique<CRegistry>(*GetConnection());
  m_tizenRegistry->RequestSingleton(m_tizenSurface, 1, 1, false);
  m_tizenRegistry->RequestSingleton(m_keyrouter, 1, 2, false);
  m_tizenRegistry->RequestSingleton(m_policy, 1, 1, false);
  m_tizenRegistry->Bind();

  if (tizen_emss_available())
    CRendererTizen::Register();

  m_hdrDisplay = TvSupportsHdr();

  if (pipe2(m_lifecyclePipe, O_CLOEXEC) == 0)
  {
    g_lifecycleFd = m_lifecyclePipe[1];
    struct sigaction action = {};
    action.sa_handler = OnLifecycleSignal;
    action.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &action, nullptr);
    sigaction(SIGUSR2, &action, nullptr);
    m_lifecycleThread = std::thread(&CWinSystemWaylandTizen::WatchLifecycle, this);
  }
  return true;
}

bool CWinSystemWaylandTizen::DestroyWindowSystem()
{
  if (m_lifecycleThread.joinable())
  {
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);
    g_lifecycleFd = -1;
    close(m_lifecyclePipe[1]);
    m_lifecycleThread.join();
    close(m_lifecyclePipe[0]);
  }

  m_tizenResource = wayland::tizen_resource_t{};
  m_visibility = wayland::tizen_visibility_t{};
  m_policy = wayland::tizen_policy_t{};
  m_keyrouter = wayland::tizen_keyrouter_t{};
  m_tizenSurface = wayland::tizen_surface_t{};
  if (m_tizenRegistry)
    m_tizenRegistry->UnbindSingletons();
  m_tizenRegistry.reset();
  return CWinSystemWayland::DestroyWindowSystem();
}

bool CWinSystemWaylandTizen::CreateNewWindow(const std::string& name,
                                            bool fullScreen,
                                            RESOLUTION_INFO& res)
{
  if (!CWinSystemWayland::CreateNewWindow(name, fullScreen, res))
    return false;

  if (m_tizenSurface)
  {
    m_tizenResource = m_tizenSurface.get_tizen_resource(GetMainSurface());
    m_tizenResource.on_resource_id() = [this](uint32_t id)
    {
      CLog::LogF(LOGDEBUG, "Window resource id {}", id);
      m_resourceId = id;
    };
  }
  else
  {
    CLog::LogF(LOGWARNING, "No tizen_surface, the TV's player cannot show video on the window");
  }

  // The TV does not tell the app when it goes to the background, as the window is
  // Kodi's and not the app's, but the compositor tells Kodi.
  if (m_policy)
  {
    m_visibility = m_policy.get_visibility(GetMainSurface());
    m_visibility.on_notify() = [](uint32_t visibility)
    {
      CLog::Log(LOGDEBUG, "Tizen: window visibility {}", visibility);
      SendLifecycleEvent(visibility == static_cast<uint32_t>(wayland::tizen_visibility_visibility::fully_obscured)
                             ? EVENT_BACKGROUND
                             : EVENT_VISIBLE);
    };
    m_policy.on_iconify_state_changed() =
        [](wayland::surface_t const&, uint32_t iconified, uint32_t)
    {
      CLog::Log(LOGDEBUG, "Tizen: window {}", iconified ? "iconified" : "uniconified");
      if (iconified)
        SendLifecycleEvent(EVENT_BACKGROUND);
    };
  }
  else
  {
    CLog::LogF(LOGWARNING, "No tizen_policy, Kodi cannot come to the front when started again");
  }

  GrabRemoteKeys();
  return true;
}

void CWinSystemWaylandTizen::WatchLifecycle()
{
  char event;
  bool background = false;
  while (read(m_lifecyclePipe[0], &event, 1) == 1)
  {
    // The compositor and the launcher may both tell of the same change.
    if (event == EVENT_BACKGROUND && background)
      continue;
    if (event == EVENT_VISIBLE && !background)
      continue;
    if (event != EVENT_ACTIVATE)
      background = event == EVENT_BACKGROUND;
    OnLifecycleEvent(event);
  }
}

void CWinSystemWaylandTizen::OnLifecycleEvent(char event)
{
  switch (event)
  {
    case EVENT_BACKGROUND:
    {
      CLog::Log(LOGINFO, "Tizen: app in the background");
      // A TV gives the video decoder and the sound to the app in front, so playback stops.
      const auto player = CServiceBroker::GetAppComponents().GetComponent<CApplicationPlayer>();
      if (player && player->IsPlaying())
        CServiceBroker::GetAppMessenger()->SendMsg(TMSG_GUI_ACTION, WINDOW_INVALID, -1,
                                                   static_cast<void*>(new CAction(ACTION_STOP)));
      break;
    }
    case EVENT_VISIBLE:
      CLog::Log(LOGINFO, "Tizen: app in front");
      break;
    case EVENT_ACTIVATE:
      // Starting the app again brings the app's process to the front, which has no window,
      // so Kodi raises its own.
      CLog::Log(LOGINFO, "Tizen: app started again, bringing Kodi to the front");
      if (m_policy && GetMainSurface())
      {
        m_policy.uniconify(GetMainSurface());
        m_policy.activate(GetMainSurface());
        GetConnection()->GetDisplay().flush();
      }
      break;
  }
}

std::unique_ptr<CSeat> CWinSystemWaylandTizen::CreateSeat(std::uint32_t name,
                                                          wayland::seat_t& seat)
{
  auto newSeat = std::make_unique<CSeatTizen>(name, seat, *GetConnection());
  newSeat->AddRawInputHandlerKeyboard(this);
  return newSeat;
}

void CWinSystemWaylandTizen::OnKeyboardKeymap(CSeat* seat,
                                              wayland::keyboard_keymap_format format,
                                              std::string const& keymap)
{
  if (format != wayland::keyboard_keymap_format::xkb_v1)
    return;

  // The TV's keymap names keys that xkbcommon does not know, which it then drops. They
  // are listed with their keycodes, to tell which key of a remote control sends what.
  {
    std::map<std::string, std::string> codes;
    const std::regex keycode(R"(<([A-Za-z0-9+-]+)>\s*=\s*([0-9]+)\s*;)");
    for (std::sregex_iterator it(keymap.begin(), keymap.end(), keycode), end; it != end; ++it)
      codes[(*it)[1]] = (*it)[2];
    const std::regex symbol(R"(key\s+<([A-Za-z0-9+-]+)>\s*\{[^}]*?\[\s*(XF86[A-Za-z0-9_]+))");
    std::string keys;
    for (std::sregex_iterator it(keymap.begin(), keymap.end(), symbol), end; it != end; ++it)
      keys += " " + codes[(*it)[1]] + "=" + std::string((*it)[2]);
    CLog::LogF(LOGINFO, "Remote control keys of the TV:{}", keys);
  }

  xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_keymap* map = context ? xkb_keymap_new_from_string(context, keymap.c_str(),
                                                         XKB_KEYMAP_FORMAT_TEXT_V1,
                                                         XKB_KEYMAP_COMPILE_NO_FLAGS)
                            : nullptr;
  if (map)
  {
    std::unique_lock lock(m_keysLock);
    m_remoteKeys.clear();
    xkb_keymap_key_for_each(
        map,
        [](xkb_keymap* keymap, xkb_keycode_t key, void* data)
        {
          const xkb_keysym_t* syms = nullptr;
          if (xkb_keymap_key_get_syms_by_level(keymap, key, 0, 0, &syms) < 1)
            return;
          char name[64];
          if (xkb_keysym_get_name(syms[0], name, sizeof(name)) <= 0)
            return;
          const std::string_view keyName(name);
          if (std::ranges::find(REMOTE_KEYS, keyName) == REMOTE_KEYS.end())
            return;
          static_cast<CWinSystemWaylandTizen*>(data)->m_remoteKeys.push_back(key);
        },
        this);
    CLog::LogF(LOGDEBUG, "{} remote control keys in the keymap", m_remoteKeys.size());
    m_keysGrabbed = false;
  }
  if (map)
    xkb_keymap_unref(map);
  if (context)
    xkb_context_unref(context);

  GrabRemoteKeys();
}

void CWinSystemWaylandTizen::GrabRemoteKeys()
{
  std::unique_lock lock(m_keysLock);
  if (m_keysGrabbed || !m_keyrouter || m_remoteKeys.empty() || !GetMainSurface())
    return;

  m_keyrouter.on_keygrab_notify() =
      [](const wayland::surface_t&, uint32_t key, uint32_t mode, uint32_t error)
  {
    // A public certificate does not give the privilege, the keys then reach the window
    // only while it has the focus.
    if (error)
      CLog::Log(LOGDEBUG, "tizen_keyrouter: key {} not grabbed: {}", key, error);
  };
  for (uint32_t key : m_remoteKeys)
    m_keyrouter.set_keygrab(GetMainSurface(), key, KEYGRAB_REGISTERED);
  m_keysGrabbed = true;
}

void CWinSystemWaylandTizen::ApplyOpaqueRegion()
{
  // The TV's player shows video on a plane under the window, which has to be transparent
  // where Kodi draws no GUI.
}

bool CWinSystemWaylandTizen::HasCursor()
{
  return false;
}

bool CWinSystemWaylandTizen::IsHDRDisplay()
{
  return m_hdrDisplay;
}

CHDRCapabilities CWinSystemWaylandTizen::GetDisplayHDRCapabilities() const
{
  // The TV only says whether it shows HDR at all, which means HDR10. HLG, HDR10+ and
  // Dolby Vision vary between models and are not told.
  CHDRCapabilities caps;
  if (m_hdrDisplay)
    caps.SetHDR10();
  return caps;
}

} // namespace KODI::WINDOWING::WAYLAND
