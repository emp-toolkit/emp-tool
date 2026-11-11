# Changelog

Notable changes to emp-tool. Versions are the git tags. CMake package
metadata is numeric (see the README's version guidance).

## v1.0.0-alpha.2

Second alpha of the 1.0 development line. It fixes a Delta recovery in
`v1.0.0-alpha.1`'s garbling hash (see Security), so both parties of a protocol
must upgrade together, and class layouts and wire transcripts change, so every
dependent must be rebuilt. Both alphas install as CMake package version `1.0.0`,
so pin the tag.

### Security

- `MITCCRH`: the bucketed key reuse (`ReuseShift > 0`, default 3) now
  carries the gate id into the cipher input,
  `H(x, i) = y ⊕ E_{S ⊕ (i >> ReuseShift)}(y)` with `y = σ(x) ⊕ T(i)`,
  `T(i) = makeBlock(i, 0)`. Previously the gids sharing a bucket key
  hashed a block to the same value, so a wire label fed to two AND gates
  of one bucket (or `AND(a, a)`) gave half-gate rows whose XOR was `0` or
  `Δ`, and the evaluator recovered `Δ`. Affects `v1.0.0-alpha.1`, the
  first release with the bucketed `ReuseShift` hash; the 0.3.x line keys
  every gate separately and is unaffected by this leak.
  Every MITCCRH-derived transcript changes; both parties must upgrade
  together, pinned wire-trace baselines must be regenerated, and every
  dependent must be rebuilt (the class layout changed). (Issue #212.)
- `MITCCRH`: an instance that hashed with more than one `K` (for example
  `hash<1, 1>` and then `hash<2, 1>`) read past its key schedule. It renewed
  the schedule only once exactly `BatchSize` lanes were used, so a mix of `K`
  stepped over that point, hashed under round keys read from the memory after
  the schedule, and never re-keyed again. It now starts a new schedule whenever
  the next `K` lanes do not fit, skipping the leftover gids. Affects
  `v1.0.0-alpha.1` and the 0.3.x line; an instance used with one `K`, or with
  `setS` called before each change of `K`, never reached the over-read.
- Deterministic test mode is a build option, `-DEMP_TEST_MODE=ON`, instead of
  the `EMP_TEST_MODE` environment variable and `set_test_mode()`. Setting the
  variable made every default PRG seed and EC scalar of any build, Release
  included, predictable. A normal build now compiles the deterministic paths
  out, and test-mode and normal objects cannot be linked into one program.
  Wire-trace tests need a test-mode build of emp-tool and of the code under
  test.
- Half-gate garbling: both parties seed `MITCCRH` with one public start point
  `S`, chosen independently of the garbler's `Δ`, and keep the gid schedule in
  step; `half_gate.h` states this. `v1.0.0-alpha.1`'s `test_halfgate` and
  `bench_halfgate` seeded `MITCCRH` with `Δ`, so code copied from them must
  switch to a separate public `S`.

### Added

- `tcp::SocketOptions` (`runtime/io/tcp_socket.h`): send and receive buffer sizes
  for `NetIO::listen` / `NetIO::connect` (new overloads) and
  `TLSConfig::socket_options`, applied before `listen()` / `connect()` so TCP
  window scaling covers them; accepted sockets inherit them and `make_sibling()`
  passes them on. `SocketOptions::for_bandwidth_and_rtt(bandwidth, rtt)` sizes both
  directions for a path: zero, the operating system's own sizing, up to a 4 MiB
  bandwidth-delay product, and the first power of two above it beyond that. A size
  above the system's limit (`net.core.rmem_max` / `wmem_max` on Linux, 212,992 bytes
  unless raised; `kern.ipc.maxsockbuf` on macOS, 8 MiB) is lowered to the limit when
  that is above 4 MiB and otherwise not made, keeping the operating system's own
  sizing; one warning per process gives the `sysctl` command that allows it.
- `vector_inn_prdt_sum_red_x4(res, a, b, n)` (`crypto/f2k.h`): four
  independent GF(2¹²⁸) inner products in one call, with the operands laid
  out term-major (`a[4l + j]` is term `l` of sum `j`). Each SIMD lane
  carries one sum, so the 512- and 256-bit VPCLMULQDQ tiers fold nothing
  across lanes, and each sum is reduced once at the end, bit-identical to
  `vector_inn_prdt_sum_red` on the de-interleaved operands.
- `ScheduledProgram` (`ir/schedule.h`) owns a dense `BooleanProgram` and its
  AND-depth schedule, validated and scheduled once in the constructor.
  `scheduled_execute_program` and `simd_execute_program` take it in place of a
  program plus a separately built plan.
- `RebindableWireBundle<V, Ctx>` and `wire_bundle_rebind_t<V, Ctx>`
  (`ir/wire_value.h`): the cross-context half of the `WireBundle` contract
  (`V::rebind<Ctx>` is a `WireBundle` over `Ctx` at the same width), required of
  every argument and return family by `frontend::run(ctx, circuit, …)`,
  `frontend::compose` and `ChunkedSession::run(circuit, …)`.
- `validate_compose_structure` and `validate_compose` (`ir/context/compose.h`):
  plan-level and full validation of a `ComposePlan`. `ComposeCtx::finish` runs
  the first on the plan it returns; `flatten_compose` runs the second.
- `digest_gate_stream(program)` (`ir/context/digest.h`), the stored-program
  counterpart of a `DigestCtx` trace.
- `ProgramWorkspace::release_wire_values()` (`ir/execute.h`) clears the
  workspace's wire vectors and keeps their capacity, for a thread-local
  workspace that holds context-bound wires.
- `FlushPlan::error` (`ir/session/flush_plan.h`) names the reason `plan_flush`
  rejected a recorded stream.
- `DynamicUInt_T<Ctx>` and `DynamicInt_T<Ctx>` (`circuits/unsigned_int.h`,
  `circuits/signed_int.h`) hold integers whose width (`>= 1`) is chosen at
  construction. They have the fixed types' arithmetic, comparisons, bitwise
  operators, `select` and public-amount shifts, plus `resize(width)`,
  `to_fixed<M>()` and `as_signed()` / `as_unsigned()`, and `DynamicUInt_T` has
  `hamming_weight()`. They are the `RuntimeWidthValue`s that a `RuntimeSessionIO`
  session inputs and reveals.
- `uint8_t` (byte-bool) overloads of `bools_to_bits` / `bits_to_bools`,
  `PRG::random_bool`, `IOChannel::send_bool` / `recv_bool`,
  `vector_inn_prdt_sum_red` (runtime and `<N>` forms) and
  `GaloisFieldPacking::packing`, so byte-bool buffers need no cast to `bool*`.
  An input byte is false when zero and true otherwise; outputs are 0 or 1. The
  `bool` and `uint8_t` paths give the same bits, wire bytes and PRG state.
- Two hooks for sessions derived from `ChunkedSession`: a
  `static constexpr bool kCountsAndsInternally` member makes the base skip its
  own AND count, and `materialize_deferred_inputs_()` is called at every `run`,
  `run_artifact` and flush boundary, so a derived session can settle deferred
  inputs there.

### Changed

- `IOChannel::sync()` is one non-virtual implementation in the base, used by every
  channel: each side sends one zero byte, flushes, and waits for the peer's, so
  neither side has to go first and no channel can turn it into a no-op. Two
  channels adopted from existing sockets (both `is_server == false`) used to
  deadlock in it, each waiting to receive first. It still stays out of the byte
  counters and the Fiat-Shamir transcript and sends the same bytes as before, so
  old and new peers still pair up, and it now reports a nonzero byte as the two
  sides being out of step. NetIO, TLSIO and TraceIO no longer override it; a
  custom channel that overrode `sync()` must drop the override, and dependents must
  be rebuilt, since `IOChannel`'s virtual table changed.
- `NetIO::flush()` stops the process when the stdio `fflush` fails instead of
  ignoring it.
- `TraceIO` rejects a null channel, creates its trace files with mode `0600`,
  flushes them before the wrapped channel, reports trace write and flush failures,
  and records the bytes of `sync()`.
- Test mode stops the process in more of the cases where deterministic streams
  would repeat. Deriving a child lane on lane 0, as `ThreadPool::enqueue` does,
  now claims lane 0 just as drawing a seed does, so a second unscoped thread
  that enqueues pool work must run under its own `test_lane_scope`. Lane 0's
  owner is a per-thread token rather than a hash of `std::thread::id`, so a
  thread started after the owner exits can no longer reuse its id and replay
  its seeds. `test_lane_scope(0)` stops the process; on lane 0's owner it
  restarted lane 0's ordinals and repeated the seeds already drawn.
- `parse_party` and `peer_port` stop the process unless the value is plain
  decimal digits within range (`EMP_PORT` must be in [1, 65535]). They used
  `atoi`, which read `2x` or `+2` as 2 and passed any `EMP_PORT`, such as `0` or
  `70000`, through unchecked.
- `clock_start` / `time_from` use `std::chrono::steady_clock`, which does not
  jump when the system clock is set. On libstdc++, where
  `high_resolution_clock` is `system_clock`, their type changes: code that
  stores `clock_start()` in a `time_point<high_resolution_clock>` must use
  `auto` or `steady_clock::time_point`.
- `ThreadPool(0)` stops the process; such a pool accepted tasks that never ran,
  so their futures never became ready. A worker thread that fails to start no
  longer ends in `std::terminate`.
- `ThreadPool::enqueue` stores its arguments without `std::bind`. They are
  passed as lvalues, as before, or as rvalues when the callable cannot take
  lvalues, so a function taking a move-only argument by value can be enqueued;
  the future's type is the result of that call, which can differ from
  `invoke_result_t<F, Args...>` for a callable overloaded on value category.
  `std::bind`'s special cases are gone: a `std::reference_wrapper` argument is
  no longer unwrapped, so a generic parameter sees the wrapper (a `T&`
  parameter still binds through its conversion), and a nested bind expression
  is passed as is instead of being called.
- `bool_to_int<bool>` no longer compiles; it wrote eight packed bits into a
  `bool`, which may only hold 0 or 1.
- Negative sizes stop the process where they used to be silent no-ops or zero
  results: the runtime `ParaEnc` / `AES_ecb_encrypt_blks` forms (also when
  `K * N` overflows `int64_t`), `CCRH::Hn`, and the F2K helpers
  `vector_inn_prdt_sum_no_red`, `vector_inn_prdt_sum_red` (block, `bool` and
  `uint8_t` selectors), `vector_inn_prdt_sum_red_x4` and `vector_self_xor`.
  Their compile-time forms, `AES_opt_key_schedule<N>` and `aes_ctr_fill_dm<W>`
  reject a negative size with a `static_assert`, `uni_hash_coeff_gen<N>`
  requires `N > 0`, and `MITCCRH` requires `BatchSize`, `K` and `H` to be
  positive. Zero sizes stay no-ops.
- The AES-CTR counter (`aes_ctr_fill_dm`, `detail::ParaCtrEnc`,
  `detail::AesLane<N>::ctr_xor_tweak`) is `uint64_t` instead of `int64_t`, so
  counter arithmetic is defined across block 2^63; existing call sites convert
  implicitly.
- `ECGroup::rand_scalar()` samples `[1, order)` instead of `[0, order)`,
  through `BN_priv_rand_range`.
- `ECGroup::hash_to_point` requires a DST of 1 to 255 bytes (an empty one was
  accepted), and `RO(domain, sid)` requires the same of `domain`.
- On a `const Scalar`, `n()` returns `const BIGNUM*`; a non-const overload
  returns `BIGNUM*`. Code that passes a const `Scalar`'s handle to an OpenSSL
  call taking `BIGNUM*` no longer compiles.
- `SessionID::derive()` is `[[nodiscard]]`.
- `MITCCRH::start_point` starts as `zero_block` instead of indeterminate. It is
  a placeholder: `setS()` must still install the parties' shared random seed.
- `WireBundle` requires a positive static `width()`. `BitVec_T<Ctx, 0>` and
  other zero-width values are no longer frontend arguments, return values or
  session I/O values; they can still be built inside circuit code.
- An explicit-context circuit body takes the context as a mutable lvalue
  reference (`Ctx&` or `auto&`). A generic by-value or forwarding-reference
  (`auto&&`) first parameter, and a body that returns a value over a different
  context type than its arguments, are compile-time errors.
- Live `frontend::run(body, args…)` passes decayed, owned argument values, as
  `compile` does, so a body overloaded on lvalue versus rvalue arguments
  describes one circuit. `compile`, both `run` forms and `compose` stop the
  process when an argument or the result belongs to another context instance,
  in every build (the `run` argument check used to exist only under
  `EMP_CONTEXT_CHECKS`), and `run` stops on a default-constructed first
  argument.
- `frontend::run(ctx, circuit, args…)` and
  `ChunkedSession::run(const frontend::Circuit<…>&, args…)` are constrained by
  `RebindableWireBundle` and spelled with `wire_bundle_rebind_t`. A session that
  derives from `ChunkedSession` and redeclares that `run` must give it the same
  constraint and spelling, or calls to it become ambiguous.
- `validate_program` rejects an unknown `wire_reuse` value. Under
  `WireReuse::Linear` an And output takes a slot no gate has written and no
  later gate writes (`Full` allows both); under `WireReuse::None` gate `i` must
  write wire `num_inputs + i`. A dense program with out-of-order outputs, or a
  Linear one that places an And output in a recycled slot, used to load and is
  rejected now, `.empbc` files included.
- Scheduled and SIMD replay take a `ScheduledProgram`. The one-shot
  `scheduled_execute_program(ctx, program, inputs)` remains and validates its
  program on every call, and `simd_execute_program` stops on a negative
  instance count. Code that cached a `ScheduledPlan` constructs a
  `ScheduledProgram` from the program instead.
- IR diagnostics separate a streamed gate trace (`DigestCtx::value()`,
  `digest_gate_stream`) from the versioned full-program fingerprint
  (`digest_program`). `DigestCtx` seals its input prefix once, at the first gate
  or at `value()`, and stops on `external_input` after a gate; code that read
  `DigestCtx::digest` calls `value()`. `digest_program` mixes a domain tag, a
  format version, `wire_reuse`, `num_wires`, the gate count and the ordered
  outputs, so its value changes for every program (the `stored_program_digest`
  and `dense_program_digest` fields of `empbc-tool inspect --json` change; the
  checked-in manifests carry neither). Code that compared a `DigestCtx` trace or
  `digest_source` with `digest_program` compares with `digest_gate_stream`.
- `session::plan_flush` rejects a malformed recorded stream instead of
  compacting it with zero operands: recorder id 0, the null sentinel, as a gate
  operand, output or keep id; a Not or constant gate with non-canonical
  operands; an unknown op; two gates producing one id; a chunk-local operand
  read before its producer; a stale keep id; and counts that do not fit
  `uint32_t`. `ok` is false and `error` names the reason, which
  `ChunkedSession` includes in its stop. Debug builds validate the compacted
  program.
- `ChunkedSession::reveal` treats only an empty decode as "not a recipient" and
  stops on a nonempty decode of the wrong width. `run_artifact` validates the
  raw program in Debug builds and trusts it in Release.
- `builtin_circuit` accepts only `aes128`, `sha256_256` and `sha3_256_256`, and
  `float_circuit` only widths 16, 32 and 64 with the shipped operations; both
  check the loaded program's input and output counts before caching it.
- `load_empbc` rejects a null buffer with a nonzero length and a nonzero
  reserved gate byte, and `save_empbc` stops when the output or gate count does
  not fit the 32-bit header field.
- `validate_artifact` rejects a zero argument or return width, and
  `CircuitSignature::total_input_bits` stops when the sum exceeds `UINT32_MAX`.
- `ComposeCtx::finish` validates the plan's wiring; `flatten_compose` validates
  the plan and every distinct unit and bounds the flattened wire count before
  indexing; `ComposeCtx::call_unit` stops on a null input array and on index or
  wire-id overflow. A glue event is tagged `instance == -1` exactly.
- `~ChunkRecorderCtx` stops the process when a recorder wire is still pinned, in
  every build; the check used to run only under `EMP_CONTEXT_CHECKS`.
- `ClearSession::input` and `reveal` stop on a value from another session's
  context, on a runtime-width codec whose `encode` returns the wrong number of
  bits or whose `from_wires` returns the wrong context or width, and on
  revealing a runtime-width value of width 0.
- `Float_T`'s IR-replay helpers (`replay_`, `unary_`, `binop_`, `ternary_`,
  `cmp_`, `classify_`) are private.
