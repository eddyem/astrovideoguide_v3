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


#include <arpa/inet.h>  // inet_ntop
#include <libgen.h>     // basename
#include <limits.h>     // INT_xxx
#include <netdb.h>      // addrinfo
#include <poll.h>
#include <pthread.h>
#include <signal.h>     // pthread_kill
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h> // syscall
#include <unistd.h>     // daemon

#include "cmdlnopts.h"
#include "config.h"
#include "debug.h"
#include "improc.h"
#include "socket.h"
#include "steppers.h"

// buffer size for received data
#define BUFLEN      (1024)
// buffer size for answer
#define ANSBUFLEN   (32768)
// Max amount of connections
#define BACKLOG     (10)

static pthread_mutex_t cmd_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
TODO3: add 'FAIL error text' if not OK and instead all "wrong message"
*/


// additional commands list - getters
typedef struct{
    const char *command;
    char *(*handler)(const char *messageid, char *buf, int buflen);
    const char *help;
} getter;
// setters
typedef struct{
    const char *command;
    char *(*handler)(const char *val, char *buf, int buflen);
    const char *help;
} setter;

static char *helpmsg(const char *messageid, char *buf, int buflen);
static char *stepperstatus(const char *messageid, char *buf, int buflen);
static char *getimagedata(const char *messageid, char *buf, int buflen);
static getter getterHandlers[] = {
    {"help", helpmsg, "List avaiable commands"},
    {"imdata", getimagedata, "Get image data (status, path, FPS, counter)"},
    {"settings", listconf, "List current configuration"},
    {"stpserv", stepperstatus, "Get status of steppers server"},
    {NULL, NULL, NULL}
};

static char *setstepperstate(const char *state, char *buf, int buflen);
static char *setfocusstate(const char *state, char *buf, int buflen);
static char *moveU(const char *val, char *buf, int buflen);
static char *moveV(const char *val, char *buf, int buflen);
static char *addcmnt(const char *cmnt, char *buf, int buflen);

static setter setterHandlers[] = {
    {"comment", addcmnt, "Add comment to XY log file"},
    {"focus", setfocusstate, "Move focus to given value"},
    {"moveU", moveU, "Relative moving by U axe"},
    {"moveV", moveV, "Relative moving by V axe"},
    {"stpstate", setstepperstate, "Set given steppers' server state"},
    {NULL, NULL, NULL}
};

static char *retFAIL(char *buf, int buflen){
    snprintf(buf, buflen, FAIL);
    return buf;
}
static char *retOK(char *buf, int buflen){
    snprintf(buf, buflen, OK);
    return buf;
}


/**************** functions to process commands ****************/
// getters
static char *helpmsg(_U_ const char *messageid, char *buf, int buflen){
    if(get_cmd_list(buf, buflen)){
        int l = strlen(buf), L = buflen - l;
        char *ptr = buf + l;
        getter *g = getterHandlers;
        while(L > 0 && g->command){
            int s = snprintf(ptr, L, "%s - %s\n", g->command, g->help);
            if(s < 1) break;
            L -= s; ptr += s;
            ++g;
        }
        setter *sh = setterHandlers;
        while(L > 0 && sh->command){
            int s = snprintf(ptr, L, "%s=newval - %s\n", sh->command, sh->help);
            if(s < 1) break;
            L -= s; ptr += s;
            ++sh;
        }
        return buf;
    }
    return NULL;
}
static char *stepperstatus(const char *messageid, char *buf, int buflen){
    if(theSteppers && theSteppers->stepstatus) return theSteppers->stepstatus(messageid, buf, buflen);
    return retFAIL(buf, buflen);
}
static char *getimagedata(const char *messageid, char *buf, int buflen){
    if(imagedata) return imagedata(messageid, buf, buflen);
    return retFAIL(buf, buflen);
}

