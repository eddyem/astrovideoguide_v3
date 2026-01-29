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

#include <float.h> // FLT_EPSILON
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cameracapture.h"
#include "cmdlnopts.h"
#include "config.h"
#include "debug.h"
#include "imagefile.h"
#include "improc.h"
#include "median.h"

// pointer to selected camera
static camera *theCam = NULL;

static float gain = 0., gainmax = 0.;
static float exptime = -1.;
static float brightness = 0.;
static int connected = FALSE;

static frameformat curformat;
static frameformat maxformat;
static frameformat stepformat;

// statistics of last image
typedef struct{
    Imtype minval, maxval, bkg;
    float avg, xc, yc;
    ptstat_t stat;
} imdata_t;

static imdata_t lastimdata = {0};

static void changeformat(){
    if(!theCam) return;
    if(maxformat.h < 1 || maxformat.w < 1){
        WARNX("Bad max format data");
        return;
    }
    if(stepformat.h < 1 || stepformat.w < 1){
        WARNX("Bad step format data");
        LOGWARN("Bad step format data");
        return;
    }
    if(stepformat.xoff < 1) stepformat.xoff = 1;
    if(stepformat.yoff < 1) stepformat.yoff = 1;
    curformat.h = (theconf.height < maxformat.h) ? theconf.height : maxformat.h;
    curformat.h -= curformat.h % stepformat.h;
    curformat.w = (theconf.width < maxformat.w) ? theconf.width : maxformat.w;
    curformat.w -= curformat.w % stepformat.w;
    curformat.xoff = (theconf.xoff + curformat.w <= maxformat.w) ? theconf.xoff : maxformat.w - curformat.w;
    curformat.xoff -= curformat.xoff % stepformat.xoff;
    curformat.yoff = (theconf.yoff + curformat.h <= maxformat.h) ? theconf.yoff : maxformat.h - curformat.h;
    curformat.yoff -= curformat.yoff % stepformat.yoff;
    if(theCam->setgeometry(&curformat)){ // now we can change config values to real
        theconf.height = curformat.h;
        theconf.width = curformat.w;
        theconf.xoff = curformat.xoff;
        theconf.yoff = curformat.yoff;
    }
}

/**
 * @brief setCamera - set active camera & initialize it
 * @param cptr - pointer to new camera
 * @return FALSE if failed
 */
int setCamera(camera *cptr){
    camdisconnect();
    theCam = cptr;
    connected = theCam->connect();
    if(!connected) return FALSE;
    gainmax = theCam->getmaxgain();
    gain = theconf.gain;
    brightness = theconf.brightness;
    if(!theCam->getgeomlimits(&maxformat, &stepformat)){
        WARNX("Can't detect camera format limits");
        LOGWARN("Can't detect camera format limits");
        return TRUE;
    }
    changeformat();
    LOGMSG("Camera connected, max gain: %.1f, max (W,H): (%d,%d)", gainmax, maxformat.w, maxformat.h);
    return TRUE;
}

void camdisconnect(){
    if(!connected) return;
    connected = FALSE;
    if(theCam) theCam->disconnect();
}

static void calcexpgain(float newexp){
    DBG("recalculate exposition: oldexp=%g, oldgain=%g, newexp=%g", exptime, gain, newexp);
    float newgain = gain;
#if 0
    while(newexp*1.25 > theconf.minexp){ // increase gain first
        if(newgain < gainmax - 0.9999){
            newgain += 1.;
            newexp /= 1.25;
        }else break;
    }
    while(newexp < theconf.minexp){
        if(1.25*newexp < theconf.maxexp && newgain > 0.9999){
            newgain -= 1.;
            newexp *= 1.25;
        }else break;
    }
#endif
    if(newexp > exptime){ // need to increase exptime - try to increase gain first
        if(newgain < gainmax - 0.9999f){
            newgain += 1.f;
            newexp = exptime; // leave exptime unchanged
        }else if(newgain < gainmax) newgain = gainmax;
    }else{ // decrease -> decrease gain if exptime too small
        if(newexp < theconf.minexp){
            if(newgain > 1.f) newgain -= 1.f;
            else newgain = 0.f;
        }
    }

    if(newexp < theconf.minexp) newexp = theconf.minexp;
    else if(newexp > theconf.maxexp) newexp = theconf.maxexp;
    LOGDBG("recalc exp from %g to %g; gain from %g to %g", exptime, newexp, gain, newgain);
    exptime = newexp;
    gain = newgain;
    DBG("New values: exp=%g, gain=%g", exptime, gain);
}

