#include "engine.h"

SDL_Surface *screen = NULL;

void setupscreen(int w, int h)
{
    int flags = SDL_RESIZABLE;
    #if defined(WIN32) || defined(__APPLE__)
        flags = 0;
    #endif

    SDL_Rect **modes = SDL_ListModes(NULL, SDL_OPENGL|flags);
    if(modes && modes!=(SDL_Rect **)-1)
    {
        bool hasmode = false;
        for(int i = 0; modes[i]; i++)
        {
            if(w <= modes[i]->w && h <= modes[i]->h) { hasmode = true; break; }
        }
        if(!hasmode) { w = modes[0]->w; h = modes[0]->h; }
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    screen = SDL_SetVideoMode(w, h, 0, SDL_OPENGL|flags);

    #ifdef WIN32
    SDL_WM_GrabInput(SDL_GRAB_ON);
    #else
    SDL_WM_GrabInput(screen->flags&SDL_FULLSCREEN ? SDL_GRAB_ON : SDL_GRAB_OFF);
    #endif
}

void resizescreen(int w, int h)
{
    SDL_Surface *resized = SDL_SetVideoMode(w, h, 0, SDL_OPENGL|SDL_RESIZABLE|(screen->flags&SDL_FULLSCREEN));
    if(!resized) return;
    screen = resized;
}

void quit()
{
    SDL_ShowCursor(1);
    SDL_Quit();

    exit(EXIT_SUCCESS);
}
COMMAND(quit, "");

void fatal(const char *s, ...)    // failure exit
{
    static int errors = 0;
    errors++;

    if(errors <= 2) // print up to one extra recursive error
    {
        defprintstringlv(msg,s,s);
        puts(msg);

        if(errors <= 1) // avoid recursion
        {
            SDL_ShowCursor(1);
            #ifdef WIN32
                MessageBox(NULL, msg, "ragdoll fatal error", MB_OK|MB_SYSTEMMODAL);
            #endif
            SDL_Quit();
        }
    }

    exit(EXIT_FAILURE);
}

struct Camera
{
    Vec3 origin;
    float yaw, pitch, fovy, fogdist;

    Camera() : origin(0, 0, 0), yaw(0), pitch(0), fovy(70), fogdist(1000)
    {
    }

    void setupmatrices()
    {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        GLdouble aspect = GLdouble(screen->w)/screen->h,
                 ydist = 0.01f * tan(radians(fovy/2)), xdist = ydist * aspect;
        glFrustum(-xdist, xdist, -ydist, ydist, 0.01f, fogdist);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRotatef(pitch, -1, 0, 0);
        glRotatef(yaw, 0, 1, 0);
        // move from RH to Z-up LH quake style worldspace
        glRotatef(-90, 1, 0, 0);
        glScalef(1, -1, 1);
        glTranslatef(-origin.x, -origin.y, -origin.z);
    }
};

Camera camera;

bool intersectraytri(const Vec3 &o, const Vec3 &ray, const Vec3 &a, const Vec3 &b, const Vec3 &c, float &dist)
{
    Vec3 e1 = b - a, e2 = c - a,
         p = ray.cross(e2);
    float det = e1.dot(p);
    if(!det) return false;
    Vec3 r = o - a;
    float u = r.dot(p) / det;
    if(u < 0 || u > 1) return false;
    Vec3 q = r.cross(e1);
    float v = ray.dot(q) / det;
    if( v < 0 || u + v > 1) return false;
    dist = e2.dot(q) / det;
    return dist >= 0;
}

bool intersectraysphere(const Vec3 &o, const Vec3 &ray, Vec3 center, float radius, float &dist)
{
    center -= o;
    float v = center.dot(ray),
          inside = radius*radius - center.squaredlen();
    if(inside < 0 && v < 0) return false;
    float raysq = ray.squaredlen(), d = inside*raysq + v*v;
    if(d < 0) return false;
    dist = max(v - sqrtf(d), 0.0f) / raysq;
    return true;
}

struct MTri
{
    int vert[3];
};

struct MVert
{
    Vec3 pos, curpos;
    int joints[4];
    float weights[4];
};

String mname = "";
Vector<MTri> mtris;
Vector<MVert> mverts;
float mscale = 1, moffset = 0;

extern int cursormode, animmode;

int curtime = 0, lastmillis = 0;
bool up = false, down = false, forward = false, backward = false, left = false, right = false, stepping = false;
int dragging = -1;
float dragdist = 0, camscale = 1, curx = 0.5f, cury = 0.5f;
Vec3 campos, camdir;

FVAR(spawndist, 1, 10, 1000);

enum
{
    SEL_SPHERE = 0,
    SEL_TRI,
    SEL_JOINT,
    MAXSEL
};

Vector<int> selected[MAXSEL];

bool checkselected(int num, int type = SEL_SPHERE)
{
    loopv(selected[type]) if(selected[type][i]==num) return true;
    return false;
}

int hovertype = -1, hoveridx = -1;
float hoverdist = 0;

FVAR(spherescale, 0, 0.2f, 10);

struct Sphere
{
    Vec3 oldpos, pos, newpos, saved[3], dragoffset;
    float size;
    int avg;
    bool sticky, collided, eye;
 
    Sphere(const Vec3 &pos, float size) : oldpos(pos), pos(pos), dragoffset(0, 0, 0), size(size), sticky(false), collided(false), eye(false)
    {
        loopk(3) saved[k] = pos;
    }

    bool intersect(const Vec3 &o, const Vec3 &ray, float &dist) const
    {
        return intersectraysphere(o, ray, pos, size*spherescale, dist);
    }
};

Vector<Sphere> spheres;

struct Constraint
{
    virtual void apply() = 0;
    virtual void render() = 0;
    virtual void save(FILE *f) = 0;
    virtual bool load(const char *buf) = 0;
    virtual bool uses(int type, int idx) = 0;
    virtual void remap(int type, int idx) = 0;
    virtual void update() {}
    virtual void writecfg(FILE *f) = 0;
    virtual void printconsole() = 0;
    virtual ~Constraint() {}
};

VAR(linweight, 1, 1, 10);

struct DistConstraint : Constraint
{
    int sphere1, sphere2;
    float dist;

    DistConstraint() {}
    DistConstraint(int sphere1, int sphere2)
      : sphere1(sphere1), sphere2(sphere2)
    {
        update();
        printconsole();
    }

    void printconsole()
    {
        conoutf("Distance %f between #%d and #%d", dist, sphere1, sphere2);
    }
    
    void update()
    {
        dist = (spheres[sphere2].pos - spheres[sphere1].pos).magnitude();
    }

    bool uses(int type, int idx) { return type==SEL_SPHERE && (idx==sphere1 || idx==sphere2); }

    void remap(int type, int idx)
    {
        if(type==SEL_SPHERE)
        {
            if(sphere1 > idx) sphere1--;
            if(sphere2 > idx) sphere2--;
        }
    }

    void apply()
    {
        Sphere &s1 = spheres[sphere1], &s2 = spheres[sphere2];
        Vec3 dir = (s2.pos - s1.pos).normalize(),
             center = (s1.pos + s2.pos) * 0.5f;
        s1.newpos += (center - dir*(dist*0.5f))*linweight;
        s1.avg += linweight;
        s2.newpos += (center + dir*(dist*0.5f))*linweight;
        s2.avg += linweight;
    }

    void render()
    {
        glColor3f(1, 1, 0);
        Sphere &s1 = spheres[sphere1], &s2 = spheres[sphere2];
        glBegin(GL_LINES);
        glVertex3fv(s1.pos.v);
        glVertex3fv(s2.pos.v);
        glEnd();
    }

    void save(FILE *f)
    {
        fprintf(f, "d %d %d %f\n", sphere1, sphere2, dist);
    } 

    void writecfg(FILE *f)
    {
        fprintf(f, "rdlimitdist %d %d %f\n", sphere1, sphere2, dist / mscale);
    }

    bool load(const char *buf)
    {
        int num = sscanf(buf+1, " %d %d %f", &sphere1, &sphere2, &dist);
        if(num>=2)
        {
            if(num<3) update();
            return true;
        }
        return false;
    }
};

Vector<Constraint *> constraints;

struct Tri
{
    int sphere1, sphere2, sphere3;

    Matrix3x3 orient;    

    Tri(int s1, int s2, int s3) : sphere1(s1), sphere2(s2), sphere3(s3) 
    {
        calcorient();
    }

    bool uses(int type, int idx) 
    { 
        return type==SEL_SPHERE && (idx==sphere1 || idx==sphere2 || idx==sphere3);
    }

    void remap(int type, int idx)
    {
        if(type==SEL_SPHERE)
        {
            if(sphere1 > idx) sphere1--;
            if(sphere2 > idx) sphere2--;
            if(sphere3 > idx) sphere3--;
        }
    }

    void calcorient()
    {
        Sphere &s1 = spheres[sphere1], &s2 = spheres[sphere2], &s3 = spheres[sphere3];
        orient.a = (s2.pos - s1.pos).normalize();
        orient.c = orient.a.cross(s3.pos - s1.pos).normalize();
        orient.b = orient.c.cross(orient.a);
    }

    void render()
    {
        glBegin(GL_TRIANGLES);
        glVertex3fv(spheres[sphere1].pos.v);
        glVertex3fv(spheres[sphere2].pos.v);
        glVertex3fv(spheres[sphere3].pos.v);
        glEnd();
    }

    bool intersect(const Vec3 &o, const Vec3 &ray, float &dist) const
    {
        return intersectraytri(o, ray, spheres[sphere1].pos, spheres[sphere2].pos, spheres[sphere3].pos, dist);
    }
};

Vector<Tri> tris;

VAR(applyrots, 0, 1, 1);
FVAR(rotfric, -10, 1, 10);
VAR(angmom, 0, 2, 6);
VAR(rotcenter, 0, 0, 2);

VAR(rotweight, 1, 1, 10);

struct RotConstraint : Constraint
{
    int tri1, tri2;
    Matrix3x3 middle;
    float maxangle;

    RotConstraint() {}
    RotConstraint(int tri1, int tri2, float maxangle = 60)
      : tri1(tri1), tri2(tri2), maxangle(radians(maxangle))
    {
        update();
        printconsole();
    }

    void printconsole()
    {
        conoutf("Rotation %f between #%d and #%d", degrees(maxangle), tri1, tri2);
    }

    void update()
    {
        Matrix3x3 inv;
        inv.transpose(tris[tri2].orient);
        middle = tris[tri1].orient;
        middle *= inv;
    }

    bool uses(int type, int idx) 
    { 
        if(type==SEL_TRI && (idx==tri1 || idx==tri2)) return true;
        return tris[tri1].uses(type, idx) || tris[tri2].uses(type, idx);
    }

    void remap(int type, int idx)
    {
        if(type==SEL_TRI)
        {
            if(tri1 > idx) tri1--;
            if(tri2 > idx) tri2--;
        }
    }

    void apply()
    {
        if(!applyrots) return;

        Tri &t1 = tris[tri1], &t2 = tris[tri2];
        Matrix3x3 rot;
        rot.transpose(t1.orient);
        rot *= middle;
        rot *= t2.orient;

        Vec3 axis;
        float angle;
        rot.calcangleaxis(angle, axis);
        if(angle < 0)
        {
            if(-angle <= maxangle) return;
            angle += maxangle;
        }
        else if(angle <= maxangle) return;
        else angle = maxangle - angle;
        angle += 1e-3f;

        Vec3 c1 = (spheres[t1.sphere1].pos + spheres[t1.sphere2].pos + spheres[t1.sphere3].pos)/3,
             c2 = (spheres[t2.sphere1].pos + spheres[t2.sphere2].pos + spheres[t2.sphere3].pos)/3,
             cmass = (c1 + c2)/2, diff1(0, 0, 0), diff2(0, 0, 0);
        if(rotcenter>=2) c1 = c2 = cmass;
        Matrix3x3 wrot, crot1, crot2;
        wrot.rotate(radians(0.5f), axis);
        float w1 = wrot.transform(spheres[t1.sphere1].pos - c1).dist(spheres[t1.sphere1].pos - c1) +
                   wrot.transform(spheres[t1.sphere2].pos - c1).dist(spheres[t1.sphere2].pos - c1) +
                   wrot.transform(spheres[t1.sphere3].pos - c1).dist(spheres[t1.sphere3].pos - c1),
              w2 = wrot.transform(spheres[t2.sphere1].pos - c2).dist(spheres[t2.sphere1].pos - c2) +
                   wrot.transform(spheres[t2.sphere2].pos - c2).dist(spheres[t2.sphere2].pos - c2) +
                   wrot.transform(spheres[t2.sphere3].pos - c2).dist(spheres[t2.sphere3].pos - c2),
              a1 = (spheres[t1.sphere2].pos - spheres[t1.sphere1].pos).cross(spheres[t1.sphere3].pos - spheres[t1.sphere1].pos).magnitude(),
              a2 = (spheres[t1.sphere2].pos - spheres[t1.sphere1].pos).cross(spheres[t1.sphere3].pos - spheres[t1.sphere1].pos).magnitude();

        switch(angmom)
        {
        case 0:
            crot1.rotate(angle*rotfric*0.5f, axis);
            crot2.rotate(angle*rotfric*0.5f, axis);
            break;
        case 1:
            crot1.rotate(angle*rotfric*w1/(w1+w2), axis);
            crot2.rotate(angle*rotfric*w2/(w1+w2), axis);
            break;
        case 2:
            crot1.rotate(angle*rotfric*w2/(w1+w2), axis);
            crot2.rotate(angle*rotfric*w1/(w1+w2), axis);
            break;
        case 3:
            crot1.rotate(angle*rotfric*w2/w1, axis);
            crot2.rotate(angle*rotfric*w1/w2, axis);
            break;
        case 4:
            crot1.rotate(angle*rotfric*a1/(a1+a2), axis);
            crot2.rotate(angle*rotfric*a2/(a1+a2), axis);
            break;
        case 5:
            crot1.rotate(angle*rotfric*a2/(a1+a2), axis);
            crot2.rotate(angle*rotfric*a1/(a1+a2), axis);
            break;
        case 6:
            crot1.rotate(angle*rotfric*a2/a1, axis);
            crot2.rotate(angle*rotfric*a1/a2, axis);
            break;
        }

        #define ROTSPHERE(sphere, crot, trans, cmass, diff) \
        { \
            Sphere &s = spheres[sphere]; \
            Vec3 cpos = crot.trans(s.pos - cmass) + cmass; \
            diff += cpos - s.pos; \
            s.newpos += cpos*rotweight; \
            s.avg += rotweight; \
        }
        ROTSPHERE(t1.sphere1, crot1, transform, c1, diff1);
        ROTSPHERE(t1.sphere2, crot1, transform, c1, diff1);
        ROTSPHERE(t1.sphere3, crot1, transform, c1, diff1);
        ROTSPHERE(t2.sphere1, crot2, transposedtransform, c2, diff2);
        ROTSPHERE(t2.sphere2, crot2, transposedtransform, c2, diff2);
        ROTSPHERE(t2.sphere3, crot2, transposedtransform, c2, diff2);

        diff1 /= 3;
        diff2 /= 3;
        if(rotcenter) { diff1 += diff2; diff1 /= 2; diff2 = diff1; }

        diff1 *= rotweight;
        diff2 *= rotweight;

        spheres[t1.sphere1].newpos -= diff1;
        spheres[t1.sphere2].newpos -= diff1;
        spheres[t1.sphere3].newpos -= diff1;
        spheres[t2.sphere1].newpos -= diff2;
        spheres[t2.sphere2].newpos -= diff2;
        spheres[t2.sphere3].newpos -= diff2;
    }

    void render()
    {
        Tri &t1 = tris[tri1], &t2 = tris[tri2];
        int a1 = -1, a2 = -1;
        if(t1.sphere1==t2.sphere1 || t1.sphere1==t2.sphere2 || t1.sphere1==t2.sphere3) a1 = t1.sphere1;
        if(t1.sphere2==t2.sphere1 || t1.sphere2==t2.sphere2 || t1.sphere2==t2.sphere3) { a2 = a1; a1 = t1.sphere2; }
        if(t1.sphere3==t2.sphere1 || t1.sphere3==t2.sphere2 || t1.sphere3==t2.sphere3) { a2 = a1; a1 = t1.sphere3; }
        if(a1 < 0)
        {
            glColor3f(1, 0, 0);
            Vec3 c1 = (spheres[t1.sphere1].pos + spheres[t1.sphere2].pos + spheres[t1.sphere3].pos) / 3,
                 c2 = (spheres[t2.sphere1].pos + spheres[t2.sphere2].pos + spheres[t2.sphere3].pos) / 3;
            glBegin(GL_LINES);
            glVertex3fv(c1.v);
            glVertex3fv(c2.v);
            glEnd();
            return;
        }
       
        Vec3 axis1, axis2, ca, o1, o2;
        if(a2 >= 0)
        {
            axis1 = axis2 = (spheres[a2].pos - spheres[a1].pos).normalize();
            ca = (spheres[a1].pos + spheres[a2].pos)/2;
            if(t1.sphere1!=a1 && t1.sphere1!=a2) o1 = spheres[t1.sphere1].pos;
            else if(t1.sphere2!=a1 && t1.sphere2!=a2) o1 = spheres[t1.sphere2].pos;
            else if(t1.sphere3!=a1 && t1.sphere3!=a2) o1 = spheres[t1.sphere3].pos;
            if(t2.sphere1!=a1 && t2.sphere1!=a2) o2 = spheres[t2.sphere1].pos;
            else if(t2.sphere2!=a1 && t2.sphere2!=a2) o2 = spheres[t2.sphere2].pos;
            else if(t2.sphere3!=a1 && t2.sphere3!=a2) o2 = spheres[t2.sphere3].pos;
        }
        else
        {
            ca = spheres[a1].pos;
            o1 = o2 = Vec3(0, 0, 0);
            if(t1.sphere1!=a1 && t1.sphere1!=a2) o1 += spheres[t1.sphere1].pos;
            if(t1.sphere2!=a1 && t1.sphere2!=a2) o1 += spheres[t1.sphere2].pos;
            if(t1.sphere3!=a1 && t1.sphere3!=a2) o1 += spheres[t1.sphere3].pos;
            if(t2.sphere1!=a1 && t2.sphere1!=a2) o2 += spheres[t2.sphere1].pos;
            if(t2.sphere2!=a1 && t2.sphere2!=a2) o2 += spheres[t2.sphere2].pos;
            if(t2.sphere3!=a1 && t2.sphere3!=a2) o2 += spheres[t2.sphere3].pos;
            o1 /= 2;
            o2 /= 2;
            if(t1.sphere1==a1) axis1 = (spheres[t1.sphere2].pos - spheres[t1.sphere3].pos).normalize();
            else if(t1.sphere2==a1) axis1 = (spheres[t1.sphere1].pos - spheres[t1.sphere3].pos).normalize();
            else if(t1.sphere3==a1) axis1 = (spheres[t1.sphere2].pos - spheres[t1.sphere1].pos).normalize();
            if(t2.sphere1==a1) axis2 = (spheres[t2.sphere2].pos - spheres[t2.sphere3].pos).normalize();
            else if(t2.sphere2==a1) axis2 = (spheres[t2.sphere1].pos - spheres[t2.sphere3].pos).normalize();
            else if(t2.sphere3==a1) axis2 = (spheres[t2.sphere2].pos - spheres[t2.sphere1].pos).normalize();
        }

        glColor3f(1, 0, 0);
        glBegin(GL_LINES);
        Matrix3x3 steprot1, steprot2;
        Vec3 p1f = o1, p1b = o1, p2f = o2, p2b = o2;
        loopi(4)
        {
            steprot1.rotate(maxangle*(i+1)/4.0f, axis1);
            steprot2.rotate(maxangle*(i+1)/4.0f, axis2); 
            Vec3 n1f = steprot1.transform(o1 - ca) + ca,
                 n1b = steprot1.transposedtransform(o1 - ca) + ca,
                 n2f = steprot2.transposedtransform(o2 - ca) + ca,
                 n2b = steprot2.transform(o2 - ca) + ca;
            glVertex3fv(p1f.v); glVertex3fv(n1f.v);
            glVertex3fv(p1b.v); glVertex3fv(n1b.v);
            glVertex3fv(p2f.v); glVertex3fv(n2f.v);
            glVertex3fv(p2b.v); glVertex3fv(n2b.v);
            p1f = n1f;
            p1b = n1b;
            p2f = n2f;
            p2b = n2b;
        }
#if 0
        glVertex3fv(p1f.v); glVertex3fv(p1b.v);
        glVertex3fv(p2f.v); glVertex3fv(p2b.v);
//#else
        glVertex3fv(p1f.v); glVertex3fv(ca.v);
        glVertex3fv(p1b.v); glVertex3fv(ca.v);
        glVertex3fv(p2f.v); glVertex3fv(ca.v);
        glVertex3fv(p2b.v); glVertex3fv(ca.v);
#endif
        if(a2 < 0)
        {
            Vec3 p1c = (p1f + p1b)/2,
                 p2c = (p2f + p2b)/2;
            axis1 = (p1c - ca).normalize();
            axis2 = (p2c - ca).normalize();
            Vec3 o1f = p1f, o1b = p1b,
                 o2f = p2f, o2b = p2b;
            Vec3 p1fl = o1f, p1fr = o1f,
                 p1bl = o1b, p1br = o1b,
                 p2fl = o2f, p2fr = o2f,
                 p2bl = o2b, p2br = o2b;
            loopi(4)
            {
                steprot1.rotate(maxangle*(i+1)/4.0f, axis1);
                steprot2.rotate(maxangle*(i+1)/4.0f, axis2); 
                Vec3 n1fl = steprot1.transform(o1f - p1c) + p1c,
                     n1bl = steprot1.transform(o1b - p1c) + p1c,
                     n1fr = steprot1.transposedtransform(o1f - p1c) + p1c,
                     n1br = steprot1.transposedtransform(o1b - p1c) + p1c,
                     n2fl = steprot2.transform(o2f - p2c) + p2c,
                     n2bl = steprot2.transform(o2b - p2c) + p2c,
                     n2fr = steprot2.transposedtransform(o2f - p2c) + p2c,
                     n2br = steprot2.transposedtransform(o2b - p2c) + p2c;
                glVertex3fv(p1fl.v); glVertex3fv(n1fl.v);
                glVertex3fv(p1bl.v); glVertex3fv(n1bl.v);
                glVertex3fv(p1fr.v); glVertex3fv(n1fr.v);
                glVertex3fv(p1br.v); glVertex3fv(n1br.v);
                glVertex3fv(p2fl.v); glVertex3fv(n2fl.v);
                glVertex3fv(p2bl.v); glVertex3fv(n2bl.v);
                glVertex3fv(p2fr.v); glVertex3fv(n2fr.v);
                glVertex3fv(p2br.v); glVertex3fv(n2br.v);
                p1fl = n1fl;
                p1bl = n1bl;
                p1fr = n1fr;
                p1br = n1br;
                p2fl = n2fl;
                p2bl = n2bl;
                p2fr = n2fr;
                p2br = n2br;
            }
#if 0
            glVertex3fv(p1fl.v); glVertex3fv(p1fr.v);
            glVertex3fv(p1bl.v); glVertex3fv(p1br.v);
            glVertex3fv(p2fl.v); glVertex3fv(p2fr.v);
            glVertex3fv(p2bl.v); glVertex3fv(p2br.v);
//#else
            glVertex3fv(p1fl.v); glVertex3fv(p1c.v);
            glVertex3fv(p1bl.v); glVertex3fv(p1c.v);
            glVertex3fv(p1fr.v); glVertex3fv(p1c.v);
            glVertex3fv(p1br.v); glVertex3fv(p1c.v);
            glVertex3fv(p2fl.v); glVertex3fv(p2c.v);
            glVertex3fv(p2bl.v); glVertex3fv(p2c.v);
            glVertex3fv(p2fr.v); glVertex3fv(p2c.v);
            glVertex3fv(p2br.v); glVertex3fv(p2c.v);
#endif
        }
        glEnd();
    }

    void save(FILE *f)
    {
        fprintf(f, "r %d %d %f %f %f %f %f %f %f %f %f %f\n", 
            tri1, tri2, maxangle, 
            middle.a.x, middle.a.y, middle.a.z, 
            middle.b.x, middle.b.y, middle.b.z, 
            middle.c.x, middle.c.y, middle.c.z);
    }

    bool load(const char *buf)
    {
        int num = sscanf(buf+1, " %d %d %f %f %f %f %f %f %f %f %f %f", 
            &tri1, &tri2, &maxangle,
            &middle.a.x, &middle.a.y, &middle.a.z,
            &middle.b.x, &middle.b.y, &middle.b.z,
            &middle.c.x, &middle.c.y, &middle.c.z);
        if(num>=3)
        {
            if(num<12) update();
            return true;
        }
        return false;
    }

    void writecfg(FILE *f)
    {
        Quat q(middle);
        fprintf(f, "rdlimitrot %d %d %f %f %f %f %f\n", tri1, tri2, degrees(maxangle), q.x, q.y, q.z, q.w);
    }

};

struct Joint
{
    char name[256];
    int index, parent;
    Vec3 pos;
    bool used, haschild, moved;
    int hide, tri, spheres[3];
    Matrix3x4 tridiff, orient;

    Joint(const char *desc, int index, int parent, const Vec3 &pos)
      : index(index), parent(parent), pos(pos), used(false), haschild(false), hide(desc[0]=='!' ? 1 : 0),
        tri(-1)
    {
        strcpy(name, desc);
        loopk(3) spheres[k] = -1;
        tridiff.identity();
        orient.identity();
    }

    bool uses(int type, int idx) 
    { 
        if(type==SEL_TRI && idx==tri) return true;
        return tri>=0 && tris.inrange(tri) && tris[tri].uses(type, idx);
    }

    void remap(int type, int idx)
    {
        if(type==SEL_TRI)
        {
            if(tri > idx) tri--;
        }
        else if(type==SEL_SPHERE)
        {
            loopk(3) if(spheres[k] > idx) spheres[k]--;
        }
    }

    void killbind()
    {
        tri = -1;
        loopk(3) spheres[k] = -1;
        orient.identity();
        tridiff.identity();
    }

    Vec3 getpos() const { return orient.transform(pos); }

    bool intersect(const Vec3 &o, const Vec3 &ray, float size, float &dist) const
    {
        return intersectraysphere(o, ray, getpos(), size, dist);
    }

    void save(FILE *f)
    {
        if(tri<0) return;
        fprintf(f, "j %d %d %d %d %d %f %f %f %f %f %f %f %f %f %f %f %f\n",
            index, tri, spheres[0], spheres[1], spheres[2],
            tridiff.a.x, tridiff.a.y, tridiff.a.z, tridiff.a.w,
            tridiff.b.x, tridiff.b.y, tridiff.b.z, tridiff.b.w,
            tridiff.c.x, tridiff.c.y, tridiff.c.z, tridiff.c.w);
    }

    bool load(const char *buf)
    {
        int num = sscanf(buf+1, " %*d %d %d %d %d %f %f %f %f %f %f %f %f %f %f %f %f",
            &tri, &spheres[0], &spheres[1], &spheres[2],
            &tridiff.a.x, &tridiff.a.y, &tridiff.a.z, &tridiff.a.w,
            &tridiff.b.x, &tridiff.b.y, &tridiff.b.z, &tridiff.b.w,
            &tridiff.c.x, &tridiff.c.y, &tridiff.c.z, &tridiff.c.w);
        if(num>=16) return true;
        tri = -1; 
        loopk(3) spheres[k] = -1;
        return false;
    }
};
Vector<Joint> joints;

VAR(showspheres, 0, 1, 1);
VAR(showjoints, 0, 1, 2);
VAR(showtris, 0, 1, 1);
VAR(showconstraints, 0, 1, 1);

void hover()
{
    hovertype = -1;
    hoveridx = -1;
    hoverdist = 0;
    if(dragging >= 0) return;

    float dist;
    if(showspheres) loopv(spheres)
    {
        const Sphere &s = spheres[i];
        if(s.intersect(campos, camdir, dist) && (hovertype < 0 || dist < hoverdist)) { hovertype = SEL_SPHERE; hoveridx = i; hoverdist = dist; }
    }
    if(showjoints) loopv(joints)
    {
        const Joint &j = joints[i];
        if(!j.hide && j.intersect(campos, camdir, 0.1f, dist) && (hovertype < 0 || dist < hoverdist)) { hovertype = SEL_JOINT; hoveridx = i; hoverdist = dist; }
    }
    if(showtris) loopv(tris)
    {
        const Tri &t = tris[i];
        if(t.intersect(campos, camdir, dist) && (hovertype < 0 || dist < hoverdist)) { hovertype = SEL_TRI; hoveridx = i; hoverdist = dist; }
    }
    hoverdist /= camscale;
}

ICOMMAND(gethoverdist, "", (), floatret(hoverdist));

VAR(dbgselect, 0, 0, 1);

void select()
{
    if(hovertype>=0 && (selected[hovertype].empty() || selected[hovertype].last()!=hoveridx)) 
    {
        static const char *names[MAXSEL] = { "sphere", "tri", "joint" }; 
        if(dbgselect) conoutf("select: %s %d", names[hovertype], hoveridx);
        selected[hovertype].add(hoveridx);
        if(dragging>=0 && hovertype==SEL_SPHERE) spheres[hoveridx].dragoffset = spheres[hoveridx].pos - spheres[dragging].pos;
    }
}
COMMAND(select, "");

void selectall(int *type)
{
    switch(*type)
    {
        case 0: selected[SEL_SPHERE].setsize(0); loopv(spheres) selected[SEL_SPHERE].add(i); break;  
        case 1: selected[SEL_TRI].setsize(0); loopv(tris) selected[SEL_TRI].add(i); break;
        case 2: selected[SEL_JOINT].setsize(0); loopv(joints) selected[SEL_JOINT].add(i); break;
    }
}
COMMAND(selectall, "i");

void cancelselect()
{
    loopk(MAXSEL) selected[k].setsize(0);
}
COMMAND(cancelselect, "");

void drag(int *isdown)
{
    if(!*isdown) { dragging = -1; return; }
    if(hovertype==SEL_SPHERE) 
    {
        dragging = hoveridx;
        dragdist = camera.origin.dist(spheres[hoveridx].pos)/camscale;
        spheres[dragging].dragoffset = Vec3(0, 0, 0);

        loopv(selected[SEL_SPHERE])
        {
            Sphere &s = spheres[selected[SEL_SPHERE][i]];
            s.dragoffset = s.pos - spheres[dragging].pos;
        }
    }
}
COMMAND(drag, "D");

int delcmp(const int *x, const int *y)
{
    if(*x > *y) return -1;
    if(*x < *y) return 1;
    return 0;
}

void delselect()
{
    Vector<int> ds, dt, dj, dc;
    loopv(selected[SEL_SPHERE]) ds.add(selected[SEL_SPHERE][i]);
    loopv(selected[SEL_TRI]) dt.add(selected[SEL_TRI][i]);
    loopv(selected[SEL_JOINT]) dj.add(selected[SEL_JOINT][i]);
    loopv(ds)
    {
        if(dragging==ds[i]) dragging = -1;
        loopvj(tris) if(tris[j].uses(SEL_SPHERE, ds[i]) && dt.find(j)<0) dt.add(j);
        loopvj(joints) if(joints[j].uses(SEL_SPHERE, ds[i]) && dj.find(j)<0) dj.add(j);
        loopvj(constraints) if(constraints[j]->uses(SEL_SPHERE, ds[i]) && dc.find(j)<0) dc.add(j);
    }
    loopv(dt)
    {
        loopvj(joints) if(joints[j].uses(SEL_TRI, dt[i]) && dj.find(j)<0) dj.add(j);
        loopvj(constraints) if(constraints[j]->uses(SEL_TRI, dt[i]) && dc.find(j)<0) dc.add(j);
    }
    dc.sort(delcmp);
    dj.sort(delcmp);
    dt.sort(delcmp);
    ds.sort(delcmp);
    loopv(ds)
    {
        loopvj(tris) tris[j].remap(SEL_SPHERE, ds[i]);
        loopvj(joints) joints[j].remap(SEL_SPHERE, ds[i]);
        loopvj(constraints) constraints[j]->remap(SEL_SPHERE, ds[i]);
    }
    loopv(dt)
    {
        loopvj(joints) joints[j].remap(SEL_TRI, dt[i]);
        loopvj(constraints) constraints[j]->remap(SEL_TRI, dt[i]);
    }
    loopv(dc) delete constraints.remove(dc[i]);
    loopv(dj) joints[dj[i]].killbind();
    loopv(dt) tris.remove(dt[i]);
    loopv(ds) spheres.remove(ds[i]);
    loopk(3) selected[k].setsize(0);
}
COMMAND(delselect, "");
    
void stopspheres()
{
    loopv(spheres)
    {
        Sphere &s = spheres[i];
        s.oldpos = s.pos;
    }
}

void clearmodel()
{
    mname[0] = '\0';
    mtris.setsize(0);
    mverts.setsize(0); 
    mscale = 1;
    moffset = 0;
    joints.setsize(0);
    selected[SEL_JOINT].setsize(0);
}
COMMAND(clearmodel, "");

void setupmodel(const char *fname)
{
    printf("loaded %d joints from %s\n", joints.size(), fname);
    loopv(joints)
    {
        Joint &j = joints[i];
        if(j.used)
        {
            for(int parent = j.parent; joints.inrange(parent); parent = joints[parent].parent)
                joints[parent].haschild = true;
        }
    }
    int unused = 0;
    loopv(joints)
    {
        Joint &j = joints[i];
        if(j.used || j.hide) continue;
        if(!j.haschild) j.hide = 1;
        else
        {
            if(!unused) printf("unused joints: %s", j.name);
            else printf(", %s", j.name);
            unused++;
        } 
    }
    if(unused) putchar('\n');
    int hideused = 0;
    loopv(joints)
    {
        Joint &j = joints[i];
        if(j.hide && j.used)
        {
            if(!hideused) printf("using hidden joints: %s", j.name);
            else printf(", %s", j.name);
            hideused++;
        } 
    }
    if(hideused) putchar('\n');
}

struct md5weight
{
    int joint;
    float bias;
    Vec3 pos;
};

struct md5vert
{
    float u, v;
    int start, count;
};

void loadmd5(const char *fname, float scale)
{
    if(!fname[0]) fname = "model.md5mesh";
    fname = path(fname, true);
    FILE *f = fopen(fname, "r");
    if(!f) { conoutf(CON_ERROR, "failed loading %s", fname); return; }
    clearmodel();
    copystring(mname, fname);
    mscale = scale;
    char buf[512];
    Vector<Matrix3x4> orients;
    for(;;)
    {
        fgets(buf, sizeof(buf), f);
        if(feof(f)) break;
        if(strstr(buf, "joints {"))
        {
            for(;;)
            {
                fgets(buf, sizeof(buf), f);
                if(buf[0]=='}' || feof(f)) break;
                char name[256];
                int parent;
                Vec3 pos;
                Quat orient;
                if(sscanf(buf, " %s %d ( %f %f %f ) ( %f %f %f )",
                    name, &parent, &pos.x, &pos.y, &pos.z,
                    &orient.x, &orient.y, &orient.z)==8)
                {
                    pos.y = -pos.y;
                    orient.x = -orient.x;
                    orient.z = -orient.z;
                    orient.restorew();
                    char *desc = name;
                    while(isspace(*desc) || *desc=='"') desc++;
                    char *end = strchr(desc, '"');
                    if(end) *end = '\0';
                    int index = joints.size();
                    joints.add(Joint(desc, index, parent, pos * mscale));
                    orients.add(Matrix3x4(Matrix3x3(orient), pos));
                }
            }
            loopv(joints) if(joints[i].pos.z < 1) moffset = max(moffset, 1 - joints[i].pos.z);
            loopv(joints) joints[i].pos.z += moffset;
        }
        else if(strstr(buf, "mesh {"))
        {
            Vector<md5weight> weights;
            Vector<md5vert> verts;
            Vector<MTri> tris;
            md5weight w;
            md5vert v;
            MTri t;
            int index;
            for(;;)
            {
                fgets(buf, sizeof(buf), f);
                if(buf[0]=='}' || feof(f)) break;
                if(sscanf(buf, " vert %d ( %f %f ) %d %d", &index, &v.u, &v.v, &v.start, &v.count)==5)
                {
                    if(index>=0) { while(!verts.inrange(index)) verts.add(v); verts[index] = v; } 
                }
                else if(sscanf(buf, " tri %d %d %d %d", &index, &t.vert[0], &t.vert[1], &t.vert[2])==4)
                {
                    if(index>=0) { while(!tris.inrange(index)) tris.add(t); tris[index] = t; }
                }
                else if(sscanf(buf, " weight %d %d %f ( %f %f %f ) ", &index, &w.joint, &w.bias, &w.pos.x, &w.pos.y, &w.pos.z)==6)
                {
                    w.pos.y = -w.pos.y;
                    if(index>=0) { while(!weights.inrange(index)) weights.add(w); weights[index] = w; }
                    if(joints.inrange(w.joint)) joints[w.joint].used = true;
                }
            }
            int mvoffset = mverts.size();
            loopv(tris)
            {
                MTri &t = mtris.add(tris[i]);
                loopk(3) t.vert[k] += mvoffset;
            }
            loopv(verts)
            {
                md5vert &v = verts[i];
                Vec3 pos(0, 0, 0);
                loopj(v.count)
                {
                    md5weight &w = weights[v.start + j];
                    if(joints.inrange(w.joint)) pos += orients[w.joint].transform(w.pos)*w.bias;
                }
                pos *= mscale;
                pos.z += moffset;

                MVert &mv = mverts.add();
                mv.pos = pos;
                memset(mv.weights, 0, sizeof(mv.weights));
                memset(mv.joints, 0, sizeof(mv.joints));
                loopj(min(v.count, 4))
                {
                    md5weight &w = weights[v.start + j];
                    mv.joints[j] = w.joint;
                    mv.weights[j] = w.bias;
                }
                for(int j = 4; j < v.count; j++)
                {
                    md5weight &w = weights[v.start + j];
                    int replace = -1;
                    loopk(4) if(w.bias > mv.weights[k] && (replace < 0 || mv.weights[k] < mv.weights[replace])) replace = k;
                    if(replace >= 0)
                    {
                        mv.joints[replace] = w.joint;
                        mv.weights[replace] = w.bias;
                    }
                }
                float total = 0;
                loopj(4) total += mv.weights[j];
                if(total) loopj(4) mv.weights[j] /= total;

                if(v.count > 4 || total < 0.999f || total > 1.001f)
                    printf("vert %d: %d weights, %f sum\n", i, v.count, total);
            } 
        }                    
    }
    fclose(f);
    setupmodel(fname);
}

#include "iqm.h"

void loadiqm(const char *fname, float scale)
{
    uchar *buf = NULL;
    iqmheader hdr;
    Vector<Matrix3x4> orients;
    if(!fname[0]) fname = "model.iqm";
    fname = path(fname, true);
    FILE *f = fopen(fname, "rb");
    if(!f) goto error;
    if(fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) || memcmp(hdr.magic, IQM_MAGIC, sizeof(hdr.magic)))
        goto error;
    lilswap(&hdr.version, (sizeof(hdr) - sizeof(hdr.magic))/sizeof(uint));
    if(hdr.version != IQM_VERSION)
        goto error;
    if(hdr.filesize > (16<<20))
        goto error; // sanity check... don't load files bigger than 16 MB
    buf = new uchar[hdr.filesize];
    if(fread(buf + sizeof(hdr), 1, hdr.filesize - sizeof(hdr), f) != hdr.filesize - sizeof(hdr))
        goto error;
    if(hdr.num_meshes <= 0) goto error;
    fclose(f);

    clearmodel();
    copystring(mname, fname);
    mscale = scale;

    {
    lilswap((uint *)&buf[hdr.ofs_vertexarrays], hdr.num_vertexarrays*sizeof(iqmvertexarray)/sizeof(uint));
    lilswap((uint *)&buf[hdr.ofs_triangles], hdr.num_triangles*sizeof(iqmtriangle)/sizeof(uint));
    lilswap((uint *)&buf[hdr.ofs_meshes], hdr.num_meshes*sizeof(iqmmesh)/sizeof(uint));
    lilswap((uint *)&buf[hdr.ofs_joints], hdr.num_joints*sizeof(iqmjoint)/sizeof(uint));
    
    const char *str = hdr.ofs_text ? (char *)&buf[hdr.ofs_text] : "";
    float *vpos = NULL;
    uchar *vindex = NULL, *vweight = NULL;
    iqmvertexarray *vas = (iqmvertexarray *)&buf[hdr.ofs_vertexarrays];
    iqmjoint *jdata = (iqmjoint *)&buf[hdr.ofs_joints];
    iqmmesh *mdata = (iqmmesh *)&buf[hdr.ofs_meshes];
    iqmtriangle *tdata = (iqmtriangle *)&buf[hdr.ofs_triangles];
    loopi(hdr.num_vertexarrays)
    {
        iqmvertexarray &va = vas[i];
        switch(va.type)
        {
            case IQM_POSITION: if(va.format != IQM_FLOAT || va.size != 3) goto error; vpos = (float *)&buf[va.offset]; lilswap(vpos, 3*hdr.num_vertexes); break;
            case IQM_BLENDINDEXES: if(va.format != IQM_UBYTE || va.size != 4) goto error; vindex = (uchar *)&buf[va.offset]; break;
            case IQM_BLENDWEIGHTS: if(va.format != IQM_UBYTE || va.size != 4) goto error; vweight = (uchar *)&buf[va.offset]; break;
        }
    }

    loopi(hdr.num_joints)
    {
        iqmjoint &j = jdata[i];
        Vec3 pos(j.translate[0], -j.translate[1], j.translate[2]);
        Quat orient;
        orient.x = -j.rotate[0];
        orient.y = j.rotate[1];
        orient.z = -j.rotate[2];
        orient.w = j.rotate[3];
        orient.normalize();
        //if(j.parent >= 0) printf("%s -> %s\n", str+j.name, str+jdata[j.parent].name);
        //else printf("%s\n", str+j.name);
        joints.add(Joint(&str[j.name], i, j.parent, (j.parent >= 0 ? orients[j.parent].transform(pos) : pos) * mscale));
        orients.add(Matrix3x4(Matrix3x3(orient), pos));
        if(j.parent >= 0) orients.last() = orients[j.parent] * orients.last(); 
        if(joints.last().pos.z < 1) moffset = max(moffset, 1 - joints.last().pos.z);
    }

    loopi(hdr.num_meshes)
    {
        iqmmesh &m = mdata[i];
        int mvoffset = mverts.size();
        loopj(m.num_triangles)
        {
            MTri &t = mtris.add();
            loopk(3) t.vert[k] = tdata[j + m.first_triangle].vertex[k] - m.first_vertex + mvoffset; 
        }
        loopj(m.num_vertexes)
        {
            MVert &mv = mverts.add();
            mv.pos = Vec3(&vpos[3*(j + m.first_vertex)]);
            mv.pos.y = -mv.pos.y;
            mv.pos *= mscale;
            loopk(4) 
            { 
                mv.joints[k] = vindex[4*(j + m.first_vertex) + k]; 
                mv.weights[k] = vweight[4*(j + m.first_vertex) + k]/255.0f; 
                if(mv.weights[k] && joints.inrange(mv.joints[k])) joints[mv.joints[k]].used = true;
            }
            if(mv.pos.z < 1) moffset = max(moffset, 1 - mv.pos.z);
        }
    }
    loopv(mverts) mverts[i].pos.z += moffset;
    loopv(joints) joints[i].pos.z += moffset;
   
    setupmodel(fname);
 
    }

    return;

error:
    if(f) fclose(f);
    if(buf) delete[] buf;
    conoutf(CON_ERROR, "failed loading %s", fname);
}

