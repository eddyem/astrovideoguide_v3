/*                                                                                                  geany_encoding=koi8-r
 * cmdlnopts.c - the only function that parse cmdln args and returns glob parameters
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
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#include "cmdlnopts.h"
#include "config.h"
#include "debug.h"
#include "improc.h"

/*
 * here are global parameters initialisation
 */
static int help;
glob_pars *GP = NULL;

//            DEFAULTS
// default global parameters
static glob_pars G = {
    .pidfile = DEFAULT_PIDFILE,
    .configname = DEFAULT_CONFFILE,
    .steppersport = DEFAULT_STEPPERSPORT,
    .throwpart = DEFAULT_THROWPART,
//    .medradius = 1.,
    .ndilations = DEFAULT_DILATIONS,
    .nerosions = DEFAULT_EROSIONS,
    .minarea = DEFAULT_MINAREA,
    .maxarea = DEFAULT_MAXAREA,
    .intensthres = DEFAULT_INTENSTHRES,
    .maxexp = EXPOS_MAX,
    .minexp = EXPOS_MIN,
    .xtarget = -1.,
    .ytarget = -1.,
    .Naveraging = DEFAULT_NAVERAGE,
    .ioport = DEFAULT_IOPORT,
    .outputjpg = DEFAULT_OUTPJPEG,
};

/*
 * Define command line options by filling structure:
 *  name        has_arg     flag    val     type        argptr              help
*/
static myoption cmdlnopts[] = {
// common options
    {"maxexp",  NEED_ARG,   NULL,   0,      arg_double, APTR(&G.maxexp),    _("maximal exposition time (ms), default: 500")},
    {"minexp",  NEED_ARG,   NULL,   0,      arg_double, APTR(&G.minexp),    _("minimal exposition time (ms), default: 0.001")},
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        _("show this help")},
    {"chkconf", NO_ARGS,    NULL,   'C',    arg_int,    APTR(&G.chkconf),   _("check configuration file")},
    {"logfile", NEED_ARG,   NULL,   'l',    arg_string, APTR(&G.logfile),   _("file to save logs (default: none)")},
    {"pidfile", NEED_ARG,   NULL,   'P',    arg_string, APTR(&G.pidfile),   _("pidfile (default: " DEFAULT_PIDFILE ")")},
    {"verbose", NO_ARGS,    NULL,   'v',    arg_none,   APTR(&G.verb),      _("increase verbosity level of log file (each -v increased by 1)")},
    {"input",   NEED_ARG,   NULL,   'i',    arg_string, APTR(&G.inputname), _("file or directory name for monitoring (or grasshopper/basler for capturing)")},
    {"blackp",  NEED_ARG,   NULL,   'b',    arg_double, APTR(&G.throwpart), _("fraction of black pixels to throw away when make histogram eq")},
//    {"radius",  NEED_ARG,   NULL,   'r',    arg_int,    APTR(&G.medradius), _("radius of median filter (r=1 -> 3x3, r=2 -> 5x5 etc.)")},
    {"equalize", NO_ARGS,   NULL,   'e',    arg_int,    APTR(&G.equalize),  _("make historam equalization of saved jpeg")},
    {"ndilat",  NEED_ARG,   NULL,   'D',    arg_int,    APTR(&G.ndilations),_("amount of dilations after thresholding (default: 2)")},
    {"neros",   NEED_ARG,   NULL,   'E',    arg_int,    APTR(&G.nerosions), _("amount of erosions after dilations (default: 2)")},
    {"minarea", NEED_ARG,   NULL,   'I',    arg_int,    APTR(&G.minarea),   _("minimal object pixels amount (default: 400)")},
    {"maxarea", NEED_ARG,   NULL,   'A',    arg_int,    APTR(&G.maxarea),   _("maximal object pixels amount (default: 150000)")},
    {"intthres",NEED_ARG,   NULL,   'T',    arg_double, APTR(&G.intensthres),_("threshold by total object intensity when sorting = |I1-I2|/(I1+I2), default: 0.01")},
    {"xoff",    NEED_ARG,   NULL,   'x',    arg_int,    APTR(&G.xoff),      _("X offset at grabbed image")},
    {"yoff",    NEED_ARG,   NULL,   'y',    arg_int,    APTR(&G.yoff),      _("Y offset at grabbed image")},
    {"width",   NEED_ARG,   NULL,   'W',    arg_int,    APTR(&G.width),     _("grabbed subimage width")},
    {"height",  NEED_ARG,   NULL,   'H',    arg_int,    APTR(&G.height),    _("grabbed subimage height")},
    {"xtarget", NEED_ARG,   NULL,   'X',    arg_double, APTR(&G.xtarget),   _("target point X coordinate")},
    {"ytarget", NEED_ARG,   NULL,   'Y',    arg_double, APTR(&G.ytarget),   _("target point Y coordinate")},
    {"logXY",   NEED_ARG,   NULL,   'L',    arg_string, APTR(&G.logXYname), _("file to log XY coordinates of selected star")},
    {"confname",NEED_ARG,   NULL,   'c',    arg_string, APTR(&G.configname),_("name of configuration file (default: ./loccorr.conf)")},
    {"stpport", NEED_ARG,   NULL,   'S',    arg_string, APTR(&G.steppersport),_("port of local steppers server (default: 4444)")},
    {"naverage",NEED_ARG,   NULL,   'N',    arg_int,    APTR(&G.Naveraging),_("amount of images to average processing (min 2, max 25)")},
    {"ioport",  NEED_ARG,   NULL,   0,      arg_int,    APTR(&G.ioport),    _("port for IO communication")},
    {"jpegout", NEED_ARG,   NULL,   'j',    arg_string, APTR(&G.outputjpg), _("output jpeg file location (default: '" DEFAULT_OUTPJPEG "')")},
   end_option
};

/**
 * Parse command line options and return dynamically allocated structure
 *      to global parameters
 * @param argc - copy of argc from main
 * @param argv - copy of argv from main
 * @return allocated structure with global parameters
 */
glob_pars *parse_args(int argc, char **argv){
    size_t hlen = 1024;
    char helpstring[1024], *hptr = helpstring;
    snprintf(hptr, hlen, "Usage: %%s [args]\n\n\tWhere args are:\n");
    // format of help: "Usage: progname [args]\n"
    change_helpstring(helpstring);
    // parse arguments
    parseargs(&argc, &argv, cmdlnopts);
    if(help) showhelp(-1, cmdlnopts);
    if(argc > 0){
        WARNX("Extra parameters!");
        showhelp(-1, cmdlnopts);
    }
    return &G;
}

