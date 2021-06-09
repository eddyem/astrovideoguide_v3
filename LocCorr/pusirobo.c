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
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <math.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <usefull_macros.h>

#include "config.h"
#include "pusirobo.h"
#include "socket.h"

// max time to wait answer "OK" from server
#define WAITANSTIME         (1.0)
#define ANSOK               "OK\n"

// amount of consequent center coordinates coincidence in `process_targetstate`
#define NCONSEQ             (2)
// tolerance of coordinates coincidence (pix)
#define COORDTOLERANCE      (0.1)

// messages for CAN server
#define registerUaxe        "register U 0x581 stepper"
#define registerVaxe        "register V 0x582 stepper"
#define registerFocus       "register F 0x583 stepper"
#define setUspeed           "mesg U maxspeed 12800"
#define setVspeed           "mesg V maxspeed 12800"
#define setFspeed           "mesg F maxspeed 1600"
#define Urelsteps           "mesg U relmove "
#define Vrelsteps           "mesg V relmove "
#define Fabssteps           "mesg F absmove "
#define Frelsteps           "mesg F relmove "
#define Ustatus             "mesg U status"
#define Vstatus             "mesg V status"
#define Fstatus             "mesg F status"
#define Usetzero            "mesg U setzero"
#define Vsetzero            "mesg V setzero"
#define Fsetzero            "mesg F setzero"
// parameter's names
#define PARstatus           "devstatus"
#define STEPSstatus         "steps"
#define ERRstatus           "errstatus"
#define CURPOSstatus        "curpos"
// max range of U and V motors (all in microsteps!)
#define UVmaxsteps          (35200)
#define Fmaxsteps           (64000)
// steps to move from the edge
#define UVedgesteps         (960)


#define moveU(s)                move_motor(Urelsteps, s)
#define moveV(s)                move_motor(Vrelsteps, s)
#define moveF(s)                move_motor(Frelsteps, s)
#define setF(s)                 move_motor(Fabssteps, s)
#define UVmoving_finished()     (moving_finished(Ustatus, NULL) && moving_finished(Vstatus, NULL))

typedef enum{
    SETUP_NONE,             // no setup
    SETUP_INIT,             // the starting - move U&V to 0
    SETUP_WAITUV0,          // wait & move U&V to middle
    SETUP_WAITUVMID,        // wait
    SETUP_WAITU0,           // move U->0
    SETUP_WAITUMAX,         // move U->max
    SETUP_WAITV0,           // V->0
    SETUP_WAITVMAX,         // V->max
    SETUP_FINISH
} setupstatus;
static setupstatus sstatus = SETUP_NONE; // setup state

static pusistate state = PUSI_DISCONN;   // server state

static int sockfd = -1; // server file descriptor

// current steps counters (zero at the middle)
static int Uposition = 0, Vposition = 0, Fposition = 0;

void pusi_disconnect(){
    if(sockfd > -1) close(sockfd);
    sockfd = -1;
    state = PUSI_DISCONN;
}

static char *findval(const char *par, const char *statusmesg){
    if(!statusmesg || !par) return NULL;
    char *pair = strcasestr(statusmesg, par);
    if(!pair) return NULL;
    pair += strlen(par);
    while(*pair && *pair != '\n' && *pair != '=') ++pair;
    if(*pair != '=') return NULL; // no equal sign
    ++pair; while(*pair == ' ' || *pair == '\t') ++pair;
    //DBG("val fof '%s' is '%s'", par, pair);
    return pair;
}

/**
 * @brief getparval - return value of parameter
 * @param par (i)        - parameter value
 * @param statusmesg (i) - message of 'status' command
 * @param val (o)        - value of parameter
 * @return TRUE if parameter found and set `val` to its value
 */