// setters
static char *setstepperstate(const char *state, char *buf, int buflen){
    DBG("set steppersstate to %s", state);
    if(theSteppers && theSteppers->setstepstatus) return theSteppers->setstepstatus(state, buf, buflen);
    return retFAIL(buf, buflen);
}
static char *setfocusstate(const char *state, char *buf, int buflen){
    DBG("move focus to %s", state);
    if(theSteppers && theSteppers->movefocus) return theSteppers->movefocus(state, buf, buflen);
    return retFAIL(buf, buflen);
}
static char *moveU(const char *val, char *buf, int buflen){
    if(theSteppers && theSteppers->moveByU) return theSteppers->moveByU(val, buf, buflen);
    return retFAIL(buf, buflen);
}
static char *moveV(const char *val, char *buf, int buflen){
    if(theSteppers && theSteppers->moveByV) return theSteppers->moveByV(val, buf, buflen);
    return retFAIL(buf, buflen);
}
static char *addcmnt(const char *cmnt, char *buf, int buflen){
    if(!cmnt) return retFAIL(buf, buflen);
    char *line = strdup(cmnt);
    int ret = XYcomment(line);
    free(line);
    return ret ? retOK(buf, buflen) : retFAIL(buf, buflen) ;
}

/*
static char *rmnl(const char *msg, char *buf, int buflen){
    strncpy(buf, msg, buflen);
    char *nl = strchr(buf, '\n');
    if(nl) *nl = 0;
    return buf;
}
*/

/**
 * @brief processCommand - command parser
 * @param msg - incoming message
 * @param ans - buffer for answer
 * @param anslen - length of `ans`
 * @return NULL if no answer or pointer to ans
 */
static char *processCommand(const char msg[BUFLEN], char *ans, int anslen){
    char value[BUFLEN];
    char *kv = get_keyval(msg, value); // ==NULL for getters/commands without equal sign
    DBG("message: %s, value: %s, key: %s", msg, value, kv);
    confparam *par;
    if(kv){
        DBG("got KEY '%s' with value '%s'", kv, value);
        key_value result;
        par = chk_keyval(kv, value, &result);
        if(par){ // configuration parameter
            DBG("found this key in conf");
            switch(par->type){
                case PAR_INT:
                    DBG("FOUND! Integer, old=%d, new=%d", *((int*)par->ptr), result.val.intval);
                    *((int*)par->ptr) = result.val.intval;
                break;
                case PAR_DOUBLE:
                    DBG("FOUND! Double, old=%g, new=%g", *((double*)par->ptr), result.val.dblval);
                    *((double*)par->ptr) = result.val.dblval;
                break;
                default:
                    snprintf(ans, anslen, FAIL);
                    free(kv);
                    return ans;
            }
            snprintf(ans, anslen, OK);
            free(kv);
            return ans;
        }else{
            DBG("check common setters");
            setter *s = setterHandlers;
            while(s->command){
                //int l = strlen(s->command);
                //if(strncasecmp(msg, s->command, l) == 0)
                DBG("check '%s' == '%s' ?", kv, s->command);
                if(strcmp(kv, s->command) == 0){
                    free(kv);
                    return s->handler(value, ans, anslen);
                }
                ++s;
            }
        }
        free(kv);
    }else{
        DBG("getter?");
        // first check custom getters
        getter *g = getterHandlers;
        while(g->command){
            //int l = strlen(g->command);
            //if(strncasecmp(msg, g->command, l) == 0)
            if(0 == strcmp(msg, g->command))
                return g->handler(g->command, ans, anslen);
            ++g;
        }
        DBG("not found in getterHandlers");
        // check custom setters
        setter *s = setterHandlers;
        while(s->command){
            if(strcmp(msg, s->command) == 0){
                size_t p = snprintf(ans, anslen, "%s=", msg);
                s->handler(NULL, ans+p, anslen-p);
                return ans;
            }
            ++s;
        }
        DBG("not found in setterHandlers");
        // and check configuration parameters
        confparam *par = find_key(msg);
        if(par){
            switch(par->type){
                case PAR_INT:
                    snprintf(ans, anslen, "%s=%d", msg, *((int*)par->ptr));
                break;
                case PAR_DOUBLE:
                    snprintf(ans, anslen, "%s=%g",  msg, *((double*)par->ptr));
                break;
                default:
                    snprintf(ans, anslen, FAIL);
                break;
            }
            return ans;
        }
        DBG("not found in parameters");
    }
    snprintf(ans, anslen, FAIL);
    return ans;
}

