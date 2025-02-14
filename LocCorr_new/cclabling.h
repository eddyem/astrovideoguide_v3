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

//#define TESTMSGS

#ifdef TESTMSGS
#define TEST(...) printf(__VA_ARGS__)
#else
#define TEST(...)
#endif

size_t Nmax = W*H/4; // max number of 4-connected labels
size_t *assoc = MALLOC(size_t, Nmax); // allocate memory for "remark" array
size_t last_assoc_idx = 1; // last index filled in assoc array
// was: 35
// check table and rename all "oldval" into "newval"
inline void remark(size_t newval, size_t oldval){
    TEST("\tnew = %zd, old=%zd; ", newval, oldval);
    // find the least values
    do{newval = assoc[newval];}while(assoc[newval] != newval);
    do{oldval = assoc[oldval];}while(assoc[oldval] != oldval);
    TEST("\trealnew = %zd, realold=%zd ", newval, oldval);
    // now change larger value to smaller
    if(newval > oldval){
        assoc[newval] = oldval;
        TEST("change %zd to %zd\n", newval, oldval);
    }else{
        assoc[oldval] = newval;
        TEST("change %zd to %zd\n", oldval, newval);
    }
}

for(int y = 0; y < H; ++y){
    bool found = false;
    size_t *ptr = &labels[y*W];
    size_t curmark = 0; // mark of pixel to the left
    for(int x = 0; x < W; ++x, ++ptr){
        if(!*ptr){found = false; continue;} // empty pixel
        size_t U = (y) ? ptr[-W] : 0; // upper mark
        if(found){ // there's a pixel to the left
            if(U && U != curmark){ // meet old mark -> remark one of them in assoc[]
                TEST("(%d, %d): remark %zd --> %zd\n", x, y, U, curmark);
                remark(U, curmark);
                curmark = U; // change curmark to upper mark (to reduce further checks)
            }
        }else{ // new mark -> change curmark
            found = true;
            if(U) curmark = U; // current mark will copy upper value
            else{ // current mark is new value
                curmark = last_assoc_idx++;
                assoc[curmark] = curmark;
                TEST("(%d, %d): new mark=%zd\n", x, y, curmark);
            }
        }
        *ptr = curmark;
    }
}
size_t *indexes = MALLOC(size_t, last_assoc_idx); // new indexes
size_t cidx = 1;
TEST("\n\n\nRebuild indexes\n\n");
for(size_t i = 1; i < last_assoc_idx; ++i){
    TEST("%zd\t%zd ",i,assoc[i]);
    // search new index
    register size_t realnew = i, newval = 0;
    do{
        realnew = assoc[realnew];
        TEST("->%zd ", realnew);
        if(indexes[realnew]){ // find least index
            newval = indexes[realnew];
            TEST("real: %zd ", newval);
            break;
        }
    }while(assoc[realnew] != realnew);
    if(newval){
        TEST(" ==> %zd\n", newval);
        indexes[i] = newval;
        continue;
    }
    TEST("new index %zd\n", cidx);
    // enter next label
    indexes[i] = cidx++;
}
/*
// rebuild indexes
for(size_t i = 1; i < last_assoc_idx; ++i){
    printf("%zd\t%zd ",i,assoc[i]);
    if(i == assoc[i]){
        if(i == cidx){
            printf("- keep\n");
        }else{
            printf("- change to %zd\n", cidx);
            size_t _2change = assoc[i];
            assoc[i] = cidx;
            for(size_t j = i+1; j < last_assoc_idx; ++j){
                if(assoc[j] == _2change){
                    printf("\t%zd\t%zd -> %zd\n", j, assoc[j], cidx);
                    assoc[j] = cidx;
                }
            }
        }
        ++cidx;
    }else printf("\n");
}*/

--cidx; // cidx now is amount of detected objects
DBG("amount after rebuild: %zd", cidx);

#ifdef TESTMSGS
printf("\n\n\nI\tASS[I]\tIDX[I]\n");
for(size_t i = 1; i < last_assoc_idx; ++i)
    printf("%zd\t%zd\t%zd\n",i,assoc[i],indexes[i]);
#endif

int wh = W*H;
OMP_FOR()
for(int i = 0; i < wh; ++i) labels[i] = indexes[labels[i]];

if(Nobj) *Nobj = cidx;

FREE(assoc);
FREE(indexes);
