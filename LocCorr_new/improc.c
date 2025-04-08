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

#include "basler.h"
#include "binmorph.h"
#include "cameracapture.h"
#include "cmdlnopts.h"
#include "config.h"
#include "debug.h"
#include "draw.h"
#include "grasshopper.h"
#include "hikrobot.h"
#include "improc.h"
#include "inotify.h"
#include "steppers.h"

volatile atomic_ullong ImNumber = 0; // GLOBAL: counter of processed images
volatile atomic_bool stopwork = FALSE; // GLOBAL: suicide

// GLOBAL: get image information
char *(*imagedata)(const char *messageid, char *buf, int buflen) = NULL;

static FILE *fXYlog = NULL;
static double tstart = 0.; // time of logging start
static double FPS = 0.; // frames per second
static float xc = -1., yc = -1.; // center coordinates

typedef struct{
    uint32_t area;      // object area in pixels
    double Isum;        // total object's intensity over background
    double WdivH;       // width of object's box divided by height
    double xc;          // centroid coordinates
    double yc;
    double xsigma;      // STD by horizontal and vertical axes
    double ysigma;
} object;

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
    double xtg = theconf.xtarget - theconf.xoff, ytg = theconf.ytarget - theconf.yoff;
    double  xa = oa->xc - xtg, xb = ob->xc - xtg,
            ya = oa->yc - ytg, yb = ob->yc - ytg;
    double r2a = xa*xa + ya*ya;
    double r2b = xb*xb + yb*yb;
    return (r2a < r2b) ? -1 : 1;
}

static void XYnewline(){
    if(!fXYlog) return;
    fprintf(fXYlog, "\n");
    fflush(fXYlog);
}

// add comment string to XY log; @return FALSE if failed (file not exists)
int XYcomment(char *cmnt){
    if(!fXYlog || !cmnt) return FALSE;
    if(*cmnt == '"'){
        ++cmnt;
        char *e = strrchr(cmnt, '"');
        if(e) *e = 0;
    }
    char *n = strrchr(cmnt, '\n');
    if(n) *n = 0;
    fprintf(fXYlog, "# %s\n", cmnt);
    fflush(fXYlog);
    return TRUE;
}

static void getDeviation(object *curobj){
    int averflag = 0;
    static double Xc[NAVER_MAX+1], Yc[NAVER_MAX+1];
    double xx = curobj->xc, yy = curobj->yc, xsum2 = 0., ysum2 = 0.;
    double Sx = 0., Sy = 0.;
    static int counter = 0;
    Xc[counter] = curobj->xc; Yc[counter] = curobj->yc;
    if(fXYlog){ // make log record
        fprintf(fXYlog, "%-14.2f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t",
                dtime() - tstart, curobj->xc, curobj->yc,
                curobj->xsigma, curobj->ysigma, curobj->WdivH);
    }
    //DBG("counter = %d", counter);
    if(++counter < theconf.naverage){
        goto process_corrections;
    }
    // it's time to calculate average deviations
    xx = 0.; yy = 0.;
    for(int i = 0; i < counter; ++i){
        double x = Xc[i], y = Yc[i];
        xx += x; yy += y;
        xsum2 += x*x; ysum2 += y*y;
    }
    xx /= counter; yy /= counter;
    Sx = sqrt(xsum2/counter - xx*xx);
    Sy = sqrt(ysum2/counter - yy*yy);
    counter = 0;
#ifdef EBUG
    green("\n Average centroid: X=%.1f (+-%.1f), Y=%.1f (+-%.1f)\n", xx, Sx, yy, Sy);
#endif
    LOGDBG("getDeviation(): Average centroid: X=%.1f (+-%.1f), Y=%.1f (+-%.1f)", xx, Sx, yy, Sy);
    averflag = 1;
    if(fXYlog) fprintf(fXYlog, "%.1f\t%.1f\t%.1f\t%.1f", xx, yy, Sx, Sy);
process_corrections:
    LOGDBG("here");
    if(theSteppers){
        DBG("Process corrections");
        if(theSteppers->proc_corr && averflag){
            if(Sx > XY_TOLERANCE || Sy > XY_TOLERANCE){
                LOGDBG("Bad value - not process"); // don't run processing for bad data
            }else
                theSteppers->proc_corr(xx, yy);
        }
    }else{
        LOGERR("Lost connection with stepper server");
        WARNX("Lost connection with stepper server");
    }
    LOGDBG("And there");
    XYnewline();
}