void loadmodel(const char *name, float scale)
{
    const char *type = strrchr(name, '.');
    if(!type) conoutf(CON_ERROR, "no file type");
    else if(!strcasecmp(type, ".md5mesh")) loadmd5(name, scale);
    else if(!strcasecmp(type, ".iqm")) loadiqm(name, scale);
    else conoutf(CON_ERROR, "unknown file type: %s", type);
}
ICOMMAND(loadmodel, "sf", (char *name, float *scale), loadmodel(name, *scale > 0 ? *scale : 1));

ICOMMAND(getmodelscale, "", (), floatret(mscale));
ICOMMAND(getmodeloffset, "", (), floatret(moffset));

void clearscene()
{
    animmode = false;
    dragging = -1;
    selected[SEL_SPHERE].setsize(0);
    selected[SEL_TRI].setsize(0);
    spheres.setsize(0);
    tris.setsize(0);
    while(constraints.size()) delete constraints.pop();
    clearmodel();
}
COMMAND(clearscene, "");

void savescene(const char *fname)
{
    if(!fname[0]) fname = "quicksave.txt";
    fname = path(fname, true);
    FILE *f = fopen(fname, "w");
    if(!f) { conoutf(CON_ERROR, "save failed"); return; }
    fprintf(f, "c %f %f %f %f %f\n", camera.origin.x, camera.origin.y, camera.origin.z, camera.yaw, camera.pitch);
    loopv(spheres) 
    {
        Sphere &s = spheres[i];
        fprintf(f, "s %f %f %f %f %d", s.pos.x, s.pos.y, s.pos.z, max(s.size, 1.0f), s.sticky ? 1 : 0);
        if(s.saved[0]!=s.pos || s.saved[1]!=s.pos || s.saved[2]!=s.pos) fprintf(f, " %f %f %f", s.saved[0].x, s.saved[0].y, s.saved[0].z);
        if(s.saved[1]!=s.pos || s.saved[2]!=s.pos) fprintf(f, " %f %f %f", s.saved[1].x, s.saved[1].y, s.saved[1].z);
        if(s.saved[2]!=s.pos) fprintf(f, " %f %f %f", s.saved[2].x, s.saved[2].y, s.saved[2].z);
        fprintf(f, "\n");
        if(s.eye) fprintf(f, "e %d\n", i);
    }
    loopv(tris)
    {
        Tri &t = tris[i];
        fprintf(f, "t %d %d %d\n", t.sphere1, t.sphere2, t.sphere3);
    }
    loopv(constraints) constraints[i]->save(f);
    if(mname[0]) 
    {
        fprintf(f, "m %f %s\n", mscale, mname); 
        loopv(joints) joints[i].save(f);
    }
    fclose(f);
    conoutf("saved %s", fname);
}
COMMAND(savescene, "s");

