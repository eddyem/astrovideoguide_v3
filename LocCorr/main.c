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
#include <string.h>         // strdup
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "config.h"
#include "grasshopper.h"
#include "improc.h"
#include "pusirobo.h"
#include "socket.h"

/**
 * We REDEFINE the default WEAK function of signal processing
 */
void signals(int sig){
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    DBG("exit %d", sig);
    LOGERR("Exit with status %d", sig);
    stopwork = 1;
    saveconf(NULL);
    usleep(10000);
    DBG("disconnectGrasshopper()");
    disconnectGrasshopper();
    DBG("pusi_disconnect()");
    pusi_disconnect();
    DBG("closeXYlog()");
    closeXYlog();
    if(GP && GP->pidfile){ // remove unnesessary PID file
        DBG("unlink(GP->pidfile)");
        unlink(GP->pidfile);
    }
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
        case T_CAPT_GRASSHOPPER:
            printf("capture grasshopper camera");
        break;
        default:
            printf("Unsupported type\n");
            return T_WRONG;
    }
    printf("\n");
    return tp;
}

int main(int argc, char *argv[]){
    initial_setup();
    char *self = strdup(argv[0]);
    GP = parse_args(argc, argv);
    if(GP->throwpart < 0. || GP->throwpart > 0.99){
        ERRX("Fraction of black pixels should be in [0., 0.99]");
    }
    if(GP->Naveraging < 2 || GP->Naveraging > MAX_AVERAGING_ARRAY_SIZE)
        ERRX("Averaging amount should be from 2 to 25");
    InputType tp = chk_inp(GP->inputname);
    if(tp == T_WRONG) ERRX("Enter correct image file or directory name");
    if(GP->logfile){
        sl_loglevel lvl = LOGLEVEL_ERR; // default log level - errors
        int v = GP->verb;
        while(v--){ // increase loglevel for each "-v"
            if(++lvl == LOGLEVEL_ANY) break;
        }
        OPENLOG(GP->logfile, lvl, 1);
        DBG("Opened log file @ level %d", lvl);
    }
    int C = chkconfig(GP->configname);
    if(!C){
        LOGWARN("Wrong/absent configuration file");
        WARNX("Wrong/absent configuration file");
    }
    // change `theconf` parameters to user values
    {
        if(GP->maxarea != DEFAULT_MAXAREA || theconf.maxarea == 0) theconf.maxarea = GP->maxarea;
        if(GP->minarea != DEFAULT_MINAREA || theconf.minarea == 0) theconf.minarea = GP->minarea;
        if(GP->xtarget > 0.) theconf.xtarget = GP->xtarget;
        if(GP->ytarget > 0.) theconf.ytarget = GP->ytarget;
        if(GP->nerosions != DEFAULT_EROSIONS || theconf.Nerosions == 0){
            if(GP->nerosions < 1 || GP->nerosions > MAX_NEROS) ERRX("Amount of erosions should be from 1 to %d", MAX_NEROS);
            theconf.Nerosions = GP->nerosions;
        }
        if(GP->ndilations != DEFAULT_DILATIONS || theconf.Ndilations == 0){
            if(GP->ndilations < 1 || GP->ndilations > MAX_NDILAT) ERRX("Amount of erosions should be from 1 to %d", MAX_NDILAT);
            theconf.Ndilations = GP->ndilations;
        }
        if(fabs(GP->throwpart - DEFAULT_THROWPART) > DBL_EPSILON || theconf.throwpart < DBL_EPSILON){
            if(GP->throwpart < 0. || GP->throwpart > MAX_THROWPART) ERRX("'thworpart' should be from 0 to %g", MAX_THROWPART);
            theconf.throwpart = GP->throwpart;
        }
        if(GP->xoff && GP->xoff < MAX_OFFSET) theconf.xoff = GP->xoff;
        if(GP->yoff && GP->yoff < MAX_OFFSET) theconf.yoff = GP->yoff;
        if(GP->width && GP->width < MAX_OFFSET) theconf.width = GP->width;
        if(GP->height && GP->height < MAX_OFFSET) theconf.height = GP->height;
        if(fabs(GP->minexp - EXPOS_MIN) > DBL_EPSILON || theconf.minexp < DBL_EPSILON){
            if(GP->minexp < DBL_EPSILON || GP->minexp > EXPOS_MAX) ERRX("Minimal exposition should be > 0 and < %g", EXPOS_MAX);
            theconf.minexp = GP->minexp;
        }
        if(fabs(GP->maxexp - EXPOS_MAX) > DBL_EPSILON || theconf.maxexp < theconf.minexp){
            if(GP->maxexp < theconf.minexp) ERRX("Maximal exposition should be greater than minimal");
            theconf.maxexp = GP->maxexp;
        }
        if(GP->equalize && theconf.equalize == 0) theconf.equalize = 1;
        if(fabs(GP->intensthres - DEFAULT_INTENSTHRES) > DBL_EPSILON){
            if(GP->intensthres < DBL_EPSILON || GP->intensthres > 1.-DBL_EPSILON) ERRX("'intensthres' should be from 0 to 1");
            theconf.intensthres = GP->intensthres;
        }
        if(GP->Naveraging != DEFAULT_NAVERAGE || theconf.naverage < 1){
            if(GP->Naveraging < 1 || GP->Naveraging > NAVER_MAX) ERRX("N images for averaging should be from 1 to %d", NAVER_MAX);
            theconf.naverage = GP->Naveraging;
        }
        if(GP->pusiservport != DEFAULT_PUSIPORT || theconf.stpserverport == 0){
            if(GP->pusiservport < 1 || GP->pusiservport > 65535) ERRX("Wrong steppers' server port: %d", GP->pusiservport);
            theconf.stpserverport = GP->pusiservport;
        }
    }
    setpostprocess(GP->processing);
    check4running(self, GP->pidfile);
    DBG("%s started, snippets library version is %s\n", self, sl_libversion());
    free(self);
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    if(GP->logXYname) openXYlog(GP->logXYname);
    LOGMSG("Start application...");
    LOGDBG("xtag=%g, ytag=%g", theconf.xtarget, theconf.ytarget);
    openIOport(GP->ioport);
    int p = process_input(tp, GP->inputname);
    DBG("process_input=%d", p);
    // never reached
    signals(p); // clean everything
    return p;
}
