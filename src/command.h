// script binding functionality


enum { ID_VAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_CCOMMAND, ID_ALIAS };

struct identstack
{
    char *action;
    identstack *next;
};

union identval
{
    int i;      // ID_VAR
    float f;    // ID_FVAR
    char *s;    // ID_SVAR
};

union identvalptr
{
    int *i;   // ID_VAR
    float *f; // ID_FVAR
    char **s; // ID_SVAR
};

struct ident
{
    int type;           // one of ID_* above
    const char *name;
    union
    {
        int minval;    // ID_VAR
        float minvalf; // ID_FVAR
    };
    union
    {
        int maxval;    // ID_VAR
        float maxvalf; // ID_FVAR
    };
    union
    {
        void (__cdecl *fun)(); // ID_VAR, ID_COMMAND, ID_CCOMMAND
        identstack *stack;     // ID_ALIAS
    };
    union
    {
        const char *narg; // ID_COMMAND, ID_CCOMMAND
        char *action;     // ID_ALIAS
        identval val;     // ID_VAR, ID_FVAR, ID_SVAR
    };
    union
    {
        void *self;           // ID_COMMAND, ID_CCOMMAND 
        char *isexecuting;    // ID_ALIAS
    };
    identvalptr storage; // ID_VAR, ID_FVAR, ID_SVAR
    int flags;
    
    ident() {}
    // ID_VAR
    ident(int t, const char *n, int m, int c, int x, int *s, void *f = NULL, int flags = 0)
        : type(t), name(n), minval(m), maxval(x), fun((void (__cdecl *)())f), flags(flags)
    { val.i = c; storage.i = s; }
    // ID_FVAR
    ident(int t, const char *n, float m, float c, float x, float *s, void *f = NULL, int flags = 0)
        : type(t), name(n), minvalf(m), maxvalf(x), fun((void (__cdecl *)())f), flags(flags)
    { val.f = c; storage.f = s; }
    // ID_SVAR
    ident(int t, const char *n, char *c, char **s, void *f = NULL, int flags = 0)
        : type(t), name(n), fun((void (__cdecl *)())f), flags(flags)
    { val.s = c; storage.s = s; }
    // ID_ALIAS
    ident(int t, const char *n, char *a, int flags)
        : type(t), name(n), stack(NULL), action(a), flags(flags) {}
    // ID_COMMAND, ID_CCOMMAND
    ident(int t, const char *n, const char *narg, void *f = NULL, void *s = NULL, int flags = 0)
        : type(t), name(n), fun((void (__cdecl *)(void))f), narg(narg), self(s), flags(flags) {}

    virtual ~ident() {}        

    ident &operator=(const ident &o) { memcpy(this, &o, sizeof(ident)); return *this; }        // force vtable copy, ugh
    
    virtual void changed() { if(fun) fun(); }
};

extern char *path(const char *s, bool copy);
extern char *loadfile(const char *fn);
extern void addident(const char *name, ident *id);
extern void intret(int v);
extern const char *floatstr(float v);
extern void floatret(float v);
extern void result(const char *s);
extern int variable(const char *name, int min, int cur, int max, int *storage, void (*fun)(), int flags = 0);
extern float fvariable(const char *name, float min, float cur, float max, float *storage, void (*fun)(), int flags = 0);
extern char *svariable(const char *name, const char *cur, char **storage, void (*fun)(), int flags = 0);
extern void setvar(const char *name, int i, bool dofunc = false);
extern void setfvar(const char *name, float f, bool dofunc = false);
extern void setsvar(const char *name, const char *str, bool dofunc = false);
extern bool identexists(const char *name);
extern ident *getident(const char *name);
extern bool addcommand(const char *name, void (*fun)(), const char *narg);
extern int execute(const char *p);
extern char *executeret(const char *p);
extern void exec(const char *cfgfile);
extern bool execfile(const char *cfgfile);
extern void alias(const char *name, const char *action);
extern const char *getalias(const char *name);
extern void explodelist(const char *s, Vector<char *> &elems);

// nasty macros for registering script functions, abuses globals to avoid excessive infrastructure
#define COMMANDN(name, fun, nargs) static bool __dummy_##fun = addcommand(#name, (void (*)())fun, nargs)
#define COMMAND(name, nargs) COMMANDN(name, name, nargs)

#define _VAR(name, global, min, cur, max)  int global = variable(#name, min, cur, max, &global, NULL)
#define VARN(name, global, min, cur, max) _VAR(name, global, min, cur, max)
#define VAR(name, min, cur, max) _VAR(name, name, min, cur, max)
#define _VARF(name, global, min, cur, max, body)  void var_##name(); int global = variable(#name, min, cur, max, &global, var_##name); void var_##name() { body; }
#define VARFN(name, global, min, cur, max, body) _VARF(name, global, min, cur, max, body)
#define VARF(name, min, cur, max, body) _VARF(name, name, min, cur, max, body)

#define _FVAR(name, global, min, cur, max) float global = fvariable(#name, min, cur, max, &global, NULL)
#define FVARN(name, global, min, cur, max) _FVAR(name, global, min, cur, max)
#define FVAR(name, min, cur, max) _FVAR(name, name, min, cur, max)
#define _FVARF(name, global, min, cur, max, body) void var_##name(); float global = fvariable(#name, min, cur, max, &global, var_##name); void var_##name() { body; }
#define FVARFN(name, global, min, cur, max, body) _FVARF(name, global, min, cur, max, body)
#define FVARF(name, min, cur, max, body) _FVARF(name, name, min, cur, max, body)

#define _SVAR(name, global, cur) char *global = svariable(#name, cur, &global, NULL)
#define SVARN(name, global, cur) _SVAR(name, global, cur)
#define SVAR(name, cur) _SVAR(name, name, cur)
#define _SVARF(name, global, cur, body) void var_##name(); char *global = svariable(#name, cur, &global, var_##name); void var_##name() { body; }
#define SVARFN(name, global, cur, body) _SVARF(name, global, cur, body)
#define SVARF(name, cur, body) _SVARF(name, name, cur, body)

// new style macros, have the body inline, and allow binds to happen anywhere, even inside class constructors, and access the surrounding class
#define _COMMAND(idtype, tv, n, g, proto, b) \
    struct cmd_##n : ident \
    { \
        cmd_##n(void *self = NULL) : ident(idtype, #n, g, (void *)run, self) \
        { \
            addident(name, this); \
        } \
        static void run proto { b; } \
    } icom_##n tv
#define ICOMMAND(n, g, proto, b) _COMMAND(ID_COMMAND, , n, g, proto, b)
#define CCOMMAND(n, g, proto, b) _COMMAND(ID_CCOMMAND, (this), n, g, proto, b)
 
#define _IVAR(n, m, c, x, b, p) \
    struct var_##n : ident \
    { \
        var_##n() : ident(ID_VAR, #n, m, c, x, &val.i, NULL, p) \
        { \
            addident(name, this); \
        } \
        int operator()() { return val.i; } \
        b \
    } n
#define IVAR(n, m, c, x)  _IVAR(n, m, c, x, )
#define IVARF(n, m, c, x, b) _IVAR(n, m, c, x, void changed() { b; })
//#define ICALL(n, a) { char *args[] = a; icom_##n.run(args); }
//
