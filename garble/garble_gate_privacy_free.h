#ifndef LIBGARBLE_GARBLE_GATE_PRIVACY_FREE_H
#define LIBGARBLE_GARBLE_GATE_PRIVACY_FREE_H

#include "garble.h"
#include "aes.h"

#include <assert.h>
#include <string.h>

static inline void
garble_gate_eval_privacy_free(garble_gate_type_e type, block A, block B,
                              block *out, const block *table, uint64_t idx,
                              const AES_KEY *key)
{
    if (type == GARBLE_GATE_XOR) {
        *out = garble_xor(A, B);
    } else {
        block HA, W;
        bool sa;
        block tweak;

        sa = garble_lsb(A);

        tweak = garble_make_block(2 * idx, (uint64_t) 0);

        {
            block tmp, mask;

            tmp = garble_xor(garble_double(A), tweak);
            mask = tmp;
            AES_ecb_encrypt_blks(&tmp, 1, key);
            HA = garble_xor(tmp, mask);
        }
        if (sa) {
            *((char *) &HA) |= 0x01;
            W = garble_xor(HA, table[0]);
            W = garble_xor(W, B);
        } else {
            *((char *) &HA) &= 0xfe;
            W = HA;
        }
        *out = W;
    }
}

    
static inline void
garble_gate_garble_privacy_free(garble_gate_type_e type, block A0, block A1,
                                block B0, block B1, block *out0, block *out1,
                                block delta, block *table, uint64_t idx,
                                const AES_KEY *key)
{
#ifdef DEBUG
    if ((*((char *) &A0) & 0x01) == 1
        || (*((char *) &B0) & 0x01) == 1
        || (*((char *) &A1) & 0x01) == 0
        || (*((char *) &B1) & 0x01) == 0) {
        assert(false && "invalid lsb in block");
    }
#endif

    if (type == GARBLE_GATE_XOR) {
        *out0 = garble_xor(A0, B0);
        *out1 = garble_xor(*out0, delta);
    } else {
        block tweak, tmp;
        block HA0, HA1;

        tweak = garble_make_block(2 * idx, (long) 0);
        {
            block masks[2], keys[2];

            keys[0] = garble_xor(garble_double(A0), tweak);
            keys[1] = garble_xor(garble_double(A1), tweak);
            memcpy(masks, keys, sizeof keys);
            AES_ecb_encrypt_blks(keys, 2, key);
            HA0 = garble_xor(keys[0], masks[0]);
            HA1 = garble_xor(keys[1], masks[1]);
        }
        *((char *) &HA0) &= 0xfe;
        *((char *) &HA1) |= 0x01;
        tmp = garble_xor(HA0, HA1);
        table[0] = garble_xor(tmp, B0);
        *out0 = HA0;
        *out1 = garble_xor(HA0, delta);
    }
}

#endif