- `make_compact` recycles a slot as soon as its value has no reader and is
  neither a program output nor a Linear And output, and lets a program output
  take a recycled slot; only Linear And outputs insist on a never-written slot.
  A consumer that maps an output slot to its producer takes the slot's last
  writer. Linear and Full programs it produces can have fewer wires than
  before, and their `digest_program` values change.
- `execute_program(ctx, program, inputs)` moves the output vector out of its
  workspace instead of copying it.
- `UInt_T<Ctx, N>` and `Int_T<Ctx, N>` require `N > 0`; their runtime-width forms
  are the separate types `DynamicUInt_T<Ctx>` and `DynamicInt_T<Ctx>`. Replace
  `UInt_T<Ctx, 0>` with `DynamicUInt_T<Ctx>` and `Int_T<Ctx, 0>` with
  `DynamicInt_T<Ctx>`, whether the width is spelled `0` or `runtime_width`, and
  read their wires through `data()`, since the wire vector is private. A dynamic
  value stops the process when it is used unbound (default-constructed or
  moved-from) or indexed out of range, and its `decode`, so a session `reveal`,
  stops when the value does not fit `uint64_t` / `int64_t` instead of returning
  its low 64 bits. A zero-width `slice` / `extract` / `trunc` and
  `BitVec_T<Ctx, 0>::as_uint()` no longer compile; they used to produce a
  zero-width runtime value.
