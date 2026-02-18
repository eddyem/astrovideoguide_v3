/*
 * This file is part of the loccorr project.
 * Copyright 2026 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <float.h>
#include <pthread.h>
#include <string.h>
#include <toupcam.h>
#include <usefull_macros.h>

#include "Toupcam.h"

// flags for image processing
typedef enum{
    IM_SLEEP,
    IM_STARTED,
    IM_READY,
    IM_ERROR
} imstate_t;

// devices
static ToupcamDeviceV2 g_dev[TOUPCAM_MAX] = {0};
static struct{
    ToupcamDeviceV2* dev;       // device
    HToupcam hcam;              // hcam for all functions
    unsigned long long flags;   // flags (read on connect)
    pthread_mutex_t mutex;      // lock mutex for `data` writing/reading
    void* data;                 // image data
    size_t imsz;                // size of current image in bytes
    imstate_t state;            // current state
    uint64_t imseqno;           // number of image from connection
    uint64_t lastcapno;         // last captured image number
} toupcam = {0};

// array - max ROI; geometry - current ROI
static frameformat array = {.xoff=0, .yoff = 0, .w = 800, .h = 600}, geometry = {0};

// exptime (in seconds!!!) and starting of exposition
static double exptimeS = 0.1, starttime = 0.;

#define TCHECK()    do{if(!toupcam.hcam) return FALSE;}while(0)

// return constant string with error code description
static const char *errcode(int ecode){
    switch(ecode){
    case 0: return "S_OK";
    case 1: return "S_FALSE";
    case 0x8000ffff: return "E_UNEXPECTED";
    case 0x80004001: return "E_NOTIMPL";
    case 0x80004002: return "E_NOINTERFACE";
    case 0x80070005: return "E_ACCESSDENIED";
    case 0x8007000e: return "E_OUTOFMEMORY";
    case 0x80070057: return "E_INVALIDARG";
    case 0x80004003: return "E_POINTER";
    case 0x80004005: return "E_FAIL";
    case 0x8001010e: return "E_WRONG_THREAD";
    case 0x8007001f: return "E_GEN_FAILURE";
    case 0x800700aa: return "E_BUSY";
    case 0x8000000a: return "E_PENDING";
    case 0x8001011f: return "E_TIMEOUT";
    case 0x80072743: return "E_UNREACH";
    default: return "Unknown error";
    }
}

static void camcancel(){
    FNAME();
    if(!toupcam.hcam) return;
    int e = Toupcam_Trigger(toupcam.hcam, 0); // stop triggering
    if(e < 0) WARNX("Can't trigger 0: %s", errcode(e));
    e = Toupcam_Stop(toupcam.hcam);
    if(e < 0) WARNX("Can't stop: %s", errcode(e));
    toupcam.state = IM_SLEEP;
}

static void Tdisconnect(){
    FNAME();
    camcancel();
    if(toupcam.hcam){
        DBG("Close camera");
        Toupcam_Close(toupcam.hcam);
        toupcam.hcam = NULL;
    }
    if(toupcam.data){
        DBG("Free image data");
        free(toupcam.data);
        toupcam.data = NULL;
    }
}

static void EventCallback(unsigned nEvent, void _U_ *pCallbackCtx){
    FNAME();
    DBG("CALLBACK with evt %d", nEvent);
    if(!toupcam.hcam || !toupcam.data){ DBG("NO data!"); return; }
    if(nEvent != TOUPCAM_EVENT_IMAGE){ DBG("Not image event"); return; }
    ToupcamFrameInfoV4 info = {0};
    pthread_mutex_lock(&toupcam.mutex);
    if(Toupcam_PullImageV4(toupcam.hcam, toupcam.data, 0, 0, 0, &info) < 0){
        DBG("Error pulling image");
        toupcam.state = IM_ERROR;
    }else{
        ++toupcam.imseqno;
        DBG("Image %lu (%dx%d) ready!", toupcam.imseqno, info.v3.width, info.v3.height);
        toupcam.state = IM_READY;
        toupcam.imsz = info.v3.height * info.v3.width;
        geometry.h = info.v3.height;
        geometry.w = info.v3.width;
    }
    pthread_mutex_unlock(&toupcam.mutex);
}

static int startexp(){
    FNAME();
    TCHECK();
    if(toupcam.state == IM_SLEEP){
        DBG("Sleeping -> start pull mode");
        if(Toupcam_StartPullModeWithCallback(toupcam.hcam, EventCallback, NULL) < 0){
            WARNX("Can't run PullMode with Callback!");
            return FALSE;
        }
    }
    // Ask to trigger for several images (maximal speed available)
    DBG("Trigger images");
    int e = Toupcam_Trigger(toupcam.hcam, 100);
    if(e < 0){
        DBG("Can't ask for images stream: %s; try 1", errcode(e));
        e = Toupcam_Trigger(toupcam.hcam, 1);
        if(e < 0){
            WARNX("Can't ask for next image: %s", errcode(e));
            return FALSE;
        }
    }
    toupcam.state = IM_STARTED;
    starttime = sl_dtime();
    return TRUE;
}

static int Texp(float t){ // t - in milliseconds!!!
    FNAME();
    TCHECK();
    if(t < FLT_EPSILON) return FALSE;
    unsigned int microseconds = (unsigned)(t * 1e3f);
    DBG("Set exptime to %dus", microseconds);
    camcancel();
    int e = Toupcam_put_ExpoTime(toupcam.hcam, microseconds);
    if(e < 0){
        WARNX("Can't set exp: %s", errcode(e));
        //startexp();
        return FALSE;
    }
    DBG("OK");
    if(Toupcam_get_ExpoTime(toupcam.hcam, &microseconds) < 0) exptimeS = (double) t / 1e3;
    else exptimeS = (float)microseconds / 1e6f;
    DBG("Real exptime: %.4fs", exptimeS);
    //startexp();
    return TRUE;
}

static int Tconnect(){
    FNAME();
    Tdisconnect();
    unsigned N = Toupcam_EnumV2(g_dev);
    if(0 == N){
        DBG("Found 0 toupcams");
        return FALSE;
    }
    toupcam.dev = &g_dev[0];
    toupcam.hcam = Toupcam_Open(g_dev[0].id);
    if(!toupcam.hcam){
        WARN("Can't open toupcam camera");
        return FALSE;
    }
    DBG("Opened %s", toupcam.dev->displayname);
    DBG("Clear ROI");
    Toupcam_put_Roi(toupcam.hcam, 0, 0, 0, 0); // clear ROI
    // now fill camera geometry
    unsigned int xoff, yoff, h, w;
    DBG("Get ROI");
    Toupcam_get_Roi(toupcam.hcam, &xoff, &yoff, &w, &h);
    DBG("off (x/y): %d/%d; wxh: %dx%d", xoff, yoff, w, h);
    geometry.xoff = xoff; geometry.yoff = yoff; geometry.w = w; geometry.h = h;
    toupcam.flags = Toupcam_query_Model(toupcam.hcam)->flag;
    DBG("flags: 0x%llx", toupcam.flags);
    DBG("Allocate data (%d bytes: 2*%d*%d)", 2 * array.w * array.h, array.w, array.h);
    toupcam.data = calloc(array.w * array.h, 1);
#define OPT(opt, val, comment)  do{DBG(comment); if(Toupcam_put_Option(toupcam.hcam, opt, val) < 0){ DBG("Can't put this option"); }}while(0)
    // 12 frames/sec
    OPT(TOUPCAM_OPTION_TRIGGER, 1, "Software/simulated trigger mode");
    OPT(TOUPCAM_OPTION_RAW, 1, "Put to RAW mode");
    OPT(TOUPCAM_OPTION_BINNING, 1, "Set binning to 1x1");
#undef OPT
    toupcam.state = IM_SLEEP;
    toupcam.imseqno = toupcam.lastcapno = 0;
    // 8bit
    if(Toupcam_put_Option(toupcam.hcam, TOUPCAM_OPTION_BITDEPTH, 0) < 0) WARNX("Cant set bitdepth");
    if(Toupcam_put_Option(toupcam.hcam, TOUPCAM_OPTION_PIXEL_FORMAT, TOUPCAM_PIXELFORMAT_RAW8) < 0){
        WARNX("Cannot init 8bit mode!");
        Tdisconnect();
        return FALSE;
    }
    pthread_mutex_init(&toupcam.mutex, NULL);
    if(!Texp(0.1)){ WARNX("Can't set default exptime"); }
    return TRUE;
}

static Image *Tcapture(){
    FNAME();
    if(!toupcam.hcam || !toupcam.data) return NULL;
    if(!startexp()){
        WARNX("Can't start exposition");
        return NULL;
    }
    DBG("here, exptime=%gs, dstart=%g", exptimeS, (sl_dtime() - starttime));
    double tremain = 0.;
    while(toupcam.state != IM_READY){
        tremain = exptimeS - (sl_dtime() - starttime);
        if(tremain < -2.0){
            WARNX("Timeout - failed");
            camcancel();
            return NULL;
        }
        usleep(100);
    }
    if(toupcam.state != IM_READY){
        WARNX("State=%d, not ready", toupcam.state);
        return NULL;
    }
    pthread_mutex_lock(&toupcam.mutex);
    Image *o = u8toImage(toupcam.data, geometry.w, geometry.h, geometry.w);
    toupcam.lastcapno = toupcam.imseqno;
    pthread_mutex_unlock(&toupcam.mutex);
    return o;
}

static int Tbright(float b){
    FNAME();
    TCHECK();
    if(b < -255.f || b > 255.f){
        WARNX("Available brightness: -255..255");
        return FALSE;
    }
    int br = (int) b;
    DBG("Try to set brightness to %d", br);
    camcancel();
    int e = Toupcam_put_Brightness(toupcam.hcam, br);
    //startexp();
    if(e < 0){
        WARNX("Can't set brightness: %s", errcode(e));
        return FALSE;
    }
    DBG("OK");
    return TRUE;
}

static int Tgain(float g){
    FNAME();
    TCHECK();
    unsigned short G = (unsigned short)(100.f * g);
    int ret = FALSE;
    camcancel();
    if(Toupcam_put_ExpoAGain(toupcam.hcam, G) < 0){
        WARNX("Gain out of range: 1..8");
    }else{ DBG("GAIN is %d", G); ret = TRUE;}
    //startexp();
    return ret;
}

static float Tmaxgain(){
    FNAME();
    return 8.f; // toupcam SDK returns wrong value: 16.
}

static int Tgeometry(frameformat *f){
    FNAME();
    TCHECK();
    int ret = FALSE;
    camcancel();
    if(Toupcam_put_Roi(toupcam.hcam, (unsigned) f->xoff, (unsigned) f->yoff, (unsigned) f->w, (unsigned) f->h) >= 0){
        geometry = *f;
        ret = TRUE;
    }
    //startexp();
    return ret;
}

static int Tglimits(frameformat *max, frameformat *step){
    FNAME();
    TCHECK();
    if(max) *max = array;
    if(step) *step = (frameformat){.w = 2, .h = 2, .xoff = 2, .yoff = 2};
    return TRUE;
}

camera Toupcam = {
    .disconnect = Tdisconnect,
    .connect = Tconnect,
    .capture = Tcapture,
    .setbrightness = Tbright,
    .setexp = Texp,
    .setgain = Tgain,
    .getmaxgain = Tmaxgain,
    .setgeometry = Tgeometry,
    .getgeomlimits = Tglimits,
};