/**************** SERVER FUNCTIONS ****************/
/**
 * Send data over socket (and add trailing '\n' if absent)
 * @param sock      - socket fd
 * @param textbuf   - zero-trailing buffer with data to send
 * @return amount of sent bytes
 */
static size_t send_data(int sock, const char *textbuf){
    size_t total = 0;
    size_t len = strlen(textbuf);
    const char *ptr = textbuf;
    while(total < len){
        ssize_t sent = write(sock, ptr + total, len - total);
        if(sent < 0){
            if(errno == EINTR) continue;
            LOGERR("Write error: %s", strerror(errno));
            return total;
        }
        total += sent;
    }
    if(textbuf[len-1] != '\n'){
        if(write(sock, "\n", 1) != 1){
            LOGERR("Failed to write newline");
        }
    }
    return total;
}

/**
 * @brief handle_socket - read and process data from socket
 * @param sock - socket fd
 * @return 0 if all OK, 1 if socket closed
 */
static int handle_socket(int sock){
    //FNAME();
    char buff[BUFLEN];
    char ansbuff[ANSBUFLEN];
    ssize_t rd = read(sock, buff, BUFLEN-1);
    if(rd < 1){
        DBG("read() == %zd", rd);
        return 1;
    }
    // add trailing zero to be on the safe side
    buff[rd] = 0;
    char *eol = strchr(buff, '\n');
    if(eol) *eol = 0; // clear '\n'
    // now we should check what do user want
    // here we can process user data
    DBG("user %d send '%s'", sock, buff);
    LOGDBG("user %d send '%s'", sock, buff);
    pthread_mutex_lock(&cmd_mutex);
    char *ans = processCommand(buff, ansbuff, ANSBUFLEN-1); // run command parser
    if(ans){
        send_data(sock, ans);   // send answer
    }
    pthread_mutex_unlock(&cmd_mutex);
    return 0;
}

// shutdown client and close
static void cleanup_client(int fd) {
    if(fd > 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        DBG("Client with fd %d closed", fd);
        LOGMSG("Client %d disconnected", fd);
    }
}

// main socket server
static void *server(void *asock){
    DBG("server(): getpid: %d, tid: %lu",getpid(), syscall(SYS_gettid));
    int sock = *((int*)asock);
    if(listen(sock, BACKLOG) == -1){
        LOGERR("server(): listen() failed");
        WARN("listen");
        return NULL;
    }
    int nfd = 1;
    struct pollfd poll_set[BACKLOG+1];
    memset(poll_set, 0, sizeof(poll_set));
    poll_set[0].fd = sock;
    poll_set[0].events = POLLIN;
    while(1){
        if(stopwork){
            DBG("server() exit @ global stop");
            LOGDBG("server() exit @ global stop");
            return NULL;
        }
        int ready = poll(poll_set, nfd, 1); // 1ms timeout
        if(ready < 0){
            if(errno == EINTR) continue;
            LOGERR("Poll error: %s", strerror(errno));
            return NULL;
        }
        if(0 == ready) continue;
        for(int fdidx = 0; fdidx < nfd; ++fdidx){ // poll opened FDs
            if((poll_set[fdidx].revents & POLLIN) == 0) continue;
            poll_set[fdidx].revents = 0;
            if(fdidx){ // client
                int fd = poll_set[fdidx].fd;
                //int nread = 0;
                //ioctl(fd, FIONREAD, &nread);
                if(handle_socket(fd)){ // socket closed - remove it from list
                    cleanup_client(fd);
                    // move last to free space
                    poll_set[fdidx] = poll_set[nfd - 1];
                    --nfd;
                }
            }else{ // server
                socklen_t size = sizeof(struct sockaddr_in);
                struct sockaddr_in their_addr;
                int newsock = accept(sock, (struct sockaddr*)&their_addr, &size);
                if(newsock <= 0){
                    LOGERR("server(): accept() failed");
                    WARN("accept()");
                    continue;
                }
                struct in_addr ipAddr = their_addr.sin_addr;
                char str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN);
                DBG("Connection from %s, give fd=%d", str, newsock);
                LOGMSG("Got connection from %s, fd=%d", str, newsock);
                if(nfd == BACKLOG + 1){
                    LOGWARN("Max amount of connections: disconnect %s (%d)", str, newsock);
                    send_data(newsock, "Max amount of connections reached!");
                    WARNX("Limit of connections reached");
                    close(newsock);
                }else{
                    memset(&poll_set[nfd], 0, sizeof(struct pollfd));
                    poll_set[nfd].fd = newsock;
                    poll_set[nfd].events = POLLIN;
                    ++nfd;
                }
            }
        } // endfor
    }
    // disconnect all
    for(int i = 1; i < nfd; ++i) cleanup_client(poll_set[i].fd);
    LOGERR("server(): UNREACHABLE CODE REACHED!");
}

