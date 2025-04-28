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

#ifdef EBUG

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"

#define DEBUGLOG        "DEBUG.log"

sl_log_t *debuglog = NULL;

void makedebuglog(){
    unlink(DEBUGLOG);
    DBG("Create debug log file: " DEBUGLOG);
    debuglog = sl_createlog(DEBUGLOG, LOGLEVEL_ANY, 0);
}

/**
 * @brief my_malloc - memory allocator for logger
 * @param N - number of elements to allocate
 * @param S - size of single element (typically sizeof)
 * @return pointer to allocated memory area
 */
void *my_malloc(size_t N, size_t S){
    size_t NS = N*S + sizeof(size_t);
    sl_putlogt(0, debuglog, LOGLEVEL_ERR, "ALLOCSZ(%zd)", N*S);
    void *p = malloc(NS);
    if(!p) ERR("malloc");
    memset(p, 0, NS);
    *((size_t*)p) = N*S;
    return (p + sizeof(size_t));
}

void my_free(void *ptr){
    void *orig = ptr - sizeof(size_t);
    sl_putlogt(0, debuglog, LOGLEVEL_ERR, "FREESZ(%zd)", *(size_t*)orig);
    free(orig);
}

#endif
