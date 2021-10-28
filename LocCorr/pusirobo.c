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
#include <inttypes.h>
#include <math.h>
#include <netdb.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "config.h"
#include "debug.h"
#include "improc.h" // global variable stopwork
#include "pusirobo.h"
#include "socket.h"

// max time to wait answer "OK" from server
#define WAITANSTIME         (1.0)
#define ANSOK               "OK\n"

// amount of consequent center coordinates coincidence in `process_targetstate`
#define NCONSEQ             (2)
// tolerance of coordinates coincidence (pix)
#define COORDTOLERANCE      (0.5)

// messages for CAN server
#define registerUaxe        "register U 0x581 stepper"
#define registerVaxe        "register V 0x582 stepper"
#define registerFocus       "register F 0x583 stepper"
#define registerRelay       "register R 1 raw"
#define RelayCmd            "mesg R 1"
#define RelayAns            "#0x001"
static const int relaySetter = 0x80; // add this to command of setter
// relay commands:
typedef enum{
    R_PING = 0,
    R_RELAY,
    R_PWM,
    R_ADC,
    R_MCU,
    R_LED,
    R_BTNS,
    R_TIME,
    R_ERRCMD
} relaycommands;
#define setUspeed           "mesg U maxspeed 22400"
#define setVspeed           "mesg V maxspeed 22400"
#define setFspeed           "mesg F maxspeed 12800"
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
#define UVmaxsteps          (96000)
// steps to move from the edge
#define UVedgesteps         (3200)

#define moveU(s)                move_motor(Urelsteps, s)
#define moveV(s)                move_motor(Vrelsteps, s)
#define moveF(s)                move_motor(Frelsteps, s)
#define setF(s)                 move_motor(Fabssteps, s)

typedef enum{
    PUSI_DISCONN,
    PUSI_RELAX,
    PUSI_SETUP,
    PUSI_GOTOTHEMIDDLE,
    PUSI_FINDTARGET,
    PUSI_FIX,
    PUSI_UNDEFINED
} pusistate;

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
static _Atomic setupstatus sstatus = SETUP_NONE; // setup state

typedef struct{
    uint8_t relays;
    uint8_t PWM[3];
    uint8_t buttons[4];
} relaystate;
static _Atomic relaystate relay;

static int errctr = 0; // sending messages error counter (if > MAX_ERR_CTR, set state to disconnected)

static pusistate state = PUSI_DISCONN;   // server state
// the `ismoving` flag allows not to make corrections with bad images made when moving
static volatile atomic_bool ismoving = FALSE; // == TRUE if any of steppers @hanging part is moving
// this flag set to TRUE when next Xc,Yc available
static volatile atomic_bool coordsRdy = FALSE;
static double Xtarget = 0., Ytarget = 0.;

// flag & new focus value
static volatile atomic_bool chfocus = FALSE;
static volatile atomic_int newfocpos = 0;

static volatile atomic_int sockfd = -1; // server file descriptor
static volatile atomic_bool motorsoff = FALSE; // flag to disconnect

// mutex for message sending
static pthread_mutex_t sendmesg_mutex = PTHREAD_MUTEX_INITIALIZER;

// current steps counters (zero at the middle)
static volatile atomic_int Uposition = 0, Vposition = 0, Fposition = 0;
static volatile atomic_int dUmove = 0, dVmove = 0;
static volatile atomic_bool Umoving = FALSE, Vmoving = FALSE, Fmoving = FALSE;
static uint8_t fixerr = 0; // ==1 if can't fixed

static void pusi_disc(){
    motorsoff = TRUE;
}

static void pusi_disconnect(){
    DBG("Try to disconnect");
    if(sockfd > -1){
        DBG("sockfd closed");
        close(sockfd);
    }
    Umoving = Vmoving = Fmoving = ismoving = FALSE;
    state = PUSI_DISCONN;
    sockfd = -1;
    LOGWARN("Canserver disconnected");
}

static int too_much_errors(){
    // FNAME();
    if(++errctr >= MAX_ERR_CTR){
        LOGERR("Canserver: too much errors -> DISCONNECT");
        errctr = 0;
        pusi_disconnect();
        return TRUE;
    }
    return FALSE;
}