void loadscene(const char *fname)
{
    if(!fname[0]) fname = "quicksave.txt";
    fname = path(fname, true);
    FILE *f = fopen(fname, "r");
    if(!f) { conoutf(CON_ERROR, "load failed"); return; }
    clearscene();
    char buf[1024];
    while(fgets(buf, sizeof(buf), f))
    {
        switch(buf[0])
        {
            case 'c':
            {
                float x, y, z, yaw, pitch;
                if(sscanf(buf+1, " %f %f %f %f %f", &x, &y, &z, &yaw, &pitch) == 5)
                {
                    camera.origin = Vec3(x, y, z);
                    camera.yaw = yaw;
                    camera.pitch = pitch;
                }
                break;
            }
            case 's':
            {
                Vec3 pos, saved[3];
                float sz;
                int sticky = 0;
                int num = sscanf(buf+1, " %f %f %f %f %d %f %f %f %f %f %f %f %f %f", &pos.x, &pos.y, &pos.z, &sz, &sticky, &saved[0].x, &saved[0].y, &saved[0].z, &saved[1].x, &saved[1].y, &saved[1].z, &saved[2].x, &saved[2].y, &saved[2].z);
                if(num >= 4)
                {
                    Sphere &s = spheres.add(Sphere(pos, max(sz, 1.0f)));
                    if(sticky) s.sticky = true;
                    if(num >= 8) s.saved[0] = saved[0];
                    if(num >= 11) s.saved[1] = saved[1];
                    if(num >= 14) s.saved[2] = saved[2];
                }
                break;
            }
            case 't':
            {
                int s1, s2, s3;
                if(sscanf(buf+1, " %d %d %d", &s1, &s2, &s3)==3)
                    tris.add(Tri(s1, s2, s3));
                break;
            }
            case 'd':
            {
                Constraint *c = new DistConstraint;
                if(c->load(buf)) constraints.add(c);
                else delete c;
                break;
            }
            case 'r':
            {
                Constraint *c = new RotConstraint;
                if(c->load(buf)) constraints.add(c);
                else delete c;
                break;
            }
            case 'm':
            {
                char *name = NULL;
                float scale = strtod(buf+2, &name);
                if(!name || !isspace(*name)) name = buf+2;
                while(isspace(*name)) name++;
                char *end = name;
                while(*end && !isspace(*end)) end++;
                *end = '\0';
                loadmodel(name, scale > 0 ? scale : 1);
                break;
            }
            case 'j':
            {
                int idx;
                if(sscanf(buf+1, " %d", &idx)==1 && joints.inrange(idx)) joints[idx].load(buf);
                break;
            }
            case 'e':
            {
                int idx;
                if(sscanf(buf+1, " %d", &idx)==1 && spheres.inrange(idx)) spheres[idx].eye = true;
                break;
            }
        }
    }
    fclose(f);
    conoutf("loaded %s", fname);
}
COMMAND(loadscene, "s");