- `popcount<R>()` requires `R >= bits_for(N)`, the width that holds `0..N`; a
  narrower `R` used to return the count modulo `2^R`.
- Integer circuits emit fewer gates; every result is unchanged, including
  division by zero and `INT_MIN / -1`. `==` / `!=` take `N - 1` AND gates
  instead of `N`, as a balanced tree of AND depth `ceil(log2 N)` instead of a
  chain of depth `N - 1`. `/` and `%` divide non-restoringly over a growing
  window, so no step needs a restoring multiplexer: an unsigned quotient takes
  `N(N+1)/2 + 2N - 3` ANDs instead of `N² + 3N - 1` (589 instead of 1,119 at 32
  bits) and a remainder `2N - 1` more; signed `/` and `%` take the magnitudes
  and re-sign them with conditional negations of `N - 1` ANDs (682 and 745
  instead of 1,308 at 32 bits). `hamming_weight` / `popcount` take
  `N - popcount(N)` ANDs, the multiplicative complexity of the Hamming weight
  (31 instead of 155 at 32 bits), and `leading_zeros` takes `N - 1` (31 instead
  of 186). `*` takes `N² - N + 1` below 24 bits and is a Karatsuba short product
  from 24 bits (931 instead of 993 at 32 bits, 3,347 instead of 4,033 at 64),
  at a higher AND depth from 48 bits (73 instead of 63 at 64). Unary `-` on
  `Int_T` takes `N - 2`, signed `<` takes `N`, and the secret-amount shifts fold
  the out-of-range test into their last stage (203 instead of 219 at 32 bits,
  198 for `Int_T >>`, at AND depth 7 instead of 28). Compiled programs, gate
  counts and `digest_program` values change, and so do pinned transcripts of
  circuits that use these operators.
