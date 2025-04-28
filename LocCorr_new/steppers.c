/*
 * This file is part of the loccorr project.
 * Copyright 2024 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#include "steppers.h"
#include "socket.h"

// buffer for socket
#define BUFLEN  (256)

// max time to wait answer "OK" from server
#define WAITANSTIME         (0.3)


// amount of consequent center coordinates coincidence in `process_targetstate`
#define NCONSEQ             (2)
// tolerance of coordinates coincidence (pix)
#define COORDTOLERANCE      (0.5)

// PID
typedef struct {
    double Kp, Ki, Kd;  // coefficients
    double integral;    // intergal error accumulator
    double prev_error;  // previous error value for D
    double prev_time;   // and previous time
} PIDController;

typedef enum{
    STP_DISCONN,
    STP_RELAX,
    STP_SETUP,
    STP_GOTOTHEMIDDLE,
    STP_FINDTARGET,
    STP_FIX,
    STP_UNDEFINED,
    STP_STATE_AMOUNT
} STPstate;

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

//static int errctr = 0; // sending messages error counter (if > MAX_ERR_CTR, set state to disconnected)

steppersproc *theSteppers = NULL;

// stepper numbers
typedef enum{
      Ustepper = 0,
      Vstepper = 2,
      Fstepper = 1,
    } stepperno;

const char *motornames[NMOTORS] = {
    [Ustepper] = "Umotor",
    [Vstepper] = "Vmotor",
    [Fstepper] = "Fmotor",
};

// a list of steppers commands
typedef enum{
    CMD_ABSPOS,
    CMD_EMSTOP,
    CMD_ESW,
    CMD_GOTO,
    CMD_GOTOZ,
    CMD_RELPOS,
    CMD_STATE,
    CMD_STOP,
    CMD_AMOUNT
} steppercmd;

typedef enum{
    ERR_OK,         // 0 - all OK
    ERR_BADPAR,     // 1 - parameter's value is wrong
    ERR_BADVAL,     // 2 - wrong parameter's value
    ERR_WRONGLEN,   // 3 - wrong message length
    ERR_BADCMD,     // 4 - unknown command
    ERR_CANTRUN,    // 5 - can't run given command due to bad parameters or other
    ERR_AMOUNT      // amount of error codes
} errcodes;


static const char *stp_commands[CMD_AMOUNT] = {
    [CMD_ABSPOS] = "abspos",
    [CMD_EMSTOP] = "emstop",
    [CMD_ESW] = "esw",
    [CMD_GOTO] = "goto",
    [CMD_GOTOZ] = "gotoz",
    [CMD_RELPOS] = "relpos",
    [CMD_STATE] = "state",
    [CMD_STOP] = "stop",
};

static const char* errtxt[ERR_AMOUNT] = {
    [ERR_OK] =   "OK",
    [ERR_BADPAR] =  "BADPAR",
    [ERR_BADVAL] = "BADVAL",
    [ERR_WRONGLEN] = "WRONGLEN",
    [ERR_BADCMD] = "BADCMD",
    [ERR_CANTRUN] = "CANTRUN",
};

static STPstate state = STP_DISCONN;   // server state
// this flag set to TRUE when next Xc,Yc available
static volatile atomic_bool coordsRdy = FALSE;
static double Xtarget = 0., Ytarget = 0.;

// Values to change U,V and F by hands
// flag & new focus value
static volatile atomic_bool chfocus = FALSE;
static volatile atomic_int newfocpos = 0, dUmove = 0, dVmove = 0;

static volatile atomic_int sockfd = -1; // server file descriptor
static volatile atomic_bool motorsoff = FALSE; // flag to disconnect

// mutex for message sending/receiving
static pthread_mutex_t mesg_mutex = PTHREAD_MUTEX_INITIALIZER;

// current steps counters (zero at the middle)
static volatile atomic_int motposition[NMOTORS] = {0};
// relative position change after current moving ends (from external command)
static volatile atomic_int motrelsteps[NMOTORS] = {0};
// current motor state
static volatile atomic_int motstates[NMOTORS] = {0};
static uint8_t fixerr = 0; // ==1 if can't fixed

// motor states:
typedef enum{
    STATE_RELAX,
    STATE_ACCEL,
    STATE_MOVE,
    STATE_MVSLOW,
    STATE_DECEL,
    STATE_STALL,
    STATE_ERR,
    STATE_NUM
} motstate;
static const char *str_states[STATE_NUM] = {
    [STATE_RELAX] = "relax",
    [STATE_ACCEL] = "accelerated",
    [STATE_MOVE] = "moving",
    [STATE_MVSLOW] = "slow moving",
    [STATE_DECEL] = "decelerated",
    [STATE_STALL] = "stalled",
    [STATE_ERR] = "error",
};

TRUE_INLINE int relaxed(int nmotor){return motstates[nmotor] == STATE_RELAX;}
#define Uposition   (motposition[Ustepper])
#define Vposition   (motposition[Vstepper])
#define Fposition   (motposition[Fstepper])

static int nth_motor_setter(steppercmd idx, int n, int p);

static void stp_disc(){
    motorsoff = TRUE;
}

static void stp_disconnect(){
    DBG("Try to disconnect");
    if(sockfd > -1){
        DBG("sockfd closed");
        close(sockfd);
    }
    state = STP_DISCONN;
    sockfd = -1;
    LOGWARN("Stepper server disconnected");
}

// check if nmot is U/V/F and return FALSE if not
static int chkNmot(int nmot){
    if(nmot == Ustepper || nmot == Vstepper || nmot == Fstepper) return TRUE;
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
    if(FD_ISSET(sockfd, &fds)){
        //DBG("CANREAD");
        return TRUE;
    }
    return FALSE;
}

// clear all data received earlier
static void clrbuf(){
    char buf[256];
    while(canread()){
        ssize_t got = recv(sockfd, buf, 256, 0);
        DBG("cleared %zd bytes of trash", got);
        if(got <= 0){ // disconnect or error
            LOGERR("Server disconnected");
            ERRX("Server disconnected");
        }
    }
}

// read message (if exists) and return its length (or -1 if none)
// There's could be many strings of data!!!
static ssize_t read_message(char *msg, size_t msglen){
    if(!msg || msglen == 0) return -1;
    if(pthread_mutex_lock(&mesg_mutex)){
        WARN("pthread_mutex_lock()");
        LOGWARN("read_message(): pthread_mutex_lock() err");
        return 0;
    }
    double t0 = sl_dtime();
    size_t gotbytes = 0;
    --msglen; // for trailing zero
    while(sl_dtime() - t0 < WAITANSTIME && gotbytes < msglen && sockfd > 0){
        if(!canread()) continue;
        int n = recv(sockfd, msg+gotbytes, msglen, 0);
        if(n <= 0){ // disconnect or error
            LOGERR("Server disconnected");
            ERRX("Server disconnected");
        }
        if(n == 0) break;
        gotbytes += n;
        msglen -= n;
        if(msg[gotbytes-1] == '\n') break;
        t0 = sl_dtime();
    }
    //DBG("Dt=%g, gotbytes=%zd, sockfd=%d, msg='%s'", dtime()-t0,gotbytes,sockfd,msg);
    pthread_mutex_unlock(&mesg_mutex);
    if(msg[gotbytes-1] != '\n'){
        //DBG("No newline at the end");
        return 0;
    }
    msg[gotbytes] = 0;
    return gotbytes;
}

static errcodes parsing(steppercmd idx, int nmot, int ival){
    int goodidx = chkNmot(nmot);
    switch(idx){
        case CMD_ABSPOS:
            if(goodidx) motposition[nmot] = ival;
            else return ERR_BADPAR;
        break;
        case CMD_RELPOS:
            if(goodidx) motrelsteps[nmot] = ival;
            else return ERR_BADPAR;
        break;
        case CMD_STATE:
            if(!goodidx) return ERR_BADPAR;
            motstates[nmot] = ival;
            if(chkNmot(nmot)){ // one of our motors - check err or stall
                if(ival == STATE_STALL || ival == STATE_ERR){
                    WARNX("BAD status of motor %d", nmot);
                    LOGWARN("BAD status of motor %d", nmot);
                    nth_motor_setter(CMD_EMSTOP, nmot, 1); // tty to clear error
                }
            }
        break;
        case CMD_EMSTOP:
        case CMD_ESW:
        case CMD_GOTO:
        case CMD_GOTOZ:
        case CMD_STOP:
        break;
        default: return ERR_BADCMD;
    }
    return ERR_OK;
}

// check if message is error text
static errcodes getecode(const char *msg){
    errcodes e;
    for(e = 0; e < ERR_AMOUNT; ++e){
        if(0 == strcmp(msg, errtxt[e])) break;
    }
    DBG("ERRcode: %d, (%s)", e, (e != ERR_AMOUNT) ? errtxt[e] : "undef");
    LOGDBG("ERRcode: %d, (%s)", e, (e != ERR_AMOUNT) ? errtxt[e] : "undef");
    return e;
}

/**
 * @brief read_parse - read answer (till got or WAITANSTIME timeout) and parse it
 * @param idx - index of message sent (to compare answer)
 * @return
 */
