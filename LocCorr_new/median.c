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

// FOR MEDIATOR:
// Copyright (c) 2011 ashelly.myopenid.com under <http://www.opensource.org/licenses/mit-license>


// TODO: resolve problem with borders

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "imagefile.h"
#include "median.h"


#define ELEM_SWAP(a, b) {register Imtype t = a; a = b; b = t;}
#define PIX_SORT(a, b)  {if (p[a] > p[b]) ELEM_SWAP(p[a], p[b]);}

static inline Imtype mean(Imtype a, Imtype b){
    register uint16_t x = ((uint16_t)a + (uint16_t)b) / 2;
    return (Imtype)x;
}

static Imtype opt_med2(Imtype *p){
    return mean(p[0], p[1]);
}
static Imtype opt_med3(Imtype *p){
    PIX_SORT(0, 1); PIX_SORT(1, 2); PIX_SORT(0, 1);
    return(p[1]) ;
}
static Imtype opt_med4(Imtype *p){
    PIX_SORT(0, 2); PIX_SORT(1, 3);
    PIX_SORT(0, 1); PIX_SORT(2, 3);
    return mean(p[1], p[2]);
}
static Imtype opt_med5(Imtype *p){
    PIX_SORT(0, 1); PIX_SORT(3, 4); PIX_SORT(0, 3);
    PIX_SORT(1, 4); PIX_SORT(1, 2); PIX_SORT(2, 3) ;
    PIX_SORT(1, 2);
    return(p[2]) ;
}
// even values are from "FAST, EFFICIENT MEDIAN FILTERS WITH EVEN LENGTH WINDOWS", J.P. HAVLICEK, K.A. SAKADY, G.R.KATZ
static Imtype opt_med6(Imtype *p){
    PIX_SORT(1, 2); PIX_SORT(3, 4);
    PIX_SORT(0, 1); PIX_SORT(2, 3); PIX_SORT(4, 5);
    PIX_SORT(1, 2); PIX_SORT(3, 4);
    PIX_SORT(0, 1); PIX_SORT(2, 3); PIX_SORT(4, 5);
    PIX_SORT(1, 2); PIX_SORT(3, 4);
    return mean(p[2], p[3]);
}
static Imtype opt_med7(Imtype *p){
    PIX_SORT(0, 5); PIX_SORT(0, 3); PIX_SORT(1, 6);
    PIX_SORT(2, 4); PIX_SORT(0, 1); PIX_SORT(3, 5);
    PIX_SORT(2, 6); PIX_SORT(2, 3); PIX_SORT(3, 6);
    PIX_SORT(4, 5); PIX_SORT(1, 4); PIX_SORT(1, 3);
    PIX_SORT(3, 4); return (p[3]);
}
// optimal Batcher's sort for 8 elements (http://myopen.googlecode.com/svn/trunk/gtkclient_tdt/include/fast_median.h)
static Imtype opt_med8(Imtype *p){
    PIX_SORT(0, 4); PIX_SORT(1, 5); PIX_SORT(2, 6);
    PIX_SORT(3, 7); PIX_SORT(0, 2); PIX_SORT(1, 3);
    PIX_SORT(4, 6); PIX_SORT(5, 7); PIX_SORT(2, 4);
    PIX_SORT(3, 5); PIX_SORT(0, 1); PIX_SORT(2, 3);
    PIX_SORT(4, 5); PIX_SORT(6, 7); PIX_SORT(1, 4);
    PIX_SORT(3, 6);
    return mean(p[3], p[4]);
}
static Imtype opt_med9(Imtype *p){
    PIX_SORT(1, 2); PIX_SORT(4, 5); PIX_SORT(7, 8);
    PIX_SORT(0, 1); PIX_SORT(3, 4); PIX_SORT(6, 7);
    PIX_SORT(1, 2); PIX_SORT(4, 5); PIX_SORT(7, 8);
    PIX_SORT(0, 3); PIX_SORT(5, 8); PIX_SORT(4, 7);
    PIX_SORT(3, 6); PIX_SORT(1, 4); PIX_SORT(2, 5);
    PIX_SORT(4, 7); PIX_SORT(4, 2); PIX_SORT(6, 4);
    PIX_SORT(4, 2); return(p[4]);
}
static Imtype opt_med16(Imtype *p){
    PIX_SORT(0, 8); PIX_SORT(1, 9); PIX_SORT(2, 10); PIX_SORT(3, 11);
    PIX_SORT(4, 12); PIX_SORT(5, 13); PIX_SORT(6, 14); PIX_SORT(7, 15);
    PIX_SORT(0, 4); PIX_SORT(1, 5); PIX_SORT(2, 6); PIX_SORT(3, 7);
    PIX_SORT(8, 12); PIX_SORT(9, 13); PIX_SORT(10, 14); PIX_SORT(11, 15);
    PIX_SORT(4, 8); PIX_SORT(5, 9); PIX_SORT(6, 10); PIX_SORT(7, 11);
    PIX_SORT(0, 2); PIX_SORT(1, 3); PIX_SORT(4, 6); PIX_SORT(5, 7);
    PIX_SORT(8, 10); PIX_SORT(9, 11); PIX_SORT(12, 14); PIX_SORT(13, 15);
    PIX_SORT(2, 8); PIX_SORT(3, 9); PIX_SORT(6, 12); PIX_SORT(7, 13);
    PIX_SORT(2, 4); PIX_SORT(3, 5); PIX_SORT(6, 8); PIX_SORT(7, 9);
    PIX_SORT(10, 12); PIX_SORT(11, 13); PIX_SORT(0, 1); PIX_SORT(2, 3);
    PIX_SORT(4, 5); PIX_SORT(6, 7); PIX_SORT(8, 9); PIX_SORT(10, 11);
    PIX_SORT(12, 13); PIX_SORT(14, 15); PIX_SORT(1, 8); PIX_SORT(3, 10);
    PIX_SORT(5, 12); PIX_SORT(7, 14); PIX_SORT(5, 8); PIX_SORT(7, 10);
    return mean(p[7], p[8]);
}
static Imtype opt_med25(Imtype *p){
    PIX_SORT(0, 1)  ; PIX_SORT(3, 4)  ; PIX_SORT(2, 4) ;
    PIX_SORT(2, 3)  ; PIX_SORT(6, 7)  ; PIX_SORT(5, 7) ;
    PIX_SORT(5, 6)  ; PIX_SORT(9, 10) ; PIX_SORT(8, 10) ;
    PIX_SORT(8, 9)  ; PIX_SORT(12, 13); PIX_SORT(11, 13) ;
    PIX_SORT(11, 12); PIX_SORT(15, 16); PIX_SORT(14, 16) ;
    PIX_SORT(14, 15); PIX_SORT(18, 19); PIX_SORT(17, 19) ;
    PIX_SORT(17, 18); PIX_SORT(21, 22); PIX_SORT(20, 22) ;
    PIX_SORT(20, 21); PIX_SORT(23, 24); PIX_SORT(2, 5) ;
    PIX_SORT(3, 6)  ; PIX_SORT(0, 6)  ; PIX_SORT(0, 3) ;
    PIX_SORT(4, 7)  ; PIX_SORT(1, 7)  ; PIX_SORT(1, 4) ;
    PIX_SORT(11, 14); PIX_SORT(8, 14) ; PIX_SORT(8, 11) ;
    PIX_SORT(12, 15); PIX_SORT(9, 15) ; PIX_SORT(9, 12) ;
    PIX_SORT(13, 16); PIX_SORT(10, 16); PIX_SORT(10, 13) ;
    PIX_SORT(20, 23); PIX_SORT(17, 23); PIX_SORT(17, 20) ;
    PIX_SORT(21, 24); PIX_SORT(18, 24); PIX_SORT(18, 21) ;
    PIX_SORT(19, 22); PIX_SORT(8, 17) ; PIX_SORT(9, 18) ;
    PIX_SORT(0, 18) ; PIX_SORT(0, 9)  ; PIX_SORT(10, 19) ;
    PIX_SORT(1, 19) ; PIX_SORT(1, 10) ; PIX_SORT(11, 20) ;
    PIX_SORT(2, 20) ; PIX_SORT(2, 11) ; PIX_SORT(12, 21) ;
    PIX_SORT(3, 21) ; PIX_SORT(3, 12) ; PIX_SORT(13, 22) ;
    PIX_SORT(4, 22) ; PIX_SORT(4, 13) ; PIX_SORT(14, 23) ;
    PIX_SORT(5, 23) ; PIX_SORT(5, 14) ; PIX_SORT(15, 24) ;
    PIX_SORT(6, 24) ; PIX_SORT(6, 15) ; PIX_SORT(7, 16) ;
    PIX_SORT(7, 19) ; PIX_SORT(13, 21); PIX_SORT(15, 23) ;
    PIX_SORT(7, 13) ; PIX_SORT(7, 15) ; PIX_SORT(1, 9) ;
    PIX_SORT(3, 11) ; PIX_SORT(5, 17) ; PIX_SORT(11, 17) ;
    PIX_SORT(9, 17) ; PIX_SORT(4, 10) ; PIX_SORT(6, 12) ;
    PIX_SORT(7, 14) ; PIX_SORT(4, 6)  ; PIX_SORT(4, 7) ;
    PIX_SORT(12, 14); PIX_SORT(10, 14); PIX_SORT(6, 7) ;
    PIX_SORT(10, 12); PIX_SORT(6, 10) ; PIX_SORT(6, 17) ;
    PIX_SORT(12, 17); PIX_SORT(7, 17) ; PIX_SORT(7, 10) ;
    PIX_SORT(12, 18); PIX_SORT(7, 12) ; PIX_SORT(10, 18) ;
    PIX_SORT(12, 20); PIX_SORT(10, 20); PIX_SORT(10, 12) ;
    return (p[12]);
}
#undef PIX_SORT
#define PIX_SORT(a, b)  {if (a > b) ELEM_SWAP(a, b);}
/**
 * quick select - algo for approximate median calculation for array idata of size n
 */