static int getparval(const char *par, const char *statusmesg, double *val){
    char *parval = findval(par, statusmesg);
    if(!parval) return FALSE;
    if(!val) return TRUE;
    *val = atof(parval);
    return TRUE;
}
// the same as getparval, but check for "=OK"
static int getOKval(const char *par, const char *statusmesg){
    //DBG("getOKval('%s', '%s')", par, statusmesg);
    char *parval = findval(par, statusmesg);
    if(!parval) return FALSE;
    if(strncmp(parval, "OK", 2) == 0) return TRUE;
    return FALSE;
}

/**
 * wait for answer from socket
 * @return FALSE in case of error or timeout, TRUE if socket is ready
 */
static int canread(){
    if(sockfd < 0) return FALSE;
    fd_set fds;
    struct timeval timeout;
    int rc;
    // wait not more than 10ms
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);
    do{
        rc = select(sockfd+1, &fds, NULL, NULL, &timeout);
        if(rc < 0){
            if(errno != EINTR){
                WARN("select()");
                return FALSE;
            }
            continue;
        }
        break;
    }while(1);
    if(FD_ISSET(sockfd, &fds)) return TRUE;
    return FALSE;
}

// clear incoming buffer
static void clearbuf(){
    if(sockfd < 0) return;
    char buf[256] = {0};
    while(canread() && 0 < read(sockfd, buf, 256)) DBG("clearbuf: %s", buf);
}

/**
 * read answer "OK" from socket
 * @param retval - if !NULL there's will be an answer copy (after "OK\n") here
 * @return FALSE if timeout or no "OK"
 */
static int waitOK(char **retval){
    if(sockfd < 0) return FALSE;
#define BUFFERSZ    (2047)
    char buf[BUFFERSZ+1];
    int Nread = 0, ctr = 0;
    double t0 = dtime();
    while(dtime() - t0 < WAITANSTIME && Nread < BUFFERSZ){
        if(!canread()){
            //DBG("No answer @ %d try", ctr);
            if(++ctr > 3) break;
            continue;
        }
        ctr = 0;
        int n = read(sockfd, buf+Nread, BUFFERSZ-Nread);
        //DBG("n=%d", n);
        if(n == 0) break;
        if(n < 0) return FALSE; // disconnect or error
        Nread += n;
        buf[Nread] = 0;
    }
    //DBG("All buffer: '%s'", buf);
    int ret = FALSE;
    char *ok = strstr(buf, ANSOK);
    if(ok){
        //DBG("ans: '%s'", OK + sizeof(ANSOK)-1);
        ret = TRUE;
        if(retval){
            *retval = strdup(ok + sizeof(ANSOK)-1);
            //DBG("RETVAL: '%s'", *retval);
        }
    }else LOGWARN("didn't get OK answer");
#undef BUFFERSZ
    return ret;
}

/**
 * @brief send_message - send character string `msg` to pusiserver
 * @param msg - message
 * @param ans - answer (if !NULL)
 * @return FALSE if failed (should reconnect)
 */
static int send_message(const char *msg, char **ans){
    if(!msg || sockfd < 0) return FALSE;
    size_t L = strlen(msg);
    clearbuf();
    if(send(sockfd, msg, L, 0) != (ssize_t)L){
        LOGWARN("send_message(): send() failed");
        return FALSE;
    }
    DBG("Message '%s' sent", msg);
    return waitOK(ans);
}

static void send_message_nocheck(const char *msg){
    if(!msg || sockfd < 0) return;
    size_t L = strlen(msg);
    clearbuf();
    if(send(sockfd, msg, L, 0) != (ssize_t)L){
        WARN("send");
    }
    DBG("Unchecked message '%s' sent", msg);
}

// try to set default speeds
static int setSpeed(const char *mesg, const char *name){
    char *ans = NULL;
    int retval = TRUE;
    if(!send_message(mesg, &ans)){
        LOGERR("Can't set %s motor speed", name);
        retval = FALSE;
    }
    if(ans && *ans){
        DBG("ans: %s\n", ans);
    }else{
        LOGERR("no %s motor", name);
        retval = FALSE;
    }
    FREE(ans);
    return retval;
}

/**
 * @brief pusi_connect - connect to a local steppers CAN server
 * @return FALSE if failed
 */
