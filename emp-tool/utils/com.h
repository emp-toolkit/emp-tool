#ifndef COM_H__
#define COM_H__
#include "string.h"
#include "emp-tool/utils/hash.h"
#include "emp-tool/utils/prg.h"
/** @addtogroup BP
    @{
  */

namespace emp {
typedef char Com[Hash::DIGEST_SIZE];
typedef block Decom[1];

class Commitment{ public:
	Hash h;
	PRG prg;
	void commit(Decom decom, Com com, const void * message, int nbytes) {
		prg.random_block(decom, 1);
		h.reset();
		h.put_block(decom, 1);
		h.put(message, nbytes);
		h.digest(com);
	}

	bool open(Decom decom, Com com, const void * message, int nbytes) {
		h.reset();
		h.put_block(decom, 1);
		h.put(message, nbytes);
		Com res;
		h.digest(res);
		return strncmp(com, res, Hash::DIGEST_SIZE)==0;
	}
};
}
/**@}*/
#endif// COM_H__
