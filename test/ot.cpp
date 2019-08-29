#include <iostream>
#include <emp-tool/emp-tool.h>
#include "emp-ot/emp-ot.h"
using namespace std;
using namespace emp;

//stupid hash
string H(const Group &G, const Point &p)
{
    return G.to_hex(p);
}

string E(string key, string m)
{
    for (int i = 0; i < (int)m.length(); i++)
        m[i] ^= key[i];
    return m;
}
string D(string key, string c)
{
    for (int i = 0; i < (int)c.length(); i++)
        c[i] ^= key[i];
    return c;
}

void send_string(NetIO *io, const Group &G, const string &As)
{

    int len = As.length();
    io->send_data(&len, 4);
    io->send_data(As.c_str(), len);
}

void recv_string(NetIO *io, const Group &G, string &As)
{
    char tmp[256];
    memset(tmp, 0, sizeof tmp);
    int len;
    io->recv_data(&len, 4);
    io->recv_data(tmp, len);
    As = string(tmp);
}
void send_point(NetIO *io, const Group &G, const Point &A)
{
    char *data = G.to_hex(A);
    int len = strlen(data);
    io->send_data(&len, 4);
    io->send_data(data, len);
}

void recv_point(NetIO *io, const Group &G, Point &A)
{

    int len;
    char *data;
    io->recv_data(&len, 4);
    data = new char[len + 1];
    data[len] = 0;
    io->recv_data(data, len);
    G.from_hex(A, data);
}

string ot(NetIO *io, int party, string m[], int c)
{
    Group G;
    BigInt a, b,order;
    G.get_order(order);
    if (party == ALICE)
    {
        //a.from_dec("324123");
        a.rand_mod(order);
    }
    else
    {
        b.rand_mod(order);
    }
    //stupid random 

    
    Point A, B;
    Point g;
    G.init(A);
    G.init(B);
    G.init(g);
    G.get_generator(g); 


    if (party == ALICE)
    {
        A = g;
        //G.mul(A, A, a);
        G.mul_gen(A,a);
        //send A
        send_point(io,G,A);
    }
    else
    {
        recv_point(io,G,A);
    }

    string kb;
    if (party == BOB)
    {
        if (c == 0)
        {
            B = g;
            G.mul(B, B, b);
        }
        else
        {
            B = g;
            G.mul(B, B, b);
            G.add(B, B, A);
        }
        //send B
        send_point(io, G, B);

        Point t0;
        G.init(t0);
        t0 = A;
        G.mul(t0, t0, b);
        kb = H(G, t0);
    }
    else
    {
        recv_point(io, G, B);
    }
    string e[2];

    if (party == ALICE)
    {
        Point t1, t2, iv;
        G.init(t1);
        G.init(t2);
        G.init(iv);
        t1 = B;
        G.mul(t1, t1, a);

        t2 = B;

        iv = A;
        G.inv(iv, iv);
        G.add(t2, t2, iv);

        G.mul(t2, t2, a);
        string k0 = H(G, t1);
        string k1 = H(G, t2);
        e[0] = E(k0, m[0]);
        e[1] = E(k1, m[1]);

        send_string(io, G, e[0]);
        send_string(io, G, e[1]);
        return "";
    }
    else
    {
        recv_string(io, G, e[0]);
        recv_string(io, G, e[1]);
        return D(kb, e[c]);
    }
}

int main(int argc, char **argv)
{
#ifndef ECC_USE_RELIC
    cout<<"using openssl"<<endl;
#else
    cout<<"using relic"<<endl;
#endif

    if (argc < 2)
    {
        cout << "./ot <party>";
        exit(0);
    }

    int party, port = 12345;
    sscanf(argv[1], "%d", &party);
    NetIO *io = new NetIO(party == ALICE ? nullptr : "127.0.0.1", port);

    string m[2];
    m[0] = "hello";
    m[1] = "goodbye";

    cout << ot(io, party, m, 0) << endl;

    return 0;
}
