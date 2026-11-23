# How we would like reports written

## Layout and length

One root cause per report, at most about a page, with these sections in this
order:

1. **Title:** `<component>: <defect>`, where the component is the file or
   class, such as `IOChannel::recv_pt` or `ir/empbc.h`.
2. **Summary:** two or three sentences.
3. **Attacker and entry point:**
   - which party attacks: another protocol party, a network attacker on
     `TLSIO`, or the supplier of a `.empbc` file;
   - what that party sends;
   - which public API carries it to the faulty code.
   
   Use the trust model in `.oss-scanner/threat_model.md`.
4. **Impact and severity:** the rating under `threat_model.md`, and why.
5. **Reproducer:**
   - the emp-tool commit;
   - the build directory (`/src/build` or `/src/build-san`);
   - the exact commands;
   - the output that shows the defect: a sanitizer report, a wrong value or
     an abort.
6. **Proposed fix:** a short description. The patch follows
   `patch_guidelines.md`.

## What every report should state

- Any non-default configuration the defect needs: `EMP_TEST_MODE`, a BLAKE3
  hash backend (`EMP_HASH_BACKEND` or `EMP_FS_HASH`), Fiat-Shamir enabled,
  `NetIO` or `TLSIO`.
- Which parts of the impact the reproducer demonstrates and which are
  inferred. Keep the two apart.
- If the defect needs the caller to break a documented precondition, which
  precondition it is.

## Style

- Plain prose. Code identifiers go in backticks, with `file:line` references.
- Nothing beyond what the evidence shows, and no boilerplate disclaimers.