static errcodes read_and_parse(steppercmd idx){
    char value[128], msg[1024];
    double t0 = sl_dtime();
    while(sl_dtime() - t0 < WAITANSTIME*10.){
        ssize_t got = read_message(msg, 1024);
        if(got < 1) continue;
        //LOGDBG("GOT from stepper server:\n%s\n", msg);
        char *saveptr, *tok = msg;
        for(;; tok = NULL){
            char *token = strtok_r(tok, "\n", &saveptr);
            if(!token) break;
            //LOGDBG("Got line: %s", token);
            char *key = get_keyval(token, value);
            if(key){
                int ival = atoi(value);
                //LOGDBG("key = %s, value = %s (%d)", key, value, ival);
                size_t l = strlen(key);
                size_t numpos = strcspn(key, "0123456789");
                int parno = -1;
                if(numpos < l){
                    parno = atoi(key + numpos);
                    key[numpos] = 0;
                }
                //DBG("numpos=%zd, parno=%d", numpos, parno);
                if(parno > -1){ // got motor number
                    if(!chkNmot(parno)){
                        DBG("Not our business");
                        free(key); continue;
                    }
                }
                // search index in commands
                if(0 == strcmp(stp_commands[idx], key)){ // found our
                    free(key);
                    //LOGDBG("OK, idx=%d, cmd=%s", idx, stp_commands[idx]);
                    return parsing(idx, parno, ival);
                }
                free(key);
            }else{
                DBG("GOT NON-setter %s", token);
                errcodes e = getecode(token);
                if(e != ERR_AMOUNT) return e; // ERR_AMOUNT means some other message
            }
        }
    }
    DBG("No answer detected to our command");
    LOGDBG("read_and_parse(): no answer detected to our command");
    return ERR_CANTRUN; // didn't get anwer need
}

