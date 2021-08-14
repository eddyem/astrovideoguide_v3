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
#ifndef IMAGEFILE_H__
#define IMAGEFILE_H__

#include <stdint.h>

#include "fits.h"

// input file/directory type
typedef enum{
    T_WRONG,
    T_DIRECTORY,
    T_BMP,
    T_FITS,
    T_GZIP,
    T_GIF,
    T_JPEG,
    T_PNG,
    T_CAPT_GRASSHOPPER, // capture grasshopper
    T_CAPT_BASLER
} InputType;

void Image_minmax(Image *I);
uint8_t *linear(const Image *I, int nchannels);
uint8_t *equalize(const Image *I, int nchannels, double throwpart);
InputType chkinput(const char *name);
Image *Image_read(const char *name);
Image *Image_new(int w, int h);
Image *Image_sim(const Image *i);
void Image_free(Image **I);
int Image_write_jpg(const Image *I, const char *name, int equalize);

Image *u8toImage(uint8_t *data, int width, int height, int stride);
Image *bin2Im(uint8_t *image, int W, int H);
uint8_t *Im2bin(const Image *im, Imtype bk);
size_t *bin2ST(uint8_t *image, int W, int H);
Image *ST2Im(size_t *image, int W, int H);

#endif // IMAGEFILE_H__
