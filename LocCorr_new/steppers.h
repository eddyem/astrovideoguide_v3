/*
 * This file is part of the loccorr project.
 * Copyright 2024 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

// set state to `disconnect` after this amount of errors in `moving_finished`
#define MAX_ERR_CTR     (15)

// max time interval from previous correction to clear integral/time (seconds)
#define MAX_PID_TIME    (5.)

// amount of ALL motors
#define NMOTORS (8)

typedef struct{
    void (*proc_corr)(double, double);
    char *(*stepstatus)(const char *messageid, char *buf, int buflen);
    char *(*setstepstatus)(const char *newstatus, char *buf, int buflen);
    char *(*movefocus)(const char *newstatus, char *buf, int buflen);
    char *(*moveByU)(const char *val, char *buf, int buflen);
    char *(*moveByV)(const char *val, char *buf, int buflen);
    void (*stepdisconnect)();
} steppersproc;

steppersproc *steppers_connect();
extern steppersproc* theSteppers;

