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

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <usefull_macros.h>

#include "cmdlnopts.h"
#include "config.h"

static char *conffile = NULL; // configuration file name

configuration theconf = {
    .maxUsteps=DEFAULT_MAXUSTEPS,
    .maxVsteps=DEFAULT_MAXVSTEPS,
    .minarea=DEFAULT_MINAREA,
    .maxarea=DEFAULT_MAXAREA,
    .Nerosions=DEFAULT_NEROSIONS,
    .Ndilations=DEFAULT_NDILATIONS,
    .xoff=0,
    .yoff=0,
    .width=0,
    .height=0,
    .equalize=1,
    .naverage=DEFAULT_NAVERAGE,
    .stpserverport=DEFAULT_PUSIPORT,
    .starssort=0,
    .Kxu=0,
    .Kyu=0,
    .Kxv=0,
    .Kyv=0,
    .xtarget=-1,
    .ytarget=-1,
    .throwpart=DEFAULT_THROWPART,
    .maxexp=EXPOS_MAX + DBL_EPSILON,
    .minexp=EXPOS_MIN - DBL_EPSILON,
    .fixedexp=EXPOS_MIN,
    .intensthres=DEFAULT_INTENSTHRES
};

// {"", PAR_DOUBLE, (void*)&theconf., 0},
static confparam parvals[] = {
    {"maxarea", PAR_INT, (void*)&theconf.maxarea, 0, MINAREA-DBL_EPSILON, MAXAREA+DBL_EPSILON,
     "maximal area (in square pixels) of recognized star image"},
    {"minarea", PAR_INT, (void*)&theconf.minarea, 0, MINAREA-DBL_EPSILON, MAXAREA+DBL_EPSILON,
     "minimal area (in square pixels) of recognized star image"},
    {"ndilat", PAR_INT, (void*)&theconf.Ndilations, 0, 1.-DBL_EPSILON, MAX_NDILAT+DBL_EPSILON,
     "amount of dilations on binarized image"},
    {"neros", PAR_INT, (void*)&theconf.Nerosions, 0, 1.-DBL_EPSILON, MAX_NEROS+DBL_EPSILON,
     "amount of erosions after dilations"},
    {"xoffset", PAR_INT, (void*)&theconf.xoff, 0, -DBL_EPSILON, MAX_OFFSET+DBL_EPSILON,
     "X offset of subimage"},
    {"yoffset", PAR_INT, (void*)&theconf.yoff, 0, -DBL_EPSILON, MAX_OFFSET+DBL_EPSILON,
     "Y offset of subimage"},
    {"width", PAR_INT, (void*)&theconf.width, 0, -DBL_EPSILON, MAX_OFFSET+DBL_EPSILON,
     "subimage width"},
    {"height", PAR_INT, (void*)&theconf.height, 0, -DBL_EPSILON, MAX_OFFSET+DBL_EPSILON,
     "subimage height"},
    {"equalize", PAR_INT, (void*)&theconf.equalize, 0, -DBL_EPSILON, 1.+DBL_EPSILON,
     "make histogram equalization"},
    {"expmethod", PAR_INT, (void*)&theconf.expmethod, 0, -DBL_EPSILON, 1.+DBL_EPSILON,
     "exposition method: 0 - auto, 1 - fixed"},
    {"naverage", PAR_INT, (void*)&theconf.naverage, 0, 1-DBL_EPSILON, NAVER_MAX+DBL_EPSILON,
     "calculate mean position by N images"},
    {"umax", PAR_INT, (void*)&theconf.maxUsteps, 0, MINSTEPS-DBL_EPSILON, MAXSTEPS+DBL_EPSILON,
     "maximal value of steps on U semi-axe"},
    {"vmax", PAR_INT, (void*)&theconf.maxVsteps, 0, MINSTEPS-DBL_EPSILON, MAXSTEPS+DBL_EPSILON,
     "maximal value of steps on V semi-axe"},
    {"stpservport", PAR_INT, (void*)&theconf.stpserverport, 0, -DBL_EPSILON, 65536.,
     "port number of steppers' server"},
    {"Kxu", PAR_DOUBLE, (void*)&theconf.Kxu, 0, KUVMIN-DBL_EPSILON, KUVMAX+DBL_EPSILON,
     "dU = Kxu*dX + Kyu*dY"},
    {"Kyu", PAR_DOUBLE, (void*)&theconf.Kyu, 0, KUVMIN-DBL_EPSILON, KUVMAX+DBL_EPSILON,
     "dU = Kxu*dX + Kyu*dY"},
    {"Kxv", PAR_DOUBLE, (void*)&theconf.Kxv, 0, KUVMIN-DBL_EPSILON, KUVMAX+DBL_EPSILON,
     "dV = Kxv*dX + Kyv*dY"},
    {"Kyv", PAR_DOUBLE, (void*)&theconf.Kyv, 0, KUVMIN-DBL_EPSILON, KUVMAX+DBL_EPSILON,
     "dV = Kxv*dX + Kyv*dY"},
    {"xtarget", PAR_DOUBLE, (void*)&theconf.xtarget, 0, 1.-DBL_EPSILON, MAX_OFFSET+DBL_EPSILON,
     "X coordinate of target position"},
    {"ytarget", PAR_DOUBLE, (void*)&theconf.ytarget, 0, 1.-DBL_EPSILON, MAX_OFFSET+DBL_EPSILON,
     "Y coordinate of target position"},
    {"eqthrowpart", PAR_DOUBLE, (void*)&theconf.throwpart, 0, -DBL_EPSILON, MAX_THROWPART+DBL_EPSILON,
     "a part of low intensity pixels to throw away when histogram equalized"},
    {"minexp", PAR_DOUBLE, (void*)&theconf.minexp, 0, -DBL_EPSILON, EXPOS_MAX+DBL_EPSILON,
     "minimal exposition time"},
    {"maxexp", PAR_DOUBLE, (void*)&theconf.maxexp, 0, -DBL_EPSILON, EXPOS_MAX+DBL_EPSILON,
     "maximal exposition time"},
    {"fixedexp", PAR_DOUBLE, (void*)&theconf.fixedexp, 0, EXPOS_MIN-DBL_EPSILON, EXPOS_MAX+DBL_EPSILON,
     "fixed (in manual mode) exposition time"},
    {"intensthres", PAR_DOUBLE, (void*)&theconf.intensthres, 0, DBL_EPSILON, 1.+DBL_EPSILON,
     "threshold by total object intensity when sorting = |I1-I2|/(I1+I2)"},
    {"gain", PAR_DOUBLE, (void*)&theconf.gain, 0, GAIN_MIN-DBL_EPSILON, GAIN_MAX+DBL_EPSILON,
     "gain value in manual mode"},
    {"starssort", PAR_INT, (void*)&theconf.starssort, 0, -DBL_EPSILON, 1.+DBL_EPSILON,
     "stars sorting algorithm: by distance from target (0) or by intensity (1)"},
    {NULL,  0,  NULL, 0, 0., 0., NULL}
};

