#!/bin/bash
# End-to-end checks for empbc-tool's human/JSON inspection, comparison, and
# deterministic manifest generation/checking.

if [[ $# -ne 2 ]]; then
	echo "usage: $0 <empbc-tool> <asset-directory>" >&2
	exit 2
fi

tool=$1
assets=$2
sample=$assets/aes128.empbc
tmp=$(mktemp -d "${TMPDIR:-/tmp}/empbc-tool-test.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT

"$tool" inspect "$sample" >"$tmp/human.txt" || exit 1
grep -q 'AND depth' "$tmp/human.txt" || exit 1

"$tool" inspect --json "$sample" >"$tmp/inspect.json" || exit 1
grep -q '"sha256"' "$tmp/inspect.json" || exit 1
grep -q '"num_and"' "$tmp/inspect.json" || exit 1
grep -q '"peak_live_wires"' "$tmp/inspect.json" || exit 1

"$tool" compare "$sample" "$sample" >"$tmp/compare.txt" || exit 1
grep -q 'signature compatible: yes' "$tmp/compare.txt" || exit 1

"$tool" check-manifest "$assets/manifests" "$assets" || exit 1
"$tool" manifest --output "$tmp/manifests" "$assets" || exit 1
"$tool" check-manifest "$tmp/manifests" "$assets" || exit 1
printf '\n' >>"$tmp/manifests/aes128.json"
if "$tool" check-manifest "$tmp/manifests" "$assets"; then
	echo "test_empbc_tool: modified manifest was accepted" >&2
	exit 1
fi

# Regeneration repairs a changed entry and removes stale per-asset metadata.
printf '{}\n' >"$tmp/manifests/stale.json"
"$tool" manifest --output "$tmp/manifests" "$assets" || exit 1
if [[ -e "$tmp/manifests/stale.json" ]]; then
	echo "test_empbc_tool: stale manifest was not removed" >&2
	exit 1
fi
"$tool" check-manifest "$tmp/manifests" "$assets" || exit 1

echo "test_empbc_tool: OK"
