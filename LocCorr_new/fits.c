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
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "debug.h"
#include "fits.h"

static int fitsstatus = 0;

/*
 * Macros for error processing when working with cfitsio functions
 */
#define TRYFITS(f, ...)							\
do{ fitsstatus = 0;								\
    f(__VA_ARGS__, &fitsstatus);				\
    if(fitsstatus){								\
        fits_report_error(stderr, fitsstatus);	\
        return FALSE;}							\
}while(0)
#define FITSFUN(f, ...)							\
do{ fitsstatus = 0;								\
    int ret = f(__VA_ARGS__, &fitsstatus);		\
    if(ret || fitsstatus)						\
        fits_report_error(stderr, fitsstatus);	\
}while(0)
#define WRITEKEY(...)							\
do{ fitsstatus = 0;								\
    fits_write_key(__VA_ARGS__, &fitsstatus);	\
    if(status) fits_report_error(stderr, status);\
}while(0)

// I->data should be allocated!!!
static inline void convflt2ima(float *f, Image *I){
    if(!I || !I->data || !f) return;
    float min = *f, max = min;
    int wh = I->height * I->width;
    #pragma omp parallel shared(min, max)
    {
        float min_p = min, max_p = min;
        #pragma omp for nowait
        for(int i = 0; i < wh; ++i){
            if(f[i] < min_p) min_p = f[i];
            else if(f[i] > max_p) max_p = f[i];
        }
        #pragma omp critical
        {
            if(min > min_p) min = min_p;
            if(max < max_p) max = max_p;
        }
    }
    float W = 255./(max - min);
    #pragma omp for
    for(int i = 0; i < wh; ++i){
         I->data[i] = (Imtype)(W*(f[i] - min));
    }
    I->maxval = 255;
    I->minval = 0;
}

bool FITS_read(const char *filename, Image **fits){
    FNAME();
    bool ret = TRUE;
    fitsfile *fp;
    int hdunum;
    int naxis, dtype;
    long naxes[2];
    Image *img = MALLOC(Image, 1);

    TRYFITS(fits_open_file, &fp, filename, READONLY);
    FITSFUN(fits_get_num_hdus, fp, &hdunum);
    DBG("Got %d HDUs", hdunum);
    if(hdunum < 1){
        WARNX(_("Can't read HDU"));
        ret = FALSE;
        goto returning;
    }
    // get image dimensions
    TRYFITS(fits_get_img_param, fp, 2, &dtype, &naxis, naxes);
    DBG("Image have %d axis", naxis);
    if(naxis > 2){
        WARNX(_("Images with > 2 dimensions are not supported"));
        ret = FALSE;
        goto returning;
    }
    img->width = naxes[0];
    img->height = naxes[1];
    DBG("got image %ldx%ld pix, bitpix=%d", naxes[0], naxes[1], dtype);
    size_t sz = naxes[0] * naxes[1];
    img->data = MALLOC(Imtype, sz);
    float *targ = MALLOC(float, sz);
    int stat = 0;
    TRYFITS(fits_read_img, fp, TFLOAT, 1, sz, NULL, targ, &stat);
    if(stat) WARNX(_("Found %d pixels with undefined value"), stat);
    convflt2ima(targ, img);
    FREE(targ);
    DBG("ready");

returning:
    FITSFUN(fits_close_file, fp);
    if(!ret || !fits){
        Image_free(&img);
    }else *fits = img;
    return ret;
}
