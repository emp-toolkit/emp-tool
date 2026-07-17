#!/bin/bash
# Regression checks for the two-party ./run supervisor. A temporary fake party
# lets the test control each role's exit status and lifetime without opening a
# network socket.

if [[ $# -ne 1 ]]; then
	echo "usage: $0 <run-harness>" >&2
	exit 2
fi

harness=$1
state_dir=$(mktemp -d "${TMPDIR:-/tmp}/emp-test-run.XXXXXX") || exit 1
helper=$state_dir/fake-party
failures=0

cleanup() {
	rm -rf "$state_dir"
}
trap cleanup EXIT

cat >"$helper" <<'EOF'
#!/bin/bash
party=$1
scenario=$2
state_dir=$3
shift 3

echo $$ >"$state_dir/party-$party.pid"
peer=1
if [[ $party == 1 ]]; then peer=2; fi

# Make both roles observable before either intentional failure fires, avoiding
# a test-only launch race when checking that the sibling was cleaned up.
attempts=0
while [[ ! -f "$state_dir/party-$peer.pid" ]] && (( attempts < 200 )); do
	sleep 0.01
	((attempts += 1))
done

hang() {
	local sleeper
	sleep 30 &
	sleeper=$!
	echo "$sleeper" >"$state_dir/sleeper-$party.pid"
	trap 'kill "$sleeper" 2>/dev/null; wait "$sleeper" 2>/dev/null; exit 0' INT TERM HUP
	wait "$sleeper"
}

case "$scenario" in
	success)
		exit 0
		;;
	alice-fails)
		if [[ $party == 1 ]]; then exit 7; fi
		hang
		;;
	bob-fails)
		if [[ $party == 2 ]]; then exit 9; fi
		hang
		;;
	hang)
		hang
		;;
	arguments)
		if [[ $# -ne 2 || $1 != "argument with spaces" || $2 != "*.literal" ]]; then
			exit 41
		fi
		exit 0
		;;
	*)
		exit 42
		;;
esac
EOF
chmod +x "$helper"

record_failure() {
	echo "test_run: FAIL: $1" >&2
	((failures += 1))
}

processes_are_gone() {
	local label=$1
	local party pid_file pid
	for party in 1 2; do
		pid_file=$state_dir/party-$party.pid
		if [[ ! -f "$pid_file" ]]; then
			record_failure "$label: party $party did not start"
			continue
		fi
		pid=$(<"$pid_file")
		if kill -0 "$pid" 2>/dev/null; then
			record_failure "$label: party $party was left running (pid $pid)"
			kill -KILL "$pid" 2>/dev/null || true
		fi
		pid_file=$state_dir/sleeper-$party.pid
		if [[ ! -f "$pid_file" ]]; then
			continue
		fi
		pid=$(<"$pid_file")
		if kill -0 "$pid" 2>/dev/null; then
			record_failure "$label: party $party's sleeper was left running (pid $pid)"
			kill -KILL "$pid" 2>/dev/null || true
		fi
	done
}

run_case() {
	local expected=$1
	local scenario=$2
	local timeout=$3
	local label=$4
	shift 4

	rm -f "$state_dir"/party-*.pid "$state_dir"/sleeper-*.pid
	EMP_RUN_TIMEOUT=$timeout "$harness" "$helper" "$scenario" "$state_dir" "$@"
	local status=$?
	if [[ $status -ne $expected ]]; then
		record_failure "$label: expected status $expected, got $status"
	fi
	processes_are_gone "$label"
}

run_case 0 success 5 "both succeed"
run_case 7 alice-fails 5 "ALICE failure"
run_case 9 bob-fails 5 "BOB failure"
run_case 0 arguments 5 "quoted arguments" "argument with spaces" "*.literal"
run_case 124 hang 1 "timeout"

# A signal delivered to the supervisor must clean up both live parties and
# preserve the conventional shell status for that signal.
rm -f "$state_dir"/party-*.pid "$state_dir"/sleeper-*.pid
EMP_RUN_TIMEOUT=30 "$harness" "$helper" hang "$state_dir" &
harness_pid=$!
attempts=0
while [[ (! -f "$state_dir/party-1.pid" || ! -f "$state_dir/party-2.pid") && $attempts -lt 200 ]]; do
	sleep 0.01
	((attempts += 1))
done
kill -TERM "$harness_pid" 2>/dev/null
wait "$harness_pid"
status=$?
if [[ $status -ne 143 ]]; then
	record_failure "signal cleanup: expected status 143, got $status"
fi
processes_are_gone "signal cleanup"

if (( failures != 0 )); then
	echo "test_run: $failures failure(s)" >&2
	exit 1
fi

echo "test_run: OK"
