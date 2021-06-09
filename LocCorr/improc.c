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

#include <math.h>
#include <stb/stb_image_write.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <usefull_macros.h>

#include "binmorph.h"
#include "cmdlnopts.h"
#include "config.h"
#include "draw.h"
#include "grasshopper.h"
#include "fits.h"
#include "improc.h"
#include "inotify.h"
#include "median.h"
#include "pusirobo.h"

static FILE *fXYlog = NULL;

static double tstart = 0.; // time of logging start
int stopwork = 0;

// function to process calculated corrections
static void (*proc_corr)(double, double, int) = NULL;
// function to get stepper server status
char *(*stepstatus)(char *buf, int buflen) = NULL;
// set new status
char *(*setstepstatus)(const char *newstatus, char *buf, int buflen) = NULL;
// move focus
char *(*movefocus)(const char *newstatus, char *buf, int buflen) = NULL;

typedef struct{
    uint32_t area;      // object area in pixels
    double Isum;        // total object's intensity over background
    double WdivH;       // width of object's box divided by height
    double xc;          // centroid coordinates
    double yc;
    double xsigma;      // STD by horizontal and vertical axes
    double ysigma;
} object;

typedef enum{
    PROCESS_NONE,
    PROCESS_PUSIROBO
} postproc_type;

static postproc_type postprocess = PROCESS_NONE;

static bool save_fits(Image *I, const char *name){
    char fname[PATH_MAX];
    snprintf(fname, PATH_MAX, name);
    char *pt = strrchr(fname, '.');
    if(pt) *pt = 0;
    strncat(fname, ".jpg", PATH_MAX);
    Image_write_jpg(I, fname, theconf.equalize);
    unlink(name);
    return FITS_write(name, I);
}

static void savebin(uint8_t *b, int W, int H, const char *name){
    Image *I = bin2Im(b, W, H);
    if(I){
        save_fits(I, name);
        Image_free(&I);
    }
}

// functions for Qsort
static int compIntens(const void *a, const void *b){ // compare by intensity
    const object *oa = (const object*)a;
    const object *ob = (const object*)b;
    double idiff = (oa->Isum - ob->Isum)/(oa->Isum + ob->Isum);
    if(fabs(idiff) > theconf.intensthres) return (idiff > 0) ? -1:1;
    double r2a = oa->xc * oa->xc + oa->yc * oa->yc;
    double r2b = ob->xc * ob->xc + ob->yc * ob->yc;
    return (r2a < r2b) ? -1 : 1;
}
static int compDist(const void *a, const void *b){ // compare by distanse from target
    const object *oa = (const object*)a;
    const object *ob = (const object*)b;
    double  xa = oa->xc - theconf.xtarget, xb = ob->xc - theconf.xtarget,
            ya = oa->yc - theconf.ytarget, yb = ob->yc - theconf.ytarget;
    double r2a = xa*xa + ya*ya;
    double r2b = xb*xb + yb*yb;
    return (r2a < r2b) ? -1 : 1;
}

static void XYnewline(){
    if(!fXYlog) return;
    fprintf(fXYlog, "\n");
    fflush(fXYlog);
}

static void getDeviation(object *curobj){
    int averflag = 0;
    static double Xc[MAX_AVERAGING_ARRAY_SIZE], Yc[MAX_AVERAGING_ARRAY_SIZE];
    double xx = curobj->xc, yy = curobj->yc, xsum2 = 0., ysum2 = 0.;
    double Sx = 0., Sy = 0.;
    static int counter = 0;
    Xc[counter] = curobj->xc; Yc[counter] = curobj->yc;
    if(fXYlog){ // make log record
        fprintf(fXYlog, "%.2f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t",
                dtime() - tstart, curobj->xc, curobj->yc,
                curobj->xsigma, curobj->ysigma, curobj->WdivH);
    }
    DBG("counter = %d", counter);
    if(++counter != theconf.naverage){
        goto process_corrections;
    }
    // it's time to calculate average deviations
    counter = 0; xx = 0.; yy = 0.;
    for(int i = 0; i < theconf.naverage; ++i){
        double x = Xc[i], y = Yc[i];
        xx += x; yy += y;
        xsum2 += x*x; ysum2 += y*y;
    }
    xx /= theconf.naverage; yy /= theconf.naverage;
    Sx = sqrt(xsum2/theconf.naverage - xx*xx);
    Sy = sqrt(ysum2/theconf.naverage - yy*yy);
    green("\n\n\n Average centroid: X=%g (+-%g), Y=%g (+-%g)\n", xx, Sx, yy, Sy);
    LOGDBG("getDeviation(): Average centroid: X=%g (+-%g), Y=%g (+-%g)", xx, Sx, yy, Sy);
    averflag = 1;
    if(fXYlog) fprintf(fXYlog, "%.1f\t%.1f\t%.1f\t%.1f", xx, yy, Sx, Sy);
process_corrections:
    if(proc_corr){
        if(Sx > 1. || Sy > 1.){
            LOGDBG("Bad value - not process"); // don't run processing for bad data
        }else
            proc_corr(xx, yy, averflag);
    }
    XYnewline();
}