static Imtype quick_select(Imtype *idata, int n){
    int low, high;
    int median;
    int middle, ll, hh;
    Imtype *arr = MALLOC(Imtype, n);
    memcpy(arr, idata, n*sizeof(Imtype));
    low = 0 ; high = n-1 ; median = (low + high) / 2;
    for(;;){
        if(high <= low) // One element only
            break;
        if(high == low + 1){ // Two elements only
            PIX_SORT(arr[low], arr[high]) ;
            break;
        }
        // Find median of low, middle and high Imtypes; swap into position low
        middle = (low + high) / 2;
        PIX_SORT(arr[middle], arr[high]) ;
        PIX_SORT(arr[low], arr[high]) ;
        PIX_SORT(arr[middle], arr[low]) ;
        // Swap low Imtype (now in position middle) into position (low+1)
        ELEM_SWAP(arr[middle], arr[low+1]) ;
        // Nibble from each end towards middle, swapping Imtypes when stuck
        ll = low + 1;
        hh = high;
        for(;;){
            do ll++; while (arr[low] > arr[ll]);
            do hh--; while (arr[hh] > arr[low]);
            if(hh < ll) break;
            ELEM_SWAP(arr[ll], arr[hh]) ;
        }
        // Swap middle Imtype (in position low) back into correct position
        ELEM_SWAP(arr[low], arr[hh]) ;
        // Re-set active partition
        if (hh <= median) low = ll;
        if (hh >= median) high = hh - 1;
    }
    Imtype ret = arr[median];
    FREE(arr);
    return ret;
}
#undef PIX_SORT
#undef ELEM_SWAP

