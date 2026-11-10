# Floating-Point Circuit Assets

This document records how the shipped IEEE floating-point circuit assets are
represented, where their provenance comes from, and what must be true before a
replacement asset is accepted. The runtime assets live in:

```text
emp-tool/ir/files/fp<width>_<op>.empbc
```

The `.empbc` files are the canonical stored format for nontrivial generated
circuits. emp-tool loads them through `emp::circuit::float_circuit()`, which
uses the shared circuit asset resolver: `EMP_CIRCUIT_DIR`, then build-tree
assets, then install-tree assets beside the installed headers.

## Scope

The public floating-point suite has 69 operations: 3 IEEE binary widths times 23
operations. Of those, 60 are shipped as `.empbc` assets. The three structural
operations in each width (`abs`, `neg`, and `copysign`) are implemented directly
as EMP bit operations.

Widths:

```text
fp16  IEEE binary16  1 sign,  5 exponent, 10 fraction bits
fp32  IEEE binary32  1 sign,  8 exponent, 23 fraction bits
fp64  IEEE binary64  1 sign, 11 exponent, 52 fraction bits
```

Circuit-backed operations:

```text
add div eq fma ge gt isinf isnan iszero le lt max min
mul ne recip rsqrt sqrt square sub
```

Direct structural operations:

```text
abs neg copysign
```

Notes:

- Predicate/classifier circuits output 8 bits; bit 0 is the result.
- `add` / `sub` / `mul` / `div` / `sqrt` / `recip` / `min` / `max` are
  correctly rounded (round-to-nearest-even; `test_float` verifies them
  bit-exactly against the host). An exact zero from `add`, `sub` or `fma` is
  `+0` unless both addends are `-0`.
- `fma` is intentionally unfused: `add(mul(a, b), c)` with two roundings.
- `rsqrt` is unfused too: exactly `recip(sqrt(x))`, rounded twice. It is not a
  correctly rounded `1/sqrt(x)`; the two differ on 8,402 of the 2^16 fp16
  inputs.
- NaN results of `div`, `recip`, `sqrt` and `rsqrt`: in fp16 and fp32 a NaN
  operand is returned unchanged (sign and payload kept; a signaling NaN stays
  signaling), and `div` returns its first operand if it is a NaN, else its
  second. Invalid operations (`0/0`, `inf/inf`, the square root of a negative
  number, including `-inf`) return `+qNaN` (`0x7e00`, `0x7fc00000`). In fp64
  every NaN result is the canonical `+qNaN` `0x7ff8000000000000`.
- Transcendentals (`sin`, `cos`, `tan`, `exp`, `log`, etc.) are not shipped.
- IEEE exception flags, signaling-NaN behavior, exact NaN payload propagation,
  and non-default rounding modes are not modeled.

## Asset Contract

The checked-in `.empbc` files are the canonical source artifacts consumed by
emp-tool at runtime. This tree intentionally does not ship a complete
reproducible generation harness or exact reproduction instructions for the
current assets. Instead, the contract below documents the ABI and validation bar
that any generated asset must satisfy.

The contract for any checked-in generated circuit is:

- Store the checked-in runtime artifact as `.empbc`.
- Document the generator/source provenance when the artifact is replaced,
  especially when third-party source material is involved.
- Model the circuit as one `emp::circuit::BooleanProgram`.
- Use dense wire IDs: inputs are `[0, num_inputs)`, and every non-input wire in
  `[num_inputs, num_wires)` is defined exactly once.
- Keep gates in topological order.
- Use only `And`, `Xor`, `Not`, `Const0`, and `Const1`.
- Normalize unused operands to 0: `Not` uses only `in0`; constants use neither
  operand.
- Preserve LSB-first bit order for integer/float payloads.
- Validate by loading through `load_empbc_file()` before replacing any shipped
  asset.

For floating-point assets specifically:

- File names are `fp<width>_<op>.empbc`, for example `fp64_add.empbc`.
  Structural `abs`, `neg`, and `copysign` do not have files.
- Put runtime assets in `emp-tool/ir/files/`; CMake installs the `.empbc`
  assets and the asset README beside the headers.
- Unary inputs are `A`, binary inputs are `A||B`, and ternary inputs are
  `A||B||C`, each operand LSB-first.