- `compare_swap`, `sort` and `sort_by_key` exchange two values with one AND per
  bit, `d = s & (a ^ b)`, `a ^= d`, `b ^= d`, instead of two `select`s that each
  computed that AND. A compare-swap of 32-bit integers takes 64 AND gates instead
  of 96, and one carrying a 32-bit payload 96 instead of 160. Values without
  `pack_wires` / `from_wires` (or the runtime-width integers' `^`) still use two
  `select`s. Results are unchanged; gate counts and pinned transcripts change.
- `batcher_network` (and so `sort` / `sort_by_key`) sorts the two halves of any
  length, larger first, and joins them with Batcher's merge for unequal runs,
  instead of cutting the next power-of-two network down to the length. At 2^k
  and 2^k - 1 inputs the network is the same; at the other lengths checked
  (every length up to 8192) it has no more compare-swaps and no more layers,
  and up to 17% fewer compare-swaps just above a power of two (85 to 74 at 17
  inputs, 679 to 565 at 65). Integer `sort` results are unchanged. At the
  lengths whose network changed, `sort_by_key` may leave the payloads of equal
  keys in a different order, and `Float_T` sorts may place NaNs and zeros of
  opposite sign differently.
- Sorts of 9 to 16 values, and the 9- to 16-value blocks that larger sorts
  merge, use the smallest known sorting networks (25, 29, 35, 39, 45, 51, 56
  and 60 compare-swaps; Knuth, TAOCP vol. 3, §5.3.4) instead of Batcher's (26,
  31, 37, 41, 48, 53, 59 and 63). Sorting 16, 64 or 1024 values takes 60, 531 or
  23871 compare-swaps instead of 63, 543 or 24063, with no more layers. As
  above, integer `sort` results are unchanged, while `sort_by_key` may order the
  payloads of equal keys differently and `Float_T` sorts may place NaNs and
  signed zeros differently.
