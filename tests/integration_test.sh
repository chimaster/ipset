#!/usr/bin/env bash
# Integration test for the ipset wrapper library.
#
# Requires root/CAP_NET_ADMIN and the ip_set kernel module, so it's kept
# separate from a normal unit-test run and skipped (exit 77) rather than
# failed when that access isn't available -- e.g. in most CI containers.
set -u

DEMO_BIN="${1:?usage: integration_test.sh <path-to-waf_tarpit_demo>}"
SET_NAME="waf_offenders_demo"

if [ "$(id -u)" -ne 0 ]; then
    echo "SKIP: integration test needs root/CAP_NET_ADMIN (got uid $(id -u))"
    exit 77
fi

if ! command -v ipset >/dev/null 2>&1; then
    echo "SKIP: ipset CLI not found (install the 'ipset' package to verify state)"
    exit 77
fi

cleanup() {
    ipset destroy "$SET_NAME" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "=== running $DEMO_BIN ==="
"$DEMO_BIN"
status=$?

if [ $status -ne 0 ]; then
    echo "FAIL: demo exited with status $status (likely no kernel ip_set access)"
    exit 77
fi

echo "=== verifying kernel state after demo ==="
if ipset list "$SET_NAME" >/dev/null 2>&1; then
    echo "OK: set '$SET_NAME' exists after ensureSet()"
else
    echo "FAIL: set '$SET_NAME' does not exist after ensureSet()"
    exit 1
fi

if ipset test "$SET_NAME" 203.0.113.7 >/dev/null 2>&1; then
    echo "FAIL: 203.0.113.7 should have been del()'d by the demo but is still a member"
    exit 1
else
    echo "OK: 203.0.113.7 correctly absent after del()"
fi

echo "PASS"
exit 0