void process_file(Image *I){
#ifdef EBUG
    double t0 = dtime(), tlast = t0;
#define DELTA(p) do{double t = dtime(); DBG("---> %s @ %gms (delta: %gms)", p, (t-t0)*1e3, (t-tlast)*1e3); tlast = t;}while(0)
#else
#define DELTA(x)
#endif
    // I - original image
    // mean - local mean
    // std  - local STD
    /**** read original image ****/
    DELTA("Imread");
    if(!I){
        WARNX("No image");
        return;
    }
    int W = I->width, H = I->height;
    if(!I->dtype) I->dtype = FLOAT_IMG;
    save_fits(I, "fitsout.fits");
    DELTA("Save original");
    Imtype bk;
    if(calc_background(I, &bk)){
        DBG("backgr = %g", bk);
        DELTA("Got background");
        uint8_t *ibin = Im2bin(I, bk);
        DELTA("Made binary");
        if(ibin){
            savebin(ibin, W, H, "binary.fits");
            DELTA("save binary.fits");
            uint8_t *er = erosionN(ibin, W, H, theconf.Nerosions);
            FREE(ibin);
            DELTA("Erosion");
            savebin(er, W, H, "erosion.fits");
            DELTA("Save erosion");
            uint8_t *opn = dilationN(er, W, H, theconf.Ndilations);
            FREE(er);
            DELTA("Opening");
            savebin(opn, W, H, "opening.fits");
            DELTA("Save opening");
            ConnComps *cc;
            size_t *S = cclabel4(opn, W, H, &cc);
            FREE(opn);
            if(cc->Nobj > 1){
                object *Objects = MALLOC(object, cc->Nobj-1);
                int objctr = 0;
                for(size_t i = 1; i < cc->Nobj; ++i){
                    Box *b = &cc->boxes[i];
                    double wh = ((double)b->xmax - b->xmin)/(b->ymax - b->ymin);
                    //DBG("Obj# %zd: wh=%g, area=%d", i, wh, b->area);
                    // TODO: change magick numbers to parameters
                    if(wh < MINWH || wh > MAXWH) continue;
                    if((int)b->area < theconf.minarea || (int)b->area > theconf.maxarea) continue;
                    double xc = 0., yc = 0.;
                    double x2c = 0., y2c = 0., Isum = 0.;
                    for(size_t y = b->ymin; y <= b->ymax; ++y){
                        size_t idx = y*W + b->xmin;
                        size_t *maskptr = &S[idx];
                        Imtype *Iptr = &I->data[idx];
                        for(size_t x = b->xmin; x <= b->xmax; ++x, ++maskptr, ++Iptr){
                            if(*maskptr != i) continue;
                            double intens = (double) (*Iptr - bk);
                            if(intens < 0.) continue;
                            double xw = x * intens, yw = y * intens;
                            xc += xw;
                            yc += yw;
                            x2c += xw * x;
                            y2c += yw * y;
                            Isum += intens;
                        }
                    }
                    xc /= Isum; yc /= Isum;
                    x2c = x2c/Isum - xc*xc;
                    y2c = y2c/Isum - yc*yc;
                    Objects[objctr++] = (object){
                        .area = b->area, .Isum = Isum,
                        .WdivH = wh, .xc = xc, .yc = yc,
                        .xsigma = sqrt(x2c), .ysigma = sqrt(y2c)
                    };
                }
                DELTA("Labeling");
                printf("T%zd, N=%d\n", time(NULL), objctr);
                if(objctr > 1){
                    if(theconf.starssort)
                        qsort(Objects, objctr, sizeof(object), compIntens);
                    else
                        qsort(Objects, objctr, sizeof(object), compDist);
                }
                object *o = Objects;
                green("%6s\t%6s\t%6s\t%6s\t%6s\t%6s\t%6s\t%6s\n",
                      "N", "Area", "Mv", "W/H", "Xc", "Yc", "Sx", "Sy");
                for(int i = 0; i < objctr; ++i, ++o){
                    // 1.0857 = 2.5/ln(10)
                    printf("%6d\t%6d\t%6.1f\t%6.1f\t%6.1f\t%6.1f\t%6.1f\t%6.1f\n",
                           i, o->area, 20.-1.0857*log(o->Isum), o->WdivH, o->xc, o->yc, o->xsigma, o->ysigma);
                }
                getDeviation(Objects);
                { // prepare image
                    uint8_t *outp = NULL;
                    if(theconf.equalize)
                        outp = equalize(I, 3, theconf.throwpart);
                    else
                        outp = linear(I, 3);
                    if(objctr){ // draw crosses @ objects' centers
                        static Pattern *cross = NULL;
                        if(!cross) cross = Pattern_cross(33, 33);
                        int H = I->height;
                        Img3 i3 = {.data = outp, .w = I->width, .h = H};
                        Pattern_draw3(&i3, cross, Objects[0].xc, H-Objects[0].yc, C_G);
                        for(int i = 1; i < objctr; ++i)
                            Pattern_draw3(&i3, cross, Objects[i].xc, H-Objects[i].yc, C_R);
                        // Pattern_free(&cross); don't free - static variable!
                    }
                    stbi_write_jpg(GP->outputjpg, I->width, I->height, 3, outp, 95);
                    FREE(outp);
                }
                FREE(cc);
                FREE(Objects);
                Image *c = ST2Im(S, W, H);
                DELTA("conv size_t -> Ima");
                save_fits(c, "size_t.fits");
                Image_free(&c);
                DELTA("Save size_t");
                Image *obj = Image_sim(I);
                OMP_FOR()
                for(int y = 0; y < H; ++y){
                    size_t idx = y*W;
                    Imtype *optr = &obj->data[idx];
                    Imtype *iptr = &I->data[idx];
                    size_t *mask = &S[idx];
                    for(int x = 0; x < W; ++x, ++mask, ++iptr, ++optr){
                        if(*mask) *optr = *iptr - bk;
                    }
                }
                Image_minmax(obj);
                save_fits(obj, "object.fits");
                Image_free(&obj);
            }
            FREE(S);
        }
    }
    DELTA("End");
}