// return pointer to buff with size l filled with list of all commands (+help messages + low/high values)
char *get_cmd_list(char *buff, int l){
    if(!buff || l < 1) return NULL;
    int L = l;
    char *ptr = buff;
    confparam *par = parvals;
    while(L > 0 && par->name){
        int s = snprintf(ptr, L, "%s=newval - %s (from %g to %g)\n", par->name, par->help, par->minval+DBL_EPSILON, par->maxval-DBL_EPSILON);
        if(s < 1) break;
        L -= s; ptr += s;
        ++par;
    }
    return buff;
}

static char *omitspaces(char *v){
    if(!v) return NULL;
    while(*v && (*v == ' ' || *v == '\t')) ++v;
    char *ptr = strchr(v, ' ');
    if(ptr) *ptr = 0;
    ptr = strchr(v, '\t');
    if(ptr) *ptr = 0;
    return v;
}

// Read key/value from `pair` (key = value)
// RETURNed value should be FREEd
char *get_keyval(const char *pair, char value[128]){
    char key[128];
    char val[128];
    if(!pair || strlen(pair) < 3) return strdup("#"); // empty line
    char *keyptr = key, *valptr = val;
    int x = sscanf(pair, "%127[^=]=%127[^\n]%*c", key, val);
    //DBG("x=%d, key='%s', val='%s'", x, key, val);
    if(x < 0 || x > 2) return NULL; // wrong data or EOF
    if(x == 0) return strdup("#"); // empty line
    if(x == 2){ // param = value
        keyptr = omitspaces(key);
        valptr = omitspaces(val);
        sprintf(value, "%s", valptr);
        return strdup(keyptr);
    }
    keyptr = omitspaces(key);
    if(*keyptr == '#' || *keyptr == '%'){ // comment
        *value = 0;
        return strdup("#");
    }
    return NULL;
}

