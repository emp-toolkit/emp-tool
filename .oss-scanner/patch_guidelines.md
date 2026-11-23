# How we would like patches made

`AGENTS.md` at the repository root maps the layers and names the document
for each kind of change. Read it first.

## Size and scope of a fix

- Fix the root cause in the faulty function, as narrowly as possible. Do not
  refactor, rename or reformat code the fix does not need.
- If the same defect appears at sibling sites (for example every `recv_*`
  path of `IOChannel`), fix all of them in one patch and say so.
- Keep the wire format unchanged unless the fix needs it. If it does, say so
  in the first line of the description, because both parties must then
  upgrade together.
- Do not add protocol messages, rounds or bytes only to detect or diagnose a
  problem.

## Checks and errors

- Write every check on data from another party, or from a file, as
  `expecting(cond, "component: what went wrong")` from
  `emp-tool/runtime/core/error.h`. It stays active in Release and ends the
  process, which is the intended fail-stop behaviour.
- Never use `assert()` for such data. Never throw: the library is built to
  work without exceptions.
- Public length and count parameters are `int64_t`. Reject negative values at
  the boundary, check for overflow before multiplying, and treat zero as a
  no-op (`docs/api_conventions.md`).
- New file-scope `block` or `__m128i` constants follow `docs/static_init.md`.

## Code style

- Match the surrounding file: tabs in `runtime/` and `ir/`, four spaces in
  `circuits/`, and the file's brace style and naming. There is no formatter.
- Few comments, only where the code is not obvious. A comment explains the
  design or the invariant the code keeps, and describes the code as it is
  now. Leave out:
  - history ("previously", "fixed");
  - report or review identifiers;
  - performance numbers;
  - names of other projects that use emp-tool.

## Tests

- Add a regression test to the component's existing file,
  `test/<layer>/test_<header>.cpp`, following that file's shape
  (`docs/test_conventions.md`).
- A test that expects the process to abort runs the failing call in a child
  process with the file's fork-based helper (`dies()` or
  `run_capturing_stderr()`) and checks the exit status.
- Two-party tests run under `./run` and take their port from `EMP_PORT`. They
  are registered with `add_test_case_with_run`.
- Build and test both `/src/build` and `/src/build-san` (ASan + UBSan), and
  give the `ctest` result for each.

## Commit message

- Subject: imperative, about 60 characters at most, no prefix tag and no
  trailing period, such as "Reject nonzero padding bits in recv_bool".
- Body: what was wrong, why it matters, and what the fix does. End with a
  "Tested:" paragraph that names the tests run.
