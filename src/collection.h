#ifndef __COLLECTION_H__
#define __COLLECTION_H__

#define loopv(v) loopi((v).size())
#define loopvj(v) loopj((v).size())
#define loopvk(v) loopk((v).size())
#define loopvl(v) loopl((v).size())
#define loopvrev(v) looprevi((v).size())

template<class T>
struct Vector
{
    static const int MINSIZE = 8;

    T *buf;
    int len, maxlen;

    Vector() : buf(NULL), len(0), maxlen(0) {}
    Vector(const Vector &v) : buf(NULL), len(0), maxlen(0) 
    {
        *this = v;
    }

    ~Vector() 
    { 
        setsize(0); 
        if(buf) delete[] (uchar *)buf; 
    }

    void resize(int sz)
    {
        int olen = maxlen;
        if(!maxlen) maxlen = max(MINSIZE, sz);
        else while(maxlen < sz) maxlen *= 2;
        if(maxlen <= olen) return;
        uchar *newbuf = new uchar[maxlen*sizeof(T)];
        if(olen > 0)
        {
            memcpy(newbuf, buf, olen*sizeof(T));
            delete[] (uchar *)buf;
        }
        buf = (T *)newbuf;
    }

    const Vector &operator=(const Vector &v)
    {
        setsize(0);
        if(v.len > maxlen) resize(v.len);
        loopv(v) add(v[i]);
        return *this;
    }

    T &add(const T &x)
    {
        if(len==maxlen) resize(len+1);
        new (&buf[len]) T(x);
        return buf[len++];
    }

    T &add()
    {
        if(len==maxlen) resize(len+1);
        new (&buf[len]) T;
        return buf[len++];
    }

    T *add(const T *v, int n)
    {
        if(len+n > maxlen) resize(len+n);
        memcpy(&buf[len], v, n*sizeof(T));
        len += n;
        return &buf[len-n];
    }

    bool inrange(size_t i) const { return i<size_t(len); }
    bool inrange(int i) const { return i>=0 && i<len; }

    T &pop() { return buf[--len]; }
    T &last() { return buf[len-1]; }
    void drop() { buf[--len].~T(); }

    bool empty() const { return !len; }
    bool full() const { return len==maxlen; }
    int size() const { return len; }
    int capacity() const { return maxlen; }

    T &operator[](int i) { return buf[i]; }
    const T &operator[](int i) const { return buf[i]; }

    void setsize(int i) { len = i; }

    T *getbuf() { return buf; }
    const T *getbuf() const { return buf; }

    void remove(int i, int n)
    {
        for(int p = i+n; p<len; p++) buf[p-n] = buf[p];
        len -= n;
    }

    T remove(int i)
    {
        T e = buf[i];
        for(int p = i+1; p<len; p++) buf[p-1] = buf[p];
        len--;
        return e;
    }

    template<class U>
    int find(const U &o)
    {
        loopi(len) if(buf[i]==o) return i;
        return -1;
    }

    void removeall(const T &o)
    {
        loopi(len) if(buf[i]==o) remove(i--);
    }

    void replaceallwithlast(const T &o)
    {
        if(!len) return;
        loopi(len-1) if(buf[i]==o) buf[i] = buf[--len];
        if(buf[len-1]==o) len--;
    }

    T &insert(int i, const T &e)
    {
        add();
        for(int p = len-1; p>i; p--) buf[p] = buf[p-1];
        buf[i] = e;
        return buf[i];
    }

    T *insert(int i, const T *e, int n)
    {
        if(len+n>maxlen) resize(len+n);
        loopj(n) add();
        for(int p = len-1; p>=i+n; p--) buf[p] = buf[p-n];
        loopj(n) buf[i+j] = e[j];
        return &buf[i];
    }

    void reverse()
    {
        loopi(len/2) swap(buf[i], buf[len-1-i]);
    }

    template<class ST>
    void sort(int (__cdecl *cf)(ST *, ST *), int i = 0, int n = -1) 
    { 
        qsort(&buf[i], n<0 ? len : n, sizeof(T), (int (__cdecl *)(const void *,const void *))cf); 
    }
};

static inline uint hash(const char *key)
{
    uint h = 5381;
    for(int i = 0, k; (k = key[i]); i++) h = ((h<<5)+h)^k;    // bernstein k=33 xor
    return h;
}

static inline bool compare(const char *x, const char *y)
{
    return !strcmp(x, y);
}

static inline uint hash(int key)
{
    return key;
}

static inline bool compare(int x, int y)
{
    return x==y;
}

template <class K, class T> struct Hashtable
{
    typedef K key;
    typedef const K const_key;
    typedef T value;
    typedef const T const_value;

    enum { CHUNKSIZE = 64 };

    struct chain      { T data; K key; chain *next; };
    struct chainchunk { chain chains[CHUNKSIZE]; chainchunk *next; };

    int size;
    int numelems;
    chain **table;
    chain *enumc;

    chainchunk *chunks;
    chain *unused;

    Hashtable(int size = 1<<10)
      : size(size)
    {
        numelems = 0;
        chunks = NULL;
        unused = NULL;
        table = new chain *[size];
        loopi(size) table[i] = NULL;
    }

    ~Hashtable()
    {
        if(table) delete[] table;
        deletechunks();
    }

    chain *insert(const K &key, uint h)
    {
        if(!unused)
        {
            chainchunk *chunk = new chainchunk;
            chunk->next = chunks;
            chunks = chunk;
            loopi(CHUNKSIZE-1) chunk->chains[i].next = &chunk->chains[i+1];
            chunk->chains[CHUNKSIZE-1].next = unused;
            unused = chunk->chains;
        }
        chain *c = unused;
        unused = unused->next;
        c->key = key;
        c->next = table[h];
        table[h] = c;
        numelems++;
        return c;
    }

    #define HTFIND(success, fail) \
        uint h = hash(key)&(size-1); \
        for(chain *c = table[h]; c; c = c->next) \
        { \
            if(compare(key, c->key)) return (success); \
        } \
        return (fail);

    T *access(const K &key)
    {
        HTFIND(&c->data, NULL);
    }

    T &access(const K &key, const T &data)
    {
        HTFIND(c->data, insert(key, h)->data = data);
    }

    T &operator[](const K &key)
    {
        HTFIND(c->data, insert(key, h)->data);
    }

    #undef HTFIND

    bool remove(const K &key)
    {
        uint h = hash(key)&(size-1);
        for(chain **p = &table[h], *c = table[h]; c; p = &c->next, c = c->next)
        {
            if(compare(key, c->key))
            {
                *p = c->next;
                c->data.~T();
                c->key.~K();
                new (&c->data) T;
                new (&c->key) K;
                c->next = unused;
                unused = c->next;
                numelems--;
                return true;
            }
        }
        return false;
    }

    void deletechunks()
    {
        for(chainchunk *nextchunk; chunks; chunks = nextchunk)
        {
            nextchunk = chunks->next;
            delete chunks;
        }
    }

    void clear()
    {
        if(!numelems) return;
        loopi(size) table[i] = NULL;
        numelems = 0;
        unused = NULL;
        deletechunks();
    }
};

#define enumerate(ht,k,e,t,f,b) loopi((ht).size)  for(Hashtable<k,t>::chain *enumc = (ht).table[i]; enumc; enumc = enumc->next) { Hashtable<k,t>::const_key &e = enumc->key; (void)e; t &f = enumc->data; (void)f; b; }

#endif

