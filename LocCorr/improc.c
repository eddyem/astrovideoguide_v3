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
#include "fits.h"
#include "improc.h"
#include "inotify.h"
#include "median.h"
#include "pusirobo.h"

volatile atomic_ullong ImNumber = 0; // GLOBAL: counter of processed images
volatile atomic_bool stopwork = FALSE; // GLOBAL: suicide

// GLOBAL: get image information
char *(*imagedata)(const char *messageid, char *buf, int buflen) = NULL;

steppersproc *theSteppers = NULL;

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

typedef enum{
    PROCESS_NONE,
    PROCESS_PUSIROBO
} postproc_type;

static postproc_type postprocess = PROCESS_NONE;

/*
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
*/
/*
static void savebin(uint8_t *b, int W, int H, const char *name){
    Image *I = bin2Im(b, W, H);
    if(I){
        save_fits(I, name);
        Image_free(&I);
    }
}*/

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

static void getDeviation(object *curobj){
    int averflag = 0;
    static double Xc[NAVER_MAX+1], Yc[NAVER_MAX+1];
    double xx = curobj->xc, yy = curobj->yc, xsum2 = 0., ysum2 = 0.;
    double Sx = 0., Sy = 0.;
    static int counter = 0;
    Xc[counter] = curobj->xc; Yc[counter] = curobj->yc;
    if(fXYlog){ // make log record
        fprintf(fXYlog, "%.2f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t",
                dtime(), curobj->xc, curobj->yc,
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
    if(theSteppers && theSteppers->proc_corr && averflag){
        if(Sx > XY_TOLERANCE || Sy > XY_TOLERANCE){
            LOGDBG("Bad value - not process"); // don't run processing for bad data
        }else
            theSteppers->proc_corr(xx, yy);
    }
    XYnewline();
}

void process_file(Image *I){
    static double lastTproc = 0.;
/*
#ifdef EBUG
    double t0 = dtime(), tlast = t0;
#define DELTA(p) do{double t = dtime(); DBG("---> %s @ %gms (delta: %gms)", p, (t-t0)*1e3, (t-tlast)*1e3); tlast = t;}while(0)
#else
*/
#define DELTA(x)
//#endif
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
    Imtype bk;
    if(calc_background(I, &bk)){
        DBG("backgr = %d", bk);
        DELTA("Got background");
        uint8_t *ibin = Im2bin(I, bk);
        DELTA("Made binary");
        if(ibin){
            //savebin(ibin, W, H, "binary.fits");
            //DELTA("save binary.fits");
            uint8_t *er = erosionN(ibin, W, H, theconf.Nerosions);
            FREE(ibin);
            DELTA("Erosion");
            //savebin(er, W, H, "erosion.fits");
            //DELTA("Save erosion");
            uint8_t *opn = dilationN(er, W, H, theconf.Ndilations);
            FREE(er);
            DELTA("Opening");
            //savebin(opn, W, H, "opening.fits");
            //DELTA("Save opening");
            ConnComps *cc = NULL;
            size_t *S = cclabel4(opn, W, H, &cc);
            FREE(opn);
            if(cc->Nobj > 1){ // Nobj = amount of objects + 1
                DBGLOG("Nobj=%d", cc->Nobj-1);
                object *Objects = MALLOC(object, cc->Nobj-1);
                int objctr = 0;
                for(size_t i = 1; i < cc->Nobj; ++i){
                    Box *b = &cc->boxes[i];
                    double wh = ((double)b->xmax - b->xmin)/(b->ymax - b->ymin);
                    //DBG("Obj# %zd: wh=%g, area=%d", i, wh, b->area);
                    if(wh < theconf.minwh || wh > theconf.maxwh) continue;
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
                DBGLOG("T%.2f, N=%d\n", dtime(), objctr);
                if(objctr > 1){
                    if(theconf.starssort)
                        qsort(Objects, objctr, sizeof(object), compIntens);
                    else
                        qsort(Objects, objctr, sizeof(object), compDist);
                }
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
                { // prepare image and save jpeg
                    uint8_t *outp = NULL;
                    if(theconf.equalize)
                        outp = equalize(I, 3, theconf.throwpart);
                    else
                        outp = linear(I, 3);
                    static Pattern *cross = NULL, *crossL = NULL;
                    if(!cross) cross = Pattern_xcross(33, 33);
                    if(!crossL) crossL = Pattern_xcross(51, 51);
                    Img3 i3 = {.data = outp, .w = I->width, .h = H};
                    // draw fiber center position
                    Pattern_draw3(&i3, crossL, theconf.xtarget-theconf.xoff, H-(theconf.ytarget-theconf.yoff), C_R);
                    if(objctr){ // draw crosses @ objects' centers
                        int H = I->height;
                        // draw current star centroid
                        Pattern_draw3(&i3, cross, Objects[0].xc, H-Objects[0].yc, C_G);
                        // add offset to show in target system
                        xc = Objects[0].xc + theconf.xoff;
                        yc = Objects[0].yc + theconf.yoff;
                        // draw other centroids
                        for(int i = 1; i < objctr; ++i)
                            Pattern_draw3(&i3, cross, Objects[i].xc, H-Objects[i].yc, C_B);
                    }else{xc = -1.; yc = -1.;}
                    char *tmpnm = MALLOC(char, strlen(GP->outputjpg) + 5);
                    sprintf(tmpnm, "%s-tmp", GP->outputjpg);
                    if(stbi_write_jpg(tmpnm, I->width, I->height, 3, outp, 95)){
                        if(rename(tmpnm, GP->outputjpg)){
                            WARN("rename()");
                            LOGWARN("can't save %s", GP->outputjpg);
                        }
                    }
                    FREE(tmpnm);
                    FREE(outp);
                }
                FREE(Objects);
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
    }else if(tp == T_CAPT_GRASSHOPPER || tp == T_CAPT_BASLER){
        camera *cam = &GrassHopper;
        if(tp == T_CAPT_BASLER) cam = &Basler;
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
        theSteppers = &pusyCANbus;
    }else{
        WARNX("Unknown postprocess \"%s\"", name);
        LOGERR("Unknown postprocess \"%s\"", name);
    }
}

double getFramesPerS(){ return FPS; }

void getcenter(float *x, float *y){
    if(x) *x = xc;
    if(y) *y = yc;
}