- `compare_swap(a, b)` and `sort` over `UInt_T`, `Int_T` and their runtime-width
  forms decide the exchange with a comparison that may answer either way when
  the two values are equal, where exchanging changes nothing: the borrow chain
  starts at bit 1, with one operand's bit 0 as the incoming borrow. A
  compare-swap of w-bit values takes 2w - 1 AND gates in w AND layers (before:
  2w in w + 1 layers unsigned; 2w + 1 in w + 2 layers signed, which now
  complements both sign bits instead of ending in a mux). Results are
  unchanged. `sort_by_key` keeps the exact comparison, so equal keys still never
  exchange their payloads in a compare-swap.
- The shipped floating-point circuits are rebuilt for fewer AND gates and
  compute the same results, NaN payloads and signed zeros included. At fp32
  (fp16 / fp64 in the same proportions): `add` / `sub` 1,143 -> 550, `mul`
  2,286 -> 1,463, `square` 1,876 -> 744, `fma` 3,429 -> 2,013, `div` 7,845 ->
  1,355, `recip` 5,863 -> 845, `sqrt` 5,699 -> 570, `rsqrt` 11,179 -> 1,202,
  `eq` / `ne` 111 -> 61, `lt` / `le` / `gt` / `ge` 112 -> 92, `min` 200 -> 125
  and `max` 176 -> 125 ANDs; `isinf` / `iszero` and `fp32_isnan` keep their AND
  counts at one less AND depth. The circuits minimize AND gates, not depth: the
  division family is deeper than before (fp64 `div` has AND depth 3,013 instead
  of 389). `docs/floating_point_circuits.md` lists every count and how each
  circuit was verified. Compiled programs, `digest_program` values and pinned
  transcripts of circuits that use these operators change.
