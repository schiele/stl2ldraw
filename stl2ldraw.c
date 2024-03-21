/*
 * stl2ldraw --- convert stl files to LDraw dat files
 * Copyright (C) 2024  Robert Schiele <rschiele@gmail.com>
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define _USE_MATH_DEFINES

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

static const float eps=0.0000001;
static const float pteps=0.0001;

struct stltriangle {
    float n[3];
    float v[3][3];
    uint16_t a;
};

#define NOTRIANGLE 0xffffffff
#define AMBTRIANGLE 0xfffffffe
struct edge {
    uint32_t p[4];
    uint32_t t[2];
    float angle;
};

struct triangle {
    uint32_t p[3];
    uint32_t e[3];
    float n[3];
    bool done;
};

struct data {
    char h[81];
    float* p;
    struct edge* e;
    struct triangle* t;
    uint32_t pc;
    uint32_t ec;
    uint32_t tc;
};

static float dot(float* v1, float* v2) {
    return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

static void cross(float* res, float* v1, float* v2) {
    res[0] = v1[1] * v2[2] - v1[2] * v2[1];
    res[1] = v1[2] * v2[0] - v1[0] * v2[2];
    res[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

static float mag(float* v) {
    return sqrtf(dot(v, v));
}

static void vec(float* res, float* v1, float* v2) {
    for(int i=0; i<3; ++i)
        res[i] = v1[i]-v2[i];
}

static void calcnormal(float* n, struct stltriangle* t) {
    float v1[3];
    float v2[3];
    vec(v1, t->v[1], t->v[0]);
    vec(v2, t->v[2], t->v[0]);
    cross(n, v1, v2);
    float m = mag(n);
    if (m)
        for(int i=0; i<3; ++i)
            n[i] /= m;
    else
        fprintf(stderr, "degenerated face\n");
    if(fabsf(n[0]-t->n[0]) > eps || fabsf(n[1]-t->n[1]) > eps || fabsf(n[2]-t->n[2]) > eps)
        fprintf(stderr, "fixed normal: %g %g %g (was %g %g %g)\n", n[0], n[1], n[2], t->n[0], t->n[1], t->n[2]);
}

static uint32_t storept(struct data* d, float* p) {
    for (uint32_t i=0; i<d->pc; ++i) {
        if (fabsf(d->p[i*3+0]-p[0]) < pteps && fabsf(d->p[i*3+1]-p[1]) < pteps && fabsf(d->p[i*3+2]-p[2]) < pteps)
            return i;
    }
    memcpy(d->p+3*d->pc, p, 3*sizeof(float));
    return d->pc++;
}

static uint32_t storeedge(struct data* d, uint32_t p0, uint32_t p1, uint32_t p2) {
    for (uint32_t i=0; i<d->ec; ++i) {
        if (p0 == d->e[i].p[0] && p1 == d->e[i].p[1]) {
            d->e[i].t[0] = (d->e[i].t[0] == NOTRIANGLE) ? d->tc : AMBTRIANGLE;
            d->e[i].p[2] = p2;
            return i;
        }
        if (p0 == d->e[i].p[1] && p1 == d->e[i].p[0]) {
            d->e[i].t[1] = (d->e[i].t[1] == NOTRIANGLE) ? d->tc : AMBTRIANGLE;
            d->e[i].p[3] = p2;
            return i;
        }
    }
    d->e[d->ec].p[0] = p0;
    d->e[d->ec].p[1] = p1;
    d->e[d->ec].p[2] = p2;
    d->e[d->ec].t[0] = d->tc;
    d->e[d->ec].t[1] = NOTRIANGLE;
    return d->ec++;
}

static float angle(struct data* d, uint32_t i) {
    if (d->e[i].t[0]==AMBTRIANGLE || d->e[i].t[1]==AMBTRIANGLE) {
        fprintf(stderr, "Non-manifold edge %u\n", i);
        return 1000;
    }
    if (d->e[i].t[0]==NOTRIANGLE || d->e[i].t[1]==NOTRIANGLE) {
        fprintf(stderr, "Open edge %u\n", i);
        return 360;
    }
    float* v1 = d->t[d->e[i].t[0]].n;
    float* v2 = d->t[d->e[i].t[1]].n;
    float vc[3];
    vec(vc, d->p+d->e[i].p[3]*3, d->p+d->e[i].p[2]*3);
    return ((dot(v1, vc)>0) ? -1 : 1) * acosf(fmin(dot(v1, v2) / mag(v1) / mag(v2), 1.0)) * (180.0 / M_PI);
}

static bool readword(FILE* file, char* word) {
    long pos = ftell(file);
    char c;
    int wl = strlen(word);
    int wp = 0;
    while (fread(&c, 1, 1, file) == 1) {
        if (isspace(c)) {
            if (wl == wp)
                return true;
            else
                continue;
        }
        if (c == word[wp++])
            continue;
        fseek(file, pos, SEEK_SET);
        return false;
    }
    fseek(file, pos, SEEK_SET);
    return false;
}

static float readnum(FILE* file) {
    char buf[81];
    char c;
    int wp = 0;
    while (fread(&c, 1, 1, file) == 1) {
        if (isspace(c)) {
            if (wp) {
                buf[wp] = 0;
                return atof(buf);
            } else
                continue;
        }
        if (wp < 80) {
            buf[wp++] = c;
            continue;
        } else {
            fprintf(stderr, "Number too long.\n");
            return nanf("");
        }
    }
    return nanf("");
}

static bool readendline(FILE* file, char* buf) {
    char c;
    int wp = 0;
    while (fread(&c, 1, 1, file) == 1) {
        if (c == '\n') {
            buf[wp] = 0;
            return true;
        }
        if (wp < 80)
            buf[wp++] = c;
    }
    return false;
}

static void printpnt(FILE* file, float* p, bool conv) {
    if (conv)
        fprintf(file, " %g %g %g", p[0]*2.5, -p[2]*2.5, p[1]*2.5);
    else
        fprintf(file, " %g %g %g", p[0], p[1], p[2]);
}


static bool isconvexquad(struct data* d, uint32_t t, int e) {
    if (fabsf(d->e[d->t[t].e[e]].angle) > eps)
        return false;
    uint32_t p[4] = {
        d->t[t].p[(e+0)%3],
        d->e[d->t[t].e[e]].p[(d->e[d->t[t].e[e]].t[0] == t)?3:2],
        d->t[t].p[(e+1)%3],
        d->t[t].p[(e+2)%3]};
    float v[4*3];
    float c[4*3];
    for (int i=0; i<4; ++i)
        vec(v + i*3, d->p + p[(i+1)%4]*3, d->p + p[i]*3);
    for (int i=0; i<4; ++i)
        cross(c + i*3, v + i*3, v + ((i+1)%4)*3);
    return dot(c, c + 3) * dot(c + 6, c + 9) > eps;
}

static void swap(void* p, int n) {
    (void)p;
    (void)n;
#ifndef _WIN32
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    char* c = p;
    char buf;
    for (int i=0; i<n/2; ++i) {
        buf = c[i];
        c[i] = c[n-1-i];
        c[n-1-i] = buf;
    }
#endif
#endif
}

static struct data* allocdata(uint32_t n) {
    struct data* d = calloc(1, sizeof(struct data));
    d->p = calloc(3*3*n, sizeof(float));
    d->e = 0;
    d->t = calloc(n, sizeof(struct triangle));
    d->pc = 0;
    d->ec = 0;
    d->tc = 0;
    return d;
}

static void addtriangle(struct data* d, struct stltriangle* t) {
    calcnormal(d->t[d->tc].n, t);
    for(int j=0; j<3; ++j)
        d->t[d->tc].p[j] = storept(d, t->v[j]);
    ++(d->tc);
}

static void calcedges(struct data* d) {
    d->ec = 0;
    if (d->e)
        free(d->e);
    d->e = calloc(3*d->tc, sizeof(struct edge));
    for (uint32_t i=0; i<d->tc; ++i)
        for(int j=0; j<3; ++j)
            d->t[i].e[j] = storeedge(d, d->t[i].p[j], d->t[i].p[(j+1)%3], d->t[i].p[(j+2)%3]);
    for (uint32_t i=0; i<d->ec; ++i)
        d->e[i].angle = angle(d, i);
}

static struct data* readstl(char* filename) {
    uint32_t tricnt;
    FILE* file;
    struct stltriangle t;
    file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error opening the file.\n");
        return 0;
    }
    if (fseek(file, 0, SEEK_END) < 0) {
        fprintf(stderr, "Error seeking to end of file.\n");
        return 0;
    }
    long filesize = ftell(file);
    fseek(file, 80, SEEK_SET);
    if (fread(&tricnt, 4, 1, file) != 1) {
        fprintf(stderr, "Error reading tricnt.\n");
        return 0;
    }
    swap(&tricnt, sizeof(tricnt));
    struct data* d;
    if (filesize == tricnt*50+84) {
        d = allocdata(tricnt);
        fseek(file, 0, SEEK_SET);
        if (fread(d->h, 80, 1, file) != 1) {
            fprintf(stderr, "Error reading header.\n");
            return 0;
        }
        d->h[80] = 0;
        fseek(file, 84, SEEK_SET);
        for (uint32_t i=0; i<tricnt; ++i) {
            if (fread(&t, 50, 1, file) != 1) {
                fprintf(stderr, "Error reading triangle %u.\n", i);
                return 0;
            }
            for (int j=0; j<48; j+=4)
                swap((&t)+j, sizeof(float));
            swap(&t.a, sizeof(uint16_t));
            addtriangle(d, &t);
        }
    } else {
        fseek(file, 0, SEEK_SET);
        d = allocdata(filesize/86);
        if (!(readword(file, "solid") && readendline(file, d->h))) {
            fprintf(stderr, "File must start with 'solid' line.\n");
            return 0;
        }
        while (!readword(file, "endsolid")) {
            if (!(readword(file, "facet") && readword(file, "normal"))) {
                fprintf(stderr, "'facet normal' missing.\n");
                return 0;
            }
            for (int i = 0; i<3; ++i)
                t.n[i] = readnum(file);
            if (!(readword(file, "outer") && readword(file, "loop"))) {
                fprintf(stderr, "'outer loop' missing.\n");
                return 0;
            }
            for (int j = 0; j<3; ++j) {
                if (!readword(file, "vertex")) {
                    fprintf(stderr, "'vertex' missing.\n");
                    return 0;
                }
                for (int i = 0; i<3; ++i)
                    t.v[j][i] = readnum(file);
            }
            if (!readword(file, "endloop")) {
                fprintf(stderr, "'endloop' missing.\n");
                return 0;
            }
            if (!readword(file, "endfacet")) {
                fprintf(stderr, "'endfacet' missing.\n");
                return 0;
            }
            addtriangle(d, &t);
        }
    }
    fclose(file);
    calcedges(d);
    return d;
}

#ifdef EXTRADEBUG
static void writebinstl(struct data* d, char* filename) {
    FILE* file;
    file = fopen(filename, "w");
    if (file == NULL) {
        fprintf(stderr, "Error opening the file.\n");
        return;
    }
    fwrite(d->h, 80, 1, file);
    uint32_t tricnt = d->tc;
    swap(&tricnt, sizeof(uint32_t));
    fwrite(&tricnt, 4, 1, file);
    for (uint32_t i=0; i<d->tc; ++i) {
        struct stltriangle t;
        for (int j=0; j<3; ++j)
            t.n[j] = d->t[i].n[j];
        for (int j=0; j<3; ++j)
            for (int k=0; k<3; ++k)
                t.v[j][k] = d->p[d->t[i].p[j]*3+k];
        t.a = 0;
        for (int j=0; j<48; j+=4)
            swap((&t)+j, sizeof(float));
        swap(&t.a, sizeof(uint16_t));
        fwrite(&t, 50, 1, file);
    }
    fclose(file);
}

static void writeasciistl(struct data* d, char* filename) {
    FILE* file;
    file = fopen(filename, "w");
    if (file == NULL) {
        fprintf(stderr, "Error opening the file.\n");
        return;
    }
    char header[81];
    strncpy(header, d->h, 80);
    header[80] = 0;
    for (int i=0; i<80; ++i)
        if (header[i] && !isprint(header[i]))
            header[i] = '_';
    fprintf(file, "solid %s\n", header);
    for (uint32_t i=0; i<d->tc; ++i) {
        fprintf(file, "  facet normal");
        printpnt(file, d->t[i].n, false);
        fprintf(file, "\n    outer loop");
        for (int j = 0; j<3; ++j) {
            fprintf(file, "\n      vertex");
            printpnt(file, d->p + d->t[i].p[j]*3, false);
        }
        fprintf(file, "\n    endloop\n  endfacet\n");
    }
    fprintf(file, "endsolid %s\n", header);
    fclose(file);
}
#endif

static void writeldraw(struct data* d, char* filename) {
    FILE* file;
    file = fopen(filename, "w");
    if (file == NULL) {
        fprintf(stderr, "Error opening the file.\n");
        return;
    }
    char header[81];
    strncpy(header, d->h, 80);
    header[80] = 0;
    for (int i=0; i<80; ++i)
        if (header[i] && !isprint(header[i]))
            header[i] = ' ';
    fprintf(file, "0 %s\n", header);
    fprintf(file, "0 BFC CERTIFY CCW\n");
    for (uint32_t i=0; i<d->tc; ++i)
        d->t[i].done = false;
    for (uint32_t i=0; i<d->tc; ++i) {
        if (!d->t[i].done) {
            int ind;
            uint32_t op = 0;
            for (ind=0; ind<3; ++ind)
                if (isconvexquad(d, i, ind))
                    break;
            if (ind < 3) {
                if (d->e[d->t[i].e[ind]].t[0] == i) {
                    op = d->e[d->t[i].e[ind]].p[3];
                    d->t[d->e[d->t[i].e[ind]].t[1]].done = true;
                } else {
                    op = d->e[d->t[i].e[ind]].p[2];
                    d->t[d->e[d->t[i].e[ind]].t[0]].done = true;
                }
            }
            //fprintf(file, "%u %u", (ind==3)?3:4, i%16);
            fprintf(file, "%u 16", (ind==3)?3:4);
            for (int j = 0; j<3; ++j) {
                printpnt(file, d->p + d->t[i].p[j]*3, true);
                if (ind==j)
                    printpnt(file, d->p + op*3, true);
            }
            fprintf(file, "\n");
            d->t[i].done = true;
        }
    }
    for (uint32_t i=0; i<d->ec; ++i) {
        if (fabsf(d->e[i].angle) >= 25.) {
            fprintf(file, "2 24");
            for (int j = 0; j<2; ++j)
                printpnt(file, d->p + d->e[i].p[j]*3, true);
            fprintf(file, "\n");
        } else if (d->e[i].angle >= eps) {
            fprintf(file, "5 24");
            for (int j = 0; j<4; ++j)
                printpnt(file, d->p + d->e[i].p[j]*3, true);
            fprintf(file, "\n");
        }
    }
    fclose(file);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s INPUTFILENAME OUTPUTFILENAME\n", argv[0]);
        return 1;
    }
    struct data* d = readstl(argv[1]);
    if (!d)
        return 1;
    writeldraw(d, argv[2]);
#ifdef EXTRADEBUG
    writebinstl(d, "bin.stl");
    writeasciistl(d, "ascii.stl");
#endif
    return 0;
}

// vim: set tabstop=4 shiftwidth=4 expandtab :
