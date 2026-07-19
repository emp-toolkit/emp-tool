#!/bin/bash
# Release-package smoke: install emp-tool to a staged prefix, then build and run
# an external find_package(emp-tool) consumer against it, and check the installed
# license/manifest payload is present. Proves the tagged package is consumable.
#
#   run.sh <source-dir> [cmake-configure-args...]
# Configure args (e.g. -DCMAKE_PREFIX_PATH=..., -DEMP_WITH_BLAKE3=ON) are passed
# through to emp-tool's configure so the caller controls backend/deps.
set -euo pipefail

if [[ $# -lt 1 ]]; then
	echo "usage: $0 <emp-tool-source-dir> [configure-args...]" >&2
	exit 2
fi
src=$1
shift

work=$(mktemp -d "${TMPDIR:-/tmp}/emp-install-smoke.XXXXXX")
prefix=$work/prefix
build=$work/build
cbuild=$work/consumer-build
trap 'rm -rf "$work"' EXIT

echo "== configure + build + install emp-tool -> $prefix"
cmake -S "$src" -B "$build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$prefix" \
	-DEMP_TOOL_BUILD_TESTS=OFF \
	"$@"
cmake --build "$build" -j
cmake --install "$build"

echo "== installed license + manifest payload present"
for f in share/licenses/emp-tool/LICENSE \
         share/licenses/emp-tool/THIRD_PARTY_NOTICES.md \
         include/emp-tool/ir/files/manifests/fp32_add.json \
         include/emp-tool/ir/files/fp32_add.empbc \
         lib/cmake/emp-tool/emp-tool-config.cmake; do
	if [[ ! -e "$prefix/$f" ]]; then
		echo "MISSING from install: $f" >&2
		exit 1
	fi
done

echo "== build + run the find_package(emp-tool) consumer"
cmake -S "$src/test/install_consumer" -B "$cbuild" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="$prefix" \
	"$@"
cmake --build "$cbuild" -j
"$cbuild/consumer"

echo "install_consumer smoke: OK"