- In-circuit AES-128 uses a 28-AND S-box (straight-line program
  `aes-sbox-a28-ad4-g131-gd32-xx103-47` of the NIST Circuit Complexity
  repository) instead of Boyar-Peralta's 32-AND one: AES-128 with its key
  schedule takes 5,600 ANDs instead of 6,400, at AND depth 40 instead of 60.
  SHA-256 computes majority with one AND per bit, folds public bits (the IV,
  padding and round constants) into its additions and compresses multi-operand
  sums: a compression takes 22,271 ANDs instead of 24,744, and the
  `sha256_256` asset 20,954. SHA3-256 evaluates chi in the last round only on
  the lanes the digest reads: 37,056 ANDs instead of 38,400. Results are
  unchanged; the `aes128`, `sha256_256` and `sha3_256_256` assets, their
  digests and pinned transcripts change.
- `Int_T`'s secret-amount shifts check, under `EMP_CONTEXT_CHECKS`, that the
  shift amount belongs to the value's context, as the other binary integer
  operators do.
- `ThreadPool` is `emp::ThreadPool`, so it cannot collide with another copy of
  the upstream file. Code outside `namespace emp` that names it unqualified,
  without `using namespace emp`, must qualify it.
- `NetIO` and `TLSIO` keep their socket, `FILE*` / `SSL` handles, buffers and
  `init_from_sock` private. `fd()` returns the connected socket and
  `NetIO::listener_fd()` a server channel's listening socket. `IOChannel`'s
  `send_data_internal` / `recv_data_internal` are protected: channels still
  override them, and callers use `send_data` / `recv_data`, so every byte
  passes the counters and the Fiat-Shamir transcript.
- `MITCCRH` keeps its schedule state private. `start_point()` returns the `S`
  installed by `setS`, and `next_gid()` the gid the next hash's first lane
  takes; `setS` and `renew_ks(gid)` are the only ways to change the state.
- Length and count parameters that were `size_t`, `uint64_t` or `uint32_t` are
  `int64_t`, as `docs/api_conventions.md` requires, and a negative value stops
  the process instead of wrapping to a huge count: `Scalar::to_bin_padded` /
  `from_bin`, `Point::to_bin` / `from_bin`, `ECGroup::hash_to_point`,
  `ECGroup::resize_scratch`, `RO::absorb(const void*, n)`, `to_hex`,
  `load_empbc(bytes, len)`, `sse_trans`, `sse_trans_n128`, the counts of
  `circuit::execute_program`, `external_input` on `RecordCtx` / `ComposeCtx` /
  `DigestCtx`, `ComposeCtx::call_unit`, `ChunkRecorderCtx::reserve_ids`,
  `digest_source`, `batcher_network`, the pointer forms of `sort` /
  `sort_by_key`, and the count of `BulkBooleanContext::and_many`. Callers
  passing `size_t` still compile, and a backend whose `and_many` takes
  `size_t` still satisfies the concept. Circuit widths stay `int`.
- `ChunkedSession::reveal` takes only the value and the recipient. Its trailing
  `keep...` values could not change the flush, since every value still in scope
  is kept anyway; they were only checked to belong to the session. To drop
  values that are still in scope, call `checkpoint(keep...)` first.
- `ChunkedSession::InputBatch` is move-only: its copy constructor, copy
  assignment and move assignment are deleted, where `v1.0.0-alpha.1` had
  implicit copies. Keep the batch that `input_batch()` returns, or move it into
  a new variable; do not copy or reassign one.

### Removed

- `TLSIO::is_server`, which only chose who went first in the old `sync()`.
- `set_test_mode()` and the `EMP_TEST_MODE` environment variable; test mode is
  a build option (see Security).
- `MITCCRH::renew_ks()` without an argument, which renewed from the stored gid;
  `renew_ks(gid)` remains.