VARF(cursormode, 0, 1, 1,
    if(cursormode) 
    {
        camera.yaw = floor((camera.yaw + 45)/90.0f)*90;
        camera.pitch = floor((camera.pitch + 45)/90.0f)*90;
    }
);

VARF(animmode, 0, 1, 1,
    if(!animmode) stopspheres();
);

VAR(iterconstraints, 0, 2, 100);

ICOMMAND(forward, "D", (int *isdown), forward = *isdown);
ICOMMAND(backward, "D", (int *isdown), backward = *isdown);
ICOMMAND(left, "D", (int *isdown), left = *isdown);
ICOMMAND(right, "D", (int *isdown), right = *isdown);
ICOMMAND(up, "D", (int *isdown), up = *isdown);
ICOMMAND(down, "D", (int *isdown), down = *isdown);

void hidejoints()
{
    loopv(selected[SEL_JOINT])
    {
        joints[selected[SEL_JOINT][i]].hide |= 2;
    }
    selected[SEL_JOINT].setsize(0);
}
COMMAND(hidejoints, "");

void unhidejoints()
{
    loopv(joints) joints[i].hide &= ~2;
}
COMMAND(unhidejoints, "");

void addsphere(int *dir, float *dist)
{
    Vec3 pos = campos + camdir*spawndist*camscale;
    if(hovertype==SEL_JOINT) 
    {
        Joint &j = joints[hoveridx];
        pos = j.getpos();
    }
    float sepdist = (*dist<=0 ? 1 : *dist)*0.5f,
          size = 1.0f,
          yaw = floor((camera.yaw + 45)/90.0f)*90,
          pitch = floor((camera.pitch + 45)/90.0f)*90;
    switch(*dir)
    {
        case 1: // relative X axis
        {
            Vec3 sep(radians(yaw+90), 0);
            spheres.add(Sphere(pos-sep*sepdist, size));
            spheres.add(Sphere(pos+sep*sepdist, size));
            break;
        }
        case 2: // relative Y axis
        {
            Vec3 sep(radians(yaw), radians(pitch));
            spheres.add(Sphere(pos-sep*sepdist, size));
            spheres.add(Sphere(pos+sep*sepdist, size));
            break;
        }
        case 3: // relative Z axis
        {
            Vec3 sep(radians(yaw), radians(pitch+90));
            spheres.add(Sphere(pos-sep*sepdist, size));
            spheres.add(Sphere(pos+sep*sepdist, size));
            break;
        }
        default:
            spheres.add(Sphere(pos, size));
            break;
    }
}
COMMAND(addsphere, "if");

