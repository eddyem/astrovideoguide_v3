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

// try to connect to local pusirobo server
int pusi_connect();
// disconnect
void pusi_stop();
// global variable proc_corr
void pusi_process_corrections(double X, double Y);
// global variable stepstatus
char *pusi_status(const char *messageid, char *buf, int buflen);
// global variable setstepstatus
char *set_pusistatus(const char *newstatus, char *buf, int buflen);
// global variable movefocus
char *set_pfocus(const char *newstatus, char *buf, int buflen);

#endif // PUSIROBO_H__
