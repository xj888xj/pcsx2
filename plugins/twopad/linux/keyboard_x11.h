/*  TwoPAD - author: arcum42(@gmail.com)
 *  Copyright (C) 2019-2020
 *
 *  Based on ZeroPAD, author zerofrog@gmail.com
 *  And OnePAD, author arcum42, gregory, PCSX2 team
 *  Copyright (C) 2006-2007
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#pragma once

#include <array>
#include <map>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

#include "twopad.h"

extern Display *GSdsp;
extern Window GSwin;

using x11_map = std::map<int, u32>;
extern std::array<x11_map, 2> x11_key_map;

extern void PollForX11KeyboardInput();
extern bool PollX11KeyboardMouseEvent(u32 &pkey);

extern void SetAutoRepeat(bool autorep);
extern void init_x11_keys();
extern const char* KeyToString(KeySym k);