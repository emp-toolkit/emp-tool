#ifndef LIBGARBLE_H
#define LIBGARBLE_H

#include "block.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <openssl/sha.h>

#define GARBLE_OK 0
#define GARBLE_ERR -1

/* Supported garbling types */
typedef enum {
    /* GRR3 and free-XOR as used in JustGarble */
    GARBLE_TYPE_STANDARD,
    /* Half-gates approach of Zahur, Rosulek, and Evans (Eurocrypt 2015) */
    GARBLE_TYPE_HALFGATES,
    /* Privacy-free approach of Zahur, Rosulek, and Evans (Eurocrypt 2015) */
    GARBLE_TYPE_PRIVACY_FREE,
} garble_type_e;

/* Supported gate types */
typedef enum {
    GARBLE_GATE_ZERO = 0,
    GARBLE_GATE_ONE = 15,
    GARBLE_GATE_AND = 8,
    GARBLE_GATE_OR = 14,
    GARBLE_GATE_XOR = 6,
    GARBLE_GATE_NOT = 5,
    GARBLE_GATE_EMPTY = -1,
} garble_gate_type_e;

typedef struct {
    /* The type of gate this is */
    garble_gate_type_e type;
    /* The input/output wires associated with this gate */
    uint64_t input0, input1, output;
} garble_gate;

typedef enum {
    GARBLE_FIXED_WIRE_ZERO = 0,
    GARBLE_FIXED_WIRE_ONE = 1
} garble_fixed_wire_e;

typedef struct {
    garble_fixed_wire_e type;
    int idx;
} garble_fixed_wire;

typedef struct {
    /* n: number of inputs */
    /* m: number of outputs */
    /* q: number of gates */
    /* r: number of wires */
    uint64_t n, m, q, r;
    /* garbling scheme type */
    garble_type_e type;

    garble_gate *gates;         /* q */
    block *table;               /* q */
    block *wires;               /* 2 * r */
    int *outputs;               /* m */

    /* permutation bits of output wire labels */
    bool *output_perms;
    /* fixed label used for constant values */
    block fixed_label;
    /* key used for fixed-key AES */
    block global_key;
} garble_circuit;

/* Return the table size of a garbled circuit */
inline
size_t garble_table_size(const garble_circuit *gc)
{
    switch(gc->type) {
    case GARBLE_TYPE_STANDARD:
        return 3 * sizeof(block);
    case GARBLE_TYPE_HALFGATES:
        return 2 * sizeof(block);
    case GARBLE_TYPE_PRIVACY_FREE:
        return sizeof(block);
    }
    return 0;
}

/* Create a new circuit */
int
garble_new(garble_circuit *gc, uint64_t n, uint64_t m, garble_type_e type);
/* Delete a garbled circuit */
void
garble_delete(garble_circuit *gc);

/* Garbles a circuit.
   If 'input_labels' is NULL, generate input-wire labels.
   If 'output_labels' is NULL, don't store output-wire labels.
 */
int
garble_garble(garble_circuit *gc, const block *input_labels,
              block *output_labels);
/* Hash a given garbled circuit */
void
garble_hash(const garble_circuit *gc, unsigned char hash[SHA_DIGEST_LENGTH]);
/* Check that 'gc' matches the hash specified in 'hash' */
int
garble_check(garble_circuit *gc, const unsigned char hash[SHA_DIGEST_LENGTH]);
/* Create a random delta block */
block
garble_create_delta(void);
/* Create 'n' input labels and store them in 'labels'.
   'delta' specified the delta block to use; if NULL, generate a new delta.
   'privacyfree' specifies whether you are using the privacy free garbling scheme.
 */
void
garble_create_input_labels(block *labels, uint64_t n, block *delta,
                           bool privacyfree);
/* Evaluate garbled circuit 'gc'.
   'input_labels' specifies the input wire labels to use.
   'output_labels', if not NULL, stores the output wire labels.
   'outputs', if not NULL, stores the actual output of the computation.
 */
int
garble_eval(const garble_circuit *gc, const block *input_labels,
            block *output_labels, bool *outputs);
void
garble_extract_labels(block *extracted_labels, const block *labels,
                      const bool *bits, uint64_t n);

/* XXX: not to be used in practice, as knowing both output blocks is completely
 * insecure! */
int
garble_map_outputs(const block *output_labels, const block *map, bool *vals,
                   uint64_t m);

/* write/read circuit description to/from file */
int
garble_circuit_to_file(garble_circuit *gc, char *fname);
int
garble_circuit_from_file(garble_circuit *gc, char *fname);

size_t
garble_size(const garble_circuit *gc, bool wires);

int
garble_save(const garble_circuit *gc, FILE *f, bool wires);
int
garble_load(garble_circuit *gc, FILE *f, bool wires);

int
garble_to_buffer(const garble_circuit *gc, char *buf, bool wires);
int
garble_from_buffer(garble_circuit *gc, const char *buf, bool wires);

#endif