int pusi_connect(){
    DBG("pusi_connect(%d)", theconf.stpserverport);
    char port[10];
    snprintf(port, 10, "%d", theconf.stpserverport);
    pusi_disconnect();
    struct addrinfo hints = {0}, *res, *p;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if(getaddrinfo(NULL, port, &hints, &res) != 0){
        WARN("getaddrinfo()");
        return FALSE;
    }
    // loop through all the results and connect to the first we can
    for(p = res; p; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            WARN("socket");
            continue;
        }
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            WARN("connect()");
            close(sockfd);
            continue;
        }
        break; // if we get here, we have a successfull connection
    }
    if(!p){
        WARNX("Can't connect to socket");
        sockfd = -1;
        return FALSE;
    }
    freeaddrinfo(res);
    // register and set max speed; don't check `register` answer as they could be registered already
    send_message_nocheck(registerUaxe);
    send_message_nocheck(registerVaxe);
    send_message_nocheck(registerFocus);
    int retval = TRUE;
    if(!setSpeed(setUspeed, "U")) retval = FALSE;
    if(!setSpeed(setVspeed, "V")) retval = FALSE;
    if(!setSpeed(setFspeed, "F")) retval = FALSE;
    if(!retval) pusi_disconnect();
    else{
        state = PUSI_RELAX;
        sstatus = SETUP_NONE;
    }
    return retval;
}

// return TRUE if motor is stopped
static int moving_finished(const char *mesgstatus, int *position){
    double val;
    char *ans = NULL;
    int ret = TRUE;
    if(send_message(mesgstatus, &ans) && getparval(PARstatus, ans, &val)){
        DBG("send(%s) true: %s %g\n", mesgstatus, ans, val);
    }else{
        LOGDBG("send(%s) false: %s %g\n", mesgstatus, ans, val);
        return FALSE;
    }
    int ival = (int)val;
    if(ival) ret = FALSE;
    if(position){
        if(getparval(CURPOSstatus, ans, &val)){
            *position = (int) val;
        }else LOGDBG("%s not found in '%s'", CURPOSstatus, ans);
    }
    FREE(ans);
    return ret;
}

// move motor to s steps, @return FALSE if failed
static int move_motor(const char *movecmd, int s/*, int *counter*/){
    DBG("move %s -> %d", movecmd, s);
    LOGDBG("move %s -> %d", movecmd, s);
    char buf[256], *ans;
    snprintf(buf, 255, "%s %d", movecmd, s);
    if(!send_message(buf, &ans)){
        LOGDBG("can't send message");
        return FALSE;
    }
    int ret = TRUE;
    if(!getOKval(STEPSstatus, ans)){
        LOGDBG("NO OK in %s", ans);
        ret = FALSE;
    }
    FREE(ans);
    return ret;
}

static void process_movetomiddle_stage(){
    switch(sstatus){
        case SETUP_INIT: // initial moving
            if(moveU(-UVmaxsteps) && moveV(-UVmaxsteps) && moveF(-Fmaxsteps*2))
                sstatus = SETUP_WAITUV0;
        break;
        case SETUP_WAITUV0: // wait for both coordinates moving to zero
            DBG("Moving to left border");
            if(!(UVmoving_finished() && moving_finished(Fstatus, NULL))) return;
            DBG("Reached!");
            if(!send_message(Fsetzero, NULL)) return;
            Fposition = 0;
            if(moveU(theconf.maxUsteps+UVedgesteps) && moveV(theconf.maxUsteps+UVedgesteps) && moveF(Fmaxsteps/2))
                sstatus = SETUP_WAITUVMID;
        break;
        case SETUP_WAITUVMID: // wait for the middle
            DBG("Moving to the middle");
            if(!(UVmoving_finished() && moving_finished(Fstatus, NULL))) return;
            DBG("Reached!");
            Uposition = 0; Vposition = 0; Fposition = Fmaxsteps/2;
            if(!send_message(Usetzero, NULL) || !send_message(Vsetzero, NULL)) return;
            sstatus = SETUP_NONE;
            state = PUSI_RELAX;
        break;
        default:
            sstatus = SETUP_NONE;
            state = PUSI_RELAX;
    }
}

