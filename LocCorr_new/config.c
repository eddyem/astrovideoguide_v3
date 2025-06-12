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

#include "cmdlnopts.h"
#include "config.h"
#include "debug.h"

static char *conffile = NULL; // configuration file name

configuration theconf = {
    .maxUpos=DEFAULT_MAXUSTEPS,
    .minUpos=0,
    .maxVpos=DEFAULT_MAXVSTEPS,
    .minVpos=0,
    .maxFpos=Fmaxsteps-1,
    .minFpos=-Fmaxsteps+1,
    .minarea=DEFAULT_MINAREA,
    .maxarea=DEFAULT_MAXAREA,
    .maxwh=1.1,
    .minwh=0.9,
    .Nerosions=DEFAULT_NEROSIONS,
    .Ndilations=DEFAULT_NDILATIONS,
    .xoff=0,
    .yoff=0,
    .width=0,
    .height=0,
    .equalize=1,
    .naverage=DEFAULT_NAVERAGE,
    .stpserverport=DEFAULT_STEPPERSPORT,
    .starssort=0,
    .Kxu=0,
    .Kyu=0,
    .Kxv=0,
    .Kyv=0,
    .PIDU_P = PID_P_DEFAULT,
    .PIDU_I = PID_I_DEFAULT,
    .PIDU_D = PID_D_DEFAULT,
    .PIDV_P = PID_P_DEFAULT,
    .PIDV_I = PID_I_DEFAULT,
    .PIDV_D = PID_D_DEFAULT,
    .xtarget=-1,
    .ytarget=-1,
    .throwpart=DEFAULT_THROWPART,
    .maxexp=EXPOS_MAX - 1.,
    .minexp=EXPOS_MIN,
    .exptime=EXPOS_MIN*2,
    .gain=20.,
    .intensthres=DEFAULT_INTENSTHRES,
    .medseed=MIN_MEDIAN_SEED,
};

static int isSorted = 0; // ==1 when `parvals` are sorted
static int compConfVals(const void *_1st, const void *_2nd){
    const confparam *a = (confparam*)_1st, *b = (confparam*)_2nd;
    return strcmp(a->name, b->name);
}

