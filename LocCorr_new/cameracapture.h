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
#pragma once
#ifndef CAMERACAPTURE_H__
#define CAMERACAPTURE_H__

#include "imagefile.h" // Image


// format of single frame
typedef struct{
    int w; int h;           // width & height
    int xoff; int yoff;     // X and Y offset
} frameformat;

typedef struct{
    void (*disconnect)();   // disconnect & cleanup
    int (*connect)();       // connect & init
    Image* (*capture)();    // capture an image
    // setters: brightness, exptime, gain
    int (*setbrightness)(float b);
    int (*setexp)(float e);
    int (*setgain)(float g);
    float (*getmaxgain)();  // get max available gain value
    // geometry (if TRUE, all args are changed to suitable values)
    int (*setgeometry)(frameformat *fmt);
    // get limits of geometry: maximal values and steps
    int (*getgeomlimits)(frameformat *max, frameformat *step);
    //int (*getgeometry)(frameformat *fmt);
} camera;

int setCamera(camera *cptr);
void camdisconnect();
int camcapture(void (*process)(Image *));
char *camstatus(const char *messageid, char *buf, int buflen);


#endif // CAMERACAPTURE_H__
