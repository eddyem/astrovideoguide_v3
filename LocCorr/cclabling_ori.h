/*
 * cclabling_1.h - inner part of functions cclabel4 and cclabel8
 *
 * Copyright 2015 Edward V. Emelianoff <eddy@sao.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

//double t0 = dtime();

    size_t N = 0, // current label
    Nmax = W*H/4; // max number of labels
    int w = W - 1, h = H - 1;
    int y;
    size_t *assoc = MALLOC(size_t, Nmax); // allocate memory for "remark" array
    size_t last_assoc_idx = 0; // last index filled in assoc array
    size_t currentnum = 0; // current pixel number
    inline void remark(size_t old, size_t new){ // remark in assoc[] pixel with value old to assoc[new] or vice versa
        size_t New = assoc[new], Old = assoc[old];
        if(Old == New){
            return;
        }
        // now we must check Old: if  Old<New we should swap them
        if(Old < New){
            register size_t _tmp_ = Old; Old = New; New = _tmp_; // swap values
            _tmp_ = old; old = new; new = _tmp_;
        }
        // decrement counters for current value (because we make merging)
        --currentnum;
        for(size_t i = 1; i < last_assoc_idx; ++i){
            size_t m = assoc[i];
            if(m < Old) continue; // lower values
            if(m == Old){ // change all old markers to new
                assoc[i] = New;
            }else{ // decrement all higher values
                --assoc[i];
            }
        }
    }
    size_t *ptr = labels;
    for(y = 0; y < H; y++){
        bool found = false;
        size_t curmark = 0; // mark of pixel to the left
        for(int x = 0; x < W; x++, ptr++){
            size_t curval = *ptr;
            if(!curval){ found = false; continue;}
            size_t *U = (y) ? &ptr[-W] : NULL;
            size_t upmark = 0; // mark from line above
            if(!found){ // new pixel, check neighbours above
                found = true;
                // now check neighbours in upper string:
                if(U){
                    #ifdef LABEL_8
                    if(x && U[-1]){ // there is something in upper left corner -> use its value
                        upmark = U[-1];
                    }else // check point above only if there's nothing in left up
                    #endif
                        if(U[0]) upmark = U[0];
                    #ifdef LABEL_8
                    if(x < w && U[1]){ // there's something in upper right
                        if(upmark){ // to the left of it was pixels
                            remark(U[1], upmark);
                        }else
                            upmark = U[1];
                    }
                    #endif
                }
                if(!upmark){ // there's nothing above - set current pixel to incremented counter
                    #ifdef LABEL_8  // check, whether pixel is not single
                    size_t *D = (y < h) ? &ptr[W] : NULL;
                    if(  !(x && ((D && D[-1]) /*|| ptr[-1]*/))   // no left neighbours
                      && !(x < w && ((D && D[1]) || ptr[1])) // no right neighbours
                      && !(D && D[0])){ // no neighbour down
                        *ptr = 0; // erase this hermit!
                        continue;
                    }
                    #else
                    // no neighbour down & neighbour to the right -> hermit
                    if((y < h && ptr[W] == 0) && (x < w && ptr[1] == 0)){
                        *ptr = 0; // erase this hermit!
                        continue;
                    }
                    #endif
                    upmark = ++N;
                    assoc[upmark] = ++currentnum; // refresh "assoc"
                    last_assoc_idx = upmark + 1;
                }
                *ptr = upmark;
                curmark = upmark;
            }else{ // there was something to the left -> we must chek only U[1]
                if(U){
                    if(x < w && U[1]){ // there's something in upper right
                        remark(U[1], curmark);
                    }
                }
                *ptr = curmark;
            }
        }
    }
#if 0
    // Step 2: rename markers
    // first correct complex assotiations in assoc
    OMP_FOR()
    for(y = 0; y < H; y++){
        size_t *ptr = &labels[y*W];
        for(int x = 0; x < W; x++, ptr++){
            size_t p = *ptr;
            if(!p){continue;}
            *ptr = assoc[p];
        }
    }
#endif
    FREE(assoc);
    if(Nobj) *Nobj = currentnum;
//printf("%6.4f\t%zd\n", dtime()-t0, currentnum);
