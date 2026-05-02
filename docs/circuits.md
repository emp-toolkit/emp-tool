# Circuit class layer (`emp-tool/circuits/`)

Conventions for the user-facing circuit primitives — `Bit_T`,
`BitVec_T`, `UnsignedInt_T`, `SignedInt_T`, `Float_T` — and their
default-aliased forms `Bit` / `BitVec` / `UnsignedInt` / `SignedInt` /
`Float`. Read this when modifying any header under `emp-tool/circuits/`
or adding a new circuit primitive.

For numeric semantics (wrap, division, shifts, resize), read
[numeric_semantics.md](numeric_semantics.md). For protocol code that
*uses* these primitives, read [EMP_TRANSLATION.md](EMP_TRANSLATION.md)
instead.

## Templating and storage

- Circuit primitives — `Bit_T`, `BitVec_T`, `UnsignedInt_T`,
  `SignedInt_T`, `Float_T` — are class templates on `Wire`. Class
  definitions must not reference `block`. Wire storage is inline
  (`Wire bit;` in `Bit_T`, `vector<Bit_T<Wire>> bits;` in `BitVec_T`)
  — required for the gate-rate budget; do not introduce indirection.

- The `block`-typed default aliases (`Bit`, `BitVec`, `UnsignedInt`,
  `SignedInt`, `Float`, plus the AES/SHA3 calculator aliases) live
  only in `emp-tool/emp-tool.h`. Custom-wire users include the
  templated headers directly and spell out their own aliases.

## Inheritance and operator dispatch

- `UnsignedInt_T` and `SignedInt_T` inherit from `BitVec_T` for storage
  and bitwise/structural ops. The derived classes override `& | ^ ~`
  and the static-shamt shifts so they return the derived type — keeps
  `UnsignedInt ^ UnsignedInt → UnsignedInt` rather than slicing into
  `BitVec`. Conversion between signed and unsigned is an explicit
  `.as_signed()` / `.as_unsigned()` bit-cast (no gates).

- Sortable / If / sort dispatch goes through the template-template
  mixin `Sortable<Wire, T>` where `T` is `template<typename> class T`.
  Derived classes supply `geq` / `equal` / `select`; the mixin provides
  `>=`, `<=`, `<`, `>`, `==`, `!=`, `If`. Don't add `operator==` on
  `BitVec_T` itself — it would collide with the mixin's operator==
  on classes that inherit both `BitVec_T` and `Sortable`.

## Shared kernels

- Shared bit-array kernels (`add_full`, `sub_full`, `mul_full`,
  `div_full`, `cond_neg`, `if_then_else`) live in
  `circuits/numeric_kernels.h`. Both `UnsignedInt_T` and `SignedInt_T`
  consume them. Sign semantics live one level up: signed division is
  unsigned div with pre/post `cond_neg`, signed comparison is
  sign-extended subtraction, etc.
