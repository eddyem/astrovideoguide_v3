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

#include <signal.h>         // signal
#include <string.h>         // strdup
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "improc.h"

/**
 * We REDEFINE the default WEAK function of signal processing
 */
void signals(int sig){
    DBG("exit %d", sig);
    if(sig){
        signal(sig, SIG_IGN);
        DBG("Get signal %d, quit.\n", sig);
    }
    closeXYlog();
    if(GP && GP->pidfile) // remove unnesessary PID file
        unlink(GP->pidfile);
    LOGERR("Exit with status %d", sig);
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
    if(GP->logXYname) openXYlog(GP->logXYname);
    LOGMSG("Start application...");
    LOGDBG("xtag=%g, ytag=%g", GP->xtarget, GP->ytarget);
    int p = process_input(tp, GP->inputname);
    // never reached
    signals(p); // clean everything
    return p;
}