//convertedImage.pData, convertedImage.cols, convertedImage.rows, convertedImage.stride
static void recalcexp(Image *I){
#ifdef EBUG
    green("RECALCEXP\n"); fflush(stdout);
#endif
    // check if user changed exposition values
    if(exptime < theconf.minexp){
        exptime = theconf.minexp;
        LOGDBG("recalcexp(): minimal exptime");
        return;
    }
    else if(exptime > theconf.maxexp){
        exptime = theconf.maxexp;
        LOGDBG("recalcexp(): maximal exptime");
        return;
    }
    size_t histogram[HISTOSZ];
    if(!get_histogram(I, histogram)){
        WARNX("Can't calculate histogram");
        LOGWARN("recalcexp(): can't calculate histogram");
        return;
    }
    int idx100;
    size_t sum100 = 0;
    for(idx100 = HISTOSZ-1; idx100 >= 0; --idx100){
        sum100 += histogram[idx100];
        if(sum100 > 100) break;
    }
    DBG("Sum100=%zd, idx100=%d", sum100, idx100);
    if(idx100 > 230 && idx100 < 253){
        DBG("idx100=%d - good", idx100);
        return; // good values
    }
    if(idx100 > 253){ // exposure too long
        DBG("Exp too long");
        calcexpgain(exptime * 0.3f);
    }else{ // exposure too short
        if(idx100 > 5){
            DBG("Exp too short");
            calcexpgain(exptime * 230.f / (float)idx100);
        }else{
            DBG("increase exptime 50 times");
            calcexpgain(exptime * 50.f);
        }
    }
}

static int needs_exposure_adjustment(const Image *I, float curr_x, float curr_y) {
    static float last_avg_intensity = -1.f;
    static float last_centroid_x = -1.f, last_centroid_y = -1.f;
    float avg = I->avg_intensity;
    float dx = fabsf(curr_x - last_centroid_x);
    float dy = fabsf(curr_y - last_centroid_y);
    //LOGDBG("avg: %g, curr_x: %g, curr_y: %g", avg, curr_x, curr_y);
    // don't change brightness if average value in 5..50
    if(avg > 5.f && avg < 50.f){
        last_avg_intensity = avg;
        return FALSE;
    }
    // Adjust if intensity changes >10% or centroid moves >20px or no x/y centroids
    if(curr_x < 0.f || curr_y < 0.f){ // star wasn't detected
        int ret = FALSE;
        if(fabsf(avg - last_avg_intensity) > 0.1f * last_avg_intensity ||
            avg < 0.001f || avg > 200.f){
            LOGDBG("Need adj: image too bad");
            ret = TRUE;
        }
        last_avg_intensity = avg;
        return ret;
    }
    if(fabsf(avg - last_avg_intensity) > 0.1f * last_avg_intensity ||
        dx > 20.f || dy > 20.f){
        DBG("avg_cur=%g, avg_last=%g, dx=%g, dy=%g", avg, last_avg_intensity, dx, dy);
        LOGDBG("avg_cur=%g, avg_last=%g, dx=%g, dy=%g", avg, last_avg_intensity, dx, dy);
        last_avg_intensity = avg;
        last_centroid_x = curr_x;
        last_centroid_y = curr_y;
        LOGDBG("Need adj: changed conditions");
        return TRUE;
    }
    return FALSE;
}