/**
 * @brief process_setup_stage - process all stages of axes setup
 */
static void process_setup_stage(double x, double y, int aver){
    DBG("PROCESS: %d\n", sstatus);
    static int ctr; // iterations counter
     // coordinates for corrections calculation
    static double X0U, Y0U, XmU, YmU;
    static double X0V, Y0V, XmV, YmV;
    switch(sstatus){
        case SETUP_INIT: // initial moving
            if(moveU(-UVmaxsteps) && moveV(-UVmaxsteps))
                sstatus = SETUP_WAITUV0;
        break;
        case SETUP_WAITUV0: // wait for both coordinates moving to zero
            DBG("Moving to left border");
            if(!UVmoving_finished()) return;
            DBG("Reached!");
            if(moveU(theconf.maxUsteps+UVedgesteps) && moveV(theconf.maxUsteps+UVedgesteps))
                sstatus = SETUP_WAITUVMID;
        break;
        case SETUP_WAITUVMID: // wait for the middle
            DBG("Moving to the middle");
            if(!UVmoving_finished()) return;
            DBG("Reached!");
            Uposition = 0; Vposition = 0;
            if(moveU(-theconf.maxUsteps)) sstatus = SETUP_WAITU0;
            ctr = 0;
        break;
        case SETUP_WAITU0: // wait while U moves to zero
            if(!aver) return;
            if(!moving_finished(Ustatus, NULL)) return;
            if(++ctr < 2) return; // wait for next average coordinates
            X0U = x; Y0U = y;
            LOGDBG("got X0U=%.1f, Y0U=%.1f", x, y);
            if(moveU(2*theconf.maxUsteps)) sstatus = SETUP_WAITUMAX;
            ctr = 0;
        break;
        case SETUP_WAITUMAX: // wait while U moves to UVworkrange
            if(!aver) return;
            if(!moving_finished(Ustatus, NULL)) return;
            if(++ctr < 2) return; // wait for next average coordinates
            XmU = x; YmU = y;
            LOGDBG("got XmU=%.1f, YmU=%.1f", x, y);
            if(moveU(-theconf.maxUsteps) && moveV(-theconf.maxVsteps)) sstatus = SETUP_WAITV0;
            ctr = 0;
        break;
        case SETUP_WAITV0: // wait while V moves to 0
            if(!aver) return;
            if(!moving_finished(Vstatus, NULL)) return;
            if(++ctr < 2) return; // wait for next average coordinates
            X0V = x; Y0V = y;
            LOGDBG("got X0V=%.1f, Y0V=%.1f", x, y);
            if(moveV(2*theconf.maxVsteps)) sstatus = SETUP_WAITVMAX;
            ctr = 0;
        break;
        case SETUP_WAITVMAX: // wait while V moves to UVworkrange
            if(!aver) return;
            if(!moving_finished(Vstatus, NULL)) return;
            if(++ctr < 2) return; // wait for next average coordinates
            ctr = 0;
            XmV = x; YmV = y;
            LOGDBG("got XmV=%.1f, YmV=%.1f", x, y);
            // calculate
            double dxU = XmU - X0U, dyU = YmU - Y0U, dxV = XmV - X0V, dyV = YmV - Y0V;
            LOGDBG("dxU=%.1f, dyU=%.1f, dxV=%.1f, dyV=%.1f", dxU, dyU, dxV, dyV);
            double sqU = sqrt(dxU*dxU + dyU*dyU), sqV = sqrt(dxV*dxV + dyV*dyV);
            LOGDBG("sqU=%g, sqV=%g", sqU, sqV);
            if(sqU < DBL_EPSILON || sqV < DBL_EPSILON) goto endmoving;
            // TODO: check configuration !!111111
            // proportion coefficients for axes
            double KU = 2 * theconf.maxUsteps / sqU;
            double KV = 2 * theconf.maxVsteps / sqV;
            double sa = dyU/sqU, ca = dxU/sqU, sb = dyV/sqV, cb = dxV/sqV; // sin(alpha) etc
            // ctg(beta-alpha)=cos(b-a)/sin(b-a)=[cos(b)cos(a)+sin(b)sin(a)]/[sin(b)cos(a)-cos(b)sin(a)]
            double sba = sb*ca - cb*sa; // sin(beta-alpha)
            double ctba = (cb*ca + sb*sa) / sba;
            if(fabs(ctba) < DBL_EPSILON || fabs(sba) < DBL_EPSILON) goto endmoving;
            LOGDBG("KU=%.4f, KV=%.4f, sa=%.4f, ca=%.4f, sb=%.4f, cb=%.4f, ctba=%.5f, 1/sba=%.5f",
                   KU, KV, sa, ca, sb, cb, ctba, 1./sba);
            /*
             * U = x*(cos(alpha) - sin(alpha)*ctg(beta-alpha)) + y*(-sin(alpha)-cos(alpha)*ctg(beta-alpha))
             * V = x*sin(alpha)/sin(beta-alpha) + y*cos(alpha)/sin(beta-alpha)
             */
            theconf.Kxu = KU*(ca - sa*ctba);
            theconf.Kyu = KU*(-sa - ca*ctba);
            theconf.Kxv = KV*sa/sba;
            theconf.Kyv = KV*ca/sba;
            LOGDBG("Kxu=%g, Kyu=%g; Kxv=%g, Kyv=%g", theconf.Kxu, theconf.Kyu, theconf.Kxv, theconf.Kyv);
            DBG("Now save new configuration");
            saveconf(NULL); // try to store configuration
        endmoving:
            moveV(-theconf.maxVsteps);
            sstatus = SETUP_FINISH;
        break;
        case SETUP_FINISH: // reset current coordinates
            if(!UVmoving_finished()) return;
            if(!send_message(Usetzero, NULL) || !send_message(Vsetzero, NULL)) return;
            // now inner steppers' counters are in zero position -> set to zero local
            Uposition = Vposition = 0;
            sstatus = SETUP_NONE;
            state = PUSI_RELAX;
        break;
        default: // SETUP_NONE - do nothing
            return;
    }
}