// could be in unsorted order as whould be sorted at first help call
// {"", PAR_DOUBLE, (void*)&theconf., 0},
static confparam parvals[] = {
    {"maxarea", PAR_INT, (void*)&theconf.maxarea, 0, MINAREA, MAXAREA,
     "maximal area (in square pixels) of recognized star image"},
    {"minarea", PAR_INT, (void*)&theconf.minarea, 0, MINAREA, MAXAREA,
     "minimal area (in square pixels) of recognized star image"},
    {"minwh", PAR_DOUBLE, (void*)&theconf.minwh, 0, MINWH, 1.,
     "minimal value of W/H roundness parameter"},
    {"maxwh", PAR_DOUBLE, (void*)&theconf.maxwh, 0, 1., MAXWH,
     "maximal value of W/H roundness parameter"},
    {"ndilat", PAR_INT, (void*)&theconf.Ndilations, 0, 1., MAX_NDILAT,
     "amount of dilations on binarized image"},
    {"neros", PAR_INT, (void*)&theconf.Nerosions, 0, 1., MAX_NEROS,
     "amount of erosions after dilations"},
    {"xoffset", PAR_INT, (void*)&theconf.xoff, 0, 0., MAX_OFFSET,
     "X offset of subimage"},
    {"yoffset", PAR_INT, (void*)&theconf.yoff, 0, 0., MAX_OFFSET,
     "Y offset of subimage"},
    {"width", PAR_INT, (void*)&theconf.width, 0, 0., MAX_OFFSET,
     "subimage width"},
    {"height", PAR_INT, (void*)&theconf.height, 0, 0., MAX_OFFSET,
     "subimage height"},
    {"equalize", PAR_INT, (void*)&theconf.equalize, 0, 0., 1.,
     "make histogram equalization"},
    {"expmethod", PAR_INT, (void*)&theconf.expmethod, 0, 0., 1.,
     "0 - automatic calculation of gain and exptime, 1 - use fixed values"},
    {"naverage", PAR_INT, (void*)&theconf.naverage, 0, 1., NAVER_MAX,
     "calculate mean position by N images"},
    {"umax", PAR_INT, (void*)&theconf.maxUpos, 0, -MAXSTEPS, MAXSTEPS,
     "maximal value of steps on U semi-axe"},
    {"umin", PAR_INT, (void*)&theconf.minUpos, 0, -MAXSTEPS, MAXSTEPS,
     "minimal value of steps on U semi-axe"},
    {"vmax", PAR_INT, (void*)&theconf.maxVpos, 0, -MAXSTEPS, MAXSTEPS,
     "maximal value of steps on V semi-axe"},
    {"vmin", PAR_INT, (void*)&theconf.minVpos, 0, -MAXSTEPS, MAXSTEPS,
     "minimal value of steps on V semi-axe"},
    {"focmax", PAR_INT, (void*)&theconf.maxFpos, 0, 0., Fmaxsteps,
     "maximal focus position in microsteps"},
    {"focmin", PAR_INT, (void*)&theconf.minFpos, 0, -Fmaxsteps, 0.,
     "minimal focus position in microsteps"},
    {"stpservport", PAR_INT, (void*)&theconf.stpserverport, 0, 0., 65535.,
     "port number of steppers' server"},
    {"Kxu", PAR_DOUBLE, (void*)&theconf.Kxu, 0, KUVMIN, KUVMAX,
     "dU = Kxu*dX + Kyu*dY"},
    {"Kyu", PAR_DOUBLE, (void*)&theconf.Kyu, 0, KUVMIN, KUVMAX,
     "dU = Kxu*dX + Kyu*dY"},
    {"Kxv", PAR_DOUBLE, (void*)&theconf.Kxv, 0, KUVMIN, KUVMAX,
     "dV = Kxv*dX + Kyv*dY"},
    {"Kyv", PAR_DOUBLE, (void*)&theconf.Kyv, 0, KUVMIN, KUVMAX,
     "dV = Kxv*dX + Kyv*dY"},
    {"xtarget", PAR_DOUBLE, (void*)&theconf.xtarget, 0, 1., MAX_OFFSET,
     "X coordinate of target position"},
    {"ytarget", PAR_DOUBLE, (void*)&theconf.ytarget, 0, 1., MAX_OFFSET,
     "Y coordinate of target position"},
    {"pidup", PAR_DOUBLE, (void*)&theconf.PIDU_P, 0, PID_P_MIN, PID_P_MAX,
     "U axis P PID parameter"},
    {"pidui", PAR_DOUBLE, (void*)&theconf.PIDU_I, 0, PID_I_MIN, PID_I_MAX,
     "U axis I PID parameter"},
    {"pidud", PAR_DOUBLE, (void*)&theconf.PIDU_D, 0, PID_I_MIN, PID_I_MAX,
     "U axis D PID parameter"},
    {"pidvp", PAR_DOUBLE, (void*)&theconf.PIDV_P, 0, PID_P_MIN, PID_P_MAX,
     "V axis P PID parameter"},
    {"pidvi", PAR_DOUBLE, (void*)&theconf.PIDV_I, 0, PID_I_MIN, PID_I_MAX,
     "V axis I PID parameter"},
    {"pidvd", PAR_DOUBLE, (void*)&theconf.PIDV_D, 0, PID_I_MIN, PID_I_MAX,
     "V axis D PID parameter"},
    {"eqthrowpart", PAR_DOUBLE, (void*)&theconf.throwpart, 0, 0., MAX_THROWPART,
     "a part of low intensity pixels to throw away when histogram equalized"},
    {"minexp", PAR_DOUBLE, (void*)&theconf.minexp, 0, 0., EXPOS_MAX,
     "minimal exposition time"},
    {"maxexp", PAR_DOUBLE, (void*)&theconf.maxexp, 0, 0., EXPOS_MAX,
     "maximal exposition time"},
    {"exptime", PAR_DOUBLE, (void*)&theconf.exptime, 0, EXPOS_MIN, EXPOS_MAX,
     "exposition time (you can change it only when expmethod==1)"},
    {"intensthres", PAR_DOUBLE, (void*)&theconf.intensthres, 0, 0., 1.,
     "threshold by total object intensity when sorting = |I1-I2|/(I1+I2)"},
    {"gain", PAR_DOUBLE, (void*)&theconf.gain, 0, GAIN_MIN, GAIN_MAX,
     "gain value in manual mode"},
    {"brightness", PAR_DOUBLE, (void*)&theconf.brightness, 0, BRIGHT_MIN, BRIGHT_MAX,
     "brightness value"},
    {"starssort", PAR_INT, (void*)&theconf.starssort, 0, 0., 1.,
     "stars sorting algorithm: by distance from target (0) or by intensity (1)"},
    {"medfilt", PAR_INT, (void*)&theconf.medfilt, 0, 0., 1.,
     "use median filter (1) or not (0)"},
    {"medseed", PAR_INT, (void*)&theconf.medseed, 0, MIN_MEDIAN_SEED, MAX_MEDIAN_SEED,
     "median filter radius"},
    {"fixedbg", PAR_INT, (void*)&theconf.fixedbkg, 0, 0., 1.,
     "don't calculate background, use fixed value instead"},
    {"background", PAR_INT, (void*)&theconf.background, 0, FIXED_BK_MIN, FIXED_BK_MAX,
     "fixed background level"},
    {"writedi", PAR_INT, (void*)&theconf.writedebugimgs, 0, 0., 1.,
     "write debug images (binary/erosion/opening)"},
    {NULL,  0,  NULL, 0, 0., 0., NULL}
};

// return pointer to buff with size l filled with list of all commands (+help messages + low/high values)
char *get_cmd_list(char *buff, int l){
    if(!buff || l < 1) return NULL;
    int L = l;
    char *ptr = buff;
    if(!isSorted){
        qsort(parvals, sizeof(parvals)/sizeof(confparam) - 1, sizeof(confparam), compConfVals);
        isSorted = 1;
    }
    confparam *par = parvals;
    while(L > 0 && par->name){
        int s = snprintf(ptr, L, "%s=newval - %s (from %g to %g)\n", par->name, par->help, par->minval, par->maxval);
        if(s < 1) break;
        L -= s; ptr += s;
        ++par;
    }
    return buff;
}

