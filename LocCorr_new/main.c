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
#include <pthread.h>
#include <signal.h>         // signal
#include <stdio.h>
#include <string.h>         // strdup
#include <sys/prctl.h>      //prctl
#include <sys/wait.h>       // wait

#include "cmdlnopts.h"
#include "config.h"
#include "debug.h"
#include "improc.h"
#include "steppers.h"
#include "socket.h"

static InputType tp;
static pid_t childpid;

/**
 * We REDEFINE the default WEAK function of signal processing
 */
void signals(int sig){
    DBG("SIGN: %d, child PID: %d", sig, childpid);
    if(childpid){
        DBG("Father -> just exit");
        exit(sig); // father -> do nothin @ end
    }
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d.", sig);
    }
    stopwork = TRUE;
    saveconf(NULL);
    if(GP && GP->pidfile){ // remove unnesessary PID file
        DBG("unlink(GP->pidfile)");
        unlink(GP->pidfile);
    }
    if(theSteppers && theSteppers->stepdisconnect) theSteppers->stepdisconnect();
    DBG("closeXYlog()");
    closeXYlog();
    DBG("EXIT %d", sig);
    LOGERR("Exit with status %d", sig);
    exit(sig);
}

void iffound_default(pid_t pid){
    ERRX("Another copy of this process found, pid=%d. Exit.", pid);
}

static void *procinp_thread(_U_ void* arg){
    LOGDBG("procinp_thread(%s)", GP->inputname);
    int p = process_input(tp, GP->inputname);
    LOGERR("procinp_thread(%s)=%d", GP->inputname, p);
    return NULL;
}

static InputType chk_inp(const char *name){
    if(!name) ERRX("Point file or directory name to monitor");
    InputType itp = chkinput(name);
    if(T_WRONG == itp) return T_WRONG;
    green("\n%s is a ", name);
    switch(itp){
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
        case T_CAPT_BASLER:
            printf("capture basler camera");
        break;
        case T_CAPT_HIKROBOT:
            printf("hikrobot camera capture");
        break;
        default:
            printf("unsupported type\n");
            return T_WRONG;
    }
    printf("\n");
    return itp;
}

int main(int argc, char *argv[]){
    sl_init();
    char *self = strdup(argv[0]);
    GP = parse_args(argc, argv);
    if(!chkconfig(GP->configname)){
        LOGWARN("Wrong/absent configuration file");
        WARNX("Wrong/absent configuration file");
        if(GP->chkconf) return 1;
    }
    if(GP->chkconf){
        printf("File %s OK\n", GP->configname);
        return 0;
    }
    if(GP->throwpart < 0. || GP->throwpart > 0.99){
        ERRX("Fraction of black pixels should be in [0., 0.99]");
    }
    if(GP->Naveraging < 1 || GP->Naveraging > NAVER_MAX)
        ERRX("Averaging amount should be from 1 to %d", NAVER_MAX);
    tp = chk_inp(GP->inputname);
    if(tp == T_WRONG) ERRX("Enter correct image file or directory name");
    // check ability of saving file
    {
        FILE *f = fopen(GP->outputjpg, "w");
        if(!f) ERR("Can't create %s", GP->outputjpg);
        fclose(f);
    }
    if(GP->logfile){
        sl_loglevel_e lvl = LOGLEVEL_ERR + GP->verb; // default log level - errors
        if(lvl > LOGLEVEL_ANY) lvl = LOGLEVEL_ANY;
        OPENLOG(GP->logfile, lvl, 1);
        DBG("Opened log file @ level %d", lvl);
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
        if(GP->steppersport != DEFAULT_STEPPERSPORT || theconf.stpserverport == 0){
            if(GP->steppersport < 1 || GP->steppersport > 65535) ERRX("Wrong steppers' server port: %d", GP->steppersport);
            theconf.stpserverport = GP->steppersport;
        }
    }
    sl_check4running(self, GP->pidfile);
    DBG("%s started, snippets library version is %s\n", self, sl_libversion());
    free(self); self = NULL;
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, SIG_IGN);  // hup - ignore
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    DBGLOG("\n\n\nStarted; capt: %s", GP->inputname);
    while(1){ // guard for dead processes
        childpid = fork();
        if(childpid){ // father
            LOGMSG("create child with PID %d\n", childpid);
            DBG("Created child with PID %d\n", childpid);
            wait(NULL);
            LOGMSG("child %d died\n", childpid);
            WARNX("Child %d died\n", childpid);
            sleep(5);
        }else{ // son
            //prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
            break; // go out to normal functional
        }
    }
    DBGLOG("start thread; capt: %s", GP->inputname);
    if(!(theSteppers = steppers_connect())){
        LOGERR("Steppers server unavailable, can't run");
        WARNX("Steppers server unavailable, can't run");
    }
    if(GP->logXYname) openXYlog(GP->logXYname);
    LOGMSG("Start application...");
    LOGDBG("xtag=%g, ytag=%g", theconf.xtarget, theconf.ytarget);
    openIOport(GP->ioport);
    pthread_t inp_thread;
    if(pthread_create(&inp_thread, NULL, procinp_thread, NULL)){
        LOGERR("pthread_create() for image input failed");
        ERR("pthread_create()");
    }
    while(1){
        if(stopwork || pthread_kill(inp_thread, 0) == ESRCH){
            DBG("close");
            pthread_join(inp_thread, NULL);
            DBG("out");
            return 0;
        }
    };
    return 0;
}
