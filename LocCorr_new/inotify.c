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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // strlen
#include <sys/inotify.h>
#include <sys/select.h>
#include <unistd.h>

#include "debug.h"
#include "imagefile.h"

static char filenm[FILENAME_MAX];

// handle inotify event IN_CLOSE_WRITE
/**
 * @brief handle_events - handle inotify event IN_CLOSE_WRITE
 * @param name  - directory or file name
 * @param fd    - inotify init descriptor
 * @return 1 if file was changed and ready to be processed, 0 if not changed, -1 if removed
 */
static int changed(const char *name, int fd, uint32_t mask){
    struct inotify_event buf[10] = {0};
    ssize_t len = read(fd, buf, sizeof buf);
    if(len == -1 && errno != EAGAIN){
        WARN("inotify read()");
        return -1;
    }
    DBG("got: %zd", len);
    if(len < 1) return 0; // not ready
    uint32_t bm = buf[0].mask;
    buf[0].mask = 0;
    int i = 9;
    for(; i > -1; --i){ // find last changed file
        DBG("%d: mask=%04x; len=%u", i, buf[i].mask, buf[i].len);
        if(buf[i].mask & IN_IGNORED || buf[i].len < 1) continue;
        DBG("%d: name=%s", i, buf[i].name);
    }
    if(i == -1){
        DBG("NO names found");
        return -1;
    }
    if(bm & mask){
        if(name){
            snprintf(filenm, FILENAME_MAX, "%s/%s", name, buf[i].name); // full path
        }else{
            snprintf(filenm, FILENAME_MAX, "%s", buf[i].name); // file name
        }
        return 1;
    }
    DBG("IN_IGNORED");
    return -1; // IN_IGNORED (file removed)
}

static int initinot(const char *name, int *filed, uint32_t mask){
    static int fd = -1, wd = -1;
    if(wd > -1){
        inotify_rm_watch(fd, wd);
        wd = -1; fd = -1;
    }
    if(fd == -1) fd = inotify_init();//1(IN_NONBLOCK);
    if(fd == -1){
       WARN("inotify_init()");
       return 0;
    }
    if(-1 == (wd = inotify_add_watch(fd, name, mask))){
        WARN("inotify_add_watch()");
        return 0;
    }
    if(filed) *filed = fd;
    return wd;
}

static int watch_any(const char *name, void (*process)(Image*), uint32_t mask){
    int fd = -1, wd = -1;
    while(1){
        if(wd < 1 || fd < 1){
            wd = initinot(name, &fd, mask);
            if(wd < 1){
                usleep(1000);
                continue;
            }
        }
        int ch;
        if(mask == IN_CLOSE_WRITE)
            ch = changed(NULL, fd, mask); // only file name
        else
            ch = changed(name, fd, mask); // full path
        if(ch == -1){ sleep(1); wd = -1; continue; } // removed
        if(ch == 1){ // changed
            if(process){
                Image *I = Image_read(filenm);
                if(!I && (mask & IN_ISDIR)){ // changed file isn't an image
                    DBG("Changed file isn't an image");
                    continue;
                }
                process(I);
            }
        }
    }
    close(fd);
    return 0;
}



int watch_file(const char *name, void (*process)(Image*)){
    DBG("try to watch file %s", name);
    if(!name){
        WARNX("Need filename");
        return 1;
    }
    return watch_any(name, process, IN_CLOSE_WRITE);
}

int watch_directory(char *name, void (*process)(Image*)){
    DBG("try to watch directory %s", name);
    if(!name){
        WARNX("Need directory name");
        return 1;
    }
    int l = strlen(name) - 1;
    if(name[l] == '/') name[l] = 0;
    return watch_any(name, process, IN_CLOSE_WRITE|IN_ISDIR);
}