- Non-predicate outputs are one LSB-first float payload of the same width.
- Predicate/classifier outputs are 8 bits, with bit 0 holding the
  boolean result.

For non-floating-point generated circuits, prefer the same shape: choose a
stable input/output ABI, emit a dense `BooleanProgram`, save it with
`save_empbc_file()`, and load it through the shared circuit asset resolver
instead of adding another ad hoc search path.

## Historical Generation Notes

The assets that preceded the current generators (see "Current Assets") came
from one-off CBMC-GC-2 pipelines, and `fp16_isnan` and `fp64_isnan` still do.
The common shape was: source-level operation wrappers,
CBMC-GC-2 gate generation, conversion into emp-tool's dense `BooleanProgram`
format, validation through `load_empbc_file()`, and semantic verification
against host floating-point behavior or a native C reference.

The core library does not parse external text circuit formats at runtime.
Conversion scripts and generator source trees are development/provenance tools
only; `.empbc` is the stored format consumed by emp-tool and downstream
protocols.

If a future generation driver is added, keep it outside the runtime library and
make it emit `BooleanProgram` / `.empbc` directly. It should compact any
generator-local wire numbering to emp-tool's dense invariant, reject unsupported
gate kinds, preserve the ABI above, and run the same `load_empbc_file()`
validation path that production uses.

## Replacement Bar

A replacement asset should be treated like a source change to a cryptographic
primitive: the final `.empbc` must be checked in, its provenance must be
recorded, its ABI must match this document, and verification must justify the
change. At minimum, record:

1. The source/generator family used for the replacement.
2. The exact input/output ABI and bit order.
3. The semantic oracle used for verification.
4. Edge-case coverage for NaN, Inf, zero signs, subnormals, and shared-input
   cases where relevant.
5. AND-count comparison against the current table, with an explanation for any
   increase.
6. The emp-tool and downstream protocol tests run.

## Generation Tricks

These historical tricks and gotchas mattered for correctness or circuit size.
They are notes for evaluating or replacing assets, not exact reproduction
instructions for the current checked-in files.

- Shape C for CBMC-GC, not for a normal optimizer. Smaller, flatter C usually
  means fewer gates. Data-dependent `while` loops can hang unwinding; use fixed
  trip-count loops.
- Keep wrappers boring. Use `mpc_main`, fixed-width integer inputs named
  `INPUT_A_x`, `INPUT_B_x`, `INPUT_C_x`, and one `OUTPUT_*`. Keep the bit-level
  float payload in an integer type.
- Avoid fragile platform headers. `<stdint.h>` worked for the SoftFloat path;
  macOS `<inttypes.h>` did not. The fp64 sources avoid system headers where
  possible.
- Remove union type-punning before symbolic execution. The SoftFloat-derived
  fp32/fp64 sources use no-union edits so CBMC-GC sees integer fields directly.
- Unity-build the edited SoftFloat core when needed. The wrapper includes only
  the required `.c` files plus a small `platform.h` shim, keeping the generated
  circuit focused on one operation.
- For fp64 multiplication, the Karatsuba-shaped 53-bit significand multiply was
  materially smaller than the straightforward product in the SoftFloat path.
- Do not assume tying a binary circuit's two inputs is safe. `mul(a, a)` exposed
  a CBMC-GC shared-input/NaN issue for fp16/fp64 square. The robust pattern is a
  branch-free dedicated square: compute the symmetric product unconditionally,
  then mux NaN/Inf/Zero at the end.
- Prefer exact digit-recurrence/long-division datapaths for sqrt/div/recip when
  they fit. They beat Newton-style approximations here and are bit-exact rather
  than merely close.
- Comparisons are cheapest when converted to an integer ordering problem plus
  a few class flags. See "Comparison, Min/Max and Classifier Circuits" below.
- Implement pure rewiring operations directly in EMP. `abs`, `neg`, and
  `copysign` do not need a gate generator or `.empbc` asset.
- Compose circuits for intentionally unfused operations. `fma` is
  `add(mul(a, b), c)` and `rsqrt` is `1 / sqrt(x)`, so composition is clearer
  than asking the generator for a larger fused-looking expression.
- For heavy ops, a low CBMC-GC minimization time limit, usually `1`, avoided SAT
  stalls and still produced the checked-in sizes.
