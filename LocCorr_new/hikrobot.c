/*
 * This file is part of the loccorr project.
 * Copyright 2025 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <MvCameraControl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <usefull_macros.h>

#include "hikrobot.h"

static struct{
    float maxgain;
    float mingain;
    float maxbright;
    float minbright;
    float minexp;
    float maxexp;
    int maxbin;
} extrvalues = {0}; // extremal values

static MV_CC_DEVICE_INFO_LIST stDeviceList;
static void *handle = NULL;
static char camname[BUFSIZ] = {0};
static float exptime = 0.;      // exposition time (in seconds)
static uint8_t *pdata = NULL;
static int pdatasz = 0;
static int lastecode = MV_OK;
static frameformat array; // max geometry

static void printErr(){
    const char *errcode = "unknown error";
    switch(lastecode){
        case MV_E_HANDLE:           errcode = "Error or invalid handle ";                                         break;
        case MV_E_SUPPORT:          errcode = "Not supported function ";                                          break;
        case MV_E_BUFOVER:          errcode = "Cache is full ";                                                   break;
        case MV_E_CALLORDER:        errcode = "Function calling order error ";                                    break;
        case MV_E_PARAMETER:        errcode = "Incorrect parameter ";                                             break;
        case MV_E_RESOURCE:         errcode = "Applying resource failed ";                                        break;
        case MV_E_NODATA:           errcode = "No data ";                                                         break;
        case MV_E_PRECONDITION:     errcode = "Precondition error, or running environment changed ";              break;
        case MV_E_VERSION:          errcode = "Version mismatches ";                                              break;
        case MV_E_NOENOUGH_BUF:     errcode = "Insufficient memory ";                                             break;
        case MV_E_ABNORMAL_IMAGE:   errcode = "Abnormal image, maybe incomplete image because of lost packet ";   break;
        case MV_E_UNKNOW:           errcode = "Unknown error ";                                                   break;
        case MV_E_GC_GENERIC:       errcode = "General error ";                                                   break;
        case MV_E_GC_ACCESS:        errcode = "Node accessing condition error ";                                  break;
        case MV_E_ACCESS_DENIED:	errcode = "No permission ";                                                   break;
        case MV_E_BUSY:             errcode = "Device is busy, or network disconnected ";                         break;
        case MV_E_NETER:            errcode = "Network error ";
    }
    WARNX("CMOS error: %s", errcode);
}

#define TRYERR(fn, ...)    do{lastecode = MV_CC_ ## fn(handle __VA_OPT__(,) __VA_ARGS__); if(lastecode != MV_OK) printErr(); }while(0)
#define TRY(fn, ...)  do{lastecode = MV_CC_ ## fn(handle __VA_OPT__(,) __VA_ARGS__); }while(0)
#define ONERR()         if(MV_OK != lastecode)
#define ONOK()          if(MV_OK == lastecode)

static int changeenum(const char *key, uint32_t val){
    if(!handle) return FALSE;
    MVCC_ENUMVALUE e;
    TRY(GetEnumValue, key, &e);
    ONERR(){
        WARNX("Enum '%s' is absent", key);
        return FALSE;
    }
    DBG("Try to change '%s' to %u, cur=%u", key, val, e.nCurValue);
    if(e.nCurValue == val) return TRUE;
    TRYERR(SetEnumValue, key, val);
    ONERR(){
        WARNX("Cant change %s to %d, supported values are:", key, val);
        for(int i = 0; i < (int)e.nSupportedNum; ++i){
            fprintf(stderr, "%s%u", i ? ", " : "", e.nSupportValue[i]);
        }
        fprintf(stderr, "\n");
        return FALSE;
    }
    TRY(GetEnumValue, key, &e);
    ONERR() return FALSE;
    if(e.nCurValue == val) return TRUE;
    WARNX("New ENUM value of '%s' changed to %d, not to %d", key, e.nCurValue, val);
    return FALSE;
}

static int changeint(const char *key, uint32_t val){
    if(!handle) return FALSE;
    MVCC_INTVALUE i;
    TRY(GetIntValue, key, &i);
    ONERR(){
        WARNX("Int '%s' is absent", key);
        return FALSE;
    }
    if(i.nCurValue == val) return TRUE;
    TRYERR(SetIntValue, key, val);
    ONERR(){
        WARNX("Can't change %s to %u; available range is %u..%u", key, val, i.nMin, i.nMax);
        return FALSE;
    }
    TRY(GetIntValue, key, &i);
    ONERR() return FALSE;
    if(i.nCurValue == val) return TRUE;
    WARNX("New INT value of '%s' changed to %d, not to %d", key, i.nCurValue, val);
    return FALSE;
}

static int changefloat(const char *key, float val){
    if(!handle) return FALSE;
    MVCC_FLOATVALUE f;
    TRY(GetFloatValue, key, &f);
    ONERR(){
        WARNX("Float '%s' is absent", key);
        return FALSE;
    }
    if(f.fCurValue == val) return TRUE;
    TRYERR(SetFloatValue, key, val);
    ONERR(){
        WARNX("Cant change %s to %g; available range is %g..%g", key, val, f.fMin, f.fMax);
        return FALSE;
    }
    TRY(GetFloatValue, key, &f);
    ONERR() return FALSE;
    DBG("need: %g, have: %g (min/max: %g/%g)", val, f.fCurValue, f.fMin, f.fMax);
    if(fabs(f.fCurValue - val) < HR_FLOAT_TOLERANCE) return TRUE;
    WARNX("New FLOAT value of '%s' changed to %g, not to %g", key, f.fCurValue, val);
    return FALSE;
}

static int cam_getgain(float *g){
    if(!handle) return FALSE;
    MVCC_FLOATVALUE gain;
    TRYERR(GetFloatValue, "Gain", &gain);
    ONERR() return FALSE;
    if(g) *g = gain.fCurValue;
    extrvalues.maxgain = gain.fMax;
    extrvalues.mingain = gain.fMin;
    DBG("Gain: cur=%g, min=%g, max=%g", gain.fCurValue, gain.fMin, gain.fMax);
    return TRUE;
}

static float maxgain(){
    if(!handle) return 0.;
    return extrvalues.maxgain;
}

static int cam_setgain(float g){
    if(!handle) return FALSE;
    return changefloat("Gain", g);
}

static int cam_getbright(float *b){
    if(!handle) return FALSE;
    DBG("Get brightness");
    MVCC_INTVALUE bright;
    //TRY(GetIntValue, "Brightness", &bright);
    TRY(GetBrightness, &bright);
    ONERR(){
        DBG("There's no such function");
        return FALSE;
    }
    if(b) *b = bright.nCurValue;
    extrvalues.maxgain = bright.nMax;
    extrvalues.mingain = bright.nMin;
    DBG("Brightness: cur=%d, min=%d, max=%d", bright.nCurValue, bright.nMin, bright.nMax);
    return TRUE;
}

static int cam_setbright(float b){
    if(!handle) return FALSE;
    TRY(SetBrightness, (uint32_t)b);
    ONERR() return FALSE;
    return TRUE;
    //return changeint("Brightness", (uint32_t)b);
}

static void cam_closecam(){
    DBG("CAMERA CLOSE");
    if(handle){
        MV_CC_StopGrabbing(handle);
        TRY(CloseDevice);
        ONERR() WARNX("Can't close opened camera");
        TRY(DestroyHandle);
        ONERR() WARNX("Can't destroy camera handle");
        handle = NULL;
    }
    FREE(pdata);
    pdatasz = 0;
}

static void PrintDeviceInfo(MV_CC_DEVICE_INFO* pstMVDevInfo){
    if(!pstMVDevInfo) return;
    if(pstMVDevInfo->nTLayerType == MV_GIGE_DEVICE){
        int nIp1 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0xff000000) >> 24);
        int nIp2 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x00ff0000) >> 16);
        int nIp3 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x0000ff00) >> 8);
        int nIp4 = (pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x000000ff);
        printf("Device Model Name: %s\n", pstMVDevInfo->SpecialInfo.stGigEInfo.chModelName);
        strncpy(camname, (char*)pstMVDevInfo->SpecialInfo.stGigEInfo.chModelName, BUFSIZ-1);
        printf("CurrentIp: %d.%d.%d.%d\n" , nIp1, nIp2, nIp3, nIp4);
        printf("UserDefinedName: %s\n\n" , pstMVDevInfo->SpecialInfo.stGigEInfo.chUserDefinedName);
    }else if (pstMVDevInfo->nTLayerType == MV_USB_DEVICE){
        printf("Device Model Name: %s\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chModelName);
        printf("UserDefinedName: %s\n\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chUserDefinedName);
        strncpy(camname, (char*)pstMVDevInfo->SpecialInfo.stUsb3VInfo.chModelName, BUFSIZ-1);
    }else {
        printf("Not support.\n");
    }
}

static int cam_findCCD(){
    DBG("Try to find HIKROBOT cameras .. ");
    memset(&stDeviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    if(MV_OK != MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList)){
        WARNX("No HIKROBOT cameras found");
        return FALSE;
    }
    if(stDeviceList.nDeviceNum > 0){
        for(uint32_t i = 0; i < stDeviceList.nDeviceNum; ++i){
            DBG("[device %d]:\n", i);
            MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
            if(!pDeviceInfo) continue;
            PrintDeviceInfo(pDeviceInfo);
        }
    }else{
        WARNX("No HIKROBOT cameras found");
        return FALSE;
    }
    return TRUE;
}

static int cam_connect(){
    if(!cam_findCCD()) return FALSE;
    cam_closecam();
    lastecode = MV_CC_CreateHandleWithoutLog(&handle, stDeviceList.pDeviceInfo[0]);
    ONERR(){
        WARNX("Can't create camera handle");
        printErr();
        return FALSE;
    }
    TRYERR(OpenDevice, MV_ACCESS_Exclusive, 0);
    ONERR(){
        WARNX("Can't open camera file");
        return FALSE;
    }
    if(stDeviceList.pDeviceInfo[0]->nTLayerType == MV_GIGE_DEVICE){
        int nPacketSize = MV_CC_GetOptimalPacketSize(handle);
        if(nPacketSize > 0){
            if(!changeint("GevSCPSPacketSize", nPacketSize)){
                WARNX("Can't set optimal packet size");
            }
        } else{
            WARNX("Can't get optimal packet size");
        }
    }
    if(!changeenum("BinningHorizontal", 1)){
        WARNX("Can't clear soft H binning");
        return FALSE;
    }
    if(!changeenum("BinningVertical", 1)){
        WARNX("Can't clear soft V binning");
        return FALSE;
    }
    if(!changeenum("TriggerMode", MV_TRIGGER_MODE_OFF)){
        WARNX("Can't turn off triggered mode");
        return FALSE;
    }
    if(!changeenum("AcquisitionMode", MV_ACQ_MODE_SINGLE)){
        WARNX("Can't set acquisition mode to single");
        return FALSE;
    }
    if(!changeenum("ExposureMode", MV_EXPOSURE_MODE_TIMED)){
        WARNX("Can't change exposure mode to timed");
        return FALSE;
    }
    if(!changeenum("ExposureAuto", MV_EXPOSURE_AUTO_MODE_OFF)){
        WARNX("Can't turn off auto exposure mode");
        return FALSE;
    }
    if(!changeenum("GainAuto", 0)){
        WARNX("Can't turn off auto gain");
        return FALSE;
    }
    if(!changeenum("PixelFormat", PixelType_Gvsp_Mono8) || !changeenum("PixelSize", 8)){
        WARNX("Can't change format to 8 pix");
        return FALSE;
    }
    cam_getgain(NULL); // get extremal gain values
    cam_getbright(NULL); // get extremal brightness values
    MVCC_FLOATVALUE FloatValue;
    // get extremal exptime values
    TRY(GetFloatValue, "ExposureTime", &FloatValue);
    ONOK(){
        extrvalues.maxexp = FloatValue.fMax / 1e6;
        extrvalues.minexp = FloatValue.fMin / 1e6;
        exptime = FloatValue.fCurValue / 1e6;
        printf("Min exp: %g s, max exp: %g s\n", extrvalues.minexp, extrvalues.maxexp);
    }
    MVCC_INTVALUE IntValue;
    array.xoff = array.yoff = 0;
    int *values[2] = {&array.w, &array.h};
    const char *names[2] = {"WidthMax", "HeightMax"};//, "Width", "Height", "OffsetX", "OffsetY"};
    for(int i = 0; i < 2; ++i){
        TRYERR(GetIntValue, names[i], &IntValue);
        ONERR(){
            WARNX("Can't get %s", names[i]); return FALSE;
        }
        *values[i] = IntValue.nCurValue;
        DBG("%s = %d", names[i], *values[i]);
    }
    pdatasz = array.h * array.w;
    DBG("2*w*h = %d", pdatasz);
    if(changeenum("DeviceTemperatureSelector", 0)){
        if(!changefloat("DeviceTemperature", -20.)) WARNX("Can't set camtemp to -20");
    }
/*
#ifdef EBUG
    MVCC_INTVALUE stParam = {0};
    TRY(GetIntValue, "PayloadSize", &stParam);
    ONOK(){DBG("PAYLOAD: %u", stParam.nCurValue);}
#endif
    */
    pdata = MALLOC(uint8_t, pdatasz); // allocate max available buffer
    return TRUE;
}

