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

/*
 * here are some typedef's for global data
 */
typedef struct{
    char *pidfile;          // name of PID file
    char *logfile;          // logging to this file
    char *inputname;        // input for monitor file or directory name
    char *logXYname;        // file to log XY coordinates of first point
    double throwpart;       // fraction of black pixels to throw away when make histogram eq
    int equalize;           // make historam equalization of saved jpeg
    int medradius;          // radius of median filter (r=1 -> 3x3, r=2 -> 5x5 etc.)
    int verb;               // logfile verbosity level
    int ndilations;         // amount of erosions (default: 2)
    int nerosions;          // amount of dilations (default: 2)
    int minarea;            // minimal object pixels amount (default: 5)
    double intensthres;     // threshold by total object intensity when sorting = |I1-I2|/(I1+I2), default: 0.01
    double maxexp;          // max exposition time (ms)
    double minexp;          // min exposition time (ms)
    int xoff; int yoff;     // offset by X and Y axes
    int width; int height;  // target width and height of image
    double xtarget; double ytarget;// target point coordinates
} glob_pars;

extern glob_pars *GP;  // for GP->pidfile need in `signals`

glob_pars *parse_args(int argc, char **argv);

#endif // CMDLNOPTS_H__