// Read key/value from file
static char *read_key(FILE *file, char value[128]){
    char *line = NULL;
    size_t n = 0;
    int got = getline(&line, &n, file);
    if(got < 0){
        FREE(line);
        return NULL;
    }
    char *kv = get_keyval(line, value);
    FREE(line);
    return kv;
}

static int str2int(int *num, const char *str){
    long res;
    char *endptr;
    if(!str) return 0;
    res = strtol(str, &endptr, 0);
    if(endptr == str || *str == '\0' || *endptr != '\0'){
        return FALSE;
    }
    if(res > INT_MAX || res < INT_MIN) return FALSE;
    if(num) *num = (int)res;
    return TRUE;
}

/**
 * @brief chk_keyval - check key for presence in theconf and calculate its value
 * @param key (i) - keyword
 * @param val (i) - value
 * @param result - result calculated from `val`
 * @return pointer to confparam if found & checked
 */
confparam *chk_keyval(const char *key, const char *val, key_value *result){
    if(!key || !val || !result) return NULL;
    confparam *par = parvals;
    while(par->name){
        if(strcmp(key, par->name) == 0){
            DBG("key='%s', par->name='%s'", key, par->name);
            result->type = par->type;
            switch(par->type){
                case PAR_INT:
                    DBG("INTEGER");
                    if(!str2int(&result->val.intval, val)){
                        WARNX("Wrong integer value '%s' of parameter '%s'", val, key);
                        return NULL;
                    }
                    if(result->val.intval > par->minval && result->val.intval < par->maxval)
                        return par;
                    else WARNX("Value (%d) of parameter %s out of range %g..%g",
                               result->val.intval, par->name, par->minval, par->maxval);
                break;
                case PAR_DOUBLE:
                    DBG("DOUBLE");
                    if(!str2double(&result->val.dblval, val)){
                        WARNX("Wrong double value '%s' of parameter '%s'", val, key);
                        return NULL;
                    }
                    DBG("val: %g, min: %g, max: %g", result->val.dblval, par->minval, par->maxval);
                    if(result->val.dblval > par->minval && result->val.dblval < par->maxval)
                        return par;
                    else WARNX("Value (%g) of parameter %s out of range %g..%g",
                               result->val.dblval, par->name, par->minval, par->maxval);
                break;
            }
            return NULL;
        }
        ++par;
    }
    return NULL;
}

/**
 * @brief chkconfig - check configuration file and init variables
 * @param confname - name of file
 * @return FALSE if configuration file wrong or absent
 */
