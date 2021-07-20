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
#ifndef PUSIROBO_H__
#define PUSIROBO_H__

typedef enum{
    PUSI_DISCONN,
    PUSI_RELAX,
    PUSI_SETUP,
    PUSI_GOTOTHEMIDDLE,
    PUSI_FINDTARGET,
    PUSI_FIX,
    PUSI_UNDEFINED
} pusistate;

// try to connect to local pusirobo server
int pusi_connect();
int pusi_setstate(pusistate newstate);
pusistate pusi_getstate();
void pusi_disconnect();
void pusi_process_corrections(double X, double Y, int corrflag);
char *pusi_status(const char *messageid, char *buf, int buflen);
char *set_pusistatus(const char *newstatus, char *buf, int buflen);
char *set_pfocus(const char *newstatus, char *buf, int buflen);
char *get_JSON_status(char *buf, int buflen);
// ADD global SEND

#endif // PUSIROBO_H__
