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
} Box;

typedef struct{
    size_t Nobj;
    Box *boxes;
} ConnComps;


// morphological operations:
uint8_t *dilation(uint8_t *image, int W, int H);
uint8_t *dilationN(uint8_t *image, int W, int H, int N);
uint8_t *erosion(uint8_t *image, int W, int H);
uint8_t *erosionN(uint8_t *image, int W, int H, int N);
uint8_t *openingN(uint8_t *image, int W, int H, int N);
uint8_t *closingN(uint8_t *image, int W, int H, int N);
uint8_t *topHat(uint8_t *image, int W, int H, int N);
uint8_t *botHat(uint8_t *image, int W, int H, int N);

// clear non 4-connected pixels
uint8_t *filter4(uint8_t *image, int W, int H);
// clear single pixels
uint8_t *filter8(uint8_t *image, int W, int H);

size_t *cclabel4(uint8_t *Img, int W, int H, ConnComps **CC);

#if 0
size_t *cclabel8(uint8_t *Img, int W, int H, size_t *Nobj);
// logical operations
uint8_t *imand(uint8_t *im1, uint8_t *im2, int W, int H);
uint8_t *substim(uint8_t *im1, uint8_t *im2, int W, int H);
#endif

#endif // BINMORPH_H__