#define clr_errors()    do{errctr = 0;}while(0)

static char *findval(const char *par, const char *statusmesg){
    // FNAME();
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
    // FNAME();
    char *parval = findval(par, statusmesg);
    if(!parval) return FALSE;
    if(!val) return TRUE;
    *val = atof(parval);
    return TRUE;
}
// the same as getparval, but check for "=OK"
static int getOKval(const char *par, const char *statusmesg){
    // FNAME();
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
    // FNAME();
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
    // FNAME();
    if(sockfd < 0) return FALSE;
#define BUFFERSZ    (2047)
    char buf[BUFFERSZ+1];
    int Nread = 0, ctr = 0;
    double t0 = dtime();
    //DBG("read ans");
    while(dtime() - t0 < WAITANSTIME && Nread < BUFFERSZ && sockfd > 0){
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
    }
    buf[Nread] = 0;
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
        clr_errors();
    }else{
        LOGWARN("didn't get OK answer");
    }
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
    // FNAME();
    if(!msg || sockfd < 0) return FALSE;
    size_t L = strlen(msg);
    if(pthread_mutex_lock(&sendmesg_mutex)) return FALSE;
    clearbuf();
    if(send(sockfd, msg, L, 0) != (ssize_t)L){
        LOGWARN("send_message(): send() failed");
        return FALSE;
    }
    //DBG("Message '%s' sent", msg);
    int r = waitOK(ans);
    pthread_mutex_unlock(&sendmesg_mutex);
    return r;
}

/**
 * @brief getRansArg - check relay answer & return args
 * @param ans - answer
 * @param buf - full buffer
 * @return amount of args found (0 - if answer is wrong or no n'th arg found)
 */
static int getRansArg(char *ans, uint8_t buf[8]){
    // FNAME();
    //DBG("check relay answer, ans: %s", ans);
    if(!ans) return 0;
    if(strncmp(ans, RelayAns, sizeof(RelayAns)-1)) return 0; // bad answer
    ans += sizeof(RelayAns);
    int got = sscanf(ans, "%hhx %hhx %hhx %hhx %hhx %hhx %hhx %hhx", &buf[0], &buf[1], &buf[2], &buf[3], &buf[4], &buf[5], &buf[6], &buf[7]);
    //DBG("got ans: %d, arg0..2=%u, %u, %u", got, buf[0], buf[1], buf[2]);
    return got;
}

/**
 * @brief chkRelay - check relay state & change `relay` variable
 * @return FALSE if failed
 */
static int chkRelay(){
    // FNAME();
    char *ans = NULL;
    char buf[512];
    uint8_t canbuf[8];
    relaystate r = {0};
    int ret = FALSE;
    snprintf(buf, 511, "%s %d", RelayCmd, R_RELAY);
    if(send_message(buf, &ans) && 2 == getRansArg(ans, canbuf) && canbuf[0] == R_RELAY){
        r.relays = canbuf[1];
    }else goto rtn;
    snprintf(buf, 511, "%s %d", RelayCmd, R_PWM);
    if(send_message(buf, &ans) && 4 == getRansArg(ans, canbuf) && canbuf[0] == R_PWM){
        memcpy(r.PWM, canbuf+1, 3);
    }else goto rtn;
    for(int btn = 0; btn < 4; ++btn){
        snprintf(buf, 511, "%s %d %d", RelayCmd, R_BTNS, btn);
        if(send_message(buf, &ans) && 8 == getRansArg(ans, canbuf) && canbuf[0] == R_BTNS){
            r.buttons[btn] = canbuf[2];
        }else goto rtn;
    }
    relay = r;
    ret = TRUE;
rtn:
    free(ans);
    return ret;
}

static void send_message_nocheck(const char *msg){
    // FNAME();
    if(!msg || sockfd < 0) return;
    size_t L = strlen(msg);
    if(pthread_mutex_lock(&sendmesg_mutex)) return;
    clearbuf();
    if(send(sockfd, msg, L, 0) != (ssize_t)L){
        WARN("send");
    }
    pthread_mutex_unlock(&sendmesg_mutex);
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
    if(ans) free(ans);
    return retval;
}

/**
 * @brief pusi_connect_server - try connect to a local steppers CAN server
 * @return FALSE if failed
 */
