#ifndef EMP_PRIVACY_FREE_H__
#define EMP_PRIVACY_FREE_H__

#include "emp-tool/core/block.h"
#include "emp-tool/core/utils.h"
#include "emp-tool/crypto/prp.h"
#include "emp-tool/execution/backend.h"
#include "emp-tool/io/io_channel.h"

namespace emp {

// Privacy-free garbling. PrivacyFree holds the io / constants / PRP and
// the asymmetry-free gate ops; PrivacyFreeGen / PrivacyFreeEva override
// `and_gate` with the garble / eval halves.

inline block privacy_free_garble(block LA0, block A1, block LB0, block B1,
                                  block delta, block* table, uint64_t idx,
                                  const AES_KEY* key) {
	(void)delta;  // delta is implicit via A1 = LA0 ^ delta etc.
	block tweak = makeBlock(2 * idx, 0ULL);
	block masks[2] = { sigma(LA0) ^ tweak, sigma(A1) ^ tweak };
	block keys[2]  = { masks[0], masks[1] };
	AES_ecb_encrypt_blks(keys, 2, key);
	block HLA0 = keys[0] ^ masks[0];
	block HA1  = keys[1] ^ masks[1];
	*reinterpret_cast<char*>(&HLA0) &= 0xfe;
	*reinterpret_cast<char*>(&HA1)  |= 0x01;
	block tmp = HLA0 ^ HA1;
	table[0] = tmp ^ LB0;
	(void)B1;
	return HLA0;
}

inline block privacy_free_eval(block A, block B, const block& table,
                                uint64_t idx, const AES_KEY* key) {
	block tweak = makeBlock(2 * idx, 0ULL);
	block tmp = sigma(A) ^ tweak;
	block mask = tmp;
	AES_ecb_encrypt_blks(&tmp, 1, key);
	block HA = tmp ^ mask;
	if (getLSB(A)) {
		*reinterpret_cast<char*>(&HA) |= 0x01;
		return HA ^ table ^ B;
	}
	*reinterpret_cast<char*>(&HA) &= 0xfe;
	return HA;
}

class PrivacyFree : public Backend {
public:
	IOChannel* io;
	block constant[2];
	int64_t gid = 0;

	PrivacyFree(int party_, IOChannel* io_) : Backend(party_), io(io_) {}

	size_t wire_bytes() const override { return sizeof(block); }

	void public_label(void* out, bool b) override {
		*static_cast<block*>(out) = constant[b];
	}

	void xor_gate(void* out, const void* l, const void* r) override {
		*static_cast<block*>(out) =
		    *static_cast<const block*>(l) ^ *static_cast<const block*>(r);
	}

	void not_gate(void* out, const void* in) override {
		*static_cast<block*>(out) =
		    *static_cast<const block*>(in) ^ constant[1];
	}

	uint64_t num_and() override { return gid; }

protected:
	// Protected so PrivacyFreeGen / PrivacyFreeEva can access the AES
	// key. Kept off the public surface to avoid external mutation that
	// would break the gate-numbering scheme.
	PRP prp;
};

class PrivacyFreeGen : public PrivacyFree {
public:
	block delta;

	explicit PrivacyFreeGen(IOChannel* io_) : PrivacyFree(ALICE, io_) {
		block a;
		PRG().random_block(&a, 1);
		delta = set_bit(a, 0);
		PRG().random_block(constant, 2);
		*reinterpret_cast<char*>(&constant[0]) &= 0xfe;
		*reinterpret_cast<char*>(&constant[1]) |= 0x01;
		io->send_block(constant, 2);
		constant[1] = constant[1] ^ delta;
	}

	void and_gate(void* out, const void* l, const void* r) override {
		block table[1];
		block a = *static_cast<const block*>(l);
		block b = *static_cast<const block*>(r);
		block res = privacy_free_garble(a, a ^ delta, b, b ^ delta, delta,
		                                 table, gid++, &prp.aes);
		io->send_block(table, 1);
		*static_cast<block*>(out) = res;
	}
};

class PrivacyFreeEva : public PrivacyFree {
public:
	explicit PrivacyFreeEva(IOChannel* io_) : PrivacyFree(BOB, io_) {
		io->recv_block(constant, 2);
	}

	void and_gate(void* out, const void* l, const void* r) override {
		block table;
		io->recv_block(&table, 1);
		*static_cast<block*>(out) = privacy_free_eval(
		    *static_cast<const block*>(l),
		    *static_cast<const block*>(r),
		    table, gid++, &prp.aes);
	}
};

}  // namespace emp
#endif
