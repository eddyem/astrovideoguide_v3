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

// simplest interface to draw lines & ellipsis
#include "draw.h"
#include "fits.h"

#include <usefull_macros.h>

// base colors:
const uint8_t
    C_R[] = {255, 0, 0},
    C_G[] = {0, 255, 0},
    C_B[] = {0, 0, 255},
    C_K[] = {0, 0, 0},
    C_W[] = {255,255,255};

void Img3_free(Img3 **im){
    if(!im || !*im) return;
    FREE((*im)->data);
    FREE(*im);
}

void Pattern_free(Pattern **p){
    if(!p || !*p) return;
    FREE((*p)->data);
    FREE(*p);
}

// make a single-channel (opaque) mask for cross; allocated here!!!
Pattern *Pattern_cross(int h, int w){
    int s = h*w, hmid = h/2, wmid = w/2;
    uint8_t *data = MALLOC(uint8_t, s);
    uint8_t *ptr = &data[wmid];
    for(int y = 0; y < h; ++y, ptr += w) *ptr = 255;
    ptr = &data[hmid*w];
    for(int x = 0; x < w; ++x, ++ptr) *ptr = 255;
    Pattern *p = MALLOC(Pattern, 1);
    p->data = data;
    p->h = h; p->w = w;
    return p;
}

/**
 * @brief draw3_pattern - draw pattern @ 3-channel image
 * @param img (io)    - image
 * @param p (i)       - the pattern
 * @param xc, yc      - coordinates of pattern center @ image
 * @param colr        - color to draw pattern (when opaque == 255)
 */
void Pattern_draw3(Img3 *img, Pattern *p, int xc, int yc, const uint8_t colr[]){
    int xul = xc - p->w/2, yul = yc - p->h/2;
    int xdr = xul+p->w-1, ydr = yul+p->h-1;
    int R = img->w, D = img->h; // right and down border coordinates + 1
    if(ydr < 0 || xdr < 0 || xul > R-1 || yul > D-1) return; // box outside of image

    int oxlow, oxhigh, oylow, oyhigh; // output limit coordinates
    int ixlow, iylow; // intput limit coordinates
    if(xul < 0){
        oxlow = 0; ixlow = -xul;
    }else{
        oxlow = xul; ixlow = 0;
    }
    if(yul < 0){
        oylow = 0; iylow = -yul;
    }else{
        oylow = yul; iylow = 0;
    }
    if(xdr < R){
        oxhigh = xdr;
    }else{
        oxhigh = R;
    }
    if(ydr < D){
        oyhigh = ydr;
    }else{
        oyhigh = D;
    }
    OMP_FOR()
    for(int y = oylow; y < oyhigh; ++y){
        uint8_t *in = &p->data[(iylow+y-oylow)*p->w + ixlow]; // opaque component
        uint8_t *out = &img->data[(y*img->w + oxlow)*3]; // 3-colours
        for(int x = oxlow; x < oxhigh; ++x, ++in, out += 3){
            float opaque = *in/255.;
            for(int c = 0; c < 3; ++c){
                out[c] = (uint8_t)(colr[c] * opaque + out[c]*(1.-opaque));
            }
        }
    }
}
