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
#include <string.h>

#include "config.h"
#include "debug.h"
#include "grasshopper.h"
#include "imagefile.h"

static fc2Context context;
static fc2PGRGuid guid;
static fc2Error err = FC2_ERROR_OK;

#ifndef Stringify
#define Stringify(x) #x
#endif

#define FC2FN(fn, ...) do{err = FC2_ERROR_OK; if(FC2_ERROR_OK != (err=fn(context __VA_OPT__(,) __VA_ARGS__))){ \
    WARNX(Stringify(fn) "(): %s", fc2ErrorToDescription(err)); return FALSE;}}while(0)

static void disconnect(){
    fc2DestroyContext(context);
}

/**
 * @brief setfloat - set absolute property value (float)
 * @param t        - type of property
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
            return FALSE;
        }
        prop.autoManualMode = false;
    }
    if(!prop.absControl){
        if(!i.absValSupported){
            WARNX("Can't set non-absolute property to absolute value");
            return FALSE;
        }
        prop.absControl = true;
    }
    if(!prop.onOff){
        if(!i.onOffSupported){
            WARNX("Can't set property ON");
            return FALSE;
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
        return FALSE;
    }
    return TRUE;
}

static int setbrightness(float b){
    return setfloat(FC2_BRIGHTNESS, b);
}
static int setexp(float e){
    return setfloat(FC2_SHUTTER, e);
}
static int setgain(float e){
    return setfloat(FC2_GAIN, e);
}
// TODO: +set saturation, hue, sharpness ?


static int propOnOff(fc2PropertyType t, BOOL onOff){
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

static int geometrylimits(frameformat *max, frameformat *step){
    fc2Format7Info f = {.mode = FC2_MODE_0};
    BOOL b;
    fc2Format7Info i = {0};
    FC2FN(fc2GetFormat7Info, &i, &b);
    if(!b || !max || !step) return FALSE;
    max->h = f.maxHeight; max->w = f.maxWidth;
    max->xoff = f.maxWidth - f.offsetHStepSize;
    max->yoff = f.maxHeight - f.offsetVStepSize;
    step->w = f.imageHStepSize;
    step->h = f.imageVStepSize;
    step->xoff = f.offsetHStepSize;
    step->yoff = f.offsetVStepSize;
    return TRUE;
}

static int getformat(frameformat *fmt){
    if(!fmt) return FALSE;
    unsigned int packsz; float pc;
    fc2Format7ImageSettings f7;
    FC2FN(fc2GetFormat7Configuration, &f7, &packsz, &pc);
    fmt->h = f7.height; fmt->w = f7.width;
    fmt->xoff = f7.offsetX; fmt->yoff = f7.offsetY;
    return TRUE;
}

static int changeformat(frameformat *fmt){
    if(!fmt) return FALSE;
    BOOL b;
    fc2Format7ImageSettings f7;
    f7.mode = FC2_MODE_0;
    f7.offsetX = fmt->xoff;
    f7.offsetY = fmt->yoff;
    f7.width = fmt->w;
    f7.height = fmt->h;
    DBG("offx=%d, offy=%d, w=%d, h=%d ", f7.offsetX, f7.offsetY, f7.width, f7.height);
    f7.pixelFormat = FC2_PIXEL_FORMAT_MONO8;
    fc2Format7PacketInfo f7p;
    FC2FN(fc2ValidateFormat7Settings, &f7, &b, &f7p);
    if(!b) return FALSE; // invalid
    FC2FN(fc2SetFormat7Configuration, &f7, f7p.recommendedBytesPerPacket);
    getformat(fmt); // change values to currently used
    return TRUE;
}

static int connect(){
    FNAME();
    unsigned int numCameras = 0;
    if(FC2_ERROR_OK != (err = fc2CreateContext(&context))){
        WARNX("fc2CreateContext(): %s", fc2ErrorToDescription(err));
        return FALSE;
    }
    FC2FN(fc2GetNumOfCameras, &numCameras);
    if(numCameras == 0){
        WARNX("No cameras detected!");
        camdisconnect();
        return FALSE;
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
    return TRUE;
}

static int GrabImage(fc2Image *convertedImage){
    if(!convertedImage) return FALSE;
    int ret = FALSE;
    fc2Image rawImage;
    // start capture
    FC2FN(fc2StartCapture);
    err = fc2CreateImage(&rawImage);
    if(err != FC2_ERROR_OK){
        WARNX("Error in fc2CreateImage: %s", fc2ErrorToDescription(err));
        fc2StopCapture(context);
        return FALSE;
    }
    // Retrieve the image
    err = fc2RetrieveBuffer(context, &rawImage);
    if(err != FC2_ERROR_OK){
        WARNX("Error in fc2RetrieveBuffer: %s", fc2ErrorToDescription(err));
        goto rtn;
    }
    // Convert image to gray
    err = fc2ConvertImageTo(FC2_PIXEL_FORMAT_MONO8, &rawImage, convertedImage);
    if(err != FC2_ERROR_OK){
        WARNX("Error in fc2ConvertImageTo: %s", fc2ErrorToDescription(err));
        goto rtn;
    }
    ret = TRUE;
rtn:
    fc2StopCapture(context);
    fc2DestroyImage(&rawImage);
    return ret;
}

// capture image, memory allocated HERE
static Image *capture(){
    FNAME();
    Image *oIma = NULL;
    fc2Image convertedImage;
    err = fc2CreateImage(&convertedImage);
    if(err != FC2_ERROR_OK){
        WARNX("capture_grasshopper(): can't create image, %s", fc2ErrorToDescription(err));
        return NULL;
    }
    if(!GrabImage(&convertedImage)){
        WARNX("Can't grab image");
        return NULL;
    }
    oIma = u8toImage(convertedImage.pData, convertedImage.cols, convertedImage.rows, convertedImage.stride);
    fc2DestroyImage(&convertedImage);
    return oIma;
}

static float maxgain(){
    return GAIN_MAX;
}

// exported object
camera GrassHopper = {
    .disconnect = disconnect,
    .connect = connect,
    .capture = capture,
    .setbrightness = setbrightness,
    .setexp = setexp,
    .setgain = setgain,
    .setgeometry = changeformat,
    .getgeomlimits = geometrylimits,
    .getmaxgain = maxgain,
};
