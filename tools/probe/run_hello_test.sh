#!/usr/bin/env bash
set -euo pipefail

SERVER=""
PROBE=""
PORT=17777

while [[ $# -gt 0 ]]; do
	case "$1" in
		--server) SERVER="$2"; shift 2 ;;
		--probe) PROBE="$2"; shift 2 ;;
		--port) PORT="$2"; shift 2 ;;
		*) echo "unknown arg: $1" >&2; exit 2 ;;
	esac
done

export CMP_CONHOST=1
"$SERVER" --port "$PORT" &
spid=$!
cleanup() {
	kill "$spid" 2>/dev/null || true
	wait "$spid" 2>/dev/null || true
}
trap cleanup EXIT
sleep 0.4
"$PROBE" 127.0.0.1 "$PORT"
