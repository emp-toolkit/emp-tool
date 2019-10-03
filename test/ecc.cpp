#include "emp-tool/emp-tool.h"
#include <iostream>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>

using namespace std;
using namespace emp;

int main() {
	Group G;
	Point a, b, c, d, e;
	G.init(a);
	G.init(b);
	G.init(c);
	G.init(d);
	G.init(e);
	BigInt ia, ib, ic, id;
	G.get_rand_bn(ia);
	G.get_rand_bn(ib);
	G.get_rand_bn(ic);
	G.get_rand_bn(id);
	G.mul_gen(a, ia);//g^a
	G.mul_gen(b, ib);//g^a
	ic = ia;
	ic.add(ib);
	G.mul_gen(c, ic);//g^{a+b}
	G.add(d, a, b);//g^ag^b
	int res = EC_POINT_cmp(G.ec_group, d.p, c.p,nullptr);	
	cout << res<<endl;
	return 0;
}