/**
 * calculate median of array idata with size n
 */
Imtype calc_median(Imtype *idata, int n){
    if(!idata || n < 1){
        WARNX("calc_median(): wrong dta"); return 0.;
    }
    typedef Imtype (*medfunc)(Imtype *p);
    medfunc fn = NULL;
    const medfunc fnarr[] = {opt_med2, opt_med3, opt_med4, opt_med5, opt_med6,
            opt_med7, opt_med8, opt_med9};
    if(n == 1) return *idata;
    if(n < 10) fn = fnarr[n - 2];
    else if(n == 16) fn = opt_med16;
    else if(n == 25) fn = opt_med25;
    if(fn){
        return fn(idata);
    }else{
        return quick_select(idata, n);
    }
}

#define ImtypeLess(a,b) ((a)<(b))
#define ImtypeMean(a,b) (((a)+(b))/2)

typedef struct Mediator_t{
    Imtype* data;   // circular queue of values
    int* pos;       // index into `heap` for each value
    int* heap;      // max/median/min heap holding indexes into `data`.
    int N;          // allocated size.
    int idx;        // position in circular queue
    int ct;         // count of Imtypes in queue
} Mediator;

/*--- Helper Functions ---*/

#define minCt(m) (((m)->ct-1)/2) //count of Imtypes in minheap
#define maxCt(m) (((m)->ct)/2) //count of Imtypes in maxheap

