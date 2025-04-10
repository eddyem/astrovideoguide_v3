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

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include "basler.h"
#include "cameracapture.h"
#include "cmdlnopts.h"
#include "config.h"
#include "debug.h"
#include "draw.h"
#include "fits.h"
#include "grasshopper.h"
#include "hikrobot.h"
#include "imagefile.h"
#include "median.h"

typedef struct{
    const char signature[8];
    uint8_t len;
    InputType it;
} imsign;

const imsign signatures[] = {
    {"BM", 2, T_BMP},
    {"SIMPLE", 6, T_FITS},
    {{0x1f, 0x8b, 0x08}, 3, T_GZIP},
    {"GIF8", 4, T_GIF},
    {{0xff, 0xd8, 0xff, 0xdb}, 4, T_JPEG},
    {{0xff, 0xd8, 0xff, 0xe0}, 4, T_JPEG},
    {{0xff, 0xd8, 0xff, 0xe1}, 4, T_JPEG},
    {{0x89, 0x50, 0x4e, 0x47}, 4, T_PNG},
   // {{0x49, 0x49, 0x2a, 0x00}, 4, T_TIFF},
    {"", 0, T_WRONG}
};

#ifdef EBUG
static char *hexdmp(const char sig[8]){
    static char buf[128];
    char *bptr = buf;
    bptr += sprintf(bptr, "[ ");
    for(int i = 0; i < 7; ++i){
        bptr += sprintf(bptr, "%02X ", (uint8_t)sig[i]);
    }
    bptr += sprintf(bptr, "]");
    return buf;
}
#endif

/**
 * @brief imtype - check image type of given file
 * @param f - opened image file structure
 * @return image type or T_WRONG
 */
static InputType imtype(FILE *f){
    char signature[8];
    int x = fread(signature, 1, 7, f);
    DBG("x=%d", x);
    if(7 != x){
        WARN("Can't read file signature");
        return T_WRONG;
    }
    signature[7] = 0;
    const imsign *s = signatures;
    DBG("Got signature: %s (%s)", hexdmp(signature), signature);
    while(s->len){
        DBG("Check %s", s->signature);
        if(0 == memcmp(s->signature, signature, s->len)){
            DBG("Found signature %s", s->signature);
            return s->it;
        }
        ++s;
    }
    return T_WRONG;
}

/**
 * @brief chkinput - check file/directory name
 * @param name - name of file or directory
 * @return type of `name`
 */
InputType chkinput(const char *name){
    DBG("input name: %s", name);
#ifdef FLYCAP_FOUND
    if(0 == strcmp(name, GRASSHOPPER_CAPT_NAME)) return T_CAPT_GRASSHOPPER;
#endif
#ifdef BASLER_FOUND
    if(0 == strcmp(name, BASLER_CAPT_NAME)) return T_CAPT_BASLER;
#endif
#ifdef MVS_FOUND
    if(0 == strcmp(name, HIKROBOT_CAPT_NAME)) return T_CAPT_HIKROBOT;
#endif
    struct stat fd_stat;
    stat(name, &fd_stat);
    if(S_ISDIR(fd_stat.st_mode)){
        DBG("%s is a directory", name);
        DIR *d = opendir(name);
        if(!d){
            WARN("Can't open directory %s", name);
            return T_WRONG;
        }
        closedir(d);
        return T_DIRECTORY;
    }
    FILE *f = fopen(name, "r");
    if(!f){
        WARN("Can't open file %s", name);
        return T_WRONG;
    }
    InputType tp = imtype(f);
    DBG("Image type of %s is %d", name, tp);
    fclose(f);
    return tp;
}

/**
 * @brief u8toImage - convert uint8_t data to Image structure (flipping upside down for FITS coordinates)
 * @param data      - original image data
 * @param width     - image width
 * @param height    - image height
 * @param stride    - image width with alignment
 * @return Image structure (fully allocated, you can FREE(data) after it)
 */
