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

// inner part of function `filter4`

#if ! defined IM_UP && ! defined IM_DOWN
OMP_FOR()
for(int y = 1; y < h; y++)
#endif
{
    uint8_t *iptr = &image[W0*y];
    uint8_t *optr = &ret[W0*y];
    // x=0
    register uint8_t inp = *iptr;
    register uint8_t p = (inp << 1) | (inp >> 1)
        #ifndef IM_UP
            | iptr[-W0]
        #endif
        #ifndef IM_DOWN
            | iptr[W0]
        #endif
    ;
    if(iptr[1] & 0x80) p |= 1;
    *optr++ = inp & p;
    ++iptr;
    for(int x = 1; x < w; ++x){
        inp = *iptr;
        p = (inp << 1) | (inp >> 1)
            #ifndef IM_UP
                | iptr[-W0]
            #endif
            #ifndef IM_DOWN
                | iptr[W0]
            #endif
        ;
        if(iptr[1] & 0x80) p |= 1;
        if(iptr[-1] & 1) p |= 0x80;
        *optr++ = inp & p;
        ++iptr;
    }
    // x = W0-1
    inp = *iptr;
    p = (inp << 1) | (inp >> 1)
        #ifndef IM_UP
            | iptr[-W0]
        #endif
        #ifndef IM_DOWN
            | iptr[W0]
        #endif
    ;
    if(iptr[-1] & 1) p |= 0x80;
    *optr = inp & p;
}