//returns 1 if heap[i] < heap[j]
static inline int mmless(Mediator* m, int i, int j){
    return ImtypeLess(m->data[m->heap[i]],m->data[m->heap[j]]);
}

//swaps Imtypes i&j in heap, maintains indexes
static inline int mmexchange(Mediator* m, int i, int j){
    int t = m->heap[i];
    m->heap[i] = m->heap[j];
    m->heap[j] = t;
    m->pos[m->heap[i]] = i;
    m->pos[m->heap[j]] = j;
    return 1;
}

//swaps Imtypes i&j if i<j; returns true if swapped
static inline int mmCmpExch(Mediator* m, int i, int j){
    return (mmless(m,i,j) && mmexchange(m,i,j));
}

//maintains minheap property for all Imtypes below i/2.
static inline void minSortDown(Mediator* m, int i){
    for(; i <= minCt(m); i*=2){
        if(i>1 && i < minCt(m) && mmless(m, i+1, i)) ++i;
        if(!mmCmpExch(m,i,i/2)) break;
    }
}

//maintains maxheap property for all Imtypes below i/2. (negative indexes)
static inline void maxSortDown(Mediator* m, int i){
    for(; i >= -maxCt(m); i*=2){
        if(i<-1 && i > -maxCt(m) && mmless(m, i, i-1)) --i;
    if(!mmCmpExch(m,i/2,i)) break;
    }
}

//maintains minheap property for all Imtypes above i, including median
//returns true if median changed
static inline int minSortUp(Mediator* m, int i){
    while (i > 0 && mmCmpExch(m, i, i/2)) i /= 2;
    return (i == 0);
}

//maintains maxheap property for all Imtypes above i, including median
//returns true if median changed
static inline int maxSortUp(Mediator* m, int i){
    while (i < 0 && mmCmpExch(m, i/2, i)) i /= 2;
    return (i == 0);
}

/*--- Public Interface ---*/

//creates new Mediator: to calculate `nImtypes` running median.
//mallocs single block of memory, caller must free.
static Mediator* MediatorNew(int nImtypes){
    int size = sizeof(Mediator) + nImtypes*(sizeof(Imtype)+sizeof(int)*2);
    Mediator* m = malloc(size);
    m->data = (Imtype*)(m + 1);
    m->pos = (int*) (m->data + nImtypes);
    m->heap = m->pos + nImtypes + (nImtypes / 2); //points to middle of storage.
    m->N = nImtypes;
    m->ct = m->idx = 0;
    while (nImtypes--){ //set up initial heap fill pattern: median,max,min,max,...
        m->pos[nImtypes] = ((nImtypes+1)/2) * ((nImtypes&1)? -1 : 1);
        m->heap[m->pos[nImtypes]] = nImtypes;
    }
    return m;
}

//Inserts Imtype, maintains median in O(lg nImtypes)
static void MediatorInsert(Mediator* m, Imtype v){
    int isNew=(m->ct<m->N);
    int p = m->pos[m->idx];
    Imtype old = m->data[m->idx];
    m->data[m->idx]=v;
    m->idx = (m->idx+1) % m->N;
    m->ct+=isNew;
    if(p>0){ //new Imtype is in minHeap
        if (!isNew && ImtypeLess(old,v)) minSortDown(m,p*2);
        else if (minSortUp(m,p)) maxSortDown(m,-1);
    }else if (p<0){ //new Imtype is in maxheap
        if (!isNew && ImtypeLess(v,old)) maxSortDown(m,p*2);
        else if (maxSortUp(m,p)) minSortDown(m, 1);
    }else{ //new Imtype is at median
        if (maxCt(m)) maxSortDown(m,-1);
        if (minCt(m)) minSortDown(m, 1);
    }
}

//returns median Imtype (or average of 2 when Imtype count is even)
static Imtype MediatorMedian(Mediator* m){
    Imtype v = m->data[m->heap[0]];
    if ((m->ct&1) == 0) v = ImtypeMean(v, m->data[m->heap[-1]]);
    return v;
}

