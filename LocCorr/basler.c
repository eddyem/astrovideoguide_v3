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

#include <pylonc/PylonC.h>

#include "basler.h"
#include "debug.h"
#include "imagefile.h"

static PYLON_DEVICE_HANDLE hDev;
static int isopened = FALSE;
static size_t payloadsize = 0; // size of imgBuf
static unsigned char *imgBuf = NULL;
static float expostime = 0.; // current exposition time
static PYLON_DEVICECALLBACK_HANDLE hCb;

typedef struct{
    int64_t min;
    int64_t max;
    int64_t incr;
    int64_t val;
} int64_values;

typedef struct{
    double min;
    double max;
    double val;
} float_values;

static char* describeError(GENAPIC_RESULT reserr){
    static char buf[1024];
    char* errMsg, *bptr = buf;
    size_t length, l = 1023;
    GenApiGetLastErrorMessage(NULL, &length);
    errMsg = MALLOC(char, length);
    GenApiGetLastErrorMessage(errMsg, &length);
    size_t ll = snprintf(bptr, l, "%s (%d); ", errMsg, (int)reserr);
    if(ll > 0){l -= ll; bptr += ll;}
    FREE(errMsg);
    GenApiGetLastErrorDetail(NULL, &length);
    errMsg = MALLOC(char, length);
    GenApiGetLastErrorDetail(errMsg, &length);
    snprintf(bptr, l, "%s", errMsg);
    FREE(errMsg);
    return buf;
}