typedef struct{ // statistics: mean and RMS
    float xc; float yc; float xsigma; float ysigma;
} ptstat_t;

/**
 * @brief sumAndStat - calculate statistics in region of interest
 * @param I - image (with background calculated)
 * @param mask - labeled mask for objects (or NULL)
 * @param idx - index on labeled mask
 * @param roi - region of interest
 * @param stat - (region - bacground) statistics
 * @return total intensity sum
 */
static float sumAndStat(const Image *I, const size_t *mask, size_t idx, const il_Box *roi, ptstat_t *stat){
    if(!I || !roi) return -1.;
    //FNAME();
    float xc = 0., yc = 0.;
    float x2c = 0., y2c = 0., Isum = 0.;
    int W = I->width;
    //DBG("imw=%d, roi=%d:%d:%d:%d", W, roi->xmin, roi->xmax, roi->ymin, roi->ymax);
    // dumb calculation as paralleling could be much slower
    for(int y = roi->ymin; y <= roi->ymax; ++y){
        size_t istart = y*W + roi->xmin;
        //DBG("istart=%zd", istart);
        const size_t *maskptr = (mask) ? &mask[istart] : NULL;
        //DBG("mask %s NULL", mask ? "!=":"==");
        Imtype *Iptr = &I->data[istart];
        for(int x = roi->xmin; x <= roi->xmax; ++x, ++Iptr){
            if(maskptr){if(*maskptr++ != idx) continue;}
            if(*Iptr <= I->background) continue;
            float intens = (float)(*Iptr - I->background);
            float xw = x * intens, yw = y * intens;
            xc += xw;
            yc += yw;
            x2c += xw * x;
            y2c += yw * y;
            Isum += intens;
        }
    }
    if(stat && Isum > 0.){
        stat->xc = xc / Isum;
        stat->yc = yc / Isum;
        stat->xsigma = x2c/Isum - stat->xc*stat->xc;
        stat->ysigma = y2c/Isum - stat->yc*stat->yc;
    }
    //DBG("xc=%g, yc=%g, xs=%g, ys=%g", stat->xc, stat->yc, stat->xsigma, stat->ysigma);
    return Isum;
}

#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define MIN(a, b)   ((a) < (b) ? (a) : (b))