ICOMMAND(getspheredist, "", (), floatret(selected[SEL_SPHERE].size()>=2 ? spheres[selected[SEL_SPHERE][0]].pos.dist(spheres[selected[SEL_SPHERE][1]].pos) : 0.0f));

void setspheredist(float *dist, int *dir)
{
    if(selected[SEL_SPHERE].size() >= 2)
    {
        Sphere &s1 = spheres[selected[SEL_SPHERE][0]],
               &s2 = spheres[selected[SEL_SPHERE][1]];
        Vec3 axis = (s2.pos - s1.pos).normalize(),
             center = (s1.pos + s2.pos)*0.5f;
        float sepdist = (*dist<=0 ? 1 : *dist)*0.5f,
              yaw = floor((camera.yaw + 45)/90.0f)*90,
              pitch = floor((camera.pitch + 45)/90.0f)*90;
        switch(*dir)
        {
            case 1: axis = Vec3(radians(yaw+90), 0); break;
            case 2: axis = Vec3(radians(yaw), radians(pitch)); break;
            case 3: axis = Vec3(radians(yaw), radians(pitch+90)); break;
        }
        s1.pos = center - axis*sepdist;
        s2.pos = center + axis*sepdist;
    }
}
COMMAND(setspheredist, "fi");

void rotspheres(float *angle, int *dir, int *numcenter)
{
    if(selected[SEL_SPHERE].empty()) return;
    Vec3 center(0, 0, 0);
    loopv(selected[SEL_SPHERE]) if(*numcenter<=0 || i<*numcenter) center += spheres[selected[SEL_SPHERE][i]].pos;
    center /= *numcenter<=0 ? selected[SEL_SPHERE].size() : min(*numcenter, selected[SEL_SPHERE].size());

    Vec3 axis(0, 0, 1);
    float yaw = floor((camera.yaw + 45)/90.0f)*90,
          pitch = floor((camera.pitch + 45)/90.0f)*90;
    switch(*dir)
    {
        case 1: axis = Vec3(radians(yaw+90), 0); break;
        case 2: axis = Vec3(radians(yaw), radians(pitch)); break;
        case 3: axis = Vec3(radians(yaw), radians(pitch+90)); break;
        case 4: 
            if(selected[SEL_SPHERE].size() >= 2) axis = (spheres[selected[SEL_SPHERE][1]].pos -  spheres[selected[SEL_SPHERE][0]].pos).normalize(); 
            if(*numcenter<=0) center = (spheres[selected[SEL_SPHERE][0]].pos +  spheres[selected[SEL_SPHERE][1]].pos) / 2;
            break;
    }
    loopv(selected[SEL_SPHERE])
    {
        Sphere &s = spheres[selected[SEL_SPHERE][i]];
        s.pos = (s.pos - center).rotate(radians(*angle), axis) + center;
    }
}
COMMAND(rotspheres, "fii");