int chkconfig(const char *confname){
    DBG("Config name: %s", confname);
    FREE(conffile);
    conffile = strdup(confname);
    FILE *f = fopen(confname, "r");
    int ret = TRUE;
    if(!f){
        WARN("Can't open %s", confname);
        return FALSE;
    }
    char *key, val[128];
    confparam *par = parvals;
    while(par->name){
        par->got = 0;
        ++par;
    }
    while((key = read_key(f, val))){
        if(*key == '#'){
            FREE(key);
            continue; // comment
        }
        //DBG("key: %s", key);
        key_value kv;
        par = chk_keyval(key, val, &kv);
        if(!par){
            WARNX("Parameter '%s' is wrong or out of range", key);
            FREE(key);
            continue;
        }
        switch(par->type){
            case PAR_INT:
                *((int*)par->ptr) = kv.val.intval;
            break;
            case PAR_DOUBLE:
                *((double*)par->ptr) = kv.val.dblval;
            break;
        }
        ++par->got;
        FREE(key);
    }
    fclose(f);
    int found = 0;
    par = parvals;
    while(par->name){
        DBG("parvals[]={%s, %d, %d(%g), %d}", par->name, par->type, *((int*)par->ptr), *((double*)par->ptr), par->got);
        int k = par->got;
        if(!k){
            ++par;
            continue;
        }
        if(k > 1){
            WARNX("parameter '%s' meets %d times", par->name, k);
            ret = FALSE;
        }
        ++found; ++par;
    }
#if 0
        int Nchecked = 0;
        if(theconf.maxUsteps >= MINSTEPS && theconf.maxUsteps <= MAXSTEPS) ++Nchecked;
        if(theconf.maxVsteps >= MINSTEPS && theconf.maxVsteps <= MAXSTEPS) ++Nchecked;
        if(theconf.cosXU >= -1. && theconf.cosXU <= 1.) ++Nchecked;
        if(theconf.sinXU >= -1. && theconf.sinXU <= 1.) ++Nchecked;
        if(theconf.cosXV >= -1. && theconf.cosXV <= 1.) ++Nchecked;
        if(theconf.sinXV >= -1. && theconf.sinXV <= 1.) ++Nchecked;
        if(theconf.KU >= COEFMIN && theconf.KU <= COEFMAX) ++Nchecked;
        if(theconf.KV >= COEFMIN && theconf.KV <= COEFMAX) ++Nchecked;
        if(theconf.maxarea <= MAXAREA && theconf.maxarea >= MINAREA) ++Nchecked;
        if(theconf.minarea <= MAXAREA && theconf.minarea >= MINAREA) ++Nchecked;
        if(theconf.Nerosions > 0 && theconf.Nerosions <= MAX_NEROS) ++Nchecked;
        if(theconf.Ndilations > 0 && theconf.Ndilations <= MAX_NDILAT) ++Nchecked;
        if(theconf.xtarget > 1.) ++Nchecked;
        if(theconf.ytarget > 1.) ++Nchecked;
        if(theconf.throwpart > -DBL_EPSILON && theconf.throwpart < MAX_THROWPART+DBL_EPSILON) ++Nchecked;
        if(theconf.xoff > 0 && theconf.xoff < MAX_OFFSET) ++Nchecked;
        if(theconf.yoff > 0 && theconf.yoff < MAX_OFFSET) ++Nchecked;
        if(theconf.width > 0 && theconf.width < MAX_OFFSET) ++Nchecked;
        if(theconf.height > 0 && theconf.height < MAX_OFFSET) ++Nchecked;
        if(theconf.minexp > 0. && theconf.minexp < EXPOS_MAX) ++Nchecked;
        if(theconf.maxexp > theconf.minexp) ++Nchecked;
        if(theconf.equalize > -1) ++Nchecked;
        if(theconf.intensthres > DBL_EPSILON && theconf.intensthres < 1.) ++Nchecked;
        if(theconf.naverage > 1 && theconf.naverage <= NAVER_MAX) ++Nchecked;
        if(theconf.stpserverport > 0 && theconf.stpserverport < 65536) ++Nchecked;
#endif
    DBG("chkconfig(): found %d", found);
    return ret;
}

/**
 * @brief saveconf    -   try to save configuration into file
 * @param confname - config file name
 * @return FALSE if failed
 */
int saveconf(const char *confname){
    if(!confname){
        if(!conffile){
            WARNX("no conffile given");
            return FALSE;
        }
        confname = conffile;
    }
    FILE *f = fopen(confname, "w");
    if(!f){
        WARN("Can't open %s", confname);
        LOGERR("Can't open %s to store configuration", confname);
        return FALSE;
    }
    confparam *par = parvals;
    while(par->name){
        par->got = 1;
        switch(par->type){
            case PAR_INT:
                fprintf(f, "%s = %d\n", par->name, *((int*)par->ptr));
                DBG("%s = %d", par->name, *((int*)par->ptr));
            break;
            case PAR_DOUBLE:
                fprintf(f, "%s = %.3f\n", par->name, *((double*)par->ptr));
                DBG("%s = %.3f", par->name, *((double*)par->ptr));
            break;
        }
        ++par;
    }
    DBG("%s saved", confname);
    LOGDBG("Configuration file '%s' saved", confname);
    fclose(f);
    return TRUE;
}

// return buffer filled with current configuration
char *listconf(const char *messageid, char *buf, int buflen){
    int L;
    char *ptr = buf;
    confparam *par = parvals;
    L = snprintf(ptr, buflen, "{ \"%s\": \"%s\", ", MESSAGEID, messageid);
    buflen -= L; ptr += L;
    while(par->name && buflen > 0){
        switch(par->type){
            case PAR_INT:
                L = snprintf(ptr, buflen, "\"%s\": %d", par->name, *((int*)par->ptr));
            break;
            case PAR_DOUBLE:
                L = snprintf(ptr, buflen, "\"%s\": %.3f", par->name, *((double*)par->ptr));
            break;
            default:
                L = 0;
        }
        ++par;
        if(L > -1){
            buflen -= L; ptr += L;
        }else{
            buf[buflen-1] = 0; break;
        }
        if(par->name){ // put comma
            L = snprintf(ptr, buflen, ", ");
            if(L > -1){buflen -= L; ptr += L;}
        }
    }
    snprintf(ptr, buflen, " }\n");
    return buf;
}