- Verify semantics before celebrating gate counts. Native C checks catch source
  mistakes; `.empbc` replay catches conversion/bit-order mistakes; dedicated
  edge sweeps are needed for NaN, Inf, zero signs, subnormals, and shared-input
  cases.
- Keep any "keep smaller" automation conservative: only replace an asset when
  the candidate verifies and is smaller, or when a size increase is justified by
  a correctness fix.

## Current Assets

Every shipped asset except `fp16_isnan` and `fp64_isnan` is built directly as
an XOR/AND graph by one of three parametric generators, then locally
re-minimized with mockturtle's cut rewriting against its multiplicative-
complexity database and, where it helped, CBMC-GC-2's minimizer. Every
re-minimized circuit was proven equivalent to the generator's output with ABC
`cec`. The circuits minimize AND gates; their AND depth is not a design goal.
The generators are development tools and are not part of this tree.

### Addition, Multiplication and fma

- `add`: a magnitude comparator whose tie-break picks the positive operand,
  which by itself makes an exact cancellation `+0`; a conditional swap at one
  AND per bit; a right-shift aligner that ORs shifted-out bits into a sticky
  bit at each level and saturates when either input is Inf or NaN, so the
  datapath returns the larger operand unchanged; a ripple add/subtract with the
  sticky bit as its least significant bit; a leading-zero count clamped to the
  exponent for subnormal results and one left normalizer; and exponent packing
  with the hidden bit, so the round-to-nearest-even increment carries into the
  exponent. NaN `a` takes priority over NaN `b`.
