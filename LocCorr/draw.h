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
#ifndef DRAW_H__
#define DRAW_H__

#include <stdint.h>

// 3-channel image for saving into jpg/png
typedef struct{
    uint8_t *data;  // image data
    int w;          // width
    int h;          // height
} Img3;

// opaque pattern for drawing
typedef struct{
    uint8_t *data;
    int w;
    int h;
} Pattern;

// base colors
extern const uint8_t C_R[], C_G[], C_B[], C_K[], C_W[];

void Pattern_free(Pattern **p);
void Pattern_draw3(Img3 *img, const Pattern *p, int xc, int yc, const uint8_t colr[]);
Pattern *Pattern_cross(int h, int w);
Pattern *Pattern_xcross(int h, int w);

#endif // DRAW_H__