static char *omitspaces(char *v){
    if(!v) return NULL;
    while(*v && (*v == ' ' || *v == '\t')) ++v;
    int l = strlen(v);
    while(l-- && (v[l] == ' ' || v[l] == '\t')) v[l] = 0;
    return v;
}

// Read key/value from `pair` (key = value)
// RETURNed value should be FREEd
char *get_keyval(const char *pair, char value[128]){
    char key[128];
    char val[128];
    //if(!pair || !*pair) return strdup("#"); // empty line
    if(!pair || !*pair){
        //DBG("Empty");
        return NULL; // empty line
    }
    char *keyptr = key, *valptr = val;
    int x = sscanf(pair, "%127[^=]=%127[^\n]%*c", key, val);
    //DBG("x=%d, key='%s', val='%s'", x, key, val);
    if(x < 0 || x > 2) return NULL; // wrong data or EOF
    //if(x == 0) return strdup("#"); // empty line
    if(x == 0) return NULL; // empty line
    keyptr = omitspaces(key);
    if(x == 2){ // param = value
        valptr = omitspaces(val);
        sprintf(value, "%s", valptr);
        return strdup(keyptr);
    }
    if(*keyptr == '#' || *keyptr == '%'){ // comment
        *value = 0;
        //return strdup("#");
        return NULL;
    }
    return NULL;
}

// Read key/value from file
static char *read_key(FILE *file, char value[128]){
    char *line = NULL;
    size_t n = 0;
    int got = getline(&line, &n, file);
    if(!line) return NULL;
    if(got < 0){
        free(line);
        return NULL;
    }
    char *kv = get_keyval(line, value);
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

// find configuration record for getter
confparam *find_key(const char *key){
    if(!key) return NULL;
    confparam *par = parvals;
    while(par->name){
        if(strcmp(key, par->name) == 0) return par;
        ++par;
    }
    return NULL;
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
    confparam *par = find_key(key);
    if(!par) return NULL;
    //DBG("key='%s', par->name='%s'", key, par->name);
    result->type = par->type;
    switch(par->type){
        case PAR_INT:
            //DBG("INTEGER");
            if(!str2int(&result->val.intval, val)){
                WARNX("Wrong integer value '%s' of parameter '%s'", val, key);
                return NULL;
            }
            if(result->val.intval < par->minval || result->val.intval > par->maxval){
                WARNX("Value (%d) of parameter %s out of range %g..%g",
                       result->val.intval, par->name, par->minval, par->maxval);
                break;
            } else return par;
        break;
        case PAR_DOUBLE:
            //DBG("DOUBLE");
            if(!sl_str2d(&result->val.dblval, val)){
                WARNX("Wrong double value '%s' of parameter '%s'", val, key);
                return NULL;
            }
            //DBG("val: %g, min: %g, max: %g", result->val.dblval, par->minval, par->maxval);
            if(result->val.dblval < par->minval || result->val.dblval > par->maxval){
                WARNX("Value (%g) of parameter %s out of range %g..%g",
                       result->val.dblval, par->name, par->minval, par->maxval);
                break;
            } else return par;
        break;
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
    if(conffile){ free(conffile); conffile = NULL; }
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
            free(key);
            key = NULL;
            continue; // comment
        }
        //DBG("key: %s", key);
        key_value kv;
        par = chk_keyval(key, val, &kv);
        if(!par){
            WARNX("Parameter '%s' is wrong or out of range", key);
            free(key);
            key = NULL;
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
        free(key);
        key = NULL;
    }
    fclose(f);
    int found = 0;
    par = parvals;
    while(par->name){
        //DBG("parvals[]={%s, %d, %d(%g), %d}", par->name, par->type, *((int*)par->ptr), *((double*)par->ptr), par->got);
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
    if(!isSorted){
        qsort(parvals, sizeof(parvals)/sizeof(confparam) - 1, sizeof(confparam), compConfVals);
        isSorted = 1;
    }
    confparam *par = parvals;
    while(par->name){
        par->got = 1;
        switch(par->type){
            case PAR_INT:
                fprintf(f, "%s = %d\n", par->name, *((int*)par->ptr));
                //DBG("%s = %d", par->name, *((int*)par->ptr));
            break;
            case PAR_DOUBLE:
                fprintf(f, "%s = %.3f\n", par->name, *((double*)par->ptr));
                //DBG("%s = %.3f", par->name, *((double*)par->ptr));
            break;
        }
        ++par;
    }
    DBG("%s saved", confname);
    LOGDBG("Configuration file '%s' saved (my PID=%zd)", confname, getpid());
    fclose(f);
    return TRUE;
}

// return buffer filled with current configuration
char *listconf(const char *messageid, char *buf, int buflen){
    int L;
    char *ptr = buf;
    if(!isSorted){
        qsort(parvals, sizeof(parvals)/sizeof(confparam) - 1, sizeof(confparam), compConfVals);
        isSorted = 1;
    }
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
