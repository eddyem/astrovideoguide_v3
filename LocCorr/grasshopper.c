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

#include <C/FlyCapture2_C.h>
#include <C/FlyCapture2Defs_C.h>
#include <float.h> // FLT_EPSILON
#include <math.h>
#include <stdio.h>
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "fits.h"
#include "grasshopper.h"
#include "imagefile.h"

static fc2Context context;
static fc2PGRGuid guid;
static fc2Error err = FC2_ERROR_OK;
static float exptime = 10.; // exposition time in milliseconds
static float gain = 0.;

#define FC2FN(fn, ...) do{err = FC2_ERROR_OK; if(FC2_ERROR_OK != (err=fn(context __VA_OPT__(,) __VA_ARGS__))){ \
    WARNX(#fn "(): %s", fc2ErrorToDescription(err)); return 0;}}while(0)

/**
 * @brief setfloat - set absolute property value (float)
 * @param t        - type of property
 * @param context  - initialized context
 * @param f        - new value
 * @return 1 if all OK
 */
static int setfloat(fc2PropertyType t, float f){
    fc2Property prop = {0};
    prop.type = t;
    fc2PropertyInfo i = {0};
    i.type = t;
    FC2FN(fc2GetProperty, &prop);
    FC2FN(fc2GetPropertyInfo, &i);
    if(!prop.present || !i.present) return 0;
    if(prop.autoManualMode){
        if(!i.manualSupported){
            WARNX("Can't set auto-only property");
            return 0;
        }
        prop.autoManualMode = false;
    }
    if(!prop.absControl){
        if(!i.absValSupported){
            WARNX("Can't set non-absolute property to absolute value");
            return 0;
        }
        prop.absControl = true;
    }
    if(!prop.onOff){
        if(!i.onOffSupported){
            WARNX("Can't set property ON");
            return 0;
        }
        prop.onOff = true;
    }
    if(prop.onePush && i.onePushSupported) prop.onePush = false;
    prop.valueA = prop.valueB = 0;
    prop.absValue = f;
    FC2FN(fc2SetProperty, &prop);
    // now check
    FC2FN(fc2GetProperty, &prop);
    if(fabsf(prop.absValue - f) > 0.02f){
        WARNX("Can't set property! Got %g instead of %g.", prop.absValue, f);
        return 0;
    }
    return 1;
}

int propOnOff(fc2PropertyType t, BOOL onOff){
    fc2Property prop = {0};
    prop.type = t;
    fc2PropertyInfo i = {0};
    i.type = t;
    FC2FN(fc2GetPropertyInfo, &i);
    FC2FN(fc2GetProperty, &prop);
    if(!prop.present || !i.present) return 0;
    if(prop.onOff == onOff) return 0;
    if(!i.onOffSupported){
        WARNX("Property doesn't support state OFF");
        return  0;
    }
    prop.onOff = onOff;
    FC2FN(fc2SetProperty, &prop);
    FC2FN(fc2GetProperty, &prop);
    if(prop.onOff != onOff){
        WARNX("Can't change property OnOff state");
        return 0;
    }
    return 1;
}

#define autoExpOff()        propOnOff(FC2_AUTO_EXPOSURE, false)
#define whiteBalOff()       propOnOff(FC2_WHITE_BALANCE, false)
#define gammaOff()          propOnOff(FC2_GAMMA, false)
#define trigModeOff()       propOnOff(FC2_TRIGGER_MODE, false)
#define trigDelayOff()      propOnOff(FC2_TRIGGER_DELAY, false)
#define frameRateOff()      propOnOff(FC2_FRAME_RATE, false)
// +set: saturation, hue, sharpness
#define setbrightness(b)    setfloat(FC2_BRIGHTNESS, b)
#define setexp(e)           setfloat(FC2_SHUTTER, e)
#define setgain(g)          setfloat(FC2_GAIN, g)

static int connected = 0;
static void disconnect(){
    connected = 0;
    fc2DestroyContext(context);
}

static int changeformat(){
    FNAME();
    BOOL b;
    fc2Format7Info f7i = {.mode = FC2_MODE_0};
    FC2FN(fc2GetFormat7Info, &f7i, &b);
    if(!b) return 0;
    fc2Format7ImageSettings f7;
/*    unsigned int packSz; float perc;
    FC2FN(fc2GetFormat7Configuration, &f7, &packSz, &perc);
    DBG("packsz=%u, perc=%f, off=%d/%d, HW=%d/%d", packSz, perc, f7.offsetX, f7.offsetY, f7.height, f7.width);
    */
    f7.mode = FC2_MODE_0;
    f7.offsetX = (GP->xoff < (int)f7i.maxWidth && GP->xoff > -1) ? GP->xoff : 0;
    f7.offsetY = (GP->yoff < (int)f7i.maxHeight && GP->yoff > -1) ? GP->yoff : 0;
    f7.width = (f7.offsetX+GP->width <= f7i.maxWidth && GP->width > 1) ? (unsigned int)GP->width : f7i.maxWidth - f7.offsetX;
    f7.height = (f7.offsetY+GP->height <= f7i.maxHeight && GP->height > 1) ? (unsigned int)GP->height : f7i.maxHeight - f7.offsetY;
    DBG("offx=%d, offy=%d, w=%d, h=%d ", f7.offsetX, f7.offsetY, f7.width, f7.height);
    f7.pixelFormat = FC2_PIXEL_FORMAT_MONO8;
    fc2Format7PacketInfo f7p;
    FC2FN(fc2ValidateFormat7Settings, &f7, &b, &f7p);
    if(!b) return 0; // invalid
    FC2FN(fc2SetFormat7Configuration, &f7, f7p.recommendedBytesPerPacket);
    return 1;
}

static int connect(){
    FNAME();
    if(connected) return 1;
    unsigned int numCameras = 0;
    if(FC2_ERROR_OK != (err = fc2CreateContext(&context))){
        WARNX("fc2CreateContext(): %s", fc2ErrorToDescription(err));
        return 0;
    }
    FC2FN(fc2GetNumOfCameras, &numCameras);
    if(numCameras == 0){
        WARNX("No cameras detected!");
        disconnect();
        return 0;
    }
    DBG("Found %d camera[s]", numCameras);
    if(numCameras > 1){
        WARNX("Found %d cameras, will use first", numCameras);
    }
    FC2FN(fc2GetCameraFromIndex, 0, &guid);
    FC2FN(fc2Connect, &guid);
    // turn off all shit
    autoExpOff();
    whiteBalOff();
    gammaOff();
    trigModeOff();
    trigDelayOff();
    frameRateOff();
    if(!changeformat()) WARNX("Can't change camera format");
    connected = 1;
    return 1;
}

static int GrabImage(fc2Image *convertedImage){
    FNAME();
    fc2Image rawImage;
    // start capture
    FC2FN(fc2StartCapture);
    err = fc2CreateImage(&rawImage);
    if(err != FC2_ERROR_OK){
        WARNX("Error in fc2CreateImage: %s", fc2ErrorToDescription(err));
        return 0;
    }
    // Retrieve the image
    err = fc2RetrieveBuffer(context, &rawImage);
    if(err != FC2_ERROR_OK){
        WARNX("Error in fc2RetrieveBuffer: %s", fc2ErrorToDescription(err));
        return 0;
    }
    // Convert image to gray
    err = fc2ConvertImageTo(FC2_PIXEL_FORMAT_MONO8, &rawImage, convertedImage);
    if(err != FC2_ERROR_OK){
        WARNX("Error in fc2ConvertImageTo: %s", fc2ErrorToDescription(err));
        return 0;
    }
    fc2StopCapture(context);
    fc2DestroyImage(&rawImage);
    return 1;
}

static void calcexpgain(float newexp){
    DBG("recalculate exposition: oldexp=%g, oldgain=%g, newexp=%g", exptime, gain, newexp);
    if(newexp < exptime){ // first we should make gain lower
        if(10.*newexp < GP->maxexp){
            if(gain > 10.){
                gain = 10.;
                newexp *= 10.;
            }else if(gain > 0.){
                gain = 0.;
                newexp *= 10.;
            }
        }
    }else{ // if new exposition too large, increase gain
        if(newexp > GP->maxexp){
            if(gain < 19.){
                gain += 10.;
                newexp /= 10.;
            }
        }
    }
    if(newexp < GP->minexp) newexp = GP->minexp;
    else if(newexp > GP->maxexp) newexp = GP->maxexp;
    exptime = newexp;
    DBG("New values: exp=%g, gain=%g", exptime, gain);
}

//convertedImage.pData, convertedImage.cols, convertedImage.rows, convertedImage.stride
static void recalcexp(fc2Image *img){
    uint8_t *data = img->pData;
    int W = img->cols, H = img->rows, S = img->stride;
    int histogram[256] = {0};
    DBG("W=%d, H=%d, S=%d", W, H, S);
    for(int y = 0; y < H; ++y){
        uint8_t *ptr = &data[y*S];
        for(int x = 0; x < W; ++x, ++ptr)
            ++histogram[*ptr];
    }
    int idx100, sum100 = 0;
    for(idx100 = 255; idx100 >= 0; --idx100){
        sum100 += histogram[idx100];
        if(sum100 > 100) break;
    }
    DBG("Sum100=%d, idx100=%d", sum100, idx100);
    if(idx100 > 200 && idx100 < 250) return; // good values
    if(idx100 > 250){ // exposure too long
        calcexpgain(0.7*exptime);
    }else{ // exposure too short
        if(idx100 > 5)
            calcexpgain(exptime * 210. / (float)idx100);
        else
            calcexpgain(exptime * 50.);
    }
}

int capture_grasshopper(void (*process)(Image*)){
    FNAME();
    static float oldexptime = 0.;
    static float oldgain = -1.;
    fc2Image convertedImage;
    err = fc2CreateImage(&convertedImage);
    if(err != FC2_ERROR_OK){
        WARNX("capture_grasshopper(): can't create image, %s", fc2ErrorToDescription(err));
        return 0;
    }
    Image *oIma = NULL;
    while(1){
        if(!connect()){ // wait until camera be powered on
            DBG("Disconnected");
            sleep(1);
            continue;
        }
        if(fabsf(oldexptime - exptime) > FLT_EPSILON){ // new exsposition value
            red("Change exptime to %.2fms\n", exptime);
            if(setexp(exptime)){
                oldexptime = exptime;
            }else{
                WARNX("Can't change exposition time to %gms", exptime);
                disconnect();
                continue;
            }
        }
        if(fabs(oldgain - gain) > FLT_EPSILON){ // change gain
            red("Change gain to %g\n", gain);
            if(setgain(gain)){
                oldgain = gain;
            }else{
                WARNX("Can't change gain to %g", gain);
                disconnect();
                continue;
            }
        }
        if(!GrabImage(&convertedImage)){
            WARNX("Can't grab image");
            disconnect();
            continue;
        }
        if(!process) continue;
        recalcexp(&convertedImage);
        oIma = u8toImage(convertedImage.pData, convertedImage.cols, convertedImage.rows, convertedImage.stride);
        if(oIma){
            process(oIma);
            FREE(oIma);
        }
    }
    fc2DestroyImage(&convertedImage);
    fc2DestroyContext(context);
    return 1;
}