#define PYLONFN(fn, ...) do{register GENAPIC_RESULT reserr; if(GENAPI_E_OK != (reserr=fn(__VA_ARGS__))){ \
    WARNX(#fn "(): %s", describeError(reserr)); return FALSE;}}while(0)

static void disconnect(){
    FNAME();
    if(!isopened) return;
    FREE(imgBuf);
    PylonDeviceDeregisterRemovalCallback(hDev, hCb);
    PylonDeviceClose(hDev);
    PylonDestroyDevice(hDev);
    PylonTerminate();
    isopened = FALSE;
}

/**
 * @brief chkNode - get node & check it for read/write
 * @param phNode (io) - pointer to node
 * @param nodeType    - type of node
 * @param wr          - ==TRUE if need to check for writeable
 * @return TRUE if node found & checked
 */
static int chkNode(NODE_HANDLE *phNode, const char *featureName, EGenApiNodeType nodeType, int wr){
    if(!isopened || !phNode || !featureName) return FALSE;
    NODEMAP_HANDLE hNodeMap;
    EGenApiNodeType nt;
    _Bool bv;
    PYLONFN(PylonDeviceGetNodeMap, hDev, &hNodeMap);
    PYLONFN(GenApiNodeMapGetNode, hNodeMap, featureName, phNode);
    if(*phNode == GENAPIC_INVALID_HANDLE) return FALSE;
    PYLONFN(GenApiNodeGetType, *phNode, &nt);
    if(nodeType != nt) return FALSE;
    PYLONFN(GenApiNodeIsReadable, *phNode, &bv);
    if(!bv) return FALSE; // not readable
    if(wr){
        PYLONFN(GenApiNodeIsWritable, *phNode, &bv);
        if(!bv) return FALSE; // not writeable
    }
    return TRUE;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// getters of different types of data
static int getBoolean(const char *featureName, _Bool *val){
    if(!isopened || !featureName) return FALSE;
    NODE_HANDLE hNode;
    if(!chkNode(&hNode, featureName, BooleanNode, FALSE)) return FALSE;
    if(!val) return TRUE;
    PYLONFN(GenApiBooleanGetValue, hNode, val);
    //DBG("Get boolean: %s = %s", featureName, val ? "true" : "false");
    return TRUE;
}
static int getInt(char *featureName, int64_values *val){
    if(!isopened || !featureName) return FALSE;
    NODE_HANDLE hNode;
    if(!chkNode(&hNode, featureName, IntegerNode, FALSE)) return FALSE;
    if(!val) return TRUE;
    PYLONFN(GenApiIntegerGetMin, hNode, &val->min);
    PYLONFN(GenApiIntegerGetMax, hNode, &val->max);
    PYLONFN(GenApiIntegerGetInc, hNode, &val->incr);
    PYLONFN(GenApiIntegerGetValue, hNode, &val->val);
    //DBG("Get integer %s = %ld: min = %ld, max = %ld, incr = %ld", featureName, val->val, val->min, val->max, val->incr);
    return TRUE;
}
static int getFloat(char *featureName, float_values *val){
    if(!isopened || !featureName) return FALSE;
    NODE_HANDLE hNode;
    if(!chkNode(&hNode, featureName, FloatNode, FALSE)) return FALSE;
    if(!val) return TRUE;
    PYLONFN(GenApiFloatGetMin, hNode, &val->min);
    PYLONFN(GenApiFloatGetMax, hNode, &val->max);
    PYLONFN(GenApiFloatGetValue, hNode, &val->val);
    //DBG("Get float %s = %g: min = %g, max = %g", featureName, val->val, val->min, val->max);
    return TRUE;
}

// setters of different types of data
static int setBoolean(char *featureName, _Bool val){
    if(!isopened || !featureName) return FALSE;
    NODE_HANDLE hNode;
    if(!chkNode(&hNode, featureName, BooleanNode, TRUE)) return FALSE;
    PYLONFN(GenApiBooleanSetValue, hNode, val);
    return TRUE;
}
static int setInt(char *featureName, int64_t val){
    if(!isopened || !featureName) return FALSE;
    NODE_HANDLE hNode;
    if(!chkNode(&hNode, featureName, IntegerNode, TRUE)) return FALSE;
    PYLONFN(GenApiIntegerSetValue, hNode, val);
    return TRUE;
}
static int setFloat(char *featureName, float val){
    if(!isopened || !featureName) return FALSE;
    NODE_HANDLE hNode;
    if(!chkNode(&hNode, featureName, FloatNode, TRUE)) return FALSE;
    PYLONFN(GenApiFloatSetValue, hNode, val);
    return TRUE;
}
#pragma GCC diagnostic pop

static void disableauto(){
    if(!isopened) return;
    const char *features[] = {"EnumEntry_TriggerSelector_AcquisitionStart",
                              "EnumEntry_TriggerSelector_FrameBurstStart",
                              "EnumEntry_TriggerSelector_FrameStart"};
    const char *triggers[] = {"AcquisitionStart", "FrameBurstStart", "FrameStart"};
    for(int i = 0; i < 3; ++i){
        if(PylonDeviceFeatureIsAvailable(hDev, features[i])){
            PylonDeviceFeatureFromString(hDev, "TriggerSelector", triggers[i]);
            PylonDeviceFeatureFromString(hDev, "TriggerMode", "Off");
        }
    }
    PylonDeviceFeatureFromString(hDev, "GainAuto", "Off");
    PylonDeviceFeatureFromString(hDev, "ExposureAuto", "Off");
    PylonDeviceFeatureFromString(hDev, "ExposureMode", "Timed");
    PylonDeviceFeatureFromString(hDev, "SequencerMode", "Off");
}

static void GENAPIC_CC removalCallbackFunction(_U_ PYLON_DEVICE_HANDLE hDevice){
    disconnect();
}

static int connect(){
    FNAME();
    size_t numDevices;
    disconnect();
    PylonInitialize();
    PYLONFN(PylonEnumerateDevices, &numDevices);
    if(!numDevices){
        WARNX("No cameras found");
        return FALSE;
    }
    PYLONFN(PylonCreateDeviceByIndex, 0, &hDev);
    isopened = TRUE;
    PYLONFN(PylonDeviceOpen, hDev, PYLONC_ACCESS_MODE_CONTROL | PYLONC_ACCESS_MODE_STREAM | PYLONC_ACCESS_MODE_EXCLUSIVE);
    disableauto();
    PYLONFN(PylonDeviceFeatureFromString, hDev, "PixelFormat", "Mono8");
    PYLONFN(PylonDeviceFeatureFromString, hDev, "CameraOperationMode", "LongExposure");
    PYLONFN(PylonDeviceFeatureFromString, hDev, "UserSetSelector", "HighGain"); // set high gain selector
    PYLONFN(PylonDeviceFeatureFromString, hDev, "AcquisitionMode", "SingleFrame");
    PYLONFN(PylonDeviceExecuteCommandFeature, hDev, "UserSetLoad"); // load high gain mode
    PYLON_STREAMGRABBER_HANDLE hGrabber;
    PYLONFN(PylonDeviceGetStreamGrabber, hDev, 0, &hGrabber);
    PYLONFN(PylonStreamGrabberOpen, hGrabber);
//    PYLON_WAITOBJECT_HANDLE hWaitStream;
//    PYLONFN(PylonStreamGrabberGetWaitObject, hStreamGrabber, &hWaitStream);
    PYLONFN(PylonStreamGrabberGetPayloadSize, hDev, hGrabber, &payloadsize);
    PylonStreamGrabberClose(hGrabber);
    PylonDeviceRegisterRemovalCallback(hDev, removalCallbackFunction, &hCb);
    imgBuf = MALLOC(unsigned char, payloadsize);
    //PYLONFN(PylonDeviceExecuteCommandFeature, hDev, "AcquisitionStart");
    return TRUE;
}

static Image *capture(){
    FNAME();
    static int toohot = FALSE;
    if(!isopened || !imgBuf) return NULL;
    float_values f;
    if(!getFloat("DeviceTemperature", &f)) WARNX("Can't get temperature");
    else{
        LOGDBG("Basler temperature: %.1f", f.val);
        DBG("Temperature: %.1f", f.val);
        if(f.val > 80.){
            WARNX("Device too hot");
            if(!toohot){
                LOGWARN("Device too hot");
                toohot = TRUE;
            }
        }else if(toohot && f.val < 75.){
            LOGDBG("Device temperature is normal");
            toohot = FALSE;
        }
    }
    PylonGrabResult_t grabResult;
    _Bool bufferReady;
    GENAPIC_RESULT res = PylonDeviceGrabSingleFrame(hDev, 0, imgBuf, payloadsize,
                                                    &grabResult, &bufferReady, 500 + (uint32_t)expostime);
    if(res != GENAPI_E_OK || !bufferReady){
        WARNX("res != GENAPI_E_OK || !bufferReady");
        return NULL;
    }
    if(grabResult.Status != Grabbed){
        WARNX("grabResult.Status != Grabbed");
        return NULL;
    }
    Image *oIma = u8toImage(imgBuf, grabResult.SizeX, grabResult.SizeY, grabResult.SizeX + grabResult.PaddingX);
    return oIma;
}

// Basler have no "brightness" parameter
static int setbrightness(_U_ float b){
    FNAME();
    return TRUE;
}

static int setexp(float e){
    FNAME();
    if(!isopened) return FALSE;
    e *= 1000.;
    if(!setFloat("ExposureTime", e)){
       LOGWARN("Can't set expose time %g", e);
       WARNX("Can't set expose time %g", e);
       return FALSE;
    }
    float_values f;
    if(!getFloat("ExposureTime", &f)) return FALSE;
    expostime = (float)f.val / 1000.;
    return TRUE;
}

static int setgain(_U_ float e){
    FNAME();
    if(!isopened) return FALSE;
    if(!setFloat("Gain", e)){
       LOGWARN("Can't set gain %g", e);
       WARNX("Can't set gain %g", e);
       return FALSE;
    }
    return TRUE;
}

static int changeformat(frameformat *fmt){
    FNAME();
    if(!isopened) return FALSE;
    setInt("Width", fmt->w);
    setInt("Height", fmt->h);
    setInt("OffsetX", fmt->xoff);
    setInt("OffsetY", fmt->yoff);
    int64_values i;
    if(getInt("Width", &i)) fmt->w = i.val;
    if(getInt("Height", &i)) fmt->h = i.val;
    if(getInt("OffsetX", &i)) fmt->xoff = i.val;
    if(getInt("OffsetY", &i)) fmt->yoff = i.val;
    return TRUE;
}

static int geometrylimits(frameformat *max, frameformat *step){
    FNAME();
    if(!isopened || !max || !step) return FALSE;
    int64_values i;
    if(!getInt("Width", &i)) return FALSE;
    max->w = i.max; step->w = i.incr;
    if(!getInt("Height", &i)) return FALSE;
    max->h = i.max; step->h = i.incr;
    if(!getInt("OffsetX", &i)) return FALSE;
    max->xoff = i.max; step->xoff = i.incr;
    if(!getInt("OffsetY", &i)) return FALSE;
    max->yoff = i.max; step->yoff = i.incr;
    return TRUE;
}

static float gainmax(){
    FNAME();
    float_values v;
    if(!getFloat("Gain", &v)) return 0.;
    return (float)v.max;
}

// exported object
camera Basler = {
    .disconnect = disconnect,
    .connect = connect,
    .capture = capture,
    .setbrightness = setbrightness,
    .setexp = setexp,
    .setgain = setgain,
    .setgeometry = changeformat,
    .getgeomlimits = geometrylimits,
    .getmaxgain = gainmax,
};

