#!/usr/bin/env bash
# Headless smoke test: starts the app offscreen in an isolated environment,
# saves a screenshot and fails on a missing image or any QML error.
#
#   scripts/smoke-test.sh [binary] [out.png]
#
# The private XDG_RUNTIME_DIR and TMUX_TMPDIR keep it away from a running
# instance's hook socket and tmux server.
set -euo pipefail

bin=${1:-build/x3ro-deck}
out=${2:-smoke.png}
delay=${SMOKE_DELAY_MS:-6000}

# Short path: a socket path over 107 bytes fails to bind.
tmp=$(mktemp -d /tmp/xds.XXXXXX)
trap 'tmux -L x3ro-deck kill-server 2>/dev/null || true; rm -rf "$tmp"' EXIT
chmod 700 "$tmp"
mkdir -p "$tmp/config/x3ro-deck"
echo '{"autostartAgents": false}' >"$tmp/config/x3ro-deck/config.json"

export XDG_RUNTIME_DIR=$tmp TMUX_TMPDIR=$tmp XDG_CONFIG_HOME=$tmp/config
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_FORCE_STDERR_LOGGING=1

rm -f "$out"
status=0
timeout 120 dbus-run-session -- "$bin" --tab agents --select 0 --screenshot "$out" "$delay" >"$tmp/log" 2>&1 || status=$?
cat "$tmp/log"

fail=0
if [[ $status -ne 0 ]]; then
    echo "smoke: app exited with status $status" >&2
    fail=1
fi
if [[ ! -s $out ]]; then
    echo "smoke: no screenshot at $out" >&2
    fail=1
fi
# QQmlError::toString() and QML runtime warnings start with the file URL.
if grep -E 'qrc:/.*\.qml(:[0-9]+)?' "$tmp/log" >&2; then
    echo "smoke: QML errors above" >&2
    fail=1
fi
[[ $fail -eq 0 ]] && echo "smoke: ok, screenshot at $out"
exit $fail
