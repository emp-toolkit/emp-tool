# Numeric semantics

Normative rules for arithmetic on `UInt_T` / `Int_T` /
`BitVec_T`. These match **hardware** two's-complement semantics on
commodity x86 / arm — *not* C's "undefined behavior" rules. A naive
translator that reproduces a source language's UB-avoidance dance
will produce extra gates for no reason.

Read this when modifying any arithmetic kernel, when writing a
translator from another language to EMP, or when verifying a
boundary case against ground truth.

## Wrap on `+ - *`

`+`, `-`, `*` on `UInt_T` and `Int_T` wrap mod 2^N.
Unsigned matches `uint{N}_t` directly; signed matches `int{N}_t`
*as implemented on hardware* (two's-complement wrap). C's
signed-overflow UB is sidestepped — emp-tool wraps deterministically.

## Division and modulus

- Signed `/` truncates toward zero (C99+).
- Signed `%` carries the sign of the dividend.
- `INT_MIN / -1` is a UB precondition: the most-negative operand has no
  representable magnitude, so `signed_int.h` does not guarantee the
  quotient (the current circuit happens to yield `INT_MIN`, but callers
  must not rely on it). Guard it explicitly if the dividend can be `INT_MIN`.
- Division by zero follows the arithmetic kernel's saturating result.
  If the divisor is secret and the caller needs a different policy, carry
  a validity `Bit_T` and select a sentinel result explicitly.

## Shifts

- `<<` is logical on every type.
- `>>` is logical on `BitVec_T` and `UInt_T`, arithmetic
  (sign-fill) on `Int_T`.
- Shift amounts ≥ width yield zero on logical ops, or sign-fill on
  signed `>>`.
- Both static-shamt and dynamic-shamt forms exist. The dynamic form
  treats `shamt` as unsigned.

To do a *logical* right-shift on an `Int_T`:
`s.as_unsigned() >> k` (then `.as_signed()` if you need the type back).

## Resize

- `resize(W)` zero-extends on `UInt_T`, sign-extends on
  `Int_T`. Truncation drops the high bits.
- Resize is structural — costs no AND gates.

## Negation

- `UInt_T` has no unary `-`. For modular negation subtract from a zero
  constant: `UInt_T<Ctx,N>::constant(ctx, 0) - x` (`= ~x + 1` mod 2^W,
  matching C unsigned negation); the result is still `UInt_T`.
- Unary `-` on `Int_T` is two's-complement negate. `-INT_MIN`
  wraps to `INT_MIN` as a bit pattern (no exception).

## Magnitude

There is no separate `abs()` helper on `Int_T`. Compute the magnitude when
needed as `x.select(x[W-1], -x).as_unsigned()` for a fixed-width value.
For `INT_MIN`, this returns the unsigned bit pattern `2^(W-1)` — faithful
as a magnitude even though no signed value can represent it.
