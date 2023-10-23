#include "engine.h"

struct cline { char *line; int type, outtime; };
Vector<cline> conlines;

bool saycommandon = false;
String commandbuf;
char *commandaction = NULL, *commandprompt = NULL;
int commandpos = -1;

VARF(maxcon, 10, 200, 1000, { while(conlines.size() > maxcon) delete[] conlines.pop().line; });

void conline(int type, const char *sf)        // add a line to the console buffer
{
    cline cl;
    cl.line = conlines.size()>maxcon ? conlines.pop().line : newstringbuf("");   // constrain the buffer size
    cl.type = type;
    cl.outtime = lastmillis;                       // for how long to keep line on screen
    conlines.insert(0, cl);
    copystring(cl.line, sf);
}

#define CONSPAD (FONTH/3)

void filtertext(char *dst, const char *src, bool whitespace = true, int len = MAXSTRLEN-1)
{
    for(int c = *src; c; c = *++src)
    {
        switch(c)
        {
        case '\f': ++src; continue;
        }
        if(isspace(c) ? whitespace : isprint(c))
        {
            *dst++ = c;
            if(!--len) break;
        }
    }
    *dst = '\0';
}

void conoutfv(int type, const char *fmt, va_list args)
{
    String sf, sp;
    formatstring(sf, fmt, args);
    filtertext(sp, sf);
    puts(sp);
    conline(type, sf);
}

void conoutf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    conoutfv(CON_INFO, fmt, args);
    va_end(args); 
}

void conoutf(int type, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    conoutfv(type, fmt, args);
    va_end(args);
}

int rendercommand(int x, int y, int w)
{
    if(!saycommandon) return 0;

    defprintstring(s)("%s %s", commandprompt ? commandprompt : ">", commandbuf);
    int width, height;
    text_bounds(s, width, height, w);
    y-= height-FONTH;
    draw_text(s, x, y, 0xFF, 0xFF, 0xFF, 0xFF, (commandpos>=0) ? (commandpos+1+(commandprompt?strlen(commandprompt):1)) : strlen(s), w);
    return height;
}

VAR(consize, 0, 10, 100);
VAR(confade, 0, 30, 60);
VAR(confilter, 0, 0xFFFFFF, 0xFFFFFF);

int conskip = 0;

void setconskip(int *n)
{
    int filter = confilter,
        skipped = abs(*n),
        dir = *n < 0 ? -1 : 1;
    conskip = clamp(conskip, 0, conlines.size()-1);
    while(skipped)
    {
        conskip += dir;
        if(!conlines.inrange(conskip))
        {
            conskip = clamp(conskip, 0, conlines.size()-1);
            return;
        }
        if(conlines[conskip].type&filter) --skipped;
    }
}

COMMANDN(conskip, setconskip, "i");

int renderconsole(int w, int h)                   // render buffer taking into account time & scrolling
{
    int conheight = min(FONTH*consize, h*3 - 2*CONSPAD - 2*FONTH/3),
        conwidth = w*3 - 2*CONSPAD - 2*FONTH/3,
        filter = confilter;
    
    int numl = conlines.size(), offset = min(conskip, numl);
    
    if(confade)
    {
        if(!conskip) 
        {
            numl = 0;
            loopvrev(conlines) if(lastmillis-conlines[i].outtime < confade*1000) { numl = i+1; break; }
        } 
        else offset--;
    }
   
    int y = 0;
    loopi(numl) //determine visible height
    {
        // shuffle backwards to fill if necessary
        int idx = offset+i < numl ? offset+i : --offset;
        if(!(conlines[idx].type&filter)) continue;
        char *line = conlines[idx].line;
        int width, height;
        text_bounds(line, width, height, conwidth);
        y += height;
        if(y > conheight) { numl = i; if(offset == idx) ++offset; break; }
    }
    y = CONSPAD+FONTH/3;
    loopi(numl)
    {
        int idx = offset + numl-i-1;
        if(!(conlines[idx].type&filter)) continue;
        char *line = conlines[idx].line;
        draw_text(line, CONSPAD+FONTH/3, y, 0xFF, 0xFF, 0xFF, 0xFF, -1, conwidth);
        int width, height;
        text_bounds(line, width, height, conwidth);
        y += height;
    }
    return (y+CONSPAD+FONTH/3);
}

// keymap is defined externally in keymap.cfg

struct keym
{
    enum
    {
        ACTION_DEFAULT = 0,
        NUMACTIONS
    };
    
    int code;
    char *name;
    char *actions[NUMACTIONS];
    bool pressed;

    keym() : code(-1), name(NULL), pressed(false) { loopi(NUMACTIONS) actions[i] = newstring(""); }
    ~keym() { delete[] name;  loopi(NUMACTIONS) delete[] actions[i]; }
};

Hashtable<int, keym> keyms(128);

void keymap(int *code, char *key)
{
    keym &km = keyms[*code];
    km.code = *code;
    delete[] km.name;
    km.name = newstring(key);
}
    
COMMAND(keymap, "is");

keym *keypressed = NULL;
char *keyaction = NULL;