Image *u8toImage(const uint8_t *data, int width, int height, int stride){
    //FNAME();
    Image *outp = Image_new(width, height);
    // flip image updown for FITS coordinate system
    OMP_FOR()
    for(int y = 0; y < height; ++y){
        Imtype *Out = &outp->data[(height-1-y)*width];
        const uint8_t *In = &data[y*stride];
        for(int x = 0; x < width; ++x){
            *Out++ = (Imtype)(*In++);
        }
    }
    Image_minmax(outp);
    return outp;
}

/**
 * @brief im_load - load image file
 * @param name - filename
 * @return Image structure or NULL
 */
static inline Image *im_load(const char *name){
    int width, height, channels;
    uint8_t *img = stbi_load(name, &width, &height, &channels, 1);
    if(!img){
        WARNX("Error in loading the image %s\n", name);
        return NULL;
    }
    Image *I = u8toImage(img, width, height, width);
    free(img);
    return I;
}

/**
 * @brief Image_read - read image from any supported file type
 * @param name - path to image
 * @return image or NULL if failed
 */
Image *Image_read(const char *name){
    InputType tp = chkinput(name);
    if(tp == T_DIRECTORY || tp == T_WRONG){
        WARNX("Bad file type to read");
        return NULL;
    }
    Image *outp = NULL;
    if(tp == T_FITS || tp == T_GZIP){
        if(!FITS_read(name, &outp)){
            WARNX("Can't read %s", name);
            return NULL;
        }
    }else outp = im_load(name);
    return outp;
}

/**
 * @brief Image_new - allocate memory for new struct Image & Image->data
 * @param w, h - image size
 * @return data allocated here
 */
Image *Image_new(int w, int h){
    if(w < 1 || h < 1) return NULL;
    Image *outp = MALLOC(Image, 1);
    outp->width = w;
    outp->height = h;
    outp->data = MALLOC(Imtype, w*h);
    return outp;
}

/**
 * @brief Image_sim - allocate memory for new empty Image with similar size & data type
 * @param i - sample image
 * @return data allocated here (with empty keylist & zeros in data)
 */
Image *Image_sim(const Image *i){
    if(!i) return NULL;
    Image *outp = Image_new(i->width, i->height);
    return outp;
}

/**
 * @brief get_histogram - calculate image histogram
 * @param I - orig
 * @param histo - histogram
 * @return FALSE if failed
 */
int get_histogram(const Image *I, size_t histo[HISTOSZ]){
    if(!I || !I->data || !histo) return FALSE;
    bzero(histo, HISTOSZ*sizeof(size_t));
    int wh = I->width * I->height;
#pragma omp parallel
{
    size_t histogram_private[HISTOSZ] = {0};
    #pragma omp for nowait
    for(int i = 0; i < wh; ++i){
        ++histogram_private[I->data[i]];
    }
    #pragma omp critical
    {
        for(int i = 0; i < HISTOSZ; ++i) histo[i] += histogram_private[i];
    }
}
    return TRUE;
}


/**
 * @brief calc_background - Simple background calculation by histogram
 * @param img (i) - input image (here will be modified its top2proc field)
 * @param bk (o)  - background value
 * @return 0 if error
 */
