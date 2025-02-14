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
#ifndef BINMORPH_H__
#define BINMORPH_H__

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include "fits.h"

// minimal image size for morph operations
#define MINWIDTH    (9)
#define MINHEIGHT   (3)

// simple box with given borders
typedef struct{
    uint16_t xmin;
    uint16_t xmax;
    uint16_t ymin;
    uint16_t ymax;
    uint32_t area; // total amount of object pixels inside the box
} il_Box;

typedef struct{
    size_t Nobj;
    il_Box *boxes;
} il_ConnComps;


// morphological operations:
uint8_t *il_dilation(uint8_t *image, int W, int H);
uint8_t *il_dilationN(uint8_t *image, int W, int H, int N);
uint8_t *il_erosion(uint8_t *image, int W, int H);
uint8_t *il_erosionN(uint8_t *image, int W, int H, int N);
uint8_t *il_openingN(uint8_t *image, int W, int H, int N);
uint8_t *il_closingN(uint8_t *image, int W, int H, int N);
uint8_t *il_topHat(uint8_t *image, int W, int H, int N);
uint8_t *il_botHat(uint8_t *image, int W, int H, int N);

// clear non 4-connected pixels
uint8_t *il_filter4(uint8_t *image, int W, int H);
// clear single pixels
uint8_t *il_filter8(uint8_t *image, int W, int H);

size_t *il_cclabel4(uint8_t *Img, int W, int H, il_ConnComps **CC);

#endif // BINMORPH_H__
