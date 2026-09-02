#!/usr/bin/env bash
set -euo pipefail

SERVER=""
CASES=""
CASE=""
PORT=17778
NO_FAKE=0
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--server) SERVER="$2"; shift 2 ;;
		--cases) CASES="$2"; shift 2 ;;
		--case) CASE="$2"; shift 2 ;;
		--port) PORT="$2"; shift 2 ;;
		--no-fake) NO_FAKE=1; shift ;;
		--extra-args)
			# Word-split the extra server flags, same as the PowerShell runner.
			# shellcheck disable=SC2206
			EXTRA_ARGS+=($2)
			shift 2
			;;
		*) echo "unknown arg: $1" >&2; exit 2 ;;
	esac
done

SESSION="${TMPDIR:-/tmp}/cmp-ctest-${PORT}"
rm -rf "$SESSION"
mkdir -p "$SESSION"
LOG="$SESSION/server.log"

export CMP_CONHOST=1
args=(--port "$PORT" --reset-session --session-dir "$SESSION" --log-file "$LOG")
if [[ "$NO_FAKE" -eq 1 ]]; then
	args+=(--no-fake)
fi
if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
	args+=("${EXTRA_ARGS[@]}")
fi

"$SERVER" "${args[@]}" &
spid=$!
cleanup() {
	kill "$spid" 2>/dev/null || true
	wait "$spid" 2>/dev/null || true
}
trap cleanup EXIT

sleep 0.45
if ! kill -0 "$spid" 2>/dev/null; then
	echo "server exited before the case ran. log: $LOG" >&2
	if [[ -f "$LOG" ]]; then
		tail -40 "$LOG" >&2
	fi
	exit 3
fi

set +e
"$CASES" --case "$CASE" --host 127.0.0.1 --port "$PORT" --session-dir "$SESSION"
code=$?
set -e
if [[ $code -ne 0 ]]; then
	if [[ -f "$LOG" ]]; then
		echo "--- server log ---"
		tail -40 "$LOG"
	fi
	exit "$code"
fi
exit 0