#if 0
// median + min/max
static Imtype MediatorStat(Mediator* m, Imtype *minval, Imtype *maxval){
    Imtype v = m->data[m->heap[0]];
    if ((m->ct&1) == 0) v = ImtypeMean(v, m->data[m->heap[-1]]);
    Imtype min = v, max = v;
    int i;
    for(i = -maxCt(m); i < 0; ++i){
        int v = m->data[m->heap[i]];
        if(v < min) min = v;
    }
    *minval = min;
    for(i = 1; i <= minCt(m); ++i){
        int v = m->data[m->heap[i]];
        if(v > max) max = v;
    }
    *maxval = max;
    return v;
}
#endif

/**
 * filter image by median (seed*2 + 1) x (seed*2 + 1)
 */
Image *get_median(const Image *img, int seed){
    if(seed < 1) return NULL;
    size_t w = img->width, h = img->height;
    Image *out = Image_sim(img);
    Imtype *med = out->data, *inputima = img->data;

    size_t blksz = seed * 2 + 1, fullsz = blksz * blksz;
#ifdef EBUG
    double t0 = dtime();
#endif
    OMP_FOR(shared(inputima, med))
    for(size_t x = seed; x < w - seed; ++x){
        size_t xx, yy, xm = x + seed + 1, y, ymax = blksz - 1, xmin = x - seed;
        Mediator* m = MediatorNew(fullsz);
        // initial fill
        for(yy = 0; yy < ymax; ++yy)
            for(xx = xmin; xx < xm; ++xx)
                MediatorInsert(m, inputima[xx + yy*w]);
        ymax = 2*seed*w;
        xmin += ymax;
        xm += ymax;
        ymax = h - seed;
        size_t medidx = x + seed * w;
        for(y = seed; y < ymax; ++y, xmin += w, xm += w, medidx += w){
            for(xx = xmin; xx < xm; ++xx)
                MediatorInsert(m, inputima[xx]);
            med[medidx] = MediatorMedian(m);
        }
        free(m);
    }
    Image_minmax(out);
    DBG("time for median filtering %zdx%zd of image %zdx%zd: %gs", blksz, blksz, w, h,
        dtime() - t0);
    return out;
}

/**
 * @brief get_stat - calculate floating statistics in (seed*2+1)^2
 * @param in (i) - input image
 * @param seed - radius of box
 * @param mean (o) - mean by box (excluding borders)
 * @param std  (o) - STD by box (excluding borders)
 * @retur 0 if error
 */
int get_stat(const Image *in, int seed, Image **mean, Image **std){
    if(!in) return FALSE;
    if(seed < 1 || seed > (in->width - 1)/2 || seed > (in->height - 1)/2) return FALSE;
#ifdef EBUG
    double t0 = dtime();
#endif
    Image *M = NULL, *S = NULL;
    if(mean) M = Image_sim(in);
    if(std) S = Image_sim(in);
    int ymax = in->height - seed, xmax = in->width - seed;
    int hsz = (seed*2 + 1), sz = hsz * hsz, w = in->width;
    OMP_FOR()
    for(int y = seed; y < ymax; ++y){ // dumb calculations
        int startidx = y*w + seed;
        Imtype *om = (M) ? &M->data[startidx] : NULL;
        Imtype *os = (S) ? &S->data[startidx] : NULL;
        for(int x = seed; x < xmax; ++x){
            double sum = 0, sum2 = 0;
            int yb = y + seed + 1, xm = x - seed;
            for(int yy = y - seed; yy < yb; ++yy){
                Imtype *ptr = &in->data[yy * w + xm];
                for(int xx = 0; xx < hsz; ++xx){
                    double d = *ptr++;
                    sum += d;
                    sum2 += d*d;
                }
            }
            //DBG("sum=%g, sum2=%g, sz=%d", sum, sum2, sz);
            sum /= sz;
            if(om){
                *om++ = (Imtype)sum;
                //DBG("mean (%d, %d): %g", x, y, sum);
            }
            if(os) *os++ = (Imtype)sqrt(sum2/sz - sum*sum);
        }
    }
    if(mean){
        Image_minmax(M);
        *mean = M;
    }
    if(std){
        Image_minmax(S);
        *std = S;
    }
    DBG("time for mean/sigma computation: %gs", dtime() - t0);
    return TRUE;
}
