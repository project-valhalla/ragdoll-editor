#ifndef __ENGINE_H__
#define __ENGINE_H__

#ifdef __GNUC__
#define gamma __gamma
#endif

#include <math.h>

#ifdef __GNUC__
#undef gamma
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <assert.h>
#ifdef __GNUC__
#include <new>
#else
#include <new.h>
#endif
#include <time.h>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#endif

#include <SDL.h>
#include <SDL_opengl.h>

#include "util.h"
#include "geom.h"
#include "collection.h"
#include "command.h"

extern GLuint loadtexture(const char *name, int clamp = 0, int *xs = NULL, int *ys = NULL, int *bpp = NULL);

struct font
{
    struct charinfo
    {
        short x, y, w, h;
    };

    char *name;
    GLuint tex;
    Vector<charinfo> chars;
    int defaultw, defaulth;
    int offsetx, offsety, offsetw, offseth;
    int scalex, scaley;
};

extern font *curfont;

#define FONTH (curfont->defaulth)
#define FONTW (curfont->defaultw)

extern bool setfont(const char *name);
extern void gettextres(int &w, int &h);
extern void draw_text(const char *str, int left, int top, int r = 255, int g = 255, int b = 255, int a = 255, int cursor = -1, int maxwidth = -1);
extern void draw_textf(const char *fstr, int left, int top, ...);
extern int text_width(const char *str);
extern void text_bounds(const char *str, int &width, int &height, int maxwidth = -1);
extern int text_visible(const char *str, int hitx, int hity, int maxwidth);
extern void text_pos(const char *str, int cursor, int &cx, int &cy, int maxwidth);

enum
{
    CON_INFO  = 1<<0,
    CON_WARN  = 1<<1,
    CON_ERROR = 1<<2,
    CON_DEBUG = 1<<3,
    CON_INIT  = 1<<4,
    CON_ECHO  = 1<<5
};

const char *getkeyname(int code);
extern const char *addreleaseaction(const char *s);
extern void keypress(int code, bool isdown, int cooked);
extern int rendercommand(int x, int y, int w);
extern int renderconsole(int w, int h);
extern void conoutf(const char *s, ...);
extern void conoutf(int type, const char *s, ...);

extern int lastmillis;

extern void fatal(const char *fmt, ...);

#endif

