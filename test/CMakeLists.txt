# Test cases, organized into subfolders that mirror the source layers
# (runtime / ir / circuits — see docs/test_conventions.md). Tests are fast
# examples + correctness/regression checks; throughput lives in ../bench and is
# not registered with ctest. The target/ctest name stays the file basename;
# only the source path carries the layer folder.

function(add_test_case folder name)
    add_executable(${name} ${folder}/${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    # The headers' defaultless Op switches compile in these TUs, so the
    # -Werror=switch tripwire (see ir/visit.h) must apply here too.
    target_compile_options(${name} PRIVATE -Werror=switch)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${name}
             COMMAND ${name}
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

function(add_test_case_with_run folder name)
    add_executable(${name} ${folder}/${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    target_compile_options(${name} PRIVATE -Werror=switch)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${name}
             COMMAND ${PROJECT_SOURCE_DIR}/run $<TARGET_FILE:${name}>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

# emp-tool exports PUBLIC cxx_std_20 (the BooleanContext surface requires it), so
# every test already compiles as C++20. This wrapper is a self-documenting marker
# for the context/typed-layer tests.
function(add_test_case_cxx20 folder name)
    add_executable(${name} ${folder}/${name}.cpp)
    target_link_libraries(${name} PRIVATE emp-tool::emp-tool)
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_compile_options(${name} PRIVATE -Werror=switch)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME ${name}
             COMMAND ${name}
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

# ---- runtime/ : core, crypto, io, execution primitives ----
add_test_case(runtime test_aes)
add_test_case(runtime test_block)
add_test_case(runtime test_ccrh)
add_test_case(runtime test_f2k)
add_test_case(runtime test_hash)
add_test_case(runtime test_mitccrh)
add_test_case(runtime test_prg)
add_test_case(runtime test_prp)
add_test_case(runtime test_utils)
add_test_case(runtime test_ecc)
add_test_case(runtime test_ro)
add_test_case_with_run(runtime test_netio)
add_test_case_with_run(runtime test_tlsio)
add_test_case(runtime test_halfgate)         # half-gate garble/eval free functions

# ---- ir/ : context, session, passes, program, builtins ----
add_test_case(ir test_boolean_program)
add_test_case_cxx20(ir test_passes)              # ir/passes.h: count/liveness/schedule/layout
add_test_case_cxx20(ir test_context)             # BooleanContext concept + contexts cross-check
add_test_case_cxx20(ir test_session_concepts)    # Session/DirectSession/SessionIO + WireValue
add_test_case_cxx20(ir test_chunk_recorder)        # shared chunk recorder: RAII wire + plan_flush
add_test_case_cxx20(ir test_dynamic)             # runtime-width session I/O (RuntimeWidthValue) over ClearSession
add_test_case_cxx20(ir test_builtin_circuits)    # AES/SHA/SHA3 IR-replay == live kernels (ir/builtins.h)
add_test_case_cxx20(ir test_simd)                # ir/simd.h: SIMD-batched replay over a bitsliced BulkBooleanContext

# ---- circuits/ : typed values, crypto circuits, frontend ----
add_test_case_cxx20(circuits test_bit)           # per-type value headers over ClearCtx
add_test_case_cxx20(circuits test_bitvec)
add_test_case_cxx20(circuits test_uint)
add_test_case_cxx20(circuits test_int)
add_test_case_cxx20(circuits test_float)
add_test_case_cxx20(circuits test_sort)          # data-oblivious sort over circuit values
add_test_case_cxx20(circuits test_runtime_int)   # runtime-width UInt_T<Ctx,0> / Int_T<Ctx,0>
add_test_case_cxx20(circuits test_typed)         # typed values (Bit/BitVec/UInt/Int/Float) over Ctx
add_test_case_cxx20(circuits test_circuit_fn)    # compile-once / run-on-any-context frontend (rec:: value types)
add_test_case_cxx20(circuits test_crypto_aes)    # AES-128 block over the value layer (vector/record/count)
add_test_case_cxx20(circuits test_crypto_sha256) # SHA-256 compression + full-hash wrapper
add_test_case_cxx20(circuits test_crypto_keccak) # Keccak-f[1600] + SHA3-256 wrapper

# Circuit-function contract probes (circuits/frontend). The positive case compiles
# + runs; the eight negative cases must fail to compile with the EXPECTED contract
# diagnostic. Each test builds the case and its output must match the per-case
# message (PASS_REGULAR_EXPRESSION) — so a build that succeeds (contract stopped
# rejecting) OR fails with a different/changed message both fail the test, which
# bare WILL_FAIL could not catch. Serial: the cases share one build tree.
add_test_case_cxx20(circuits circuit_fn_contract_probes)   # positive (NEG_CASE undefined)
set(_neg_msg
    "not callable with prvalue circuit-value arguments"   # 1  run + lvalue-ref
    "not callable with prvalue circuit-value arguments"   # 2  compile + lvalue-ref
    "must RETURN BY VALUE"                                 # 3  run + ref return
    "must RETURN BY VALUE"                                 # 4  compile + ref return
    "each arg must be a circuit value"                     # 5  compile<int> (not a circuit value)
    "not a plain value"                                    # 6  run + plain return
    "not void"                                             # 7  run + void return
    "callable in BOTH"                                     # 8  compile + both-forms ambiguity
    "circuit value over RecordCtx")                        # 9  compile arg not over RecordCtx
foreach(neg RANGE 1 9)
    add_executable(circuit_fn_contract_neg_${neg} EXCLUDE_FROM_ALL circuits/circuit_fn_contract_probes.cpp)
    target_link_libraries(circuit_fn_contract_neg_${neg} PRIVATE emp-tool::emp-tool)
    target_compile_features(circuit_fn_contract_neg_${neg} PRIVATE cxx_std_20)
    target_compile_definitions(circuit_fn_contract_neg_${neg} PRIVATE NEG_CASE=${neg})
    math(EXPR _idx "${neg} - 1")
    list(GET _neg_msg ${_idx} _expect)
    add_test(NAME circuit_fn_contract_neg_${neg}
             COMMAND ${CMAKE_COMMAND} --build . --target circuit_fn_contract_neg_${neg}
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(circuit_fn_contract_neg_${neg} PROPERTIES
        PASS_REGULAR_EXPRESSION "${_expect}" RUN_SERIAL TRUE)
endforeach()
