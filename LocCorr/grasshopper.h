/*
 * This file is part of the loccorr project.
 * Copyright 2021 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#ifndef GRASSHOPPER_H__
#define GRASSHOPPER_H__

#include "fits.h" // Image*

#define GRASSHOPPER_CAPT_NAME   "grasshopper"

void disconnectGrasshopper();
int capture_grasshopper(void (*process)(Image *));
char *gsimagestatus(const char *messageid, char *buf, int buflen);

#endif // GRASSHOPPER_H__
