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
#ifndef FITS_H__
#define FITS_H__

#include <fitsio.h>
#include <omp.h>
#include <stdio.h>
#include <stdbool.h>

#define Stringify(x) #x
#define OMP_FOR(x) _Pragma(Stringify(omp parallel for x))


typedef float Imtype; // maybe float or double only
// this is TFLOAT or TDOUBLE depending on Imtype
#define FITSDATATYPE    TFLOAT
/*
typedef double Imtype;
#define FITSDATATYPE    TDOUBLE
*/

typedef struct{
    int width;			// width
    int height;			// height
    int dtype;			// data type for image storage
    Imtype *data;		// picture data
    Imtype minval;      // extremal data values
    Imtype maxval;
    char **keylist;		// list of options for each key
    size_t keynum;		// full number of keys (size of *keylist)
} Image;

void Image_free(Image **ima);
bool FITS_read(const char *filename, Image **fits);
bool FITS_write(const char *filename, const Image *fits);

#endif // FITS_H__
