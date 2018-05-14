#include <stdio.h>
#include <x86intrin.h>
int main(){
	unsigned long long r;
	_rdseed64_step(&r);
	return 0;
}
