/*                                                                                                  geany_encoding=koi8-r
 * cmdlnopts.h - comand line options for parceargs
 *
 * Copyright 2013 Edward V. Emelianoff <eddy@sao.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#pragma once
#ifndef CMDLNOPTS_H__
#define CMDLNOPTS_H__

// default values
#define DEFAULT_PIDFILE     "/tmp/loccorr.pid"
#define DEFAULT_CONFFILE    "./loccorr.conf"
#define DEFAULT_OUTPJPEG    "./outpWcrosses.jpg"
#define DEFAULT_PUSIPORT    (4444)
#define DEFAULT_IOPORT      (12345)
#define DEFAULT_MAXAREA     (150000)
#define DEFAULT_MINAREA     (400)
#define DEFAULT_EROSIONS    (2)
#define DEFAULT_DILATIONS   (2)
#define DEFAULT_THROWPART   (0.5)
#define DEFAULT_INTENSTHRES (0.01)
#define DEFAULT_NAVERAGE    (5)
#define DEFAULT_MAXUSTEPS   (16000)
#define DEFAULT_MAXVSTEPS   (16000)
#define DEFAULT_NEROSIONS   (3)
#define DEFAULT_NDILATIONS  (3)

/*
 * here are some typedef's for global data
 */
typedef struct{
    char *pidfile;          // name of PID file
    char *logfile;          // logging to this file
    char *inputname;        // input for monitor file or directory name
    char *logXYname;        // file to log XY coordinates of first point
    char *configname;       // name of configuration file (default: ./loccorr.conf)
    char *processing;       // =="pusirobo" to fix corrections with pusirobot drives
    char *outputjpg;        // output jpeg name
    int pusiservport;       // port of local pusirobot CAN server
    int equalize;           // make historam equalization of saved jpeg
//    int medradius;          // radius of median filter (r=1 -> 3x3, r=2 -> 5x5 etc.)
    int verb;               // logfile verbosity level
    int ndilations;         // amount of erosions (default: 2)
    int nerosions;          // amount of dilations (default: 2)
    int minarea;            // minimal object pixels amount (default: 400)
    int maxarea;            // maximal object pixels amount (default: 150000)
    int Naveraging;         // amount of images to average processing (min 2, max 25, default 5)
    int xoff; int yoff;     // offset by X and Y axes
    int width; int height;  // target width and height of image
    int ioport;             // port for IO commands
    double throwpart;       // fraction of black pixels to throw away when make histogram eq
    double intensthres;     // threshold by total object intensity when sorting = |I1-I2|/(I1+I2), default: 0.01
    double maxexp;          // max exposition time (ms)
    double minexp;          // min exposition time (ms)
    double xtarget; double ytarget;// target point coordinates
} glob_pars;

extern glob_pars *GP;  // for GP->pidfile need in `signals`

glob_pars *parse_args(int argc, char **argv);

#endif // CMDLNOPTS_H__
