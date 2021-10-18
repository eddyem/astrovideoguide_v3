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
#ifndef CONFIG_H__
#define CONFIG_H__

// default configuration borders
// min/max total steps range
#define MINSTEPS        (100)
#define MAXSTEPS        (50000)
#define Fmaxsteps       (64000)
// steps per pixel
#define COEFMIN         (0.1)
#define COEFMAX         (10000)
// area
#define MINAREA         (4)
#define MAXAREA         (2500000)
#define MAX_NDILAT      (100)
#define MAX_NEROS       (100)
#define MAX_THROWPART   (0.9)
#define MAX_OFFSET      (10000)
// min/max exposition in ms
#define EXPOS_MIN       (0.1)
#define EXPOS_MAX       (4001.)
#define GAIN_MIN        (0.)
#define GAIN_MAX        (100.)
#define BRIGHT_MIN      (0.)
#define BRIGHT_MAX      (10.)
// max average images counter
#define NAVER_MAX       (25)
// coefficients to convert dx,dy to du,dv
#define KUVMIN           (-5000.)
#define KUVMAX           (5000.)
// default coefficient for corrections (move to Kdu, Kdv instead of du, dv)
#define KCORR           (0.90)
// min/max median seed
#define MIN_MEDIAN_SEED (1)
#define MAX_MEDIAN_SEED (7)
// fixed background
#define FIXED_BK_MIN    (0)
#define FIXED_BK_MAX    (250)

// exposition methods: 0 - auto, 1 - fixed
#define EXPAUTO         (0)
#define EXPMANUAL       (1)

// roundness parameter
#define MINWH                       (0.3)
#define MAXWH                       (3.)

// messageID field name
#define MESSAGEID       "messageid"

typedef struct{
    int maxUsteps;      // max amount of steps by both axes
    int maxVsteps;
    int maxFpos;        // min/max F position (in microsteps) - user can't change this
    int minFpos;
    int minarea;        // min/max area of star image
    int maxarea;
    int Nerosions;      // amount of erosions/dilations
    int Ndilations;
    int xoff;           // subimage offset
    int yoff;
    int width;          // subimage size
    int height;
    int equalize;       // !=0 to equalize output image histogram
    int naverage;       // amount of images for average calculation (>1)
    int stpserverport;  // steppers' server port
    int starssort;      // stars sorting algorithm: by distance from target (0) or by intensity (1)
    int expmethod;      // 0 - auto, 1 - fixed
    int medfilt;        // == 1 to make median filter before calculations
    int medseed;        // median seed
    int fixedbkg;       // don't calculate background, use fixed value instead
    int fixedbkgval;    // value of bk
    // dU = Kxu*dX + Kyu*dY; dV = Kxv*dX + Kyv*dY
    double Kxu; double Kyu;
    double Kxv; double Kyv;
    double minwh; double maxwh; // roundness parameters
    double xtarget;     // target (center) values (in absolute coordinates! screen coords = target - offset)
    double ytarget;
    double throwpart;   // part of values to throw avay @ histogram equalisation
    double maxexp;      // minimal and maximal exposition (in ms)
    double minexp;
    double fixedexp;    // exptime in manual mode
    double gain;        // gain value in manual mode
    double brightness;  // brightness @camera
    double intensthres; // threshold for stars intensity comparison: fabs(Ia-Ib)/(Ia+Ib) > thres -> stars differs
} configuration;

typedef enum{
    PAR_INT,
    PAR_DOUBLE
} partype;

typedef struct{
    const char *name;   // parameter name
    partype type;       // type of parameter's value
    void *ptr;          // pointer to value in `theconf`
    int got;            // counter of parameter in config file
    double minval;      // min/max values
    double maxval;
    const char *help;   // help message
} confparam;

typedef union{
    double dblval;
    int intval;
} dblint;

typedef struct{
    dblint val;
    partype type;
} key_value;

extern configuration theconf;
char *get_cmd_list(char *buff, int l);
int chkconfig(const char *confname);
int saveconf(const char *confname);
char *get_keyval(const char *pair, char value[128]);
confparam *chk_keyval(const char *key, const char *val, key_value *result);
char *listconf(const char *messageid, char *buf, int buflen);

#endif // CONFIG_H__
