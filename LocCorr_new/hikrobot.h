/*
 * This file is part of the loccorr project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifdef MVS_FOUND
#include "cameracapture.h" // `camera`

#define HIKROBOT_CAPT_NAME        "hikrobot"

// maximal readout time, seconds
#define MAX_READOUT_TM      (6.)

// tolerance of float values
#define HR_FLOAT_TOLERANCE  (0.005)

extern camera Hikrobot;
#endif