static int geometrylimits(frameformat *max, frameformat *step){
    if(max) *max = array;
    if(step) *step = (frameformat){.w = 1, .h = 1, .xoff = 1, .yoff = 1};
    return TRUE;
}

static int changeformat(frameformat *f){
    if(!f || !handle) return FALSE;
    DBG("set geom %dx%d (off: %dx%d)", f->w, f->h, f->xoff, f->yoff);
    if(!changeint("Width", f->w)) return FALSE;

    if(!changeint("Height", f->h)) return FALSE;
    if(!changeint("OffsetX", f->xoff)) return FALSE;
    if(!changeint("OffsetY", f->yoff)) return FALSE;
    DBG("Success!");
    return TRUE;
}

// exptime - in milliseconds!
static int setexp(float e){
    if(!handle) return FALSE;
    float eS = e / 1e3;
    if(eS > extrvalues.maxexp || eS < extrvalues.minexp){
        WARNX("Wrong exposure time: %fs (should be [%fs..%fs])", eS,
            extrvalues.minexp, extrvalues.maxexp);
        return FALSE;
    }
    if(!changefloat("ExposureTime", e * 1e3)) return FALSE;
    exptime = eS;
    return TRUE;
}

static int cam_startexp(){
    if(!handle || !pdata) return FALSE;
    DBG("Start exposition for %gs", exptime);
    MV_CC_StopGrabbing(handle);
    TRY(StartGrabbing);
    ONERR() return FALSE;
    return TRUE;
}