- `MITCCRH::renew_ks(const block*)` and `MITCCRH::hash_cir_fixed`, the
  explicit-tweak mode in which the caller scheduled the keys and owned
  tweak uniqueness. Every hash now goes through the gid stream, which the
  class keeps distinct per `(S, gid)`; a second tweak dimension is XORed
  into the low qword of a random `S` or laid out as gid gaps with
  `renew_ks(gid)`. A protocol that scheduled its own keys and bucketed on
  explicit tweaks shared one key across gates with nothing in the cipher
  input to tell them apart, the same leak one layer up; it must re-derive
  its tweak layout on the gid stream, on both ends, not rename calls.
- `macro_xstr` / `macro_str`, which `utils.h` defined for every file that
  includes it, although emp-tool does not use them.
- `ScheduledPlan` and `make_scheduled_plan` from the public namespace (they are
  `emp::detail`), the `scheduled_execute_program` / `simd_execute_program`
  overloads that took a `BooleanProgram` plus a plan, and the
  `simd_execute_program` forms over a bare `BooleanProgram`.
- `layout_pass`'s `LivenessStats` parameter; `layout_pass(program)` computes its
  own liveness.
- `DigestCtx`'s public data members (`digest`, `next_id`, `c0`, `c1`).
- `UInt_T::mod_exp`. It reduced each `N`-bit product modulo `2^N` before reducing
  it modulo `q`, so its result was wrong whenever a product reached `2^N`.
- `runtime_width`, and the `is_dynamic` member of `UInt_T` / `Int_T`
  (`RuntimeWidthValue` requires it, as `true`, of a runtime-width type).
- `audit-report/`: it reviewed an earlier tree, and its guidance no longer
  applied. The README no longer cites it.

### Fixed

- `bench_halfgate` measured nothing: with the hashes inlined into `main`
  and their results unused, the compiler removed them, so the loop timed
  the PRG and key schedules, and the printed rate divided 128M gates by a
  2M-gate loop. The outputs are now folded into a printed sink and the
  count matches the loop.
- `joinNcleanCheat` stopped calling `get()` once a task returned `true`, so it
  could return while later tasks were still running. It now waits for every
  future.
- `error()` flushes `stderr` before exiting, so a diagnostic written to a
  buffered `stderr` is no longer lost.
- A translation unit that included `runtime/core/simd_tier.h` before
  `runtime/core/block.h` compiled `sse_trans` without its AVX2 / AVX-512 / GFNI
  kernels. Linked with translation units that included them in the usual order,
  the program had two definitions of the same inline functions, and the linker
  could pick the slower one for every call that was not inlined.
- `PRG::reseed()` kept the words buffered for `operator()`, so a PRG used as a
  `UniformRandomBitGenerator` returned up to 31 words of the old stream after a
  reseed.
- `ECGroup::hash_to_point` could leave `BN_R_NOT_A_SQUARE` in OpenSSL's
  thread-local error queue, where a later OpenSSL call would see it. The queue
  is now restored to what the caller had, and a `BN_mod_sqrt` failure for any
  other reason stops the process.
- On VAES builds (x86 with AVX2 or AVX-512), `MITCCRH::hash` and `hash_cir`
  read each tile of their input with one 32- or 64-byte load. When the caller
  had just written those blocks one at a time, as half-gate garbling and
  evaluation do, the load could not be forwarded from the 16-byte stores and
  waited for them to reach the cache, so consecutive calls ran one after
  another. The input is now read 16 bytes per block. Output is unchanged, and
  builds with 1-block AES lanes (AES-NI only, ARM) compile the same code as
  before.
- `Float_T` replay kept the last replay's wires in its thread-local
  `ProgramWorkspace`. Over a context with refcounted wires they stayed pinned
  after their recorder was destroyed, and a later recorder could inherit them as
  stale ids. The helpers move the outputs out and release the workspace's wires.
- `layout_pass` marked input wire 0 reachable whenever a constant gate was
  reachable (through its normalized operand), overstating `reachable_wires` in
  `empbc-tool inspect`, and `analyze_program` counted a gate that reads the same
  wire twice as two consumers in `max_fanout`.
- `save_empbc_file` reports a failed `fclose`; a write that failed at close used
  to report success.
- `Float_T` addition, subtraction and `fma` returned `-0` for an exact zero
  such as `x - x`, where round-to-nearest-even gives `+0`, so `1 / (x - x)` was
  `-inf`. The shipped `fp{16,32,64}_{add,sub,fma}` circuits now break a
  magnitude tie toward the positive operand. Their AND counts and AND depth are
  unchanged, but programs that use them get new `digest_program` values and
  transcripts.
- `execute_program(ctx, program, inputs, ws)` resized `ws.out` before reading
  `inputs`, so passing the outputs of an earlier call on the same workspace
  read freed or destroyed elements. It now reads the inputs first.
