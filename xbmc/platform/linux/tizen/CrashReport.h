/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

namespace KODI::PLATFORM::TIZEN
{
/*!
 * \brief Writes where Kodi crashed to stderr: on a TV, there is no other way to find out.
 */
void InstallCrashReport();

/*!
 * \brief Writes where Kodi's other threads are to stderr, such as when Kodi does not end.
 */
void ReportThreads();
} // namespace KODI::PLATFORM::TIZEN
