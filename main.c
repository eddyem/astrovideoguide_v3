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
#include <signal.h>         // signal
#include <stb/stb_image_write.h>
#include <stdio.h>          // printf
#include <stdlib.h>         // exit, free
#include <string.h>         // strdup
#include <time.h>
#include <unistd.h>         // sleep
#include <usefull_macros.h>

#include "binmorph.h"
#include "cmdlnopts.h"
#include "draw.h"
#include "inotify.h"
#include "fits.h"
#include "imagefile.h"
#include "median.h"

/**
 * We REDEFINE the default WEAK function of signal processing
 */
void signals(int sig){
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    LOGERR("Exit with status %d", sig);
    if(GP && GP->pidfile) // remove unnesessary PID file
        unlink(GP->pidfile);
    exit(sig);
}

void iffound_default(pid_t pid){
    ERRX("Another copy of this process found, pid=%d. Exit.", pid);
}

static InputType chk_inp(const char *name){
    if(!name) ERRX("Point file or directory name to monitor");
    InputType tp = chkinput(GP->inputname);
    if(T_WRONG == tp) return T_WRONG;
    green("\n%s is a ", name);
    switch(tp){
        case T_DIRECTORY:
            printf("directory");
        break;
        case T_JPEG:
            printf("jpeg");
        break;
        case T_PNG:
            printf("png");
        break;
        case T_GIF:
            printf("gif");
        break;
        case T_FITS:
            printf("fits");
        break;
        case T_BMP:
            printf("bmp");
        break;
        case T_GZIP:
            printf("maybe fits.gz?");
        break;
        default:
            printf("Unsupported type\n");
            return T_WRONG;
    }
    printf("\n");
    return tp;
}

