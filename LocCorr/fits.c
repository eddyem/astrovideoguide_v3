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
#include <usefull_macros.h>
#include <math.h>

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

void Image_free(Image **img){
    size_t i, sz = (*img)->keynum;
    char **list = (*img)->keylist;
    for(i = 0; i < sz; ++i) FREE(list[i]);
    FREE((*img)->keylist);
    FREE((*img)->data);
    FREE(*img);
}

bool FITS_read(const char *filename, Image **fits){
    FNAME();
    bool ret = TRUE;
    fitsfile *fp;
    int i, j, hdunum, hdutype, nkeys, keypos;
    int naxis;
    long naxes[2];
    char card[FLEN_CARD];
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
    TRYFITS(fits_get_img_param, fp, 2, &img->dtype, &naxis, naxes);
    DBG("Image have %d axis", naxis);
    if(naxis > 2){
        WARNX(_("Images with > 2 dimensions are not supported"));
        ret = FALSE;
        goto returning;
    }
    img->width = naxes[0];
    img->height = naxes[1];
    DBG("got image %ldx%ld pix, bitpix=%d", naxes[0], naxes[1], img->dtype);
    // loop through all HDUs
    for(i = 1; !(fits_movabs_hdu(fp, i, &hdutype, &fitsstatus)); ++i){
        TRYFITS(fits_get_hdrpos, fp, &nkeys, &keypos);
        int oldnkeys = img->keynum;
        img->keynum += nkeys;
        if(!(img->keylist = realloc(img->keylist, sizeof(char*) * img->keynum))){
            ERR(_("Can't realloc"));
        }
        char **currec = &(img->keylist[oldnkeys]);
        DBG("HDU # %d of %d keys", i, nkeys);
        for(j = 1; j <= nkeys; ++j){
            FITSFUN(fits_read_record, fp, j, card);
            if(!fitsstatus){
                *currec = MALLOC(char, FLEN_CARD);
                memcpy(*currec, card, FLEN_CARD);
                DBG("key %d: %s", oldnkeys + j, *currec);
                ++currec;
            }
        }
    }
    if(fitsstatus == END_OF_FILE){
        fitsstatus = 0;
    }else{
        fits_report_error(stderr, fitsstatus);
        ret = FALSE;
        goto returning;
    }
    size_t sz = naxes[0] * naxes[1];
    img->data = MALLOC(Imtype, sz);
    int stat = 0;
    TRYFITS(fits_read_img, fp, FITSDATATYPE, 1, sz, NULL, img->data, &stat);
    Imtype *d = img->data, min = *d, max = *d;
    for(size_t x = 0; x < sz; ++x){
        if(d[x] > max) max = d[x];
        else if(d[x] < min) min = d[x];
    }
    img->maxval = max;
    img->minval = min;
    DBG("FITS stat: min=%g, max=%g", min, max);
    if(stat) WARNX(_("Found %d pixels with undefined value"), stat);
    DBG("ready");

returning:
    FITSFUN(fits_close_file, fp);
    if(!ret){
        Image_free(&img);
    }
    if(fits) *fits = img;
    return ret;
}

bool FITS_write(const char *filename, const Image *fits){
    int w = fits->width, h = fits->height;
    long naxes[2] = {w, h};
    size_t sz = w * h, keys = fits->keynum;
    fitsfile *fp;

    TRYFITS(fits_create_file, &fp, filename);
    TRYFITS(fits_create_img, fp, fits->dtype, 2, naxes);
    Imtype *outp = fits->data;
    bool need2free = FALSE;
    if(fits->dtype > 0){ // convert floating data into integer
        Imtype maxval;
        Imtype minval;
        switch(fits->dtype){
            case SBYTE_IMG: // there's a bug in cfitsio, it can't save float->sbyte
                maxval = (Imtype)INT8_MAX;
                minval = (Imtype)INT8_MIN;
            break;
            case SHORT_IMG:
                maxval = (Imtype)INT16_MAX;
                minval = (Imtype)INT16_MIN;
            break;
            case USHORT_IMG:
                maxval = (Imtype)UINT16_MAX;
                minval = (Imtype)0;
            break;
            case LONG_IMG:
                maxval = (Imtype)INT32_MAX;
                minval = (Imtype)INT32_MIN;
            break;
            case ULONG_IMG:
                maxval = (Imtype)UINT32_MAX;
                minval = (Imtype)0;
            break;
            case ULONGLONG_IMG:
                maxval = (Imtype)UINT64_MAX;
                minval = (Imtype)0;
            break;
            case LONGLONG_IMG:
                maxval = (Imtype)INT64_MAX;
                minval = (Imtype)INT64_MIN;
            break;
            case BYTE_IMG:
            default:  // byte
                maxval = (Imtype)UINT8_MAX;
                minval = (Imtype)0;
        }
        DBG("maxval = %g, minval = %g", maxval, minval);
        int w = fits->width, h = fits->height;
        Imtype min = fits->minval, max = fits->maxval, W = (maxval - minval)/(max - min);
        outp = MALLOC(Imtype, w*h);
        OMP_FOR()
        for(int y = 0; y < h; ++y){
            Imtype *o = &outp[y*w], *i = &fits->data[y*w];
            for(int x = 0; x < w; ++x, ++o, ++i){
                *o = W*((*i) - min) + minval;
                //if(*o < minval || *o > maxval) red("o: %g\n", *o);
                //if(*o < minval) *o = minval;
                //else if(*o > maxval) *o = maxval;
            }
        }
        need2free = TRUE;
        DBG("converted");
    }
    if(keys){ // there's keys
        size_t i;
        char **records = fits->keylist;
        for(i = 0; i < keys; ++i){
            char *rec = records[i];
            if(strncmp(rec, "SIMPLE", 6) == 0 || strncmp(rec, "EXTEND", 6) == 0) // key "file does conform ..."
                continue;
            else if(strncmp(rec, "COMMENT", 7) == 0) // comment of obligatory key in FITS head
                continue;
            else if(strncmp(rec, "NAXIS", 5) == 0 || strncmp(rec, "BITPIX", 6) == 0) // NAXIS, NAXISxxx, BITPIX
                continue;
            FITSFUN(fits_write_record, fp, rec);
        }
    }
    FITSFUN(fits_write_record, fp, "COMMENT  modified by loccorr");
    fitsstatus = 0;
    fits_write_img(fp, FITSDATATYPE, 1, sz, outp, &fitsstatus);
    if(need2free) FREE(outp);
    if(fitsstatus){
        fits_report_error(stderr, fitsstatus);
        return FALSE;
    }
    TRYFITS(fits_close_file, fp);
    return TRUE;
}