void seteye()
{
    if(selected[SEL_SPHERE].size() >= 1)
    {
        loopv(spheres) if(spheres[i].eye) spheres[i].eye = false;
        spheres[selected[SEL_SPHERE][0]].eye = true;
    }
    selected[SEL_SPHERE].setsize(0);
}
COMMAND(seteye, "");

void setsize(float *size)
{
    loopv(selected[SEL_SPHERE]) spheres[selected[SEL_SPHERE][i]].size = max(*size, 1.0f);
    selected[SEL_SPHERE].setsize(0);
}
COMMAND(setsize, "f");
        
void stepanim()
{
    stepping = true;
    animmode = true;
}
COMMAND(stepanim, "");

void invertsticky()
{
    loopv(spheres) spheres[i].sticky = !spheres[i].sticky;
}
COMMAND(invertsticky, "");

void setsticky(int *n)
{
    loopv(spheres) spheres[i].sticky = *n>0;
}
COMMAND(setsticky, "i");

VAR(showmverts, 0, 1, 1);
VAR(showjnames, 0, 1, 1);
VAR(mapjoints, 0, 1, 1);

void makesticky()
{
    loopv(selected[SEL_SPHERE]) spheres[selected[SEL_SPHERE][i]].sticky = !spheres[selected[SEL_SPHERE][i]].sticky;
    selected[SEL_SPHERE].setsize(0);
    if(dragging>=0) spheres[dragging].sticky = !spheres[dragging].sticky;
}
COMMAND(makesticky, "");

void constraindist()
{
    if(selected[SEL_SPHERE].size()>=2)
    {
        loopi(selected[SEL_SPHERE].size()-1)
            constraints.add(new DistConstraint(selected[SEL_SPHERE][i], selected[SEL_SPHERE][i+1]));
    }
    selected[SEL_SPHERE].setsize(0);
}
COMMAND(constraindist, "");

void updatedist()
{
    loopv(constraints) 
    {
        DistConstraint *d = dynamic_cast<DistConstraint *>(constraints[i]);
        if(d) d->update();
    }
}
COMMAND(updatedist, "");

void updateconstraints()
{
    loopv(constraints) constraints[i]->update();
}
COMMAND(updateconstraints, "");

void addtri()
{
    if(selected[SEL_SPHERE].size()>=3)
        tris.add(Tri(selected[SEL_SPHERE][0], selected[SEL_SPHERE][1], selected[SEL_SPHERE][2]));
    selected[SEL_SPHERE].setsize(0);
}
COMMAND(addtri, "");

void delconstraints()
{
    loopv(constraints)
    {
        Constraint *c = constraints[i];
        int numused = 0;
        loopk(3) loopvj(selected[k])
        {
            if(c->uses(k, selected[k][j])) numused++;
            else goto nextconstraint;
        }
        if(numused>=2) constraints.remove(i--);
    nextconstraint:;
    }
    loopk(3) selected[k].setsize(0);
}
COMMAND(delconstraints, "");

void printconstraints()
{
    loopv(constraints)
    {
        Constraint *c = constraints[i];
        int numused = 0;
        loopk(3) loopvj(selected[k])
        {
            if(c->uses(k, selected[k][j])) numused++;
            else goto nextconstraint;
        }
        if(numused>=2) c->printconsole();
    nextconstraint:;
    }
    loopk(3) selected[k].setsize(0);
}
COMMAND(printconstraints, "");

void bindjoint()
{
    if(selected[SEL_SPHERE].size()>0 && selected[SEL_TRI].size()>0 && selected[SEL_JOINT].size()>0)
    {
        Joint &j = joints[selected[SEL_JOINT][0]];
        j.tri = selected[SEL_TRI][0];
        loopk(3) j.spheres[k] = -1;
        Vec3 pos(0, 0, 0);
        loopi(min(selected[SEL_SPHERE].size(), 3)) { j.spheres[i] = selected[SEL_SPHERE][i]; pos += spheres[selected[SEL_SPHERE][i]].pos; }
        pos /= min(selected[SEL_SPHERE].size(), 3);
        j.tridiff = Matrix3x4(tris[j.tri].orient, tris[j.tri].orient.transform(-pos));
    }
    selected[SEL_SPHERE].setsize(0);
    selected[SEL_TRI].setsize(0);
    selected[SEL_JOINT].setsize(0);
}
COMMAND(bindjoint, "");

void fixmodeloffset()
{
    loopv(spheres) spheres[i].pos.z += moffset;
    loopv(joints)
    {
        Joint &j = joints[i];
        j.tridiff.a.w -= j.tridiff.a.z*moffset;
        j.tridiff.b.w -= j.tridiff.b.z*moffset;
        j.tridiff.c.w -= j.tridiff.c.z*moffset;
    }
}
COMMAND(fixmodeloffset, "");

void constrainrot(int *angle)
{
    if(selected[SEL_TRI].size()>=2)
        constraints.add(new RotConstraint(selected[SEL_TRI][0], selected[SEL_TRI][1], *angle <= 0 ? 60 : *angle));
    selected[SEL_TRI].setsize(0);
}
COMMAND(constrainrot, "i");

void printjointmap()
{
    loopv(joints)
    {
        Joint &j = joints[i];
        if(j.tri<0) continue;
        printf("joint %s(%d): tri %d, spheres ", j.name, i, j.tri);
        loopk(3) if(j.spheres[k]>=0) printf(k ? "/%d" : "%d", j.spheres[k]);
        printf("\n");
    }
}
COMMAND(printjointmap, "");

void unmapjoints()
{
    Vector<Vec3> unmap;
    Vector<int> counts;
    loopv(spheres) { unmap.add(Vec3(0, 0, 0)); counts.add(0); }
    loopv(joints)
    {
        Joint &j = joints[i];
        if(j.tri<0) continue;
        loopk(3) if(j.spheres[k]>=0)
        {
            int sphere = j.spheres[k];
            unmap[sphere] += j.orient.transposedtransform(spheres[sphere].pos);
            counts[sphere]++;
        }
    }
    loopv(spheres) if(counts[i]) spheres[i].oldpos = spheres[i].pos = unmap[i] / counts[i];
}
COMMAND(unmapjoints, "");

void movespheres(const Vec3 &pos)
{
    loopv(selected[SEL_SPHERE]) spheres[selected[SEL_SPHERE][i]].pos += pos;
    selected[SEL_SPHERE].setsize(0);
}
ICOMMAND(movespheres, "fff", (float *x, float *y, float *z), movespheres(Vec3(*x, *y, *z)));

void getpos()
{
    loopv(selected[SEL_SPHERE])
    {
        int idx = selected[SEL_SPHERE][i];
        Vec3 pos = spheres[idx].pos;
        pos.z -= moffset;
        pos /= mscale;
        conoutf("%d: %f, %f, %f", idx, pos.x, pos.y, pos.z);
    }
}
COMMAND(getpos, "");

void savepos(int *n)
{
    if(*n < 0 || *n > 2) return;
    loopv(spheres) spheres[i].saved[*n] = spheres[i].pos;
    conoutf("saved positions %d", *n);
}
COMMAND(savepos, "i");

void loadpos(int *n)
{
    if(*n < 0 || *n > 2) return;
    loopv(spheres) spheres[i].oldpos = spheres[i].pos = spheres[i].saved[*n];
    conoutf("loaded positions %d", *n);
}
COMMAND(loadpos, "i");