int calc_background(Image *img){
    if(!img || !img->data) return FALSE;
    if(img->maxval == img->minval){
        WARNX("Zero or overilluminated image!");
        return FALSE;
    }
    if(theconf.fixedbkg){
        if(theconf.fixedbkg < img->minval){
            WARNX("Image values too small");
            return FALSE;
        }
        img->background = theconf.fixedbkg;
        return TRUE;
    }
    size_t histogram[HISTOSZ];
    if(!get_histogram(img, histogram)) return FALSE;

    size_t modeidx = 0, modeval = 0;
    for(int i = 0; i < 256; ++i)
        if(modeval < histogram[i]){
            modeval = histogram[i];
            modeidx = i;
        }
    //DBG("Mode=%g @ idx%d (N=%d)", ((Imtype)modeidx / 255.)*ampl, modeidx, modeval);
    ssize_t diff2[256] = {0};
    for(int i = 2; i < 254; ++i) diff2[i] = (histogram[i+2]+histogram[i-2]-2*histogram[i])/4;
    //green("HISTO:\n");
    //for(int i = 0; i < 256; ++i) printf("%d:\t%d\t%d\n", i, histogram[i], diff2[i]);
    if(modeidx < 2) modeidx = 2;
    if(modeidx > 253){
        WARNX("Overilluminated image");
        return FALSE; // very bad image: overilluminated
    }
    size_t borderidx = modeidx;
    for(int i = modeidx; i < 254; ++i){ // search bend-point by second derivate
        if(diff2[i] <= 0 && diff2[i+1] <= 0){
            borderidx = i; break;
        }
    }
    //DBG("borderidx=%d -> %d", borderidx, (borderidx+modeidx)/2);
    //*bk = (borderidx + modeidx) / 2;
    img->background = borderidx;
    return TRUE;
}


/**
 * @brief linear - linear transform for preparing file to save as JPEG or other type (mirror image upside down!)
 * @param I - input image
 * @param nchannels - 1 or 3 colour channels
 * @return allocated here image for jpeg/png storing
 */
uint8_t *linear(const Image *I, int nchannels){ // only 1 and 3 channels supported!
    if(!I || !I->data || (nchannels != 1 && nchannels != 3)) return NULL;
    FNAME();
    int width = I->width, height = I->height;
    size_t stride = width*nchannels, S = height*stride;
    uint8_t *outp = MALLOC(uint8_t, S);
    float min = (float)I->minval, max = (float)I->maxval, W = 255./(max - min);
    //DBG("make linear transform %dx%d, %d channels", I->width, I->height, nchannels);
    if(nchannels == 3){
        OMP_FOR()
        for(int y = 0; y < height; ++y){
            uint8_t *Out = &outp[(height-1-y)*stride];
            Imtype *In = &I->data[y*width];
            for(int x = 0; x < width; ++x){
                Out[0] = Out[1] = Out[2] = (uint8_t)(W*((float)(*In++) - min));
                Out += 3;
            }
        }
    }else{
        OMP_FOR()
        for(int y = 0; y < height; ++y){
            uint8_t *Out = &outp[(height-1-y)*width];
            Imtype *In = &I->data[y*width];
            for(int x = 0; x < width; ++x){
                *Out++ = (uint8_t)(W*((float)(*In++) - min));
            }
        }
    }
    return outp;
}

/**
 * @brief equalize - hystogram equalization (mirror image upside down!)
 * @param I - input image
 * @param nchannels - 1 or 3 colour channels
 * @param throwpart - which part of black pixels (from all amount) to throw away
 * @return allocated here image for jpeg/png storing
 */