static pthread_mutex_t capt_mutex = PTHREAD_MUTEX_INITIALIZER;
static int iCaptured = -1; // index of last captured image
static Image* Icap[2] = {0}; // buffer for last captured images
// main capture thread fills empty buffers and wait until processed thread free's one of them
static void *procthread(void* v){
    typedef void (*procfn_t)(Image*);
    void (*process)(Image*) = (procfn_t)v;
#ifdef EBUG
    double t0 = sl_dtime();
#endif
    while(!stopwork){
        pthread_mutex_lock(&capt_mutex);
        //DBG("===== iCaptured=%d", iCaptured);
        if(Icap[iCaptured]){
            DBG("===== got image iCaptured=#%d @ %g", iCaptured, sl_dtime() - t0);
            Image *oIma = Icap[iCaptured]; // take image here and free buffer
            Icap[iCaptured] = NULL;
            pthread_mutex_unlock(&capt_mutex);
            if(process){
                if(theconf.medfilt){
                    Image *X = get_median(oIma, theconf.medseed);
                    if(X){
                        Image_free(&oIma);
                        oIma = X;
                    }
                }
                process(oIma);
                lastimdata.avg = oIma->avg_intensity;
                lastimdata.bkg = oIma->background;
                lastimdata.minval = oIma->minval;
                lastimdata.maxval = oIma->maxval;
                lastimdata.stat = oIma->stat;
                getcenter(&lastimdata.xc, &lastimdata.yc);
            }
            if(theconf.expmethod == EXPAUTO){
                if(needs_exposure_adjustment(oIma, lastimdata.xc, lastimdata.yc)) recalcexp(oIma);
            }else{
                if(fabs(theconf.exptime - exptime) > FLT_EPSILON)
                    exptime = theconf.exptime;
                if(fabs(theconf.gain - gain) > FLT_EPSILON)
                    gain = theconf.gain;
                if(fabs(theconf.brightness - brightness) > FLT_EPSILON)
                    brightness = theconf.brightness;
            }
            //Icap[iCaptured] = NULL;
            //pthread_mutex_unlock(&capt_mutex);
            Image_free(&oIma);
            DBG("===== cleared image data @ %g", sl_dtime() - t0);
        }else{
            //DBG("===== NO image data");
            pthread_mutex_unlock(&capt_mutex);
        }
        //DBG("===== NEXT!");
        usleep(1000);
    }
    return NULL;
}