static int pusi_connect_server(){
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
    send_message_nocheck(registerRelay);
    int retval = FALSE;
    if(chkRelay()) retval = TRUE;
    if(setSpeed(setUspeed, "U")) retval = TRUE;
    if(setSpeed(setVspeed, "V")) retval = TRUE;
    if(setSpeed(setFspeed, "F")) retval = TRUE;
    if(!retval) pusi_disconnect();
    else{
        state = PUSI_RELAX;
        sstatus = SETUP_NONE;
    }
    return retval;
}

static void *pusi_process_states(_U_ void *arg);
static pthread_t processingthread;
/**
 * @brief pusi_connect - run a thread processed steppers status
 * @return FALSE if failed to connect immediately
 */
int pusi_connect(){
    DBG("Try to connect");
    int c = pusi_connect_server();
    if(pthread_create(&processingthread, NULL, pusi_process_states, NULL)){
        LOGERR("pthread_create() for pusirobo server failed");
        ERR("pthread_create()");
    }
    return c;
}

// return TRUE if motor is stopped
static int moving_finished(const char *mesgstatus, volatile atomic_int *position){
    // FNAME();
    double val = 0.;
    char *ans = NULL;
    int ret = TRUE;
    if(send_message(mesgstatus, &ans) && getparval(PARstatus, ans, &val)){
        errctr = 0;
        //DBG("send(%s) true: %s %g\n", mesgstatus, ans, val);
    }else{
        WARNX("send(%s) false: %s %g\n", mesgstatus, ans, val);
        if(too_much_errors()){
            LOGDBG("send(%s) false: %s %g", mesgstatus, ans, val);
        }
        if(ans) free(ans);
        return FALSE;
    }
    int ival = (int)val;
    if(ival) ret = FALSE;
    if(position){
        if(getparval(CURPOSstatus, ans, &val)){
            *position = (int) val;
        }else{
            WARNX("%s not found in '%s'", CURPOSstatus, ans);
            LOGDBG("%s not found in '%s'", CURPOSstatus, ans);
        }
    }
    if(ans) free(ans);
    return ret;
}

// move motor to s steps, @return FALSE if failed
static int move_motor(const char *movecmd, int s){
    DBG("move %s -> %d", movecmd, s);
    LOGDBG("move %s -> %d", movecmd, s);
    char buf[256], *ans;
    snprintf(buf, 255, "%s %d", movecmd, s);
    if(!send_message(buf, &ans)){
        WARNX("can't send message");
        if(too_much_errors()) LOGWARN("Canserver: can't move motor");
        if(ans) free(ans);
        return FALSE;
    }
    int ret = TRUE;
    if(!getOKval(STEPSstatus, ans)){
        WARNX("NO OK in %s", ans);
        LOGWARN("NO OK in %s", ans);
        ret = FALSE;
    }
    if(ans) free(ans);
    return ret;
}

static void process_movetomiddle_stage(){
    switch(sstatus){
        case SETUP_INIT: // initial moving
            if(moveF(-Fmaxsteps) && moveU(-UVmaxsteps) && moveV(-UVmaxsteps))
                sstatus = SETUP_WAITUV0;
        break;
        case SETUP_WAITUV0: // wait for both coordinates moving to zero
            DBG("Reached UVF0!");
            if(moveF(Fmaxsteps/2) && moveU(theconf.maxUsteps+UVedgesteps) && moveV(theconf.maxVsteps+UVedgesteps))
                sstatus = SETUP_WAITUVMID;
            else{
                LOGWARN("GOTO middle: err in move command");
                sstatus = SETUP_INIT;
                if(too_much_errors()) sstatus = SETUP_NONE;
            }
        break;
        case SETUP_WAITUVMID: // wait for the middle
            DBG("Reached middle position");
            if(!send_message(Fsetzero, NULL) || !send_message(Usetzero, NULL) || !send_message(Vsetzero, NULL)){
                LOGWARN("GOTO middle: err in set 0 command");
                sstatus = SETUP_INIT;
                if(too_much_errors()) sstatus = SETUP_NONE;
                return;
            }
            Uposition = Vposition = Fposition = 0;
        // fallthrough
        default:
            sstatus = SETUP_NONE;
            state = PUSI_RELAX;
    }
}