void writecfg(const char *name)
{
    if(!name[0]) name = "ragdoll.cfg";
    name = path(name, true);
    FILE *f = fopen(name, "w");
    if(!f) { conoutf(CON_ERROR, "failed writing %s", name); return; }
    loopv(spheres) 
    {
        Vec3 pos = spheres[i].pos;
        pos.z -= moffset;
        pos /= mscale;
        fprintf(f, "rdvert %f %f %f", pos.x, pos.y, pos.z);
        if(spheres[i].size > 1)
            fprintf(f, " %f", spheres[i].size);
        fprintf(f, "\n");
        if(spheres[i].eye) fprintf(f, "rdeye %d\n", i);
    }
    loopv(tris) fprintf(f, "rdtri %d %d %d\n", tris[i].sphere1, tris[i].sphere2, tris[i].sphere3);
    loopv(joints) if(joints[i].tri >= 0)
    {
        Joint &j = joints[i];
        fprintf(f, "rdjoint %d %d", i, j.tri);
        loopk(3) if(j.spheres[k] >= 0) fprintf(f, " %d", j.spheres[k]);
        fprintf(f, "\n");
    }
    loopv(constraints) constraints[i]->writecfg(f);
    fclose(f);
    conoutf("wrote %s", name);
}
COMMAND(writecfg, "s");

FVAR(cursorsensitivity, 1e-3f, 1, 1000);
FVAR(sensitivity, 1e-3f, 3, 1000);

void mousemove(int x, int y)
{
    if(cursormode)
    {
        float sens = cursorsensitivity/500.0f;    
        curx = clamp(curx + x*sens*screen->h/float(screen->w), 0.0f, 1.0f);
        cury = clamp(cury - y*sens, 0.0f, 1.0f);
        return;
    } 
    float sens = sensitivity/33.0f; 
    camera.yaw += x*sens;
    camera.pitch -= y*sens;
    if(y > 0) camera.pitch = min(camera.pitch, 90.0f);
    if(y < 0) camera.pitch = max(camera.pitch, -90.0f);
}

bool grabmouse = false;

void checkinput()
{
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
        case SDL_QUIT:
            quit();

        #if !defined(WIN32) && !defined(__APPLE__)
        case SDL_VIDEORESIZE:
            resizescreen(event.resize.w, event.resize.h);
            break;
        #endif

        case SDL_ACTIVEEVENT:
            if(event.active.state & SDL_APPINPUTFOCUS)
                grabmouse = event.active.gain!=0;
            else if(event.active.gain)
                grabmouse = true;
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            keypress(event.key.keysym.sym, event.key.state==SDL_PRESSED, event.key.keysym.unicode);
            break;

        case SDL_MOUSEMOTION:
            #ifndef WIN32
            if(!(screen->flags & SDL_FULLSCREEN) && grabmouse)
            {
                #ifdef __APPLE__
                if(event.motion.y == 0) break;  //let mac users drag windows via the title bar
                #endif
                if(event.motion.x == screen->w / 2 && event.motion.y == screen->h / 2) break;
                SDL_WarpMouse(screen->w / 2, screen->h / 2);
            }
            if((screen->flags & SDL_FULLSCREEN) || grabmouse)
            #endif
                mousemove(event.motion.xrel, event.motion.yrel);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            keypress(-event.button.button, event.button.state!=0, 0);
            break;
        }
    }
}

Vector<Vec3> sphereverts;
Vector<ushort> sphereidxs;

void gensphere(int slices, int stacks)
{
    sphereverts.setsize(0);
    float ds = 1.0f/slices, dt = 1.0f/stacks, t = 1.0f;
    loopi(stacks+1)
    {
        float rho = M_PI*(1-t), s = 0.0f;
        loopj(slices+1)
        {
            float theta = j==slices ? 0 : 2*M_PI*s;
            Vec3 &v = sphereverts.add();
            v = Vec3(-sin(theta)*sin(rho), cos(theta)*sin(rho), cos(rho));
            s += ds;
        }
        t -= dt;
    }

    sphereidxs.setsize(0);
    loopi(stacks)
    {
        loopk(slices)
        {
            int j = i%2 ? slices-k-1 : k;

            sphereidxs.add() = i*(slices+1)+j;
            sphereidxs.add() = (i+1)*(slices+1)+j;
            sphereidxs.add() = i*(slices+1)+j+1;

            sphereidxs.add() = i*(slices+1)+j+1;
            sphereidxs.add() = (i+1)*(slices+1)+j;
            sphereidxs.add() = (i+1)*(slices+1)+j+1;
        }
    }
}

void rendersphere()
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Vec3), sphereverts.getbuf());
    glDrawElements(GL_TRIANGLES, sphereidxs.size(), GL_UNSIGNED_SHORT, sphereidxs.getbuf());
    glDisableClientState(GL_VERTEX_ARRAY);
}

bool movecam(const Vec3 &vel)
{
    camera.origin += vel;
    return true;
}

FVAR(grav, -1000, -50, 1000); 
FVAR(airfric, 0, 0.99f, 1);
FVAR(groundfric, 0, 0.7f, 1);
FVAR(velcut, 0, 1e-5f, 1);

void movesphere(Sphere &s, int idx, float ts)
{
    Vec3 curpos = s.pos;
    if(idx==dragging || (dragging>=0 && selected[SEL_SPHERE].find(idx)>=0)) s.pos = campos + camdir*dragdist*camscale + s.dragoffset;
    else if(animmode && !s.sticky && ts>0)
    {
        Vec3 dpos = (curpos - s.oldpos)*(s.collided ? groundfric : airfric);
        if(dpos.squaredlen() > velcut) s.pos += dpos;
        s.pos += Vec3(0, 0, grav)*ts*ts;
    }
    s.collided = false;
    if(s.pos.z - s.size*spherescale < 0) { s.collided = true; s.pos.z = s.size*spherescale; }
    s.oldpos = curpos;
}

void constrainspheres()
{
    loopv(tris) tris[i].calcorient();
    loopv(spheres)
    {
        Sphere &s = spheres[i];
        s.newpos = Vec3(0, 0, 0);
        s.avg = 0;
    }
    loopv(constraints) constraints[i]->apply();
    loopv(spheres)
    {
        Sphere &s = spheres[i];
        if(s.avg && !s.sticky && i!=dragging && (dragging<0 || selected[SEL_SPHERE].find(i)<0)) s.pos = s.newpos / s.avg;
    }
}

void animatespheres(float ts)
{
    loopv(spheres)
    {
        Sphere &s = spheres[i];
        movesphere(s, i, ts);
    }
    loopk(iterconstraints+1) constrainspheres();
    if(stepping) { animmode = false; stepping = false; }
}

void drawconsole()
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    int w = screen->w, h = screen->h;
    gettextres(w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w*3, h*3, 0, -1, 1);
  
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);

    renderconsole(w, h);

    int abovehud = h*3 - FONTH/2;
    rendercommand(FONTH/2, abovehud - FONTH*3/2, w*3-FONTH);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    
}

FVAR(movespeed, 0, 5, 1000);

FVAR(depthoffset, -1e8f, 2e-4f, 1e8f);

void enabledepthoffset()
{
    GLMatrixf offsetmatrix;
    offsetmatrix.projectionmatrix();
    offsetmatrix.v[14] += depthoffset * offsetmatrix.v[10];
    
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(offsetmatrix.v);
    glMatrixMode(GL_MODELVIEW);
}