// return TRUE if finished
static int process_targetstage(double X, double Y){
    static double xprev = 0., yprev = 0.;
    static int nhit = 0;
    if(fabs(X - xprev) > COORDTOLERANCE || fabs(Y - yprev) > COORDTOLERANCE){
        DBG("tolerance too bad: dx=%g, dy=%g", X-xprev, Y-yprev);
        nhit = 0;
        xprev = X; yprev = Y;
        return FALSE;
    }else if(++nhit < NCONSEQ){
        DBG("nhit = %d", nhit);
        return FALSE;
    }
    theconf.xtarget = X;
    theconf.ytarget = Y;
    DBG("Got target coordinates: (%.1f, %.1f)", X, Y);
    saveconf(FALSE);
    nhit = 0; xprev = 0.; yprev = 0.;
    return TRUE;
}

/**
 * @brief try2correct - try to correct position
 * @param dX - delta of X-coordinate in image space
 * @param dY - delta of Y-coordinate in image space
 * @return FALSE if failed or correction out of limits
 */
static int try2correct(double dX, double dY){
    double dU, dV;
    // dU = KU*(dX*cosXU + dY*sinXU); dV = KV*(dX*cosXV + dY*sinXV)
    dU = KCORR*(theconf.Kxu * dX + theconf.Kyu * dY);
    dV = KCORR*(theconf.Kxv * dX + theconf.Kyv * dY);
    int Unew = Uposition + (int)dU, Vnew = Vposition + (int)dV;
    if(Unew > theconf.maxUsteps || Unew < -theconf.maxUsteps ||
       Vnew > theconf.maxVsteps || Vnew < -theconf.maxVsteps){
        // TODO: here we should signal the interface that limit reaced
        LOGWARN("Correction failed, curpos: %d, %d, should move to %d, %d",
                Uposition, Vposition, Unew, Vnew);
        return FALSE;
    }
    LOGDBG("try2correct(): move from (%d, %d) to (%d, %d) (abs: %d, %d), delta (%.1f, %.1f)",
           Uposition, Vposition, Unew, Vnew, Uposition + (int)(dU/KCORR),
           Vposition + (int)(dV/KCORR), dU, dV);
    return (moveU((int)dU) && moveV((int)dV));
}