void process_file(Image *I){
    static double lastTproc = 0.;
    static int prev_x = -1, prev_y = -1;
    static object *Objects = NULL;
    static size_t Nallocated = 0;
    il_ConnComps *cc = NULL;
    size_t *S = NULL;
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
    //save_fits(I, "fitsout.fits");
    //DELTA("Save original");
    if(calc_background(I)){
        DBG("backgr = %d", I->background);
        DELTA("Got background");
        int objctr = 0;
        if(prev_x > 0 && prev_y > 0){
            // Define ROI bounds
            il_Box roi = {.xmin = MAX(prev_x - ROI_SIZE/2, 0),
                          .xmax = MIN(prev_x + ROI_SIZE/2, W-1),
                          .ymin = MAX(prev_y - ROI_SIZE/2, 0),
                          .ymax = MIN(prev_y + ROI_SIZE/2, H-1)};
            ptstat_t stat;
            // Calculate centroid within ROI
            DBG("Get sum and stat for simplest centroid");
            double sum = sumAndStat(I, NULL, 0, &roi, &stat);
            if(sum > 0.){
                if( fabsf(stat.xc - prev_x) > XY_TOLERANCE ||
                    fabsf(stat.yc - prev_y) > XY_TOLERANCE){
                    DBG("Bad: was x=%d, y=%d; become x=%g, y=%g ==> need fine calculations", prev_x, prev_y, xc, yc);
                }else{
                    double WdH = stat.xsigma/stat.ysigma;
                    // wery approximate area inside sigmax*sigmay
                    double area = .4 * stat.xsigma * stat.ysigma;
                    if(!isnan(WdH) && !isinf(WdH) && // if W/H is a number
                        WdH > theconf.minwh && WdH < theconf.maxwh && // if W/H near circle
                        area > theconf.minarea && area < theconf.maxarea){ // if star area is in range
                        prev_x = (int)stat.xc;
                        prev_y = (int)stat.yc;
                        DBG("Simplest centroid, Xc=%g, Yc=%g", stat.xc, stat.yc);
                        objctr = 1;
                        if(!Objects){
                            Objects = (object*)malloc(sizeof(object));
                            Nallocated = 1;
                        }
                        Objects[0] = (object){
                            .area = area, .Isum = sum,
                            .WdivH = WdH, .xc = stat.xc, .yc = stat.yc,
                            .xsigma = stat.xsigma, .ysigma = stat.ysigma
                        };
                        goto SKIP_FULL_PROCESS; // Skip full image processing
                    }else{
                        DBG("BAD image: WdH=%g, area=%g, xsigma=%g, ysigma=%g", WdH, area, stat.xsigma, stat.ysigma);
                    }
                }
            }
        }
        uint8_t *ibin = Im2bin(I, I->background);
        DELTA("Made binary");
        if(ibin){
            //savebin(ibin, W, H, "binary.fits");
            //DELTA("save binary.fits");
            uint8_t *er = il_erosionN(ibin, W, H, theconf.Nerosions);
            FREE(ibin);
            DELTA("Erosion");
            //savebin(er, W, H, "erosion.fits");
            //DELTA("Save erosion");
            uint8_t *opn = il_dilationN(er, W, H, theconf.Ndilations);
            FREE(er);
            DELTA("Opening");
            //savebin(opn, W, H, "opening.fits");
            //DELTA("Save opening");
            S = il_cclabel4(opn, W, H, &cc);
            FREE(opn);
            DBG("Nobj=%zd", cc->Nobj-1);
            if(S && cc && cc->Nobj > 1){ // Nobj = amount of objects + 1
                DBGLOG("Nobj=%zd", cc->Nobj-1);
                if(Nallocated < cc->Nobj-1){
                    Nallocated = cc->Nobj-1;
                    Objects = realloc(Objects, Nallocated*sizeof(object));
                }
                for(size_t i = 1; i < cc->Nobj; ++i){
                    il_Box *b = &cc->boxes[i];
                    double wh = ((double)b->xmax - b->xmin)/(b->ymax - b->ymin);
                    //DBG("Obj# %zd: wh=%g, area=%d", i, wh, b->area);
                    if(wh < theconf.minwh || wh > theconf.maxwh) continue;
                    if((int)b->area < theconf.minarea || (int)b->area > theconf.maxarea) continue;
                    ptstat_t stat;
                    DBG("Get sum and stat");
                    double sum = sumAndStat(I, &S[i], i, b, &stat);
                    if(sum > 0.){
                        if(cc->Nobj == 2){
                            prev_x = (int)stat.xc, prev_y = (int)stat.yc;
                        }
                        Objects[objctr++] = (object){
                            .area = b->area, .Isum = sum,
                            .WdivH = wh, .xc = stat.xc, .yc = stat.yc,
                            .xsigma = stat.xsigma, .ysigma = stat.ysigma
                        };
                    }
                }
                DELTA("Labeling");
                if(objctr > 1){
                    prev_x = -1, prev_y = -1; // don't allow simple gravcenter for a lots of objects
                    if(theconf.starssort)
                        qsort(Objects, objctr, sizeof(object), compIntens);
                    else
                        qsort(Objects, objctr, sizeof(object), compDist);
                }
SKIP_FULL_PROCESS:
                DBGLOG("T%.2f, N=%d\n", dtime(), objctr);
                DELTA("Calculate deviations");
                if(objctr){
#ifdef EBUG
                    object *o = Objects;
                    green("%6s\t%6s\t%6s\t%6s\t%6s\t%6s\t%6s\t%6s\t%8s\n",
                          "N", "Area", "Mv", "W/H", "Xc", "Yc", "Sx", "Sy", "Area/r^2");
                    for(int i = 0; i < objctr; ++i, ++o){
                        // 1.0857 = 2.5/ln(10)
                        printf("%6d\t%6d\t%6.1f\t%6.1f\t%6.1f\t%6.1f\t%6.1f\t%6.1f\t%8.1f\n",
                               i, o->area, 20.-1.0857*log(o->Isum), o->WdivH, o->xc, o->yc,
                               o->xsigma, o->ysigma, o->area/o->xsigma/o->ysigma);
                    }
#endif
                    getDeviation(Objects); // calculate dX/dY and process corrections
                }
                DELTA("prepare image");
                { // prepare image and save jpeg
                    uint8_t *outp = NULL;
                    if(theconf.equalize)
                        outp = equalize(I, 3, theconf.throwpart);
                    else
                        outp = linear(I, 3);
                    static il_Pattern *cross = NULL, *crossL = NULL;
                    if(!cross) cross = il_Pattern_xcross(33, 33);
                    if(!crossL) crossL = il_Pattern_xcross(51, 51);
                    il_Img3 i3 = {.data = outp, .w = I->width, .h = H};
                    DELTA("Draw crosses");
                    // draw fiber center position
                    il_Pattern_draw3(&i3, crossL, theconf.xtarget-theconf.xoff, H-(theconf.ytarget-theconf.yoff), C_R);
                    if(objctr){ // draw crosses @ objects' centers
                        int H = I->height;
                        // draw current star centroid
                        il_Pattern_draw3(&i3, cross, Objects[0].xc, H-Objects[0].yc, C_G);
                        // add offset to show in target system
                        xc = Objects[0].xc + theconf.xoff;
                        yc = Objects[0].yc + theconf.yoff;
                        // draw other centroids
                        for(int i = 1; i < objctr; ++i)
                            il_Pattern_draw3(&i3, cross, Objects[i].xc, H-Objects[i].yc, C_B);
                    }else{xc = -1.; yc = -1.;}
                    char tmpnm[FILENAME_MAX+5];
                    sprintf(tmpnm, "%s-tmp", GP->outputjpg);
                    if(stbi_write_jpg(tmpnm, I->width, I->height, 3, outp, 95)){
                        if(rename(tmpnm, GP->outputjpg)){
                            WARN("rename()");
                            LOGWARN("can't save %s", GP->outputjpg);
                        }
                    }
                    DELTA("Written");
                    FREE(outp);
                }
            }else{
                xc = -1.; yc = -1.;
                Image_write_jpg(I, GP->outputjpg, theconf.equalize);
            }
            DBGLOG("Image saved");
            FREE(S);
            FREE(cc);
        }
    }else Image_write_jpg(I, GP->outputjpg, theconf.equalize);
    ++ImNumber;
    if(lastTproc > 1.) FPS = 1. / (dtime() - lastTproc);
    lastTproc = dtime();
    DELTA("End");
}