void disabledepthoffset()
{
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char **argv)
{
    if(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO)<0) fatal("Unable to initialize SDL: %s", SDL_GetError());

    SDL_WM_SetCaption("ragdoll", NULL);
    SDL_ShowCursor(0);

    setupscreen(1024, 768);

    if(!execfile("keymap.cfg")) fatal("cannot find keymap");
    if(!execfile("font.cfg")) fatal("cannot find font definitions");
    if(!setfont("default")) fatal("no default font specified");

    gensphere(12, 6);

    camera.origin = Vec3(0, 0, 5);

    execfile("autoexec.cfg");

    for(;;)
    {
        int millis = SDL_GetTicks();
        curtime = millis - lastmillis;
        if(curtime < 10) { SDL_Delay(10-curtime); continue; }
        lastmillis = millis;

        checkinput();

        glDepthFunc(GL_LESS);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);

        glViewport(0, 0, screen->w, screen->h);
        glClearDepth(1.0);
        glClearColor(0, 0, 0, 1);
        glClear(GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT);

        camera.setupmatrices();

        glDisable(GL_TEXTURE_2D);

        GLMatrixf mvp, invmvp;
        mvp.mvpmatrix();
        if(invmvp.invert(mvp))
        {
            campos = invmvp.transform3(Vec3(cursormode ? 2*curx-1 : 0, cursormode ? 2*cury-1 : 0, -1));
            camdir = (invmvp.transform3(Vec3(cursormode ? 2*curx-1 : 0, cursormode ? 2*cury-1 : 0, 1)) - campos).normalize(); 
            camscale = 1.0f/camdir.dot(Vec3(radians(camera.yaw), radians(camera.pitch)));
        }

        hover();

        if(curtime)
        {
            if(forward) movecam(Vec3(radians(camera.yaw), radians(camera.pitch))*(curtime/1000.0f*movespeed));
            if(backward) movecam(-Vec3(radians(camera.yaw), radians(camera.pitch))*(curtime/1000.0f*movespeed));
            if(left) movecam(Vec3(radians(camera.yaw-90), 0)*(curtime/1000.0f*movespeed));
            if(right) movecam(Vec3(radians(camera.yaw+90), 0)*(curtime/1000.0f*movespeed));
            if(up) movecam(Vec3(radians(camera.yaw), radians(camera.pitch+90))*(curtime/1000.0f*movespeed));
            if(down) movecam(Vec3(radians(camera.yaw), radians(camera.pitch-90))*(curtime/1000.0f*movespeed));

            animatespheres(curtime/1000.0f);
            if(mapjoints)
            {
                loopv(joints)
                {
                    Joint &j = joints[i];
                    if(!tris.inrange(j.tri)) { j.moved = false; continue; }
                    Vec3 pos(0, 0, 0);
                    int total = 0;
                    loopk(3) if(spheres.inrange(j.spheres[k])) { pos += spheres[j.spheres[k]].pos; total++; }
                    if(!total) continue;
                    pos /= total;
                    Matrix3x3 inv;
                    inv.transpose(tris[j.tri].orient);
                    j.orient = Matrix3x4(inv, pos) * j.tridiff;
                    j.moved = true;
                }
                loopv(joints)
                {
                    Joint &j = joints[i];
                    if(j.moved) continue;
                    for(int parent = j.parent; parent >= 0; parent = joints[parent].parent)
                    {
                        if(joints[parent].moved)
                        {
                            j.orient = joints[parent].orient;
                            j.moved = true;
                            break;
                        }
                    }
                }
            }
            else
            {
                loopv(joints) joints[i].orient.identity();
            }
        }

        glColor3f(0.5f, 0, 0.5f);
        glBegin(GL_QUADS);
        float fsz = 1000;
        glVertex3f(-fsz, fsz, 0);
        glVertex3f(-fsz, -fsz, 0);
        glVertex3f(fsz, -fsz, 0);
        glVertex3f(fsz, fsz, 0);
        glEnd();

        enabledepthoffset();
        glDepthMask(GL_FALSE);
        glColor3f(0, 0, 0);
        if(showspheres) loopv(spheres)
        {
            const Sphere &s = spheres[i];
            glBegin(GL_TRIANGLE_FAN);
            glVertex3f(s.pos.x, s.pos.y, 0);
            loopk(13) glVertex3f(s.pos.x + s.size*spherescale*cosf(k/12.0f*2*M_PI), s.pos.y + s.size*spherescale*sinf(k/12.0f*2*M_PI), 0);
            glEnd();
        }
        glDepthMask(GL_TRUE);
        disabledepthoffset();

        glDisable(GL_CULL_FACE);
        bool selecting = false;
        if(showtris) loopv(tris) 
        {
            Tri &t = tris[i];
            if(hovertype==SEL_TRI && hoveridx==i)
            {
                glColor3f(1, 0, 0);
                selecting = true;
            } 
            else if(checkselected(i, SEL_TRI)) glColor3f(1, 1, 1);
            else glColor3f(0.5f, 0.5f, 0);
            t.render();
        }
        glEnable(GL_CULL_FACE);

        if(showjoints) loopv(joints)
        {
            const Joint &j = joints[i];
            if(j.hide && showjoints <= 1) continue;
            if(hovertype==SEL_JOINT && hoveridx==i) { glColor3f(1, 0, 0); selecting = true; }
            else if(checkselected(i, SEL_JOINT)) glColor3f(0, 0.5f, 1);
            else if(!j.used) glColor3f(j.tri>=0 ? 0 : 0.5f, 0.5f, 0.5f);
            else glColor3f(j.tri>=0 ? 0.5f : 1, 1, 1);
            glPushMatrix();
            Vec3 pos = j.getpos();
            glTranslatef(pos.x, pos.y, pos.z);
            glScalef(0.1f, 0.1f, 0.1f);
            rendersphere();
            glPopMatrix();
        }
        if(showjoints && joints.size() > 1)
        {
            glDepthFunc(GL_ALWAYS);
            glBegin(GL_LINES);
            loopv(joints)
            {
                const Joint &j = joints[i];
                if(j.hide) continue;
                Vec3 pos = j.getpos();
#if 0
                glColor3f(1, 0, 0);
                glVertex3fv(pos.v);
                Vec3 right = pos + Vec3(j.orient.a)*0.3f;
                glVertex3fv(right.v);
                glColor3f(0, 1, 0);
                glVertex3fv(pos.v);
                Vec3 up = pos + Vec3(j.orient.b)*0.3f;
                glVertex3fv(up.v);
                glColor3f(1, 1, 0);
                glVertex3fv(pos.v);
                Vec3 forward = pos + Vec3(j.orient.c)*0.3f;
                glVertex3fv(forward.v);
#endif
                if(!joints.inrange(j.parent) || joints[j.parent].hide) continue;
                glColor3f(1, 1, 1);
                glVertex3fv(pos.v);
                glColor3f(0, 0, 1);
                Vec3 ppos = joints[j.parent].getpos();
                glVertex3fv(ppos.v);
#if 0
                if(tris.inrange(j.tri))
                {
                    Vec3 dst(0, 0, 0);
                    int total = 0;
                    loopk(3) if(spheres.inrange(j.spheres[k])) { dst += spheres[j.spheres[k]].pos; total++; }
                    if(total) 
                    {
                        dst /= total;
                        glColor3f(1, 1, 1);
                        glVertex3fv(pos.v);
                        glColor3f(0, 0, 1);
                        glVertex3fv(dst.v);
                    }
                }
#endif
            }
            glEnd();
            glDepthFunc(GL_LESS);
        }
        if(showmverts && mtris.size() > 0)
        {
            loopv(mverts)
            {
                MVert &v = mverts[i];
                v.curpos = Vec3(0, 0, 0);
                loopj(4) if(v.weights[j]) v.curpos += joints[v.joints[j]].orient.transform(v.pos)*v.weights[j];
            }
                
            glColor3f(0.5f, 0.5f, 0.5f);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glBegin(GL_TRIANGLES);
            loopv(mtris)
            {
                MTri &t = mtris[i];
                loopj(3) glVertex3fv(mverts[t.vert[j]].curpos.v);
            }
            glEnd();
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        if(showspheres) loopv(spheres)
        {
            const Sphere &s = spheres[i];
            if(hovertype==SEL_SPHERE && hoveridx==i)
            {
                glColor3f(1, 0, 0);
                selecting = true;
            }
            else if(checkselected(i))
                glColor3f(0, 0.5f, 1);
            else if(s.sticky) glColor3f(1, 1, 0);
            else glColor3f(0, 1, 0);
            glPushMatrix();
            glTranslatef(s.pos.x, s.pos.y, s.pos.z);
            glScalef(s.size*spherescale, s.size*spherescale, s.size*spherescale);
            rendersphere();
            glPopMatrix();
        }

        
        enabledepthoffset();
        if(showconstraints) loopv(constraints) constraints[i]->render();
        disabledepthoffset();

        if(showspheres && selected[SEL_SPHERE].size() > 1)
        {
            glDepthFunc(GL_ALWAYS);
            glBegin(GL_LINE_STRIP);
            loopv(selected[SEL_SPHERE])
            {
                float shade = i/float(selected[SEL_SPHERE].size()-1);
                glColor3f(1-shade, shade, 0);
                glVertex3fv(spheres[selected[SEL_SPHERE][i]].pos.v);
            }
            glEnd();
            glDepthFunc(GL_LESS);
        }

        if(showtris && selected[SEL_TRI].size() > 1)
        {
            glDepthFunc(GL_ALWAYS);
            glBegin(GL_LINE_STRIP);
            loopv(selected[SEL_TRI])
            {
                float shade = i/float(selected[SEL_TRI].size()-1);
                glColor3f(1-shade, shade, 0);
                const Tri &t = tris[selected[SEL_TRI][i]];
                Vec3 center = (spheres[t.sphere1].pos + spheres[t.sphere2].pos + spheres[t.sphere3].pos) / 3;
                glVertex3fv(center.v);
            }
            glEnd();
            glDepthFunc(GL_LESS);
        }

        if(showspheres)
        {
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glEnable(GL_TEXTURE_2D);
            loopv(spheres) if(spheres[i].eye)
            {
                Sphere &s = spheres[i];
                glPushMatrix();
                glTranslatef(s.pos.x, s.pos.y, s.pos.z - 0.2f);
                glRotatef(camera.yaw-180, 0, 0, 1);
                glRotatef(camera.pitch-90, 1, 0, 0);
                float sz = 0.15f/FONTH;
                glScalef(-sz, sz, -sz);
                glTranslatef(-text_width("eye")/2, 0, 0);
                draw_text("eye", 0, 0, 0, 0xFF, 0);
                glPopMatrix();
            }
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }    
        
        if(showjoints && showjnames && joints.size() > 1)
        {
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glEnable(GL_TEXTURE_2D);
            loopv(joints)
            {
                const Joint &j = joints[i];
                if(j.hide && showjoints <= 1) continue;
                glPushMatrix();
                Vec3 pos = j.getpos();
                glTranslatef(pos.x, pos.y, pos.z + 0.3f);
                glRotatef(camera.yaw-180, 0, 0, 1);
                glRotatef(camera.pitch-90, 1, 0, 0);
                float sz = 0.15f/FONTH;
                glScalef(-sz, sz, -sz);
                glTranslatef(-text_width(j.name)/2, 0, 0);
                Vec3 color;
                if(hovertype==SEL_JOINT && hoveridx==i) color = Vec3(1, 0, 0);
                else if(checkselected(i, SEL_JOINT)) color = Vec3(0, 0.5f, 1);
                else if(!j.used) color = Vec3(j.tri>=0 ? 0 : 0.5f, 0.5f, 0.5f);
                else color = Vec3(j.tri>=0 ? 0.5f : 1, 1, 1);
                draw_text(j.name, 0, 0, int(color.x*0xFF), int(color.y*0xFF), int(color.z*0xFF));
                glPopMatrix();
            }
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }

        if(!selecting)
        {
            Vec3 inspos = campos + camdir*spawndist*camscale;
            float insz = 0.2f;
            glColor3f(0, 0, 0.5f);
            glPushMatrix();
            glTranslatef(inspos.x, inspos.y, inspos.z);
            glScalef(insz, insz, insz);
            rendersphere();
            glPopMatrix();
            
            enabledepthoffset();
            glColor3f(0, 0, 0);
            glBegin(GL_TRIANGLE_FAN);
            glVertex3f(inspos.x, inspos.y, 0);
            loopk(13) glVertex3f(inspos.x + insz*cosf(k/12.0f*2*M_PI), inspos.y + insz*sinf(k/12.0f*2*M_PI), 0);
            glEnd();
            disabledepthoffset();
        }

        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, 1, 0, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glColor3f(0, 0, 1);
        float ch = 0.005f, cw = ch*float(screen->h)/screen->w;
        if(cursormode)
        {
            glBegin(GL_QUADS);
            glVertex2f(curx-cw, cury-ch);
            glVertex2f(curx-cw, cury+ch);
            glVertex2f(curx+cw, cury+ch);
            glVertex2f(curx+cw, cury-ch);
            glEnd();
            
            glColor3f(0, 0.75f, 0.75f);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glBegin(GL_QUADS);
            glVertex2f(curx, cury-2*ch);
            glVertex2f(curx-2*cw, cury);
            glVertex2f(curx, cury+2*ch);
            glVertex2f(curx+2*cw, cury);

            glVertex2f(curx-cw, cury-ch);
            glVertex2f(curx-cw, cury+ch);
            glVertex2f(curx+cw, cury+ch);
            glVertex2f(curx+cw, cury-ch);
            glEnd();
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        else
        {
            glBegin(GL_QUADS);
            glVertex2f(0.5f, 0.5f-ch);
            glVertex2f(0.5f-cw, 0.5f);
            glVertex2f(0.5f, 0.5f+ch);
            glVertex2f(0.5f+cw, 0.5f);
            glEnd();
        }

        drawconsole();

        SDL_GL_SwapBuffers();
    }

    return EXIT_SUCCESS;
}