/**
 * @brief send_message - send character string `msg` to serial server, get and parse answer
 * @param msg - message (for setters could be like "N=M" or "=M") or NULL (for getters)
 * @return FALSE if failed (should reconnect)
 */
static errcodes send_message(steppercmd idx, char *msg){
    // FNAME();
    if(sockfd < 0) return ERR_CANTRUN;
    char buf[256];
    size_t msglen;
    if(!msg) msglen = snprintf(buf, 255, "%s\n", stp_commands[idx]);
    else msglen = snprintf(buf, 255, "%s%s\n", stp_commands[idx], msg);
    //DBG("Send message '%s', len %zd", buf, msglen);
    if(pthread_mutex_lock(&mesg_mutex)){
        WARN("pthread_mutex_lock()");
        LOGWARN("send_message(): pthread_mutex_lock() err");
        return FALSE;
    }
    clrbuf();
    if(send(sockfd, buf, msglen, 0) != (ssize_t)msglen){
        WARN("send()");
        LOGWARN("send_message(): send() failed");
        return ERR_WRONGLEN;
    }
    //LOGDBG("Message '%s' sent", buf);
    pthread_mutex_unlock(&mesg_mutex);
    return read_and_parse(idx);
}

// send command cmd to n'th motor with param p, @return FALSE if failed
static int nth_motor_setter(steppercmd idx, int n, int p){
    if(idx < 0 || idx >= CMD_AMOUNT) return FALSE;
    char buf[256];
    if(n < 0){ // setter without number
        snprintf(buf, 255, "=%d", p);
        DBG("nth_motor_setter(): set %s=%d", stp_commands[idx], p);
        LOGDBG("nth_motor_setter(): set %s=%d", stp_commands[idx], p);
    }else if(n < NMOTORS){
        snprintf(buf, 255, "%d=%d", n, p);
        DBG("nth_motor_setter(): get %s%d=%d", stp_commands[idx], n, p);
        LOGDBG("nth_motor_setter(): set %s%d=%d", stp_commands[idx], n, p);
    }else{
        WARNX("Wrong motno %d", n);
        LOGWARN("Wrong motno %d (cmd=%s, setter=%d)", n, stp_commands[idx], p);
    }
    if(ERR_OK != send_message(idx, buf)) return FALSE;
    return TRUE;
}
// and simplest getter
static int nth_motor_getter(steppercmd idx, int n){
    if(idx < 0 || idx >= CMD_AMOUNT) return FALSE;
    char buf[32], *msg = NULL;
    if(n > -1 && n < NMOTORS){
        sprintf(buf, "%d", n);
        //DBG("nth_motor_getter(): %s%d", stp_commands[idx], n);
        //LOGDBG("nth_motor_getter(): %s%d", stp_commands[idx], n);
        msg = buf;
    }else{
        WARNX("Wrong motno %d", n);
        LOGWARN("nth_motor_getter(): wrong motno %d (cmd=%s)", n, stp_commands[idx]);
    }
    if(ERR_OK != send_message(idx, msg)) return FALSE;
    return TRUE;
}

// send getter to all motors; return FALSE if failed
static int chkmots(steppercmd cmd){
    if( nth_motor_getter(cmd, Ustepper) &&
        nth_motor_getter(cmd, Vstepper) &&
        nth_motor_getter(cmd, Fstepper)) return TRUE;
    return FALSE;
}

