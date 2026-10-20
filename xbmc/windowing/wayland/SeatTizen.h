/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "Seat.h"

namespace KODI::WINDOWING::WAYLAND
{

/*!
 * \brief A seat of a Samsung Tizen TV, whose keymap names some keys of the remote control
 * in its own way.
 */
class CSeatTizen final : public CSeat
{
public:
  CSeatTizen(std::uint32_t globalName, wayland::seat_t const& seat, CConnection& connection)
    : CSeat(globalName, seat, connection)
  {
  }

protected:
  void AdjustKeymap(std::string& keymap) override;
};

} // namespace KODI::WINDOWING::WAYLAND