const char *getkeyname(int code)
{
    keym *km = keyms.access(code);
    return km ? km->name : NULL;
}

void searchbinds(char *action, int type)
{
    Vector<char> names;
    enumerate(keyms, int, code, keym, km,
    {
        if(!strcmp(km.actions[type], action))
        {
            if(names.size()) names.add(' ');
            names.add(km.name, strlen(km.name));
        }
    });
    names.add('\0');
    result(names.getbuf());
}

keym *findbind(char *key)
{
    enumerate(keyms, int, code, keym, km,
    {
        if(!strcasecmp(km.name, key)) return &km;
    });
    return NULL;
}   
    
void getbind(char *key, int type)
{
    keym *km = findbind(key);
    result(km ? km->actions[type] : "");
}   

void bindkey(char *key, char *action, int state, const char *cmd)
{
    keym *km = findbind(key);
    if(!km) { conoutf(CON_ERROR, "unknown key \"%s\"", key); return; }
    char *&binding = km->actions[state];
    if(!keypressed || keyaction!=binding) delete[] binding;
    // trim white-space to make searchbinds more reliable
    while(isspace(*action)) action++;
    int len = strlen(action);
    while(len>0 && isspace(action[len-1])) len--;
    binding = newstring(action, len);
}

ICOMMAND(bind,     "ss", (char *key, char *action), bindkey(key, action, keym::ACTION_DEFAULT, "bind"));
ICOMMAND(getbind,     "s", (char *key), getbind(key, keym::ACTION_DEFAULT));
ICOMMAND(searchbinds,     "s", (char *action), searchbinds(action, keym::ACTION_DEFAULT));

void saycommand(char *init)                         // turns input to the command line on or off
{
    SDL_EnableUNICODE(saycommandon = (init!=NULL));
    copystring(commandbuf, init ? init : "");
    if(commandaction) { delete[] commandaction; commandaction = NULL; }
    if(commandprompt) { delete[] commandprompt; commandprompt = NULL; }
    commandpos = -1;
}

void inputcommand(char *init, char *action, char *prompt)
{
    saycommand(init);
    if(action[0]) commandaction = newstring(action);
    if(prompt[0]) commandprompt = newstring(prompt);
}

COMMAND(saycommand, "C");
COMMAND(inputcommand, "sss");

struct hline
{
    char *buf, *action, *prompt;

    hline() : buf(NULL), action(NULL), prompt(NULL) {}
    ~hline()
    {
        delete[] buf;
        delete[] action;
        delete[] prompt;
    }

    void restore()
    {
        copystring(commandbuf, buf);
        if(commandpos >= (int)strlen(commandbuf)) commandpos = -1;
        if(commandaction) { delete[] commandaction; commandaction = NULL; }
        if(commandprompt) { delete[] commandprompt; commandprompt = NULL; }
        if(action) commandaction = newstring(action);
        if(prompt) commandprompt = newstring(prompt);
    }

    bool shouldsave()
    {
        return strcmp(commandbuf, buf) ||
               (commandaction ? !action || strcmp(commandaction, action) : action!=NULL) ||
               (commandprompt ? !prompt || strcmp(commandprompt, prompt) : prompt!=NULL);
    }
    
    void save()
    {
        buf = newstring(commandbuf);
        if(commandaction) action = newstring(commandaction);
        if(commandprompt) prompt = newstring(commandprompt);
    }

    void run()
    {
        if(action)
        {
            alias("commandbuf", buf);
            execute(action);
        }
        else execute(buf);
    }
};
Vector<hline *> history;
int histpos = 0;

void history_(int *n)
{
    static bool inhistory = true;
    if(!inhistory && history.inrange(*n))
    {
        inhistory = true;
        history[history.size()-*n-1]->run();
        inhistory = false;
    }
}

COMMANDN(history, history_, "i");

struct releaseaction
{
    keym *key;
    char *action;
};
Vector<releaseaction> releaseactions;

const char *addreleaseaction(const char *s)
{
    if(!keypressed) return NULL;
    releaseaction &ra = releaseactions.add();
    ra.key = keypressed;
    ra.action = newstring(s);
    return keypressed->name;
}

void onrelease(char *s)
{
    addreleaseaction(s);
}

COMMAND(onrelease, "s");

void execbind(keym &k, bool isdown)
{
    loopv(releaseactions)
    {
        releaseaction &ra = releaseactions[i];
        if(ra.key==&k)
        {
            if(!isdown) execute(ra.action);
            delete[] ra.action;
            releaseactions.remove(i--);
        }
    }
    if(isdown)
    {
        int state = keym::ACTION_DEFAULT;
        char *&action = k.actions[state][0] ? k.actions[state] : k.actions[keym::ACTION_DEFAULT];
        keyaction = action;
        keypressed = &k;
        execute(keyaction);
        keypressed = NULL;
        if(keyaction!=action) delete[] keyaction;
    }
    k.pressed = isdown;
}

struct completeval
{
    Vector<char *> vals;
};
Hashtable<char *, completeval> completions;

