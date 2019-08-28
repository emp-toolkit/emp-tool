#ifndef GROUP_H
#define GROUP_H

#include <emp-tool/emp-tool.h>
#include "emp-ot/emp-ot.h"
#include <string>
#include <cstring>
using std::string;

class Point;
class Group;
class BigInt
{
    bn_t n;

public:
    BigInt()
    {
        bn_new(n);
        bn_set_2b(n,1);
    }
    BigInt(const BigInt &oth)
    {
        bn_copy(n, oth.n);
    }
    BigInt &operator=(const BigInt &oth)
    {
        bn_copy(n, oth.n);
        return *this;
    }
    ~BigInt()
    {
        bn_free(n);
    }
    void from_dec(const string &s)
    {

        char *p_str;
        p_str = new char[s.length() + 1];
        memcpy(p_str, s.c_str(), s.length());
        p_str[s.length()] = 0;
        bn_read_str(n, p_str,s.length(),10);
        delete[] p_str;
    }
    void from_hex(const char *s)
    {
        bn_read_str(n,s,strlen(s),16);
    }
    char* to_hex()
    {
        int len=bn_size_str(n,16);
        char *number_str = new char[len];
        bn_write_str(number_str,len,n,16);
        return number_str;
    }

    BigInt &add(const BigInt &oth)
    {
        bn_add(n, n, oth.n);
        return *this;
    }
    // mod
    BigInt &mod(const BigInt &oth)
    {
        bn_mod(n, n, oth.n, NULL);
        return *this;
    }
    friend class Group;
    friend class Point;
};

void init_relic()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;
    if (core_init() != RLC_OK)
    {
        core_clean();
        exit(1);
    }
}
class Point
{
private:
    eb_t p;

public:
    Point() {eb_new(p);}
    ~Point()
    {
        eb_free(p);
    }

    Point(const Point &oth)
    {
        eb_copy(p, oth.p);
    }
    Point &operator=(const Point &oth)
    {
        eb_copy(p, oth.p);
        return *this;
    }

    friend class Group;
};
class Group
{
private:

public:
    Group()
    {
        init_relic();
        eb_param_set(EBACS_B251);
    }
    ~Group()
    {
    }
    void get_generator(Point &g){
        eb_curve_get_gen(g.p);
    }
    void precompute()
    {
        //TODO
    }
    //precomputation table for some group element
    friend class Point;

    void init(Point &p);
    void add(Point &res, const Point &lhs, const Point &rhs);
    void inv(Point &res, const Point &p);
    void mul(Point &res, const Point &lhs, const BigInt &m);
    char* to_hex(const Point &p) const;
    void from_hex(Point &p,const char *s) const;
};




void Group::init(Point &p)
{
    eb_new(p.p);
}

void Group::add(Point &res, const Point &lhs, const Point &rhs)
{
    //EC_POINT_add(ec_group, res.p, lhs.p, rhs.p, NULL);
    eb_add(res.p,lhs.p,rhs.p);
    eb_norm(res.p,res.p);
}

void Group::inv(Point &res, const Point &p)
{
    eb_neg(res.p,p.p);
    eb_norm(res.p,res.p);
}
void Group::mul(Point &res, const Point &lhs, const BigInt &m)
{
    eb_mul(res.p,lhs.p,m.n);
    eb_norm(res.p,res.p);
}

char* Group::to_hex(const Point &p) const
{
    
    /*int len=eb_size_bin(p.p,1);
    uint8_t *s=new uint8_t[len+4];
    memset(s,0,len+4);
    memcpy(s,&len,4);
    eb_write_bin(s+4,len,p.p,0);
    return (char*)s;*/
    char *sx,*sy;
    int lenx=fb_size_str(p.p->x,16);
    int leny=fb_size_str(p.p->y,16);
    sx=new char[lenx+1];
    sy=new char[leny+1];
    
    fb_write_str(sx,lenx,p.p->x,16);
    fb_write_str(sy,leny,p.p->y,16);
    int len=3+lenx+leny;
    char *res=new char[len+1];
    memset(res,0,len+1);
    strcat(res,"(");
    strcat(res,sx);
    strcat(res,",");
    strcat(res,sy);
    strcat(res,")");
    delete[] sx;
    delete[] sy;
    return res;
}

void Group::from_hex(Point &p,const char *s) const
{
    /*int len;
    memcpy(&len,s,4);
    eb_read_bin(p.p,(uint8_t*)s+4,len);
    */fb_t x,y;
    eb_new(p.p);
    int len=strlen(s);
    char *t=new char[len+1];
    memset(t,0,len+1);
    t[len]=0;
    strcpy(t,s);
    for(int i=0;i<len;i++){
        if(s[i]==','){
            t[0]=0;
            t[i]=0;
            t[len-1]=0;
            fb_read_str(x,t+1,i,16);
            fb_read_str(y,t+1+i,len-i-2,16);
        }
    }
    fb_copy(p.p->x,x);
    fb_copy(p.p->y,y);
}

#endif
