#ifndef LIBGARBLE_GARBLE_GATE_HALFGATES_H
#define LIBGARBLE_GARBLE_GATE_HALFGATES_H

#include "garble.h"
#include "aes.h"

#include <assert.h>
#include <string.h>

static inline void
garble_gate_eval_halfgates(garble_gate_type_e type, block A, block B, block *out,
                           const block *table, uint64_t idx, const AES_KEY *key)
{
    if (type == GARBLE_GATE_XOR) {
        *out = garble_xor(A, B);
    } else {
        block HA, HB, W;
        int sa, sb;
        block tweak1, tweak2;

        sa = garble_lsb(A);
        sb = garble_lsb(B);

        tweak1 = garble_make_block(2 * idx, (long) 0);
        tweak2 = garble_make_block(2 * idx + 1, (long) 0);

        {
            block keys[2];
            block masks[2];

            keys[0] = garble_xor(garble_double(A), tweak1);
            keys[1] = garble_xor(garble_double(B), tweak2);
            masks[0] = keys[0];
            masks[1] = keys[1];
            AES_ecb_encrypt_blks(keys, 2, key);
            HA = garble_xor(keys[0], masks[0]);
            HB = garble_xor(keys[1], masks[1]);
        }

        W = garble_xor(HA, HB);
        if (sa)
            W = garble_xor(W, table[0]);
        if (sb) {
            W = garble_xor(W, table[1]);
            W = garble_xor(W, A);
        }
        *out = W;
    }
}

static inline void
garble_gate_garble_halfgates(garble_gate_type_e type, block A0, block A1, block B0,
                             block B1, block *out0, block *out1, block delta,
                             block *table, uint64_t idx, const AES_KEY *key)
{
    if (type == GARBLE_GATE_XOR) {
        *out0 = garble_xor(A0, B0);
        *out1 = garble_xor(*out0, delta);
    } else {
        long pa = garble_lsb(A0);
        long pb = garble_lsb(B0);
        block tweak1, tweak2;
        block HA0, HA1, HB0, HB1;
        block tmp, W0;

        tweak1 = garble_make_block(2 * idx, (uint64_t) 0);
        tweak2 = garble_make_block(2 * idx + 1, (uint64_t) 0);

        {
            block masks[4], keys[4];

            keys[0] = garble_xor(garble_double(A0), tweak1);
            keys[1] = garble_xor(garble_double(A1), tweak1);
            keys[2] = garble_xor(garble_double(B0), tweak2);
            keys[3] = garble_xor(garble_double(B1), tweak2);
            memcpy(masks, keys, sizeof keys);
            AES_ecb_encrypt_blks(keys, 4, key);
            HA0 = garble_xor(keys[0], masks[0]);
            HA1 = garble_xor(keys[1], masks[1]);
            HB0 = garble_xor(keys[2], masks[2]);
            HB1 = garble_xor(keys[3], masks[3]);
        }
        table[0] = garble_xor(HA0, HA1);
        if (pb)
            table[0] = garble_xor(table[0], delta);
        W0 = HA0;
        if (pa)
            W0 = garble_xor(W0, table[0]);
        tmp = garble_xor(HB0, HB1);
        table[1] = garble_xor(tmp, A0);
        W0 = garble_xor(W0, HB0);
        if (pb)
            W0 = garble_xor(W0, tmp);

        *out0 = W0;
        *out1 = garble_xor(*out0, delta);
    }
}

#endif