int camcapture(void (*process)(Image*)){
    FNAME();
    static float oldexptime = 0.;
    static float oldgain = -1.;
    static float oldbrightness = 0.;
    Image *oIma = NULL;
    pthread_t proc_thread;
    if(pthread_create(&proc_thread, NULL, procthread, (void*)process)){
        LOGERR("pthread_create() for image processing failed");
        ERR("pthread_create()");
    }
    exptime = theconf.exptime;
    while(1){
#ifdef EBUG
        double t0 = sl_dtime();
#endif
        if(stopwork){
            DBG("STOP");
            break;
        }
        if(!theCam){ // wait until camera be powered on
            LOGERR("camcapture(): camera not initialized");
            ERRX("Not initialized");
        }
        DBG("T=%g", sl_dtime() - t0);
        if(!connected){
            DBG("Disconnected, try to connect");
            connected = theCam->connect();
            sleep(1);
            changeformat();
            continue;
        }
        DBG("T=%g", sl_dtime() - t0);
        if(fabsf(oldbrightness - brightness) > FLT_EPSILON){ // new brightness
            DBG("Change brightness to %g", brightness);
            if(theCam->setbrightness(brightness)){
                oldbrightness = brightness;
            }else{
                WARNX("Can't change brightness to %g", brightness);
            }
        }
        DBG("T=%g", sl_dtime() - t0);
        if(exptime > theconf.maxexp) exptime = theconf.maxexp;
        else if(exptime < theconf.minexp) exptime = theconf.minexp;
        if(fabsf(oldexptime - exptime) > FLT_EPSILON){ // new exsposition value
            DBG("Change exptime to %.2fms\n", exptime);
            if(theCam->setexp(exptime)){
                oldexptime = exptime;
                theconf.exptime = exptime;
            }else{
                WARNX("Can't change exposition time to %gms", exptime);
            }
        }
        DBG("T=%g", sl_dtime() - t0);
        if(gain > gainmax) gain = gainmax;
        if(fabsf(oldgain - gain) > FLT_EPSILON){ // change gain
            DBG("Change gain to %g\n", gain);
            LOGDBG("Change gain to %g", gain);
            if(theCam->setgain(gain)){
                oldgain = gain;
                theconf.gain = gain;
            }else{
                WARNX("Can't change gain to %g", gain);
                LOGWARN("Can't change gain to %g", gain);
                gain = oldgain;
            }
        }
        DBG("T=%g", sl_dtime() - t0);
        // change format
        if(abs(curformat.h - theconf.height) || abs(curformat.w - theconf.width) || abs(curformat.xoff - theconf.xoff) || abs(curformat.yoff - theconf.yoff)){
            changeformat();
        }
        DBG("Try to grab (T=%g)", sl_dtime() - t0);
        static int errctr = 0;
        if(!(oIma = theCam->capture())){
            WARNX("---- Can't grab image");
            if(++errctr > MAX_CAPT_ERRORS){
                LOGERR("camcapture(): too much capture errors; reconnect camera");
                camdisconnect();
                errctr = 0;
            }
            continue;
        }else errctr = 0;
        DBG("---- Grabbed @ %g", sl_dtime() - t0);
        pthread_mutex_lock(&capt_mutex);
        if(iCaptured < 0) iCaptured = 0;
        else iCaptured = !iCaptured;
        if(Icap[iCaptured]){ // try current value if previous is still busy
            DBG("---- iCap=%d busy!", iCaptured);
            iCaptured = !iCaptured;
        }
        if(!Icap[iCaptured]){ // previous buffer is free
            DBG("---- take iCaptured=%d", iCaptured);
            Icap[iCaptured] = oIma;
            oIma = NULL;
        }else{ // clear our image - there's no empty buffers
            DBG("---- no free buffers for iCap=%d", iCaptured);
            Image_free(&oIma);
        }
        pthread_mutex_unlock(&capt_mutex);
        DBG("unlocked, T=%g", sl_dtime() - t0);
    }
    pthread_cancel(proc_thread);
    if(oIma) Image_free(&oIma);
    for(int i = 0; i < 2; ++i){
        if(Icap[i]) Image_free(&Icap[i]);
    }
    camdisconnect();
    DBG("CAMCAPTURE: out");
    pthread_join(proc_thread, NULL);
    return 1;
}

/**
 * @brief camstatus - return JSON with image status
 * @param messageid - value of "messageid"
 * @param buf       - buffer for string
 * @param buflen    - length of `buf`
 * @return buf
 */
char *camstatus(const char *messageid, char *buf, int buflen){
    if(!buf || buflen < 2) return NULL;
    if(!messageid) messageid = "unknown";
    static char *impath = NULL;
    if(!impath){
        if(!(impath = realpath(GP->outputjpg, impath))){
            WARN("realpath() (%s)", impath);
            impath = strdup(GP->outputjpg);
        }
        DBG("path: %s", impath);
    }
    float xc, yc;
    getcenter(&xc, &yc);
    snprintf(buf, buflen, "{ \"%s\": \"%s\", \"camstatus\": \"%sconnected\", \"impath\": \"%s\", \"imctr\": %llu, "
         "\"fps\": %.3f, \"expmethod\": \"%s\", \"exptime\": %g, \"gain\": %g, \"maxgain\": %g, \"brightness\": %g, "
         "\"xcenter\": %.1f, \"ycenter\": %.1f , \"minval\": %d, \"maxval\": %d, \"background\": %d, "
         "\"average\": %.1f, \"xc\": %.1f, \"yc\": %.1f, \"xsigma\": %.1f, \"ysigma\": %.1f, \"area\": %d }\n",
         MESSAGEID, messageid, connected ? "" : "dis", impath, ImNumber, getFramesPerS(),
         (theconf.expmethod == EXPAUTO) ? "auto" : "manual", exptime, gain, gainmax, brightness,
         xc, yc, lastimdata.minval, lastimdata.maxval, lastimdata.bkg, lastimdata.avg,
         lastimdata.stat.xc, lastimdata.stat.yc, lastimdata.stat.xsigma, lastimdata.stat.ysigma,
         lastimdata.stat.area);
    return buf;
}
