#ifndef LIBGARBLE_GARBLE_GATE_STANDARD_H
#define LIBGARBLE_GARBLE_GATE_STANDARD_H

#include "garble.h"
#include "garble/aes.h"

#include <assert.h>
#include <string.h>

static inline void
garble_gate_eval_standard(garble_gate_type_e type, block A, block B, block *out,
                           const block *table, uint64_t idx, const AES_KEY *key)
{
    if (type == GARBLE_GATE_XOR) {
        *out = garble_xor(A, B);
    } else {
        block HA, HB, tmp, tweak, val;
        int a, b;

        a = garble_lsb(A);
        b = garble_lsb(B);
        
        HA = garble_double(A);
        HB = garble_double(garble_double(B));

        tweak = garble_make_block(idx, (long) 0);
        val = garble_xor(garble_xor(HA, HB), tweak);
        tmp = a + b ? garble_xor(table[2*a+b-1], val) : val;
        AES_ecb_encrypt_blks(&val, 1, key);

        *out = garble_xor(val, tmp);
    }
}

    
static inline void
garble_gate_garble_standard(garble_gate_type_e type, block A0, block A1, block B0,
                            block B1, block *out0, block *out1, block delta,
                            block *table, uint64_t idx, const AES_KEY *key)
{
#ifdef DEBUG
    if ((garble_equal(A0, garble_zero_block())
         || garble_equal(A1, garble_zero_block())
         || garble_equal(B0, garble_zero_block())
         || garble_equal(B1, garble_zero_block()))
        && g->type != GARBLE_GATE_NOT) {
        abort();
    }
#endif

    if (type == GARBLE_GATE_XOR) {
        *out0 = garble_xor(A0, B0);
        *out1 = garble_xor(*out0, delta);
    } else {
        block tweak, blocks[4], keys[4], mask[4];
        block newToken, newToken2;
        block *label0, *label1;
        long lsb0, lsb1;

        tweak = garble_make_block(idx, (uint64_t) 0);
        lsb0 = garble_lsb(A0);
        lsb1 = garble_lsb(B0);

        A0 = garble_double(A0);
        A1 = garble_double(A1);
        B0 = garble_double(garble_double(B0));
        B1 = garble_double(garble_double(B1));

        keys[0] = garble_xor(garble_xor(A0, B0), tweak);
        keys[1] = garble_xor(garble_xor(A0, B1), tweak);
        keys[2] = garble_xor(garble_xor(A1, B0), tweak);
        keys[3] = garble_xor(garble_xor(A1, B1), tweak);
        memcpy(mask, keys, sizeof mask);
        AES_ecb_encrypt_blks(keys, 4, key);
        mask[0] = garble_xor(mask[0], keys[0]);
        mask[1] = garble_xor(mask[1], keys[1]);
        mask[2] = garble_xor(mask[2], keys[2]);
        mask[3] = garble_xor(mask[3], keys[3]);

        newToken = mask[2 * lsb0 + lsb1];
        newToken2 = garble_xor(delta, newToken);
        label0 = out0;
        label1 = out1;

        if (lsb1 & lsb0) {
            *label0 = newToken2;
            *label1 = newToken;
        } else {
            *label0 = newToken;
            *label1 = newToken2;
        }
        blocks[0] = *label0;
        blocks[1] = *label0;
        blocks[2] = *label0;
        blocks[3] = *label1;

        if (2*lsb0 + lsb1 != 0)
            table[2*lsb0 + lsb1 -1] = garble_xor(blocks[0], mask[0]);
        if (2*lsb0 + 1-lsb1 != 0)
            table[2*lsb0 + 1-lsb1-1] = garble_xor(blocks[1], mask[1]);
        if (2*(1-lsb0) + lsb1 != 0)
            table[2*(1-lsb0) + lsb1-1] = garble_xor(blocks[2], mask[2]);
        if (2*(1-lsb0) + (1-lsb1) != 0)
            table[2*(1-lsb0) + (1-lsb1)-1] = garble_xor(blocks[3], mask[3]);
    }
}

#endif