static Image* capture(){
    double starttime = sl_dtime();
    if(!cam_startexp()) return NULL;
    MV_FRAME_OUT_INFO_EX stImageInfo = {0}; // last image info
    DBG("Started capt @ %g", sl_dtime() - starttime);
    do{
        usleep(100);
        double diff = exptime - (sl_dtime() - starttime);
        if(diff > 0.) continue; // wait until exposure ends
        DBG("diff = %g", diff);
        if(diff < -MAX_READOUT_TM){ // wait much longer than exp lasts
            DBG("OOps, time limit");
            MV_CC_StopGrabbing(handle);
            return NULL;
        }
        TRY(GetOneFrameTimeout, pdata, pdatasz, &stImageInfo, 10);
        ONOK() break;
    }while(1);
    DBG("Tcapt=%g, exptime=%g", sl_dtime() - starttime, exptime);
    Image *captIma = u8toImage(pdata, stImageInfo.nWidth, stImageInfo.nHeight, stImageInfo.nWidth);
    DBG("return @ %g", sl_dtime() - starttime);
    return captIma;
}

camera Hikrobot = {
    .disconnect = cam_closecam,
    .connect = cam_connect,
    .capture = capture,
    .setbrightness = cam_setbright,
    .setexp = setexp,
    .setgain = cam_setgain,
    .setgeometry = changeformat,
    .getgeomlimits = geometrylimits,
    .getmaxgain = maxgain,
};