- `session::plan_flush` accepted a recorded gate that reads its own output in
  Release builds, giving a program that reads a wire before writing it, and
  stopped the process in Debug builds instead of returning `ok = false`. It now
  rejects the gate with "gate reads its own output".
- `~TLSIO` waited for the peer's `close_notify`, so it blocked until the peer
  destroyed its end of the channel, and two parties that destroyed their
  channels in different orders, as NetIO allows, deadlocked. It now sends
  `close_notify` and closes without waiting. A TLSIO server also sends no
  session tickets, which TLSIO never uses, so a send-only peer has no unread
  bytes that would turn its close into a connection reset.
- The link-time guards that reject objects built with a different default
  `Hash` backend, or with and without BLAKE3, were discarded by linker section
  garbage collection (`-ffunction-sections -fdata-sections -Wl,--gc-sections`),
  and such objects then linked into one program, where `Hash` and `IOChannel`
  objects crossing between them had mismatched layouts. Each guard, like the
  new test-mode guard, is now an empty function that runs at program start and
  references its marker. Linkers keep startup functions, so the check holds
  with any compiler and linker. Every translation unit that includes `hash.h`
  or `test_mode.h` gets one such function per guard, which Clang's
  `-Wglobal-constructors` reports.
- `load_empbc` / `load_empbc_file` take a `max_wires` limit, default
  `empbc_default_max_wires` (2^30), and reject a header that declares more wires
  before anything is allocated. Input wires take no bytes in a `.empbc` file, so
  a 24-byte file could declare 2^32 − 1 of them, and the IR passes, which size
  their arrays by the wire count, then requested tens of GiB. Pass a larger
  limit to load a bigger program, or a smaller one for files from an untrusted
  source.
- `bench_f2k` counts bytes per row: 32 for two-operand products, 17 for
  byte-bool selectors, 16 for generators.

## v1.0.0-alpha.1

First tagged alpha of the 1.0 development line. The 0.3.x line
(tag `0.3.0` / branch `v0.3.x`) is maintained separately and receives
backported fixes.

### Stability

- Pre-1.0 API: headers and names may change between alpha tags and
  before the final `1.0.0`. Pin to a specific tag.
- CMake package metadata is numeric `1.0.0` (`project(VERSION)` cannot
  carry a prerelease suffix); the alpha status lives in the git tag.

### Added

- Three-layer library layout: `runtime/` (block/AES/PRG/hash/GF(2^128),
  EC, IO), `ir/` (Boolean-circuit IR, sessions, composition), `circuits/`
  (typed value layer + frontend).
- Typed circuit values (`Bit_T` / `BitVec_T` / `UInt_T` / `Int_T` /
  `Float_T` over a `BooleanContext`) with a compile-once / run-on-any-
  context frontend.
- Persisted circuit assets (`.empbc`) and `empbc-tool`
  (`inspect` / `compare` / `manifest` / `check-manifest`); IEEE
  floating-point and hash circuit assets with per-asset manifests.
- Circuit composition (`ComposeCtx` / `ComposePlan`) — experimental.
- Selectable hash backend (SHA-256 default, BLAKE3) and Fiat–Shamir
  transcript hash (`EMP_FS_HASH`), propagated to consumers as a
  build-time ABI axis (both parties must agree).
- TLS 1.3 IO channel (`TLSIO`) alongside `NetIO`; sibling channels via
  `NetIO::make_sibling`.
- Deterministic test mode (`EMP_TEST_MODE`) for wire-byte equivalence.
- SIMD kernels: SoftSpoken butterfly, six-tier bit-transpose.

### Changed

- Every failure is fail-stop through `error()`; the public surface
  raises no exceptions and is `-fno-exceptions`-compatible (enforced by
  `test_no_exceptions`).
- Requires OpenSSL ≥ 3.0 and CMake ≥ 3.25.

### Security

See the README's Security section: this is research software with no
independent audit; `TLSIO` verifies the peer's certificate chain against
the configured CA only (no identity binding — use a deployment-private
CA); there is no systematic constant-time guarantee.

### Known limitations

Tracked in [docs/roadmap.md](docs/roadmap.md):

- Circuit-asset lookup uses the absolute install path baked at configure
  time, so a prefix moved after install does not resolve assets (override
  with `EMP_CIRCUIT_DIR`).
- SIMD ISA is selected at compile time; a `-march=native` binary can
  fault (SIGILL) on a CPU without those instructions — build portable
  (`EMP_TOOL_NATIVE_ARCH=OFF`) or match `-march` to the target.
