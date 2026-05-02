# Numeric semantics

Normative rules for arithmetic on `UnsignedInt_T` / `SignedInt_T` /
`BitVec_T`. These match **hardware** two's-complement semantics on
commodity x86 / arm — *not* C's "undefined behavior" rules. A naive
translator that reproduces a source language's UB-avoidance dance
will produce extra gates for no reason.

Read this when modifying any arithmetic kernel, when writing a
translator from another language to EMP, or when verifying a
boundary case against ground truth.

## Wrap on `+ - *`

`+`, `-`, `*` on `UnsignedInt_T` and `SignedInt_T` wrap mod 2^N.
Unsigned matches `uint{N}_t` directly; signed matches `int{N}_t`
*as implemented on hardware* (two's-complement wrap). C's
signed-overflow UB is sidestepped — emp-tool wraps deterministically.

## Division and modulus

- Signed `/` truncates toward zero (C99+).
- Signed `%` carries the sign of the dividend.
- `INT_MIN / -1` is allowed and produces `INT_MIN` (the bit-pattern
  result of two's-complement division). Callers needing a different
  policy must check explicitly.
- Division by zero is undefined behavior of the *circuit*; do not
  pass it. If the divisor is secret, guard with an `If` that picks a
  sentinel result and a "valid" `Bit`.

## Shifts

- `<<` is logical on every type.
- `>>` is logical on `BitVec_T` and `UnsignedInt_T`, arithmetic
  (sign-fill) on `SignedInt_T`.
- Shift amounts ≥ width yield zero on logical ops, or sign-fill on
  signed `>>`.
- Both static-shamt and dynamic-shamt forms exist. The dynamic form
  treats `shamt` as unsigned.

To do a *logical* right-shift on a `SignedInt`:
`s.as_unsigned() >> k` (then `.as_signed()` if you need the type back).

## Resize

- `resize(W)` zero-extends on `UnsignedInt_T`, sign-extends on
  `SignedInt_T`. Truncation drops the high bits.
- Resize is structural — costs no AND gates.

## Negation

- `-x` on `UnsignedInt_T` is well-defined as `~x + 1` mod 2^W
  (i.e. `0u - x`). Matches C unsigned negation. The result is still
  `UnsignedInt`.
- Unary `-` on `SignedInt_T` is two's-complement negate. `-INT_MIN`
  wraps to `INT_MIN` as a bit pattern (no exception).

## Absolute value

`abs()` on `SignedInt_T` returns an `UnsignedInt_T` of the same
width. `abs(INT_MIN)` returns the bit pattern `2^(W-1)` — faithful as
a *magnitude* even though no signed value can represent it.