static void chkall(){
    chkmots(CMD_STATE);
    chkmots(CMD_ABSPOS);
    chkmots(CMD_RELPOS);
}

/**
 * @brief stp_connect_server - try connect to a local steppers CAN server
 * @return FALSE if failed
 */
static int stp_connect_server(){
    DBG("STP_connect(%d)", theconf.stpserverport);
    char port[10];
    snprintf(port, 10, "%d", theconf.stpserverport);
    stp_disconnect();
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
        LOGWARN("Can't connect to steppers server");
        sockfd = -1;
        return FALSE;
    }
    freeaddrinfo(res);
    // register and set max speed; don't check `register` answer as they could be registered already
    state = STP_RELAX;
    sstatus = SETUP_NONE;
    LOGMSG("Connected to stepper server");
    return TRUE;
}

static void *stp_process_states(_U_ void *arg);
static pthread_t processingthread;

static void process_movetomiddle_stage(){
    switch(sstatus){
        case SETUP_INIT: // initial moving
            if( !nth_motor_setter(CMD_EMSTOP, Ustepper, 1) ||
                !nth_motor_setter(CMD_EMSTOP, Vstepper, 1) ||
                !nth_motor_setter(CMD_EMSTOP, Fstepper, 1) ) break;
            if(nth_motor_setter(CMD_GOTOZ, Ustepper, 1) &&
               nth_motor_setter(CMD_GOTOZ, Vstepper, 1) &&
               nth_motor_setter(CMD_GOTOZ, Fstepper, 1)){
                LOGMSG("process_movetomiddle_stage(): SETUP_WAITUV0");
                sstatus = SETUP_WAITUV0;
            }
        break;
        case SETUP_WAITUV0: // wait for all coordinates moving to zero
            if(!relaxed(Ustepper) || !relaxed(Vstepper) || !relaxed(Fstepper)) break; // didn't reach yet
            // now all motors are stopped -> send positions to zero
            if( !nth_motor_setter(CMD_ABSPOS, Ustepper, 1) ||
                !nth_motor_setter(CMD_ABSPOS, Vstepper, 1) ||
                !nth_motor_setter(CMD_ABSPOS, Fstepper, 1)) break;
            DBG("Reached UVF0!");
            // goto
            if(nth_motor_setter(CMD_GOTO, Ustepper, (theconf.maxUpos + theconf.minUpos)/2) &&
               nth_motor_setter(CMD_GOTO, Vstepper, (theconf.maxVpos + theconf.minVpos)/2) &&
               nth_motor_setter(CMD_GOTO, Fstepper, (theconf.maxFpos + theconf.minFpos)/2)){
                LOGMSG("process_movetomiddle_stage(): SETUP_WAITUVMID");
                sstatus = SETUP_WAITUVMID;
            }
        break;
        case SETUP_WAITUVMID: // wait for the middle
            if(!relaxed(Ustepper) || !relaxed(Vstepper) || !relaxed(Fstepper)) break;
            // if motors ready, relsteps should be 0
            if(motrelsteps[Ustepper] || motrelsteps[Vstepper] || motrelsteps[Fstepper]){
                WARNX("Come to wrong pos: U=%d, V=%d, F=%d", Uposition, Vposition, Fposition);
                LOGWARN("Come to wrong pos: U=%d, V=%d, F=%d", Uposition, Vposition, Fposition);
                sstatus = SETUP_WAITUV0;
            }
            DBG("Reached middle position");
            LOGMSG("Reached middle position");
        // fallthrough
        default:
            sstatus = SETUP_NONE;
            state = STP_RELAX;
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
        case SETUP_INIT: // initial moving; don't move F (as it should be focused already)
            if( !nth_motor_setter(CMD_EMSTOP, Ustepper, 1) ||
                !nth_motor_setter(CMD_EMSTOP, Vstepper, 1) ) break;
            if(nth_motor_setter(CMD_GOTOZ, Ustepper, 1) &&
               nth_motor_setter(CMD_GOTOZ, Vstepper, 1) ){
                LOGMSG("process_setup_stage(): SETUP_WAITUV0");
                sstatus = SETUP_WAITUV0;
            }
        break;
        case SETUP_WAITUV0: // wait for both coordinates moving to zero
            if(!relaxed(Ustepper) || !relaxed(Vstepper)) break;
            // set current position to 0
            if( !nth_motor_setter(CMD_ABSPOS, Ustepper, 1) ||
                !nth_motor_setter(CMD_ABSPOS, Vstepper, 1)) break;
            DBG("ZERO border reached");
            // goto middle
            if(nth_motor_setter(CMD_GOTO, Ustepper, (theconf.maxUpos+theconf.minUpos)/2) &&
               nth_motor_setter(CMD_GOTO, Vstepper, (theconf.maxVpos+theconf.minVpos)/2)){
                LOGMSG("process_setup_stage(): SETUP_WAITUVMID");
                sstatus = SETUP_WAITUVMID;
            }else{
                WARNX("Can't move U/V to middle");
                LOGWARN("Can't move U/V to middle");
                sstatus = SETUP_INIT;
            }
        break;
        case SETUP_WAITUVMID: // wait for the middle
            if(!relaxed(Ustepper) || !relaxed(Vstepper)) break;
            DBG("The middle reached");
            // now move U to zero
            if(nth_motor_setter(CMD_GOTO, Ustepper, theconf.minUpos)){
                LOGMSG("process_setup_stage(): SETUP_WAITU0");
                sstatus = SETUP_WAITU0;
            }else{
                LOGWARN("Can't move U to min");
                sstatus = SETUP_INIT;
            }
        break;
        case SETUP_WAITU0: // wait while U moves to zero
            if(!coordsRdy) return;
            coordsRdy = FALSE;
            X0U = Xtarget; Y0U = Ytarget;
            DBG("got X0U=%.1f, Y0U=%.1f", X0U, Y0U);
            LOGDBG("got X0U=%.1f, Y0U=%.1f", X0U, Y0U);
            // move U to max
            if(nth_motor_setter(CMD_GOTO, Ustepper, theconf.maxUpos)){
                LOGMSG("process_setup_stage(): SETUP_WAITUMAX");
                sstatus = SETUP_WAITUMAX;
            }else{
                LOGWARN("Can't move U to max");
                sstatus = SETUP_INIT;
            }
        break;
        case SETUP_WAITUMAX: // wait while U moves to UVworkrange
            if(!coordsRdy) return;
            coordsRdy = FALSE;
            XmU = Xtarget; YmU = Ytarget;
            LOGDBG("got XmU=%.1f, YmU=%.1f", XmU, YmU);
            // now move U to zero and V to min
            if(nth_motor_setter(CMD_GOTO, Ustepper, (theconf.maxUpos+theconf.minUpos)/2) &&
               nth_motor_setter(CMD_GOTO, Vstepper, theconf.minVpos)){
                LOGMSG("process_setup_stage(): SETUP_WAITV0");
                sstatus = SETUP_WAITV0;
            }else{
                LOGWARN("Can't move U -> mid OR/AND V -> min");
                sstatus = SETUP_INIT;
            }
        break;
        case SETUP_WAITV0: // wait while V moves to 0
            if(!coordsRdy) return;
            coordsRdy = FALSE;
            X0V = Xtarget; Y0V = Ytarget;
            LOGDBG("got X0V=%.1f, Y0V=%.1f", X0V, Y0V);
            if(nth_motor_setter(CMD_GOTO, Vstepper, theconf.maxVpos)){
                LOGMSG("process_setup_stage(): SETUP_WAITVMAX");
                sstatus = SETUP_WAITVMAX;
            }else{
                LOGWARN("Can't move V -> max");
                sstatus = SETUP_INIT;
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
            double KU = (theconf.maxUpos - theconf.minUpos) / sqU;
            double KV = (theconf.maxVpos - theconf.minVpos) / sqV;
            double sa = dyU/sqU, ca = dxU/sqU, sb = dyV/sqV, cb = dxV/sqV; // sin(alpha) etc
            LOGDBG("KU=%.4f, KV=%.4f, sa=%.4f, ca=%.4f, sb=%.4f, cb=%.4f",
                   KU, KV, sa, ca, sb, cb);
            /*
             * [dX dY] = M*[dU dV], M = [ca/KU cb/KV; sa/KU sb/KV] ===>
             * [dU dV] = inv(M)*[dX dY],
             * inv(M) = 1/(ca/KU*sb/KV - sa/KU*cb/KV)*[sb/KV -cb/KV; -sa/KU ca/KU]
             */
            double mul = 1. / (ca/KU*sb/KV - sa/KU*cb/KV);
            theconf.Kxu = mul*sb/KV;
            theconf.Kyu = -mul*cb/KV;
            theconf.Kxv = -mul*sa/KU;
            theconf.Kyv = mul*ca/KU;
            LOGMSG("process_setup_stage(): Kxu=%g, Kyu=%g; Kxv=%g, Kyv=%g", theconf.Kxu, theconf.Kyu, theconf.Kxv, theconf.Kyv);
            DBG("Now save new configuration");
            saveconf(NULL); // try to store configuration
            // fallthrough
        endmoving:
            if(nth_motor_setter(CMD_GOTO, Vstepper, (theconf.maxVpos+theconf.minVpos)/2)) sstatus = SETUP_FINISH;
        break;
        case SETUP_FINISH: // goto middle again
            if(!relaxed(Ustepper) || !relaxed(Vstepper)) break;
            sstatus = SETUP_NONE;
            state = STP_RELAX;
        break;
        default: // SETUP_NONE - do nothing
            return;
    }
}

// process target finding stage (target should be fixed for at least NCONSEQ frames within COORDTOLERANCE)
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
 * @brief compute_pid - calculate PID responce for error
 * @param pid - U/V PID parameters
 * @param error - current error
 * @param current_time - and current time
 * @return PID-corrected responce
 */
static double compute_pid(PIDController *pid, double error, double current_time) {
    double dt = current_time - pid->prev_time;
    if(dt <= 0.) dt = 0.01; // Default to 10ms if time isn't tracked
    // Integral term with anti-windup
    pid->integral += error * dt;
    // Clamp integral to ?1000 (adjust based on system limits)
    if(pid->integral > 1000.) pid->integral = 1000.;
    if(pid->integral < -1000.) pid->integral = -1000.;
    // Derivative term (filtered)
    double derivative = (error - pid->prev_error) / dt;
    pid->prev_error = error;
    pid->prev_time = current_time;
    double pid_out = (pid->Kp * error) + (pid->Ki * pid->integral) + (pid->Kd * derivative);
    LOGDBG("PID: error=%.2f, integral=%.2f, derivative=%.2f, output=%.2f",
           error, pid->integral, derivative, pid_out);
    return pid_out;
}

/**
 * @brief try2correct - try to correct position
 * @param dX - delta of X-coordinate in image space
 * @param dY - delta of Y-coordinate in image space
 * @return FALSE if failed (motors are moving etc) or correction out of limits
 */
static int try2correct(double dX, double dY){
    if(!relaxed(Ustepper) || !relaxed(Vstepper)) return FALSE;
    // calculations: make Ki=0, Kd=0; increase Kp until oscillations;
    // now Tu - osc period, Ku=Kp for oscillations; so:
    // Kp = 0.6*Ku; Ki = 1.2*Ku/Tu; Kd = 0.075*Ku*Tu (Ziegler-Nichols)
    static PIDController pidU = {0}, pidV = {0};
    // refresh parameters from configuration
    pidU.Kp = theconf.PIDU_P; pidU.Ki = theconf.PIDU_I; pidU.Kd = theconf.PIDU_D;
    pidV.Kp = theconf.PIDV_P; pidV.Ki = theconf.PIDV_I; pidV.Kd = theconf.PIDV_D;
    double dU, dV;
    double current_time = sl_dtime();
    if( current_time - pidU.prev_time > MAX_PID_TIME
        || current_time - pidV.prev_time > MAX_PID_TIME){
        LOGWARN("Too old PID time: have dt=%gs", current_time - pidU.prev_time);
        pidU.prev_time = pidV.prev_time = current_time;
        pidU.integral = pidV.integral = 0.;
        return FALSE;
    }
    // dU = KU*(dX*cosXU + dY*sinXU); dV = KV*(dX*cosXV + dY*sinXV)
    dU = theconf.Kxu * dX + theconf.Kyu * dY;
    dV = theconf.Kxv * dX + theconf.Kyv * dY;
    LOGDBG("dx/dy: %g/%g; dU/dV: %g/%g", dX, dY, dU, dV);
    // Compute PID outputs
    double pidU_out = compute_pid(&pidU, dU, current_time);
    double pidV_out = compute_pid(&pidV, dV, current_time);
    int usteps = (int)pidU_out, vsteps = (int)pidV_out;
    int Unew = Uposition + usteps, Vnew = Vposition + vsteps;
    if(Unew > theconf.maxUpos || Unew < theconf.minUpos ||
       Vnew > theconf.maxVpos || Vnew < theconf.minVpos){
        // Reset integral to prevent windup
        pidU.integral = 0;
        pidV.integral = 0;
        // TODO: here we should signal that the limit reached and move by telescope
        LOGWARN("Correction failed, curpos: %d, %d, should move to %d, %d",
                Uposition, Vposition, Unew, Vnew);
        return FALSE;
    }
    LOGDBG("try2correct(): move from (%d, %d) to (%d, %d), delta (%.1f, %.1f)",
           Uposition, Vposition, Unew, Vnew, dU, dV);
    int ret = TRUE;
    if(usteps) ret = nth_motor_setter(CMD_RELPOS, Ustepper, usteps);
    if(vsteps) ret &= nth_motor_setter(CMD_RELPOS, Vstepper, vsteps);
    if(!ret) LOGWARN("Canserver: cant run corrections");
    return ret;
}

// global variable proc_corr
/**
 * @brief stp_process_corrections - get XY corrections (in pixels) and move motors to fix them
 * @param X, Y - centroid (x,y) in screen coordinate system
 * This function called from improc.c each time the corrections calculated (ONLY IF Xtarget/Ytarget > -1)
 */
static void stp_process_corrections(double X, double Y){
    static int coordstrusted = TRUE;
    if(!relaxed(Ustepper) || !relaxed(Vstepper)){ // don't process coordinates when moving
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
static int stp_setstate(STPstate newstate){
    if(newstate == state) return TRUE;
    if(newstate == STP_DISCONN){
        stp_disc();
        return TRUE;
    }
    if(state == STP_DISCONN){
        if(!stp_connect_server()) return FALSE;
    }
    if(newstate == STP_SETUP || newstate == STP_GOTOTHEMIDDLE){
        sstatus = SETUP_INIT;
    }else sstatus = SETUP_NONE;
    state = newstate;
    return TRUE;
}

// get current status (global variable stepstatus)
// return JSON string with different parameters
static char *stp_status(const char *messageid, char *buf, int buflen){
    // FNAME();
    int l;
    char *bptr = buf;
    const char *s = NULL, *stage = NULL;
    l = snprintf(bptr, buflen, "{ \"%s\": \"%s\", \"status\": ", MESSAGEID, messageid);
    buflen -= l; bptr += l;
    switch(state){
        case STP_DISCONN:
            l = snprintf(bptr, buflen, "\"disconnected\"");
        break;
        case STP_RELAX:
            l = snprintf(bptr, buflen, "\"ready\"");
        break;
        case STP_SETUP:
        case STP_GOTOTHEMIDDLE:
            s = (state == STP_SETUP) ? "setup" : "gotomiddle";
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
        case STP_FINDTARGET:
            l = snprintf(bptr, buflen, "\"findtarget\"");
        break;
        case STP_FIX:
            l = snprintf(bptr, buflen, "\"%s\"", fixerr ? "fixoutofrange" : "fixing");
        break;
        default:
            l = snprintf(bptr, buflen, "\"unknown\"");
    }
    buflen -= l; bptr += l;
    if(state != STP_DISCONN){
        l = snprintf(bptr, buflen, ", ");
        buflen -= l; bptr += l;
        for(int i = 0; i < NMOTORS; ++i){
            if(!motornames[i]) continue; // this motor not used
            l = snprintf(bptr, buflen, "\"%s\": { \"status\": \"%s\", \"position\": %d }, ",
                         motornames[i], str_states[motstates[i]], motposition[i]);
            buflen -= l; bptr += l;
        }
    }
    snprintf(bptr, buflen, " }\n");
    return buf;
}

// commands from client to change status
static const char* stringstatuses[STP_STATE_AMOUNT] = {
    [STP_DISCONN] = "disconnect",
    [STP_RELAX] = "relax",
    [STP_SETUP] = "setup",
    [STP_GOTOTHEMIDDLE] = "middle",
    [STP_FINDTARGET] = "findtarget",
    [STP_FIX] = "fix",
    [STP_UNDEFINED] = "undefined"
};

// try to set new status (global variable stepstatus)
static char *set_stpstatus(const char *newstatus, char *buf, int buflen){
    if(!buf) return NULL;
    if(!newstatus){ // getter
        snprintf(buf, buflen, "%s", stringstatuses[state]);
        return buf;
    }
    // FNAME();
    STPstate newstate = STP_UNDEFINED;
    for(int i = 0; i < STP_UNDEFINED; ++i){
        if(strcasecmp(stringstatuses[i], newstatus) == 0){
            newstate = (STPstate)i;
            break;
        }
    }
    if(newstate != STP_UNDEFINED){
        if(stp_setstate(newstate)){
            snprintf(buf, buflen, OK);
            return buf;
        }else{
            snprintf(buf, buflen, FAIL);
            return buf;
        }
    }
    int L = snprintf(buf, buflen, "status '%s' undefined, allow: ", newstatus);
    char *ptr = buf;
    for(int i = 0; i < STP_UNDEFINED && buflen > 2; ++i){
        buflen -= L;
        ptr += L;
        L = snprintf(ptr, buflen-2, "'%s' ", stringstatuses[i]);
    }
    ptr[L-1] = '\n';
    return buf;
}

// MAIN THREAD
static void *stp_process_states(_U_ void *arg){
    // FNAME();
    static int first = TRUE; // flag for logging when can't reconnect
    while(!stopwork){
        usleep(10000);
        // check for disconnection flag
        if(motorsoff){
            motorsoff = FALSE;
            stp_disconnect();
            sleep(1);
            continue;
        }
        // check for moving
        if(state == STP_DISCONN){
            DBG("DISCONNECTED - try to connect");
            sleep(1);
            stp_connect_server();
            continue;
        }
        // check request to change focus
        if(chfocus){
            DBG("Try to move F to %d", newfocpos);
            if(nth_motor_setter(CMD_GOTO, Fstepper, newfocpos)) chfocus = FALSE;
        }
        if(dUmove){
            DBG("Try to move U by %d", dUmove);
            if(nth_motor_setter(CMD_RELPOS, Ustepper, dUmove)) dUmove = 0;
        }
        if(dVmove){
            DBG("Try to move V by %d", dVmove);
            if(nth_motor_setter(CMD_RELPOS, Vstepper, dVmove)) dVmove = 0;
        }
        static double t0 = -1.;
        if(t0 < 0.) t0 = sl_dtime();
        if(state != STP_DISCONN){
            if(sl_dtime() - t0 >= 0.1){ // each 0.1s check state if steppers aren't disconnected
                t0 = sl_dtime();
                chkall();
            }
            if(!relaxed(Ustepper) && !relaxed(Vstepper)) continue;
            first = TRUE;
        }
        // if we are here, all U/V moving is finished
        switch(state){ // steppers state machine
            case STP_DISCONN:
                if(!stp_connect_server()){
                    WARNX("Can't reconnect");
                    if(first){
                        LOGWARN("Can't reconnect");
                        first = FALSE;
                    }
                    sleep(5);
                }
            break;
            case STP_SETUP: // setup axes (before this state set Xtarget/Ytarget in improc.c)
                process_setup_stage();
            break;
            case STP_GOTOTHEMIDDLE:
                process_movetomiddle_stage();
            break;
            case STP_FINDTARGET: // calculate target coordinates
                if(coordsRdy){
                    coordsRdy = FALSE;
                    if(process_targetstage(Xtarget, Ytarget))
                        state = STP_RELAX;
                }
            break;
            case STP_FIX: // process corrections
                if(coordsRdy){
                    coordsRdy = FALSE;
                    DBG("GOT AVERAGE -> correct\n");
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
            default: // STP_RELAX
                break;
        }
    }
    DBG("thread stopped");
    return NULL;
}

// change focus (global variable movefocus)
static char *set_pfocus(const char *newstatus, char *buf, int buflen){
    if(!buf) return NULL;
    if(!newstatus){ // getter
        snprintf(buf, buflen, "%d", Fposition);
        return buf;
    }
    int newval = atoi(newstatus);
    if(newval < theconf.minFpos || newval > theconf.maxFpos){
        snprintf(buf, buflen, FAIL);
        LOGDBG("Failed to move F -> %d", newval);
        DBG("Failed to move F -> %d", newval);
    }else{
        snprintf(buf, buflen, OK);
        newfocpos = newval;
        chfocus = TRUE;
    }
    return buf;
}
// move by U and V axis
static char *Umove(const char *val, char *buf, int buflen){
    if(!buf) return NULL;
    if(!val){ // getter
        snprintf(buf, buflen, "%d", Uposition);
        return buf;
    }
    int d = atoi(val);
    int Unfixed = Uposition + d;
    if(Unfixed > theconf.maxUpos || Unfixed < theconf.minUpos){
        snprintf(buf, buflen, FAIL);
        LOGDBG("Failed to move U -> %d", Unfixed);
        DBG("Failed to move U -> %d", Unfixed);
        return buf;
    }
    dUmove = d;
    snprintf(buf, buflen, OK);
    return buf;
}
static char *Vmove(const char *val, char *buf, int buflen){
    if(!buf) return NULL;
    if(!val){ // getter
        snprintf(buf, buflen, "%d", Vposition);
        return buf;
    }
    int d = atoi(val);
    int Vnfixed = Vposition + d;
    if(Vnfixed > theconf.maxVpos || Vnfixed < theconf.minVpos){
        snprintf(buf, buflen, FAIL);
        LOGDBG("Failed to move V -> %d", Vnfixed);
        DBG("Failed to move V -> %d", Vnfixed);
        return buf;
    }
    dVmove = d;
    snprintf(buf, buflen, OK);
    return buf;
}

static steppersproc steppers = {
    .stepdisconnect = stp_disc,
    .proc_corr = stp_process_corrections,
    .stepstatus = stp_status,
    .setstepstatus = set_stpstatus,
    .movefocus = set_pfocus,
    .moveByU = Umove,
    .moveByV = Vmove,
};

/**
 * @brief STP_connect - run a thread processed steppers status
 * @return FALSE if failed to connect immediately
 */
steppersproc* steppers_connect(){
    DBG("Try to connect");
    if(!stp_connect_server()) return NULL;
    if(pthread_create(&processingthread, NULL, stp_process_states, NULL)){
        LOGERR("pthread_create() for steppers server failed");
        WARNX("pthread_create()");
        return NULL;
    }
    return &steppers;
}