// data gathering & socket management
static void daemon_(int sock){
    if(sock < 0) return;
    pthread_t sock_thread;//, canserver_thread;
    if(pthread_create(&sock_thread, NULL, server, (void*) &sock)){
        LOGERR("daemon_(): pthread_create() failed");
        ERR("pthread_create()");
    }
    do{
        if(stopwork){
            DBG("kill");
            pthread_join(sock_thread, NULL);
            return;
        }
        if(pthread_kill(sock_thread, 0) == ESRCH){ // died
            WARNX("Sockets thread died");
            LOGERR("Sockets thread died");
            pthread_join(sock_thread, NULL);
            if(pthread_create(&sock_thread, NULL, server, (void*) &sock)){
                LOGERR("daemon_(): new pthread_create(sock_thread) failed");
                ERR("pthread_create(sock_thread)");
            }
        }
        usleep(1000); // sleep a little or thread's won't be able to lock mutex
    }while(1);
    LOGERR("daemon_(): UNREACHABLE CODE REACHED!");
}

/**
 * open sockets
 * // should be called only once!!!
 */
static void *connect2sock(void *data){
    FNAME();
    char port[10];
    int portN = *((int*)data);
    snprintf(port, 10, "%d", portN);
    DBG("get port: %s", port);
    int sock = -1;
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if(getaddrinfo("127.0.0.1", port, &hints, &res) != 0){ // accept only local connections
        LOGERR("daemonize(): getaddrinfo() failed");
        ERR("getaddrinfo");
    }
    struct sockaddr_in *ia = (struct sockaddr_in*)res->ai_addr;
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ia->sin_addr), str, INET_ADDRSTRLEN);
    // loop through all the results and bind to the first we can
    for(p = res; p != NULL; p = p->ai_next){
        if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            LOGWARN("openIOport(): socket() failed");
            WARN("socket");
            continue;
        }
        int reuseaddr = 1;
        if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) == -1){
            LOGERR("openIOport(): setsockopt() failed");
            ERR("setsockopt");
        }
        if(bind(sock, p->ai_addr, p->ai_addrlen) == -1){
            close(sock);
            LOGERR("openIOport(): bind() failed");
            WARN("bind");
            continue;
        }
        break; // if we get here, we have a successfull connection
    }
    if(p == NULL){
        LOGERR("openIOport(): failed to bind socket, exit");
        // looped off the end of the list with no successful bind
        ERRX("failed to bind socket");
    }
    freeaddrinfo(res);
    daemon_(sock);
    close(sock);
    LOGWARN("openIOport(): close @ global stop");
    return NULL;
}


// run socket thread
void openIOport(int portN){
    static int portnum = 0;
    if(portnum) return;
    portnum = portN;
    pthread_t connthread;
    DBG("open port: %d", portN);
    if(pthread_create(&connthread, NULL, connect2sock, (void*) &portnum)){
        LOGERR("openIOport(): pthread_create() failed");
        ERR("pthread_create()");
    }
    pthread_detach(connthread);
}
