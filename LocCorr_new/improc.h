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
#ifndef IMPROC_H__
#define IMPROC_H__

#include <stdatomic.h>

#include "imagefile.h"

// tolerance of deviations by X and Y axis (if sigmaX or sigmaY greater, values considered to be wrong)
#define XY_TOLERANCE                (5.)
#define ROI_SIZE                    (200)

extern volatile atomic_bool stopwork;
extern volatile atomic_ullong ImNumber;

extern char *(*imagedata)(const char *messageid, char *buf, int buflen);

void process_file(Image *I);
int  process_input(InputType tp, char *name);
void openXYlog(const char *name);
void closeXYlog();
int XYcomment(char *cmnt);
double getFramesPerS();
void getcenter(float *x, float *y);

#endif // IMPROC_H__