uint8_t *equalize(const Image *I, int nchannels, double throwpart){
    if(!I || !I->data || (nchannels != 1 && nchannels != 3)) return NULL;
    FNAME();
    int width = I->width, height = I->height;
    size_t stride = width*nchannels, S = height*stride;
    size_t orig_histo[HISTOSZ]; // original hystogram (linear)
    if(!get_histogram(I, orig_histo)) return NULL;
    uint8_t *outp = MALLOC(uint8_t, S);
    uint8_t eq_levls[256] = {0};   // levels to convert: newpix = eq_levls[oldpix]
    int s = width*height;
    int Nblack = 0, bpart = (int)(throwpart * (double)s);
    int startidx;
    // remove first part of black pixels
    for(startidx = 0; startidx < 256; ++startidx){
        Nblack += orig_histo[startidx];
        if(Nblack >= bpart) break;
    }
    ++startidx;
    /* remove last part of white pixels
    for(stopidx = 255; stopidx > startidx; --stopidx){
        Nwhite += orig_hysto[stopidx];
        if(Nwhite >= wpart) break;
    }*/
    //DBG("Throw %d (real: %d black) pixels, startidx=%d", bpart, Nblack, startidx);
    double part = (double)(s + 1. - Nblack) / 256., N = 0.;
    for(int i = startidx; i < 256; ++i){
        N += orig_histo[i];
        eq_levls[i] = (uint8_t)(N/part);
    }
    //for(int i = stopidx; i < 256; ++i) eq_levls[i] = 255;
#if 0
    DBG("Original / new histogram");
    for(int i = 0; i < 256; ++i) printf("%d\t%d\t%d\n", i, orig_hysto[i], eq_levls[i]);
#endif
    if(nchannels == 3){
        OMP_FOR()
        for(int y = 0; y < height; ++y){
            uint8_t *Out = &outp[(height-1-y)*stride];
            Imtype *In = &I->data[y*width];
            for(int x = 0; x < width; ++x){
                Out[0] = Out[1] = Out[2] = eq_levls[*In++];
                Out += 3;
            }
        }
    }else{
        OMP_FOR()
        for(int y = 0; y < height; ++y){
            uint8_t *Out = &outp[(height-1-y)*width];
            Imtype *In = &I->data[y*width];
            for(int x = 0; x < width; ++x){
                *Out++ = eq_levls[*In++];
            }
        }
    }
    return outp;
}

/**
 * @brief Image_write_jpg - save image as JPG file (flipping upside down)
 * @param I - image
 * @param name - filename
 * @param eq == 0 to write linear, != 0 to write equalized image
 * @return 0 if failed
 */
int Image_write_jpg(const Image *I, const char *name, int eq){
    if(!I || !I->data) return 0;
    uint8_t *outp = NULL;
    if(eq)
        outp = equalize(I, 1, theconf.throwpart);
    else
        outp = linear(I, 1);
    if(!outp) return 0;
    //DBG("Try to write %s", name);
    char *tmpnm = MALLOC(char, strlen(name) + 5);
    sprintf(tmpnm, "%s-tmp", name);
    int r = stbi_write_jpg(tmpnm, I->width, I->height, 1, outp, 95);
    if(r){
        if(rename(tmpnm, name)){
            WARN("rename()");
            r = 0;
        }
    }
    FREE(tmpnm);
    FREE(outp);
    return r;
}

// calculate extremal values of image data and store them in it
void Image_minmax(Image *I){
    if(!I || !I->data) return;
    Imtype min = *(I->data), max = min;
    float isum = 0.f;
    int wh = I->width * I->height;
#ifdef EBUG
    //double t0 = dtime();
#endif
    #pragma omp parallel shared(min, max, isum)
    {
        int min_p = min, max_p = min;
        float sum_p = 0.f;
        #pragma omp for nowait
        for(int i = 0; i < wh; ++i){
            Imtype pixval = I->data[i];
            if(pixval < min_p) min_p = pixval;
            else if(pixval > max_p) max_p = pixval;
            sum_p += (float) pixval;
        }
        #pragma omp critical
        {
            if(min > min_p) min = min_p;
            if(max < max_p) max = max_p;
            isum += sum_p;
        }
    }
    I->maxval = max;
    I->minval = min;
    I->avg_intensity = isum / (float)wh;
    DBG("Image_minmax(): Min=%d, Max=%d, Isum=%g, mean=%g", min, max, isum, I->avg_intensity);
}

/*
 * =================== CONVERT IMAGE TYPES ===================>
 */

/**
 * @brief bin2Im - convert binarized image into floating
 * @param image - binarized image
 * @param W, H  - its size (in pixels!)
 * @return Image structure
 */
