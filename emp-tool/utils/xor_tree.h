#ifndef XORTREE_H__
#define XORTREE_H__
#include "emp-tool/circuits/bit.h"
/** @addtogroup BP
    @{
  */

//TODO: hardcoded with ssp=40, should at least support a range a ssp's
namespace emp {
template<int N=232, int M=232>
class XorTree{public:
	int n, ssp;
	bool matrix[N][M];
	block blockM[N][M];
	XorTree(int n, int ssp = 40) {
		this->n=n;
		this->ssp = ssp;
		uint8_t bM[N][M];
		PRG prg(fix_key);
		prg.random_data(bM, N*M);
		for(int i = 0; i < N; ++i)
			for(int j = 0; j < M; ++j) {
				matrix[i][j] = (bM[i][j]%2 == 1);
				blockM[i][j] = matrix[i][j] ? one_block(): zero_block();
			}
	}
	void circuitN(block* out, block * in, int dim = N) {
		assert(dim <= N);

		for(int i = 0; i < dim; ++i) {
			block res = zero_block();
			for(int j = 0; j < M; ++j) {
				res = xorBlocks(res, andBlocks(in[j], blockM[i][j]));
			}
			out[i] = xorBlocks(res, in[i+M]);
		}
	}

	void circuit(block * out, block * in) {
		int i ;
		for(i = 0; i < n/N; ++i) {
			circuitN(out+(i*N), in+(N+M)*i, N);
		}
		if(n%N != 0) {
			circuitN(out+(i*N) , in+(N+M)*i, n%N);
		}
	}

	void genN(bool* out, bool * in, PRG * prg, int dim = N) {
		assert(dim <= N);
		uint8_t tmp[M];
		prg->random_data(tmp, M);
		for(int i = 0; i < M; ++i)
			out[i] = (tmp[i]%2==1);

		for(int i = 0; i < dim; ++i) {
			bool res = false;
			for(int j = 0; j < M; ++j) {
				if( matrix[i][j] == 1)
					res = res ^ (out[j]);
			}
			out[M+i] = res ^ in[i];
		}
	}
	int output_size() {
		if(n%N == 0)
			return (M+N)*n/N;
		else return (M+N)*(n/N+1);
	}
	int input_size() {
		return n;
	}

	void gen(bool * out, bool * in, PRG * prg = nullptr) {
		bool prg_provided = true;
		if (prg == nullptr) {
			prg = new PRG();
			prg_provided = false;
		}
		int i;
		for(i = 0; i < n/N; ++i) {
			genN(out+(i*(N+M)), in+N*i, prg, N);
		}
		if(n%N != 0) {
			genN(out+(i*(N+M)) , in+N*i, prg, n%N);
		}
		if(not prg_provided) {
			delete prg;
			prg = nullptr;
		}
	}
};
/**@}*/
}
#endif