#if 0
mesg U relmove -35200
mesg V relmove -35200
mesg U relmove 16960
mesg V relmove 16960
mesg U relmove -500000
mesg U relmove 100000
mesg F relmove 32000
#endif
/**
 * @brief pusi_process_corrections - get XY corrections (in pixels) and move motors to fix them
 * @param X, Y - centroid (x,y) in screen coordinate system
 * @param aver ==1 if X and Y are averaged
 * This function called from improc.c each time the corrections calculated (ONLY IF Xtarget/Ytarget > -1)
 */
void pusi_process_corrections(double X, double Y, int aver){
    DBG("got centroid data: %g, %g", X, Y);
    double xdev = X - theconf.xtarget, ydev = Y - theconf.ytarget;
    switch(state){
        case PUSI_DISCONN:
            if(!pusi_connect()){
                WARN("Can't reconnect");
                LOGWARN("Can't reconnect");
            }
        break;
        case PUSI_SETUP: // setup axes (before this state set Xtarget/Ytarget in improc.c)
            process_setup_stage(X, Y, aver);
        break;
        case PUSI_GOTOTHEMIDDLE:
            process_movetomiddle_stage();
        break;
        case PUSI_FINDTARGET: // calculate target coordinates
            if(aver && process_targetstage(X, Y))
                state = PUSI_RELAX;
        break;
        case PUSI_FIX:   // process corrections
            if(aver){
                red("GET AVERAGE -> correct\n");
                if(theconf.xtarget < 1. || theconf.ytarget < 1. || fabs(xdev) < COORDTOLERANCE || fabs(ydev) < COORDTOLERANCE){
                    DBG("Target coordinates not defined or correction too small");
                    return;
                }
                if(!moving_finished(Ustatus, &Uposition) || !moving_finished(Vstatus, &Vposition)) return;
                LOGDBG("Current position: U=%d, V=%d, deviations: dX=%.1f, dy=%.1f",
                       Uposition, Vposition, xdev, ydev);
                if(!try2correct(xdev, ydev)){
                    LOGWARN("failed to correct");
                    // TODO: do something here
                    DBG("FAILED");
                }
            }
        break;
        default: // PUSI_RELAX
            return;
    }
}

// try to change state; @return TRUE if OK
int pusi_setstate(pusistate newstate){
    if(newstate == state) return TRUE;
    if(newstate == PUSI_DISCONN){
        pusi_disconnect();
        return TRUE;
    }
    if(state == PUSI_DISCONN){
        if(!pusi_connect()) return FALSE;
    }
    if(newstate == PUSI_SETUP || newstate == PUSI_GOTOTHEMIDDLE){
        sstatus = SETUP_INIT;
    }else sstatus = SETUP_NONE;
    state = newstate;
    return TRUE;
}

pusistate pusi_getstate(){
    return state;
}