static char *localimages(const char *messageid, int isdir, char *buf, int buflen){
    static char *impath = NULL;
    if(!impath){
        if(!realpath(GP->outputjpg, impath)) impath = strdup(GP->outputjpg);
    }
    snprintf(buf, buflen, "{ \"%s\": \"%s\", \"camstatus\": \"watch %s\", \"impath\": \"%s\", \"xcenter\": %.1f, \"ycenter\": %.1f }",
             MESSAGEID, messageid, isdir ? "directory" : "file", impath, xc, yc);
    return buf;
}
static char *watchdr(const char *messageid, char *buf, int buflen){
    return localimages(messageid, 1, buf, buflen);
}
static char *watchfl(const char *messageid, char *buf, int buflen){
    return localimages(messageid, 0, buf, buflen);
}

int process_input(InputType tp, char *name){
    DBG("process_input(%d, %s)", tp, name);
    if(tp == T_DIRECTORY){
        imagedata = watchdr;
        return watch_directory(name, process_file);
    }else if(tp == T_CAPT_GRASSHOPPER || tp == T_CAPT_BASLER || tp == T_CAPT_HIKROBOT){
        camera *cam = NULL;
        switch(tp){
            case T_CAPT_GRASSHOPPER:
#ifdef FLYCAP_FOUND
                cam = &GrassHopper;
#endif
                break;
            case T_CAPT_BASLER:
#ifdef BASLER_FOUND
                cam = &Basler;
#endif
                break;
            case T_CAPT_HIKROBOT:
#ifdef MVS_FOUND
                cam = &Hikrobot;
#endif
                break;
            default: return FALSE;
        }
        if(!setCamera(cam)){
            WARNX("The camera disconnected");
            LOGWARN("The camera disconnected");
        }
        imagedata = camstatus;
        return camcapture(process_file);
    }
    imagedata = watchfl;
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
    fprintf(fXYlog, "# time\t\tXc\tYc\tSx\tSy\tW/H\taverX\taverY\tSX\tSY\n");
    fflush(fXYlog);
    tstart = dtime();
}
void closeXYlog(){
    if(!fXYlog) return;
    fclose(fXYlog);
    fXYlog = NULL;
}

double getFramesPerS(){ return FPS; }

void getcenter(float *x, float *y){
    if(x) *x = xc;
    if(y) *y = yc;
}