/**
 * @brief process_setup_stage - process all stages of axes setup
 */
static void process_setup_stage(){
    DBG("PROCESS: %d\n", sstatus);
     // coordinates for corrections calculation
    static double X0U, Y0U, XmU, YmU;
    static double X0V, Y0V, XmV, YmV;
    switch(sstatus){
        case SETUP_INIT: // initial moving
            if(moveU(-UVmaxsteps) && moveV(-UVmaxsteps))
                sstatus = SETUP_WAITUV0;
        break;
        case SETUP_WAITUV0: // wait for both coordinates moving to zero
            DBG("Left border reached");
            if(moveU(theconf.maxUsteps+UVedgesteps) && moveV(theconf.maxUsteps+UVedgesteps))
                sstatus = SETUP_WAITUVMID;
            else{
                LOGWARN("Can't move U/V -> 0");
                sstatus = SETUP_INIT;
                if(too_much_errors()) sstatus = SETUP_NONE;
            }
        break;
        case SETUP_WAITUVMID: // wait for the middle
            DBG("The middle reached");
            if(moveU(-theconf.maxUsteps)) sstatus = SETUP_WAITU0;
            else{
                LOGWARN("Can't move U -> middle");
                sstatus = SETUP_INIT;
                if(too_much_errors()) sstatus = SETUP_NONE;
            }
        break;
        case SETUP_WAITU0: // wait while U moves to zero
            if(!coordsRdy) return;
            coordsRdy = FALSE;
            X0U = Xtarget; Y0U = Ytarget;
            DBG("got X0U=%.1f, Y0U=%.1f", X0U, Y0U);
            LOGDBG("got X0U=%.1f, Y0U=%.1f", X0U, Y0U);
            if(moveU(2*theconf.maxUsteps)) sstatus = SETUP_WAITUMAX;
            else{
                LOGWARN("Can't move U -> max");
                sstatus = SETUP_INIT;
                if(too_much_errors()) sstatus = SETUP_NONE;
            }
        break;
        case SETUP_WAITUMAX: // wait while U moves to UVworkrange
            if(!coordsRdy) return;
            coordsRdy = FALSE;
            XmU = Xtarget; YmU = Ytarget;
            LOGDBG("got XmU=%.1f, YmU=%.1f", XmU, YmU);
            if(moveU(-theconf.maxUsteps) && moveV(-theconf.maxVsteps)) sstatus = SETUP_WAITV0;
            else{
                LOGWARN("Can't move U -> mid OR/AND V -> min");
                sstatus = SETUP_INIT;
                if(too_much_errors()) sstatus = SETUP_NONE;
            }
        break;
        case SETUP_WAITV0: // wait while V moves to 0
            if(!coordsRdy) return;
            coordsRdy = FALSE;
            X0V = Xtarget; Y0V = Ytarget;
            LOGDBG("got X0V=%.1f, Y0V=%.1f", X0V, Y0V);
            if(moveV(2*theconf.maxVsteps)) sstatus = SETUP_WAITVMAX;
            else{
                LOGWARN("Can't move V -> max");
                sstatus = SETUP_INIT;
                if(too_much_errors()) sstatus = SETUP_NONE;
            }
        break;
        case SETUP_WAITVMAX: // wait while V moves to UVworkrange
            if(!coordsRdy) return;
            coordsRdy = FALSE;
            XmV = Xtarget; YmV = Ytarget;
            LOGDBG("got XmV=%.1f, YmV=%.1f", XmV, YmV);
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
            LOGDBG("KU=%.4f, KV=%.4f, sa=%.4f, ca=%.4f, sb=%.4f, cb=%.4f",
                   KU, KV, sa, ca, sb, cb);
            /*
             * [dX dY] = M*[dU dV], M = [ca/KU cb/KV; sa/KU sb/KV] ===>
             * [dU dV] = inv(M)*[dX dY],
             * inv(M) = 1/(ca/KU*sb/KV - sa/KU*cb/KV)*[sb/KV -cb/KV; -sa/KU ca/KU]
             */
            double mul = 1/(ca/KU*sb/KV - sa/KU*cb/KV);
            theconf.Kxu = mul*sb/KV;
            theconf.Kyu = -mul*cb/KV;
            theconf.Kxv = -mul*sa/KU;
            theconf.Kyv = mul*ca/KU;
            LOGDBG("Kxu=%g, Kyu=%g; Kxv=%g, Kyv=%g", theconf.Kxu, theconf.Kyu, theconf.Kxv, theconf.Kyv);
            DBG("Now save new configuration");
            saveconf(NULL); // try to store configuration
            // fallthrough
        endmoving:
            moveV(-theconf.maxVsteps);
            sstatus = SETUP_FINISH;
        break;
        case SETUP_FINISH: // reset current coordinates
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
    theconf.xtarget = X + theconf.xoff;
    theconf.ytarget = Y + theconf.yoff;
    DBG("Got target coordinates: (%.1f, %.1f)", X, Y);
    LOGMSG("Got target coordinates: (%.1f, %.1f)", X, Y);
    saveconf(NULL);
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
    int Unfixed = Unew + Fposition, Vnfixed = Vnew + Fposition; // fixed by focus position
    if(Unfixed > theconf.maxUsteps || Unfixed < -theconf.maxUsteps ||
       Vnfixed > theconf.maxVsteps || Vnfixed < -theconf.maxVsteps){
        // TODO: here we should signal that the limit reached
        LOGWARN("Correction failed, curpos: %d, %d, should move to %d, %d",
                Uposition, Vposition, Unew, Vnew);
        return FALSE;
    }
    LOGDBG("try2correct(): move from (%d, %d) to (%d, %d) (abs: %d, %d), delta (%.1f, %.1f)",
           Uposition, Vposition, Unew, Vnew, Uposition + (int)(dU/KCORR),
           Vposition + (int)(dV/KCORR), dU, dV);
    int ret = FALSE;
    if(moveU((int)dU) && moveV((int)dV)) ret = TRUE;
    if(!ret && too_much_errors()) LOGERR("Canserver: stop corrections");
    return ret;
}

// global variable proc_corr
/**
 * @brief pusi_process_corrections - get XY corrections (in pixels) and move motors to fix them
 * @param X, Y - centroid (x,y) in screen coordinate system
 * This function called from improc.c each time the corrections calculated (ONLY IF Xtarget/Ytarget > -1)
 */
static void pusi_process_corrections(double X, double Y){
    static bool coordstrusted = TRUE;
    if(ismoving){ // don't process coordinates when moving
        coordstrusted = FALSE;
        coordsRdy = FALSE;
        return;
    }
    if(!coordstrusted){ // don't trust first coordinates after moving finished
        coordstrusted = TRUE;
        coordsRdy = FALSE;
        return;
    }
    //DBG("got centroid data: %g, %g", X, Y);
    Xtarget = X; Ytarget = Y;
    coordsRdy = TRUE;
}

// try to change state; @return TRUE if OK
static int pusi_setstate(pusistate newstate){
    if(newstate == state) return TRUE;
    if(newstate == PUSI_DISCONN){
        pusi_disc();
        return TRUE;
    }
    if(state == PUSI_DISCONN){
        if(!pusi_connect_server()) return FALSE;
    }
    if(newstate == PUSI_SETUP || newstate == PUSI_GOTOTHEMIDDLE){
        sstatus = SETUP_INIT;
    }else sstatus = SETUP_NONE;
    state = newstate;
    return TRUE;
}

// get current status (global variable stepstatus)
// return JSON string with different parameters
static char *pusi_status(const char *messageid, char *buf, int buflen){
    // FNAME();
    int l;
    char *bptr = buf;
    const char *s = NULL, *stage = NULL;
    l = snprintf(bptr, buflen, "{ \"%s\": \"%s\", \"status\": ", MESSAGEID, messageid);
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
            l = snprintf(bptr, buflen, "\"%s\"", fixerr ? "fixoutofrange" : "fixing");
        break;
        default:
            l = snprintf(bptr, buflen, "\"unknown\"");
    }
    buflen -= l; bptr += l;
    if(state != PUSI_DISCONN){
        l = snprintf(bptr, buflen, ", ");
        buflen -= l; bptr += l;
        const char *motors[] = {"Umotor", "Vmotor", "Fmotor"};
        volatile atomic_bool *mv[] = {&Umoving, &Vmoving, &Fmoving};
        volatile atomic_int *pos[] = {&Uposition, &Vposition, &Fposition};
        for(int i = 0; i < 3; ++i){
            const char *stat = "stopping";
            if(*mv[i]) stat = "moving";
            l = snprintf(bptr, buflen, "\"%s\": { \"status\": \"%s\", \"position\": %d }, ",
                         motors[i], stat, *pos[i]);
            buflen -= l; bptr += l;
        }
        relaystate r = relay;
        l = snprintf(bptr, buflen, "\"relay\": %d, ", r.relays);
        buflen -= l; bptr += l;
        for(int p = 0; p < 3; ++p){
            l = snprintf(bptr, buflen, "\"PWM%d\": %d, ", p, r.PWM[p]);
            buflen -= l; bptr += l;
        }
        for(int b = 0; b < 4; ++b){
            l = snprintf(bptr, buflen, "\"button%d\": %d%s", b, r.buttons[b], (b==3)?"":", ");
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
// commands from client to change status
static strstate stringstatuses[] = {
    {"disconnect", PUSI_DISCONN},
    {"relax", PUSI_RELAX},
    {"setup", PUSI_SETUP},
    {"middle", PUSI_GOTOTHEMIDDLE},
    {"findtarget", PUSI_FINDTARGET},
    {"fix", PUSI_FIX},
    {NULL, 0}
};

// try to set new status (global variable stepstatus)
static char *set_pusistatus(const char *newstatus, char *buf, int buflen){
    // FNAME();
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
        }else{
            snprintf(buf, buflen, FAIL);
            return buf;
        }
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

// MAIN THREAD
static void *pusi_process_states(_U_ void *arg){
    // FNAME();
    static bool first = TRUE; // flag for logging when can't reconnect
    while(!stopwork){
        usleep(10000);
        // check for disconnection flag
        if(motorsoff){
            motorsoff = FALSE;
            pusi_disconnect();
            sleep(1);
            continue;
        }
        // check for moving
        if(state == PUSI_DISCONN){
            DBG("DISCONNECTED - try to connect");
            sleep(1);
            pusi_connect_server();
            continue;
        }
        // check relay
        //DBG("relay");
        chkRelay();
        //DBG("U");
        if(moving_finished(Ustatus, &Uposition)) Umoving = FALSE;
        else Umoving = TRUE;
        //DBG("V");
        if(moving_finished(Vstatus, &Vposition)) Vmoving = FALSE;
        else Vmoving = TRUE;
        //DBG("F");
        if(moving_finished(Fstatus, &Fposition)) Fmoving = FALSE;
        else Fmoving = TRUE;
        if(Umoving || Vmoving || Fmoving) ismoving = TRUE;
        else ismoving = FALSE;
        if(ismoving){
            coordsRdy = FALSE;
            continue;
        }
        // check request to change focus
        if(chfocus){
            chfocus = FALSE;
            int delta = newfocpos - Fposition;
            moveF(delta); moveU(delta); moveV(delta);
            continue;
        }
        if(dUmove){
            moveU(dUmove);
            dUmove = 0;
            continue;
        }
        if(dVmove){
            moveV(dVmove);
            dVmove = 0;
            continue;
        }
        // if we are here, all U/V/F moving is finished
        if(state != PUSI_DISCONN) first = TRUE;
        switch(state){ // pusirobo state machine
            case PUSI_DISCONN:
                if(!pusi_connect_server()){
                    WARNX("Can't reconnect");
                    if(first){
                        LOGWARN("Can't reconnect");
                        first = FALSE;
                    }
                    sleep(5);
                }
            break;
            case PUSI_SETUP: // setup axes (before this state set Xtarget/Ytarget in improc.c)
                process_setup_stage();
            break;
            case PUSI_GOTOTHEMIDDLE:
                process_movetomiddle_stage();
            break;
            case PUSI_FINDTARGET: // calculate target coordinates
                if(coordsRdy){
                    coordsRdy = FALSE;
                    if(process_targetstage(Xtarget, Ytarget))
                        state = PUSI_RELAX;
                }
            break;
            case PUSI_FIX:   // process corrections
                if(coordsRdy){
                    coordsRdy = FALSE;
                    DBG("GET AVERAGE -> correct\n");
                    double xtg = theconf.xtarget - theconf.xoff, ytg = theconf.ytarget - theconf.yoff;
                    double xdev = xtg - Xtarget, ydev = ytg - Ytarget;
                    double corr = sqrt(xdev*xdev + ydev*ydev);
                    if(theconf.xtarget < 1. || theconf.ytarget < 1. || corr < COORDTOLERANCE){
                        DBG("Target coordinates not defined or correction too small, targ: (%.1f, %.1f); corr: %.1f, %.1f (abs: %.1f)",
                            theconf.xtarget, theconf.ytarget, xdev, ydev, corr);
                        break;
                    }
                    LOGDBG("Current position: U=%d, V=%d, deviations: dX=%.1f, dy=%.1f",
                           Uposition, Vposition, xdev, ydev);
                    if(!try2correct(xdev, ydev)){
                        LOGWARN("failed to correct");
                        fixerr = 1;
                        // TODO: do something here
                        DBG("FAILED");
                    }else fixerr = 0;
                }
            break;
            default: // PUSI_RELAX
                break;
        }
    }
    DBG("thread stopped");
    return NULL;
}

// change focus (global variable movefocus)
static char *set_pfocus(const char *newstatus, char *buf, int buflen){
    int newval = atoi(newstatus);
    if(newval < theconf.minFpos || newval > theconf.maxFpos){
        snprintf(buf, buflen, FAIL);
    }else{
        snprintf(buf, buflen, OK);
        newfocpos = newval;
        chfocus = TRUE;
    }
    return buf;
}
// move by U and V axis
static char *Umove(const char *val, char *buf, int buflen){
    int d = atoi(val);
    int Unfixed = Uposition + d + Fposition;
    if(Unfixed > theconf.maxUsteps || Unfixed < -theconf.maxUsteps){
        snprintf(buf, buflen, FAIL);
        return buf;
    }
    dUmove = d;
    snprintf(buf, buflen, OK);
    return buf;
}
static char *Vmove(const char *val, char *buf, int buflen){
    int d = atoi(val);
    int Vnfixed = Vposition + d + Fposition;
    if(Vnfixed > theconf.maxVsteps || Vnfixed < -theconf.maxVsteps){
        snprintf(buf, buflen, FAIL);
        return buf;
    }
    dVmove = d;
    snprintf(buf, buflen, OK);
    return buf;
}
static char *relaycmd(const char *val, char *buf, int buflen){
    const char *ans = FAIL;
    char *eq = NULL, *par = strdup(val);
    char mbuf[512];
    relaystate r = relay;
    if((eq = strchr(par, '='))){
        *eq++ = 0;
        int v = atoi(eq), tmpno = 0;
        if(1 == sscanf(par, "R%d", &tmpno)){ // relay command
            if(tmpno == 1 || tmpno == 0){
                int rval = r.relays;
                if(v) rval |= 1<<tmpno;
                else rval &= ~(1<<tmpno);
                DBG("Relay %d -> %d", r.relays, rval);
                snprintf(mbuf, 511, "%s %d %d", RelayCmd, R_RELAY + relaySetter, rval);
                if(send_message(mbuf, NULL)) ans = OK;
            }
        }else if(1 == sscanf(par, "PWM%d", &tmpno)){ // PWM command
            if(tmpno >= 0 && tmpno < 4 && v > -1 && v < 256){
                DBG("PWM %d -> %d\n", tmpno, v);
                r.PWM[tmpno] = v;
                snprintf(mbuf, 511, "%s %d %u %u %u", RelayCmd, R_PWM + relaySetter, r.PWM[0], r.PWM[1], r.PWM[2]);
                if(send_message(mbuf, NULL)) ans = OK;
            }
        }
    }
    free(par);
    snprintf(buf, buflen, "%s", ans);
    return buf;
}

steppersproc pusyCANbus = {
    .stepdisconnect = pusi_disc,
    .proc_corr = pusi_process_corrections,
    .stepstatus = pusi_status,
    .setstepstatus = set_pusistatus,
    .movefocus = set_pfocus,
    .moveByU = Umove,
    .moveByV = Vmove,
    .relay = relaycmd,
};
