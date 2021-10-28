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
#ifndef DEBUG_H__
#define DEBUG_H__

#include <usefull_macros.h>

#ifdef EBUG

extern sl_log *debuglog;
void makedebuglog();
void *my_malloc(size_t N, size_t S);
void my_free(void *ptr);

//#undef FNAME
#undef ALLOC
#undef MALLOC
#undef FREE
#define _LOG(...)       do{if(!debuglog) makedebuglog(); sl_putlogt(1, debuglog, LOGLEVEL_ERR, __VA_ARGS__);}while(0)
#define DBGLOG(...)     do{_LOG("%s (%s, line %d)", __func__, __FILE__, __LINE__); \
                           sl_putlogt(0, debuglog, LOGLEVEL_ERR, __VA_ARGS__);}while(0)
//#define FNAME()         _LOG("%s (%s, line %d)", __func__, __FILE__, __LINE__)

#define _str(x) #x
#define ALLOC(type, var, size)  DBGLOG("%s *%s = ALLOC(%d)", _str(type), _str(var), size*sizeof(type)); \
                                type * var = ((type *)my_malloc(size, sizeof(type)))
#define MALLOC(type, size)  ((type *)my_malloc(size, sizeof(type))); DBGLOG("ALLOC()")
#define FREE(ptr)           do{if(ptr){DBGLOG("FREE(%s)", _str(ptr)); my_free(ptr); ptr = NULL;}}while(0);

#else

#define DBGLOG(...)

#endif


#endif // DEBUG_H__
