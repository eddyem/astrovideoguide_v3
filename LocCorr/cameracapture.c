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
#include <stdio.h>
#include <string.h>

#include "cmdlnopts.h"
#include "config.h"
#include "debug.h"
#include "fits.h"
#include "grasshopper.h"
#include "imagefile.h"
#include "improc.h"
#include "median.h"

// pointer to selected camera
static camera *theCam = NULL;

static float gain = 0., gainmax = 0.;
static float exptime = 100.;
static float brightness = 0.;
static int connected = FALSE;

static frameformat curformat;
static frameformat maxformat;
static frameformat stepformat;

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
    while(newexp*1.25 > theconf.minexp){ // increase gain first
        if(gain < gainmax - 0.9999){
            gain += 1.;
            newexp /= 1.25;
        }else break;
    }
    while(newexp < theconf.minexp){
        if(1.25*newexp < theconf.maxexp && gain > 0.9999){
            gain -= 1.;
            newexp *= 1.25;
        }else break;
    }
    if(newexp < theconf.minexp) newexp = theconf.minexp;
    else if(newexp > theconf.maxexp) newexp = theconf.maxexp;
    exptime = newexp;
    DBG("New values: exp=%g, gain=%g", exptime, gain);
}

//convertedImage.pData, convertedImage.cols, convertedImage.rows, convertedImage.stride
static void recalcexp(Image *I){
    // check if user changed exposition values
    if(exptime < theconf.minexp){
        exptime = theconf.minexp;
        return;
    }
    else if(exptime > theconf.maxexp){
        exptime = theconf.maxexp;
        return;
    }
    size_t *histogram = get_histogram(I);
    if(!histogram){
        WARNX("Can't calculate histogram");
        return;
    }
    int idx100;
    size_t sum100 = 0;
    for(idx100 = 255; idx100 >= 0; --idx100){
        sum100 += histogram[idx100];
        if(sum100 > 100) break;
    }
    FREE(histogram);
    DBG("Sum100=%zd, idx100=%d", sum100, idx100);
    if(idx100 > 230 && idx100 < 253) return; // good values
    if(idx100 > 253){ // exposure too long
        calcexpgain(0.7*exptime);
    }else{ // exposure too short
        if(idx100 > 5)
            calcexpgain(exptime * 230. / (float)idx100);
        else
            calcexpgain(exptime * 50.);
    }
}

int camcapture(void (*process)(Image*)){
    FNAME();
    static float oldexptime = 0.;
    static float oldgain = -1.;
    static float oldbrightness = -1.;
    Image *oIma = NULL;
    while(1){
        if(stopwork){
            DBG("STOP");
            break;
        }
        if(!theCam){ // wait until camera be powered on
            LOGERR("camcapture(): camera not initialized");
            ERRX("Not initialized");
        }
        if(!connected){
            DBG("Disconnected");
            connected = theCam->connect();
            sleep(1);
            changeformat();
            continue;
        }
        if(fabsf(oldbrightness - brightness) > FLT_EPSILON){ // new brightness
            DBG("Change brightness to %g", brightness);
            if(theCam->setbrightness(brightness)){
                oldbrightness = brightness;
            }else{
                WARNX("Can't change brightness to %g", brightness);
            }
        }
        if(exptime > theconf.maxexp) exptime = theconf.maxexp;
        else if(exptime < theconf.minexp) exptime = theconf.minexp;
        if(fabsf(oldexptime - exptime) > FLT_EPSILON){ // new exsposition value
            DBG("Change exptime to %.2fms\n", exptime);
            if(theCam->setexp(exptime)){
                oldexptime = exptime;
            }else{
                WARNX("Can't change exposition time to %gms", exptime);
            }
        }
        if(gain > gainmax) gain = gainmax;
        if(fabsf(oldgain - gain) > FLT_EPSILON){ // change gain
            DBG("Change gain to %g\n", gain);
            if(theCam->setgain(gain)){
                oldgain = gain;
            }else{
                WARNX("Can't change gain to %g", gain);
            }
        }
        // change format
        if(abs(curformat.h - theconf.height) || abs(curformat.w - theconf.width) || abs(curformat.xoff - theconf.xoff) || abs(curformat.yoff - theconf.yoff)){
            changeformat();
        }
        if(!(oIma = theCam->capture())){
            WARNX("Can't grab image");
            camdisconnect();
            continue;
        }
        if(theconf.expmethod == EXPAUTO) recalcexp(oIma);
        else{
            if(fabs(theconf.fixedexp - exptime) > FLT_EPSILON)
                exptime = theconf.fixedexp;
            if(fabs(theconf.gain - gain) > FLT_EPSILON)
                gain = theconf.gain;
            if(fabs(theconf.brightness - brightness) > FLT_EPSILON)
                brightness = theconf.brightness;
        }
        if(process){
            if(theconf.medfilt){
                Image *X = get_median(oIma, theconf.medseed);
                if(X){
                    FREE(oIma->data);
                    FREE(oIma);
                    oIma = X;
                }
            }
            process(oIma);
        }
        FREE(oIma->data);
        FREE(oIma);
    }
    if(oIma){
        FREE(oIma->data);
        FREE(oIma);
    }
    camdisconnect();
    DBG("CAMCAPTURE: out");
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
             "\"fps\": %.3f, \"expmethod\": \"%s\", \"exposition\": %g, \"gain\": %g, \"brightness\": %g, "
             "\"xcenter\": %.1f, \"ycenter\": %.1f }\n",
             MESSAGEID, messageid, connected ? "" : "dis", impath, ImNumber, getFramesPerS(),
             (theconf.expmethod == EXPAUTO) ? "auto" : "manual", exptime, gain, brightness,
             xc, yc);
    return buf;
}