- `sub`: `add` with the second operand's sign inverted.
- `mul`: the possibly-subnormal operand is swapped into one position and
  normalized (its fraction's leading-zero count doubles as the zero test); the
  significand product is a column-compression multiplier of full and half
  adders at one AND each; the product's one-bit normalization is folded into
  the right shifter for subnormal results; special values saturate that
  shifter. NaN handling matches the rule above.
- `square`: a dedicated squarer that forms only the cross products, with the
  same back end as `mul`.
- `fma`: the `mul` and `add` circuits composed literally (unfused).

Verification against the previous assets: fp16 `add`, `sub` and `mul`
exhaustively over all 2^32 operand pairs, fp16 `square` over all 2^16 inputs
and fp32 `square` over all 2^32; fp32 and fp64 `add` and `sub` by ABC `cec`;
fp32 `mul` on 10^9 and fp64 `mul` and `square` on 2*10^9 random and targeted
inputs (special values, NaN payloads, subnormal inputs and outputs, overflow
boundaries, exact ties). Each `fma` is its verified `mul` and `add` composed,
and ABC `cec` proves each previous `fma` equal to the previous `add` applied
after the previous `mul`; each was also run on 5*10^8 random and targeted
inputs.

### Division and Square Root

- `div`: both significands are normalized by leading-zero stages, then a
  radix-2 non-restoring recurrence produces `F + 3` quotient bits at one
  `(F + 2)`-bit add or subtract per bit. The sticky bit is "remainder is
  nonzero", tested as `r != 0 && r != -b` on the final partial remainder. A
  single right shifter performs both the 1-bit quotient normalization and the
  shift for subnormal results. Special cases force the datapath word to zero,
  so the output stage needs only XORs plus the NaN-payload selection.
- `recip`: the same recurrence with the constant dividend 1.0, specialized.
  `1/x` is never a rounding tie (it is exact only for powers of two), so it
  has no sticky logic. Only subnormals with at most two leading zeros have
  finite reciprocals, and a subnormal result is at most two binades below the
  smallest normal, so both shifters are two single-bit stages.
- `sqrt`: the radicand is placed by exponent parity and normalized by
  even-length stages, then a non-restoring square-root recurrence produces
  `F + 2` root bits. A square root is never a rounding tie, so the rounding
  increment is the last root bit and no sticky logic is needed.
- `rsqrt`: `sqrt` as above, rounded, then the reciprocal recurrence on that
  normal significand (no normalization, no subnormal results, no ties).

The recurrences use ripple-carry steps, which minimizes AND gates but makes AND
depth grow roughly with `F^2` (fp64 `div` has AND depth 3013).

Verification against the previous assets: fp16 operations exhaustively over
all inputs (2^32 pairs for `div`); fp32 `recip`, `sqrt` and `rsqrt`
exhaustively over all 2^32 inputs; fp32 `div` and all fp64 operations on 10^9
random and targeted inputs (subnormal-result ties, near-ties, overflow and
underflow boundaries). The generator was also checked exhaustively against an
exact integer model on about 1,400 small-format instances (IEEE-like formats up
to 16 bits for `div`, up to 24 bits for the unary operations, both NaN
conventions).

### Comparison, Min/Max and Classifier Circuits

Write `n = W - 1` for the magnitude width and `|x|` for the low `n` bits. Per
operand, three cheap signals are shared:

- `N`: the fraction is zero (an AND tree over the inverted fraction bits).
- `A`: all exponent bits are equal (an AND tree over `e_i XNOR e_0`).
- `P = A AND (e_0 XOR N)`: the operand is a NaN or a zero. This is the only
  flag the ordering predicates need, because whenever the larger-magnitude
  operand is a zero the answer is fixed by the zero rules.

The circuits are:

- `eq`: `(|a| == |b|) AND H`, with `s = NOT(sa XOR sb)`,
  `q = A AND NOT(s XOR e_0)` and `H = s XOR ((N XOR s) AND q)` on `a`'s
  fields. It uses `2n - 1` ANDs, which is the lower bound: the function has
  algebraic degree `2n`. `ne` is its negation.
- `lt` / `le`: one `n`-AND ripple comparator `c` on `|a| XOR sb*1..1` versus
  `|b| XOR sb*1..1` (carry-in 0 for `lt`, 1 for `le`), the `P` flags of both
  operands and a 3-AND combine (`3n - 1` ANDs in total). For `lt`:
  `g0 = sa AND Pa`, `g1 = NOT sb AND Pb`,
  `lt = sa XOR g0 XOR ((c XOR sb) AND (c XOR sa XOR g0 XOR g1))`.
  `gt` / `ge` swap the operands.
- `min` / `max`: the same comparator and flags, a 4-AND select and a `W`-AND
  multiplexer.
- `isinf` / `iszero`: a single depth-balanced AND tree over `n` literals.
- `fp32_isnan`: `E XOR (E AND NOR(fraction))`, where `E` is the 8-leaf AND of
  the exponent bits, built as one node of a balanced 31-leaf AND tree.

The 3- and 4-AND combine blocks were found by SAT-based exact synthesis over
the reachable combinations of their inputs. Each asset was proven equivalent to
the asset it replaced with ABC `cec`, and the binary16 assets were also checked
exhaustively over every input.

## Current AND Counts

These counts are from the current `.empbc` assets. XOR/NOT are also stored in the
file, but AND gates are the dominant cost in modern garbling. `abs`, `neg`, and
`copysign` are direct bit operations and have no asset file.

| op | fp16 | fp32 | fp64 |
|---|---:|---:|---:|
| add | 261 | 550 | 1195 |
| sub | 261 | 550 | 1195 |
| mul | 466 | 1463 | 4986 |
| square | 223 | 744 | 2722 |
| fma | 727 | 2013 | 6181 |
| div | 469 | 1355 | 4463 |
| recip | 252 | 845 | 3312 |
| sqrt | 185 | 570 | 1991 |
| rsqrt | 328 | 1202 | 4921 |
| eq/ne | 29 | 61 | 125 |
| lt/le/gt/ge | 44 | 92 | 188 |
| min | 61 | 125 | 253 |
| max | 61 | 125 | 253 |
| isnan/isinf/iszero | 14 | 30 | 62 |

## Replacement Checklist

Before replacing a checked-in `.empbc`, confirm all of the following:

1. The candidate loads successfully through `load_empbc_file()`.
2. The candidate has the expected input count, output count, and LSB-first bit
   order.
3. A width-appropriate verifier passes against host behavior or the native C
   reference.
4. Any special edge-case verifier for the operation passes.
5. AND counts have been compared with the table above, and any increase has a
   clear correctness or maintainability reason.
6. emp-tool's `test_float` passes.

## Third-Party Provenance

Earlier fp32/fp64 assets were generated from modified sources derived from
Berkeley SoftFloat 3e; the current assets contain no SoftFloat-derived logic.
The asset README in `emp-tool/ir/files/README.md` keeps the SoftFloat notice
beside the `.empbc` files.
