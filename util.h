#ifndef __UTIL_H__
#define __UTIL_H__

#ifdef NULL
#undef NULL
#endif
#define NULL 0

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

#ifdef swap
#undef swap
#endif
template<class T>
static inline void swap(T &a, T &b)
{
    T t = a;
    a = b;
    b = t;
}
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
template<class T>
static inline T max(T a, T b)
{
    return a > b ? a : b;
}
template<class T>
static inline T min(T a, T b)
{
    return a < b ? a : b;
}

template<class T> static inline T radians(T x) { return (x*M_PI)/180; }
template<class T> static inline T degrees(T x) { return (x*180)/M_PI; }

template<class T>
static inline T clamp(T val, T minval, T maxval)
{
    return max(minval, min(val, maxval));
}

#define loop(v, m) for(int v = 0; v < int(m); v++)
#define loopi(m) loop(i, m)
#define loopj(m) loop(j, m)
#define loopk(m) loop(k, m)
#define loopl(m) loop(l, m)

#define looprev(v, m) for(int v = int(m)-1; v >= 0; v--)
#define looprevi(m) looprev(i, m)
#define looprevj(m) looprev(j, m)
#define looprevk(m) looprev(k, m)
#define looprevl(m) looprev(l, m)

#ifdef WIN32

#ifndef __GNUC__
#pragma warning (3: 4189)       // local variable is initialized but not referenced
#pragma warning (disable: 4244) // conversion from 'int' to 'float', possible loss of data
#pragma warning (disable: 4267) // conversion from 'size_t' to 'int', possible loss of data
#pragma warning (disable: 4355) // 'this' : used in base member initializer list
#pragma warning (disable: 4996) // 'strncpy' was declared deprecated
#endif

#define strcasecmp _stricmp
#define PATHDIV '\\'
#else
#define __cdecl
#define _vsnprintf vsnprintf
#define PATHDIV '/'
#endif

#define MAXSTRLEN 260
typedef char String[MAXSTRLEN];

inline void formatstring(char *d, const char *fmt, va_list v) { _vsnprintf(d, MAXSTRLEN, fmt, v); d[MAXSTRLEN-1] = 0; }
inline char *copystring(char *d, const char *s, size_t m) { strncpy(d,s,m); d[m-1] = 0; return d; }
inline char *copystring(char *d, const char *s) { return copystring(d,s,MAXSTRLEN); }
inline char *concatstring(char *d, const char *s) { size_t n = strlen(d); return copystring(d+n,s,MAXSTRLEN-n); }

struct FormatString
{
    char *buf;
    FormatString(char *buf): buf(buf) {}
    void operator()(const char* fmt, ...)
    {
        va_list v;
        va_start(v, fmt);
        formatstring(buf, fmt, v);
        va_end(v);
    }
};

#define printstring(d) FormatString((char *)d)
#define defprintstring(d) String d; printstring(d)
#define defprintstringlv(d,last,fmt) String d; { va_list ap; va_start(ap, last); formatstring(d, fmt, ap); va_end(ap); }
#define defpringstringv(d,fmt) defprintstringlv(d,fmt,fmt)

inline char *newstring(size_t l)                { return new char[l+1]; }
inline char *newstring(const char *s, size_t l) { return copystring(newstring(l), s, l+1); }
inline char *newstring(const char *s)           { return newstring(s, strlen(s));          }
inline char *newstringbuf(const char *s)        { return newstring(s, MAXSTRLEN-1);       }

const int islittleendian = 1;
#ifdef SDL_BYTEORDER
#define endianswap16 SDL_Swap16
#define endianswap32 SDL_Swap32
#else
inline ushort endianswap16(ushort n) { return (n<<8) | (n>>8); }
inline uint endianswap32(uint n) { return (n<<24) | (n>>24) | ((n>>8)&0xFF00) | ((n<<8)&0xFF0000); }
#endif
template<class T> inline T endianswap(T n) { union { T t; uint i; } conv; conv.t = n; conv.i = endianswap32(conv.i); return conv.t; }
template<> inline ushort endianswap<ushort>(ushort n) { return endianswap16(n); }
template<> inline short endianswap<short>(short n) { return endianswap16(n); }
template<> inline uint endianswap<uint>(uint n) { return endianswap32(n); }
template<> inline int endianswap<int>(int n) { return endianswap32(n); }
template<class T> inline void endianswap(T *buf, int len) { for(T *end = &buf[len]; buf < end; buf++) *buf = endianswap(*buf); }
template<class T> inline T endiansame(T n) { return n; }
template<class T> inline void endiansame(T *buf, int len) {}
#ifdef SDL_BYTEORDER
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define lilswap endiansame
#define bigswap endianswap
#else
#define lilswap endianswap
#define bigswap endiansame
#endif
#else
template<class T> inline T lilswap(T n) { return *(const uchar *)&islittleendian ? n : endianswap(n); }
template<class T> inline void lilswap(T *buf, int len) { if(!*(const uchar *)&islittleendian) endianswap(buf, len); }
template<class T> inline T bigswap(T n) { return *(const uchar *)&islittleendian ? endianswap(n) : n; }
template<class T> inline void bigswap(T *buf, int len) { if(*(const uchar *)&islittleendian) endianswap(buf, len); }
#endif

#endif