static bool save_fits(Image *I, const char *name){
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
//++npoints, b->area, Isum, wh, xc, yc, x2c, y2c
typedef struct{
    uint32_t area;      // object area in pixels
    double Isum;        // total object's intensity over background
    double WdivH;       // width of object's box divided by height
    double xc; double yc;// centroid coordinates
    double xsigma;      // STD by horizontal and vertical axes
    double ysigma;
} object;

// function for Qsort
int compObjs(const void *a, const void *b){
    const object *oa = (const object*)a;
    const object *ob = (const object*)b;
    double idiff = (oa->Isum - ob->Isum)/(oa->Isum + ob->Isum);
    if(fabs(idiff) > GP->intensthres) return (idiff > 0) ? -1:1;
    double r2a = oa->xc * oa->xc + oa->yc * oa->yc;
    double r2b = ob->xc * ob->xc + ob->yc * ob->yc;
    return (r2a < r2b) ? -1 : 1;
}

static void process_file(const char *name){
#ifdef EBUG
    double t0 = dtime(), tlast = t0;
#define DELTA(p) do{double t = dtime(); DBG("---> %s @ %gms (delta: %gms)", p, (t-t0)*1e3, (t-tlast)*1e3); tlast = t;}while(0)
#else
#define DELTA(x)
#endif
    // I - original image
    // M - median filtering
    // mean - local mean
    // std  - local STD
    /**** read original image ****/
    Image *I = Image_read(name);
    DELTA("Imread");
    if(!I){
        WARNX("Can't read");
        return;
    }
    int W = I->width, H = I->height;
    I->dtype = FLOAT_IMG;
    save_fits(I, "fitsout.fits");
    DELTA("Save original");
    /*
    uint8_t *outp = NULL;
    if(GP->equalize)
        outp = equalize(I, 3, GP->throwpart);
    else
        outp = linear(I, 3);
    // draw test crosses
    Pattern *cross = Pattern_cross(301, 301);
    Img3 i3 = {.data = outp, .w = I->width, .h = I->height};
    Pattern_draw3(&i3, cross, I->width-100, I->height-100, C_R);
    Pattern_draw3(&i3, cross, I->width/2, I->height/2, C_G);
    Pattern_draw3(&i3, cross, 100, 100, C_W);
    Pattern_free(&cross);
    DBG("Try to write %s", name);
    stbi_write_jpg("jpegout.jpg", I->width, I->height, 3, outp, 95);
    FREE(outp);*/
#ifdef GETMEDIAN
    /**** get median image ****/
    Image *M = get_median(I, GP->medradius);
    if(M){
        DELTA("Got median");
        /*outp = linear(M, 3);
        stbi_write_jpg("median.jpg", I->width, I->height, 3, outp, 95);
        FREE(outp);*/
        save_fits(M, "median.fits");
        DELTA("Save median");
#endif
/*
        Image *mean = NULL, *std = NULL;
        if(get_stat(I, GP->medradius, &mean, &std)){
            DBG("Save std & mean");
            save_fits(mean, "mean.fits");
            save_fits(std, "std.fits");
            int wh = I->width*I->height;
            Image *diff = Image_sim(I);
            OMP_FOR()
            for(int i = 0; i < wh; ++i){
                Imtype pixval = fabs(I->data[i] - M->data[i]);
                //register Imtype mode = fabs(2.5*M->data[i] - 1.5*mean->data[i]);
                //register Imtype pixval = fabs(mode - I->data[i]);
                if(pixval > std->data[i]) diff->data[i] = 100.;
            }
            save_fits(diff, "val_med.fits");
            Image_free(&diff);
        }else WARNX("Can't calculate statistics");
        Image_free(&mean);
        Image_free(&std);
*/
        Imtype bk;
        if(calc_background(I, &bk)){
            DBG("backgr = %g", bk);
            DELTA("Got background");
            //uint8_t *ibin = Im2bin(I, 1960.);
            uint8_t *ibin = Im2bin(I, bk);
            DELTA("Made binary");
            if(ibin){
                savebin(ibin, W, H, "binary.fits");
                DELTA("save binary.fits");
                ;
                uint8_t *er = erosionN(ibin, W, H, GP->nerosions);
                DELTA("Erosion");
                savebin(er, W, H, "erosion.fits");
                DELTA("Save erosion");
                uint8_t *opn = dilationN(er, W, H, GP->ndilations);
                FREE(er);
                DELTA("Opening");
                savebin(opn, W, H, "opening.fits");
                DELTA("Save opening");
                ConnComps *cc;
                size_t *S = cclabel4(opn, W, H, &cc);
                //double averW = 0., averH = 0.;
                //green("Found %zd objects\n", cc->Nobj-1);
                //printf("%6s\t%6s\t%6s\t%6s\t%6s\t%6s\t%6s\n", "N", "area", "w/h", "Xc", "Yc", "Xw", "Yw");
                object *Objects = MALLOC(object, cc->Nobj-1);
                int objctr = 0;
                for(size_t i = 1; i < cc->Nobj; ++i){
                    Box *b = &cc->boxes[i];
                    //DBG("BOX %zd: area=%d, x:(%d-%d), y:(%d:%d)", i, b->area, b->xmin, b->xmax, b->ymin, b->ymax);
                    double wh = ((double)b->xmax - b->xmin)/(b->ymax - b->ymin);
                    if(wh < 0.77 || wh > 1.3) continue;
                    if((int)b->area < GP->minarea) continue;
                    double xc = 0., yc = 0.;
                    double x2c = 0., y2c = 0., Isum = 0.;
                    for(size_t y = b->ymin; y <= b->ymax; ++y){
                        size_t idx = y*W + b->xmin;
                        size_t *maskptr = &S[idx];
                        Imtype *Iptr = &I->data[idx];
                        for(size_t x = b->xmin; x <= b->xmax; ++x, ++maskptr, ++Iptr){
                            //DBG("(%zd, %zd): %zd / %g", x, y, *maskptr, *Iptr);
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
                    ///object *o = &Objects[objctr-1];
                    ///printf("%6d\t%6d\t%14.1f\t%6.1f\t%6.1f\t%6.1f\t%6.1f\t%6.1f\n", i, o->area, 40.-2.5*log(o->Isum), o->WdivH, o->xc, o->yc, o->xsigma, o->ysigma);
                    //averH += y2c; averW += x2c;
                }
                DELTA("Labeling");
                printf("%zd %d\n", time(NULL), objctr);
                qsort(Objects, objctr, sizeof(object), compObjs);
                object *o = Objects;
                for(int i = 0; i < objctr; ++i, ++o){
                    // 1.0857 = 2.5/ln(10)
                    printf("%6d\t%6d\t%6.1f\t%6.1f\t%6.1f\t%6.1f\t%6.1f\t%6.1f\n", i, o->area, 20.-1.0857*log(o->Isum), o->WdivH, o->xc, o->yc, o->xsigma, o->ysigma);
                }
                FREE(cc);
                FREE(Objects);
                Image *c = ST2Im(S, W, H);
                FREE(S);
                DELTA("conv size_t -> Ima");
                save_fits(c, "size_t.fits");
                Image_free(&c);
                DELTA("Save size_t");
#if 0
                uint8_t *f4 = filter8(ibin, W, H);
                DELTA("calc f8");
                savebin(f4, W, H, "f8.fits");
                DELTA("save f8.fits");
                uint8_t *e1 = erosion(ibin, W, H);
                DELTA("Get e");
                savebin(e1, W, H, "er.fits");
                DELTA("Save er.fits");
                uint8_t *e2 = dilation(ibin, W, H);
                DELTA("Get di");
                savebin(e2, W, H, "dil.fits");
                DELTA("Save dil.fits");
                FREE(e1);
                FREE(e2);
#endif
#if 0
                e1 = openingN(ibin, W, H, 1);
                savebin(e1, W, H, "op.fits");
                e2 = closingN(ibin, W, H, 1);
                savebin(e2, W, H, "cl.fits");
                FREE(e1);
                FREE(e2);
#endif
                FREE(ibin);
                FREE(opn);
            }
        }
#ifdef GETMEDIAN
        Image_free(&M);
    }
#endif
    DELTA("End");
    Image_free(&I);
}

static int process_input(InputType tp, char *name){
    if(tp == T_DIRECTORY) return watch_directory(name, process_file);
    return watch_file(name, process_file);
}

int main(int argc, char *argv[]){
    initial_setup();
    char *self = strdup(argv[0]);
    GP = parse_args(argc, argv);
    if(GP->throwpart < 0. || GP->throwpart > 0.99){
        ERRX("Fraction of black pixels should be in [0., 0.99]");
    }
    InputType tp = chk_inp(GP->inputname);
    if(tp == T_WRONG) ERRX("Enter correct image file or directory name");
    check4running(self, GP->pidfile);
    DBG("%s started, snippets library version is %s\n", self, sl_libversion());
    free(self);
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    if(GP->logfile){
        sl_loglevel lvl = LOGLEVEL_ERR; // default log level - errors
        int v = GP->verb;
        while(v--){ // increase loglevel for each "-v"
            if(++lvl == LOGLEVEL_ANY) break;
        }
        OPENLOG(GP->logfile, lvl, 1);
        DBG("Opened log file @ level %d", lvl);
    }
    LOGMSG("Start application...");
    int p = process_input(tp, GP->inputname);
    // never reached
    signals(p); // clean everything
    return p;
}