int process_input(InputType tp, char *name){
    DBG("process_input(%d, %s)", tp, name);
    if(tp == T_DIRECTORY) return watch_directory(name, process_file);
    else if(tp == T_CAPT_GRASSHOPPER) return capture_grasshopper(process_file);
    return watch_file(name, process_file);
}

/**
 * @brief openXYlog - open file to log XY values
 * @param name - filename
 */
void openXYlog(const char *name){
    closeXYlog();
    fXYlog = fopen(name, "a");
    if(!fXYlog){
        char *e = strerror(errno);
        WARNX("Can't create file %s: %s", name, e);
        LOGERR("Can't create file %s: %s", name, e);
        return;
    }
    time_t t = time(NULL);
    fprintf(fXYlog, "# Start at: %s", ctime(&t));
    fprintf(fXYlog, "# time Xc\tYc\t\tSx\tSy\tW/H\taverX\taverY\tSX\tSY\n");
    fflush(fXYlog);
    tstart = dtime();
}
void closeXYlog(){
    if(!fXYlog) return;
    fclose(fXYlog);
    fXYlog = NULL;
}

/**
 * @brief setpostprocess - set postprocessing name (what to do with deviations data)
 * @param name - "pusirobo" for pusirobot drives
 */
void setpostprocess(const char *name){
    if(!name) return;
    if(strncasecmp(name, PUSIROBO_POSTPROC, sizeof(PUSIROBO_POSTPROC) - 1) == 0){
        postprocess = PROCESS_PUSIROBO;
        DBG("Postprocess: pusirobot drives");
        LOGMSG("Postprocess: pusirobot drives");
        if(!pusi_connect()){
            WARNX("Pusiserver unavailable, will check later");
            LOGWARN("Pusiserver unavailable, will check later");
        }
        proc_corr = pusi_process_corrections;
        stepstatus = pusi_status;
        setstepstatus = set_pusistatus;
        movefocus = set_pfocus;
    }else{
        WARNX("Unknown postprocess \"%s\"", name);
        LOGERR("Unknown postprocess \"%s\"", name);
    }
}