Image *bin2Im(const uint8_t *image, int W, int H){
    Image *ret = Image_new(W, H);
    int stride = (W + 7) / 8, s1 = (stride*8 == W) ? stride : stride - 1;
    OMP_FOR()
    for(int y = 0; y < H; y++){
        Imtype *optr = &ret->data[y*W];
        const uint8_t *iptr = &image[y*stride];
        for(int x = 0; x < s1; x++){
            register uint8_t inp = *iptr++;
            for(int i = 0; i < 8; ++i){
                *optr++ = (inp & 0x80) ? 1. : 0;
                inp <<= 1;
            }
        }
        int rest = W - s1*8;
        if(rest){
            register uint8_t inp = *iptr;
            for(int i = 0; i < rest; ++i){
                *optr++ = (inp & 0x80) ? 1. : 0;
                inp <<= 1;
            }
        }
    }
    ret->minval = 0;
    ret->maxval = 1;
    return ret;
}

/**
 * Convert floatpoint image into pseudo-packed (1 char == 8 pixels), all values > bk will be 1, else - 0
 * @param im (i)     - image to convert
 * @param stride (o) - new width of image
 * @param bk         - background level (all values < bk will be 0, other will be 1)
 * @return allocated memory area with "packed" image
 */
uint8_t *Im2bin(const Image *im, Imtype bk){
    if(!im) return NULL;
    int W = im->width, H = im->height;
    if(W < 2 || H < 2) return NULL;
    int y, W0 = (W + 7) / 8, s1 = (W/8 == W0) ? W0 : W0 - 1;
    uint8_t *ret = MALLOC(uint8_t, W0 * H);
    OMP_FOR()
    for(y = 0; y < H; ++y){
        Imtype *iptr = &im->data[y*W];
        uint8_t *optr = &ret[y*W0];
        for(int x = 0; x < s1; ++x){
            register uint8_t o = 0;
            for(int i = 0; i < 8; ++i){
                o <<= 1;
                if(*iptr++ > bk) o |= 1;
            }
            *optr++ = o;
        }
        int rest = W - s1*8;
        if(rest){
            register uint8_t o = 0;
            for(int x = 0; x < rest; ++x){
                o <<= 1;
                if(*iptr++ > bk) o |= 1;
            }
            *optr = o << (8 - rest);
        }
    }
    return ret;
}

#if 0
UNUSED function! Need to be refactored
// convert size_t labels into Image
Image *ST2Im(const size_t *image, int W, int H){
    Image *ret = Image_new(W, H);
    OMP_FOR()
    for(int y = 0; y < H; ++y){
        Imtype *optr = &ret->data[y*W];
        const size_t *iptr = &image[y*W];
        for(int x = 0; x < W; ++x){
            *optr++ = (Imtype)*iptr++;
        }
    }
    Image_minmax(ret);
    return ret;
}
#endif

/**
 * Convert "packed" image into size_t array for conncomp procedure
 * @param image (i) - input image
 * @param W, H      - size of image in pixels
 * @return allocated memory area with copy of an image
 */
size_t *bin2ST(const uint8_t *image, int W, int H){
    size_t *ret = MALLOC(size_t, W * H);
    int W0 = (W + 7) / 8, s1 = W0 - 1;
    OMP_FOR()
    for(int y = 0; y < H; y++){
        size_t *optr = &ret[y*W];
        const uint8_t *iptr = &image[y*W0];
        for(int x = 0; x < s1; ++x){
            register uint8_t inp = *iptr++;
            for(int i = 0; i < 8; ++i){
                *optr++ = (inp & 0x80) ? 1 : 0;
                inp <<= 1;
            }
        }
        int rest = W - s1*8;
        if(rest){
            register uint8_t inp = *iptr;
            for(int i = 0; i < rest; ++i){
                *optr++ = (inp & 0x80) ? 1 : 0;
                inp <<= 1;
            }
        }
    }
    return ret;
}


/*
 * <=================== CONVERT IMAGE TYPES ===================
 */