int completesize = 0;
String lastcomplete;

void resetcomplete() { completesize = 0; }

void listcomplete(char *command, char *list)
{
    completeval *exists = completions.access(command);
    if(!exists) exists = &completions[newstring(command)];
    while(exists->vals.size()) delete[] exists->vals.pop();
    explodelist(list, exists->vals);
}
COMMAND(listcomplete, "ss");

void complete(char *s)
{
    if(!s[0]) return;
    if(!completesize) { completesize = (int)strlen(s); lastcomplete[0] = '\0'; }

    completeval *cv = NULL;
    if(completesize)
    {
        char *end = strchr(s, ' ');
        if(end)
        {
            String command;
            copystring(command, s, min(size_t(end+1-s), sizeof(command)));
            cv = completions.access(command);
        }
    }

    const char *nextcomplete = NULL;
    String prefix = "";
    if(cv) // complete using list
    {
        int commandsize = strchr(s, ' ')+1-s;
        copystring(prefix, s, min(size_t(commandsize+1), sizeof(prefix)));
        loopv(cv->vals)
        {
            if(strncmp(cv->vals[i], s+commandsize, completesize-commandsize)==0 &&
               strcmp(cv->vals[i], lastcomplete) > 0 && (!nextcomplete || strcmp(cv->vals[i], nextcomplete) < 0))
                nextcomplete = cv->vals[i];
        }
    }
    else // complete using command names
    {
        extern Hashtable<const char *, ident> *idents;
        enumerate(*idents, const char *, name, ident, id,
            if(strncmp(name, s, completesize)==0 &&
               strcmp(name, lastcomplete) > 0 && (!nextcomplete || strcmp(name, nextcomplete) < 0))
                nextcomplete = name;
        );
    }
    if(nextcomplete)
    {
        copystring(s, prefix);
        concatstring(s, nextcomplete);
        copystring(lastcomplete, nextcomplete);
    }
    else lastcomplete[0] = '\0';
}

void consolekey(int code, bool isdown, int cooked)
{
    if(isdown)
    {
        switch(code)
        {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                break;

            case SDLK_HOME:
                if(strlen(commandbuf)) commandpos = 0;
                break;

            case SDLK_END:
                commandpos = -1;
                break;

            case SDLK_DELETE:
            {
                int len = (int)strlen(commandbuf);
                if(commandpos<0) break;
                memmove(&commandbuf[commandpos], &commandbuf[commandpos+1], len - commandpos);
                resetcomplete();
                if(commandpos>=len-1) commandpos = -1;
                break;
            }

            case SDLK_BACKSPACE:
            {
                int len = (int)strlen(commandbuf), i = commandpos>=0 ? commandpos : len;
                if(i<1) break;
                memmove(&commandbuf[i-1], &commandbuf[i], len - i + 1);
                resetcomplete();
                if(commandpos>0) commandpos--;
                else if(!commandpos && len<=1) commandpos = -1;
                break;
            }

            case SDLK_LEFT:
                if(commandpos>0) commandpos--;
                else if(commandpos<0) commandpos = (int)strlen(commandbuf)-1;
                break;

            case SDLK_RIGHT:
                if(commandpos>=0 && ++commandpos>=(int)strlen(commandbuf)) commandpos = -1;
                break;

            case SDLK_UP:
                if(histpos>0) history[--histpos]->restore(); 
                break;

            case SDLK_DOWN:
                if(histpos+1<history.size()) history[++histpos]->restore();
                break;

            case SDLK_TAB:
                if(!commandaction)
                {
                    complete(commandbuf);
                    if(commandpos>=0 && commandpos>=(int)strlen(commandbuf)) commandpos = -1;
                }
                break;

            default:
                resetcomplete();
                if(cooked)
                {
                    size_t len = (int)strlen(commandbuf);
                    if(len+1<sizeof(commandbuf))
                    {
                        if(commandpos<0) commandbuf[len] = cooked;
                        else
                        {
                            memmove(&commandbuf[commandpos+1], &commandbuf[commandpos], len - commandpos);
                            commandbuf[commandpos++] = cooked;
                        }
                        commandbuf[len+1] = '\0';
                    }
                }
                break;
        }
    }
    else
    {
        if(code==SDLK_RETURN || code==SDLK_KP_ENTER)
        {
            hline *h = NULL;
            if(commandbuf[0])
            {
                if(history.empty() || history.last()->shouldsave())
                    history.add(h = new hline)->save(); // cap this?
                else h = history.last();
            }
            histpos = history.size();
            saycommand(NULL);
            if(h) h->run();
        }
        else if(code==SDLK_ESCAPE)
        {
            histpos = history.size();
            saycommand(NULL);
        }
    }
}

void keypress(int code, bool isdown, int cooked)
{
    keym *haskey = keyms.access(code);
    if(haskey && haskey->pressed) execbind(*haskey, isdown); // allow pressed keys to release
    else
    {
        if(saycommandon) consolekey(code, isdown, cooked);
        else if(haskey) execbind(*haskey, isdown);
    }
}