// get current status
// return JSON string with different parameters
char *pusi_status(char *buf, int buflen){
    int l;
    char *bptr = buf;
    const char *s = NULL, *stage = NULL;
    l = snprintf(bptr, buflen, "{ \"status\": ");
    buflen -= l; bptr += l;
    switch(state){
        case PUSI_DISCONN:
            l = snprintf(bptr, buflen, "\"disconnected\"");
        break;
        case PUSI_RELAX:
            l = snprintf(bptr, buflen, "\"ready\"");
        break;
        case PUSI_SETUP:
        case PUSI_GOTOTHEMIDDLE:
            s = (state == PUSI_SETUP) ? "setup" : "gotomiddle";
            switch(sstatus){
                case SETUP_INIT:
                    stage = "init";
                break;
                case SETUP_WAITUV0:
                     stage = "waituv0";
                break;
                case SETUP_WAITUVMID:
                     stage = "waituvmid";
                break;
                case SETUP_WAITU0:
                     stage = "waitu0";
                break;
                case SETUP_WAITUMAX:
                     stage = "waitumax";
                break;
                case SETUP_WAITV0:
                     stage = "waitv0";
                break;
                case SETUP_WAITVMAX:
                     stage = "waitvmax";
                break;
                case SETUP_FINISH:
                     stage = "finishing";
                break;
                default:
                     stage = "unknown";
            }
            l = snprintf(bptr, buflen, "{ \"%s\": \"%s\" }", s, stage);
        break;
        case PUSI_FINDTARGET:
            l = snprintf(bptr, buflen, "\"findtarget\"");
        break;
        case PUSI_FIX:
            l = snprintf(bptr, buflen, "\"fixing\"");
        break;
        default:
            l = snprintf(bptr, buflen, "\"unknown\"");
    }
    buflen -= l; bptr += l;
    if(state != PUSI_DISCONN){
        l = snprintf(bptr, buflen, ", ");
        buflen -= l; bptr += l;
        const char *motors[] = {"Umotor", "Vmotor", "Fmotor"};
        const char *statuses[] = {Ustatus, Vstatus, Fstatus};
        int *pos[] = {&Uposition, &Vposition, &Fposition};
        for(int i = 0; i < 3; ++i){
            const char *stat = "moving";
            if(moving_finished(statuses[i], pos[i])) stat = "stopping";
            l = snprintf(bptr, buflen, "\"%s\": { \"status\": \"%s\", \"position\": %d }%s",
                         motors[i], stat, *pos[i], (i==2)?"":", ");
            buflen -= l; bptr += l;
        }
    }
    snprintf(bptr, buflen, " }\n");
    return buf;
}

typedef struct{
    const char *str;
    pusistate state;
} strstate;

strstate stringstatuses[] = {
    {"disconnect", PUSI_DISCONN},
    {"relax", PUSI_RELAX},
    {"setup", PUSI_SETUP},
    {"middle", PUSI_GOTOTHEMIDDLE},
    {"findtarget", PUSI_FINDTARGET},
    {"fix", PUSI_FIX},
    {NULL, 0}
};
// try to set new status
char *set_pusistatus(const char *newstatus, char *buf, int buflen){
    strstate *s = stringstatuses;
    pusistate newstate = PUSI_UNDEFINED;
    while(s->str){
        if(strcasecmp(s->str, newstatus) == 0){
            newstate = s->state;
            break;
        }
        ++s;
    }
    if(newstate != PUSI_UNDEFINED){
        if(pusi_setstate(newstate)){
            snprintf(buf, buflen, OK);
            return buf;
        }else return pusi_status(buf, buflen);
    }
    int L = snprintf(buf, buflen, "status '%s' undefined, allow: ", newstatus);
    char *ptr = buf;
    s = stringstatuses;
    while(L > 0){
        buflen -= L;
        ptr += L;
        L = snprintf(ptr, buflen, "'%s' ", s->str);
        if((++s)->str == NULL) break;
    }
    ptr[L-1] = '\n';
    return buf;
}
// change focus
char *set_pfocus(const char *newstatus, char *buf, int buflen){
    if(!moving_finished(Fstatus, &Fposition)){
        snprintf(buf, buflen, "moving\n");
        return buf;
    }
    int newval = atoi(newstatus);
    if(newval < 0 || newval > Fmaxsteps){
        snprintf(buf, buflen, "Bad value: %d", newval);
    }else{
        if(!setF(newval)) snprintf(buf, buflen, FAIL);
        else snprintf(buf, buflen, OK);
    }
    return buf;
}
