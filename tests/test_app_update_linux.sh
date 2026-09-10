#!/bin/sh

if [ "${SNIFFY_TEST_MODE:-}" = mock ]; then
    PATH="$SNIFFY_TEST_ROOT/empty"
    export PATH
    id() { printf '1000\n'; }
    [ "$SNIFFY_TEST_SCENARIO" != root ] || id() { printf '0\n'; }
    kill() { [ "$SNIFFY_TEST_SCENARIO" = parent-timeout ]; }
    sleep() { :; }
    test() {
        if [ "$1" = -x ]; then
            [ "$SNIFFY_TEST_SCENARIO" != missing-app ]
        else
            command test "$@"
        fi
    }
    rm() { "$SNIFFY_TEST_RM" "$@"; }
    nohup() { printf 'Restart requested: %s\n' "$1"; }
    kdialog() { printf 'Notification: %s\n' "$*"; }
    apt_get_mock() {
        [ -f "$SNIFFY_TEST_HELPER.ready" ] || exit 90
        [ "$#" -eq 6 ] || exit 91
        [ "$1" = -y ] && [ "$2" = --no-remove ] && [ "$3" = -o ] || exit 92
        [ "$4" = Dpkg::Options::=--force-confold ] && [ "$5" = install ] || exit 93
        [ "$6" = "/tmp/Installer's space & percent%.deb" ] || exit 94
        [ "$DEBIAN_FRONTEND" = noninteractive ] || exit 95
        printf 'APT invoked\n'
        [ "$SNIFFY_TEST_SCENARIO" != installer-failure ] || return 100
    }
    pkexec() {
        printf 'Elevation requested\n'
        [ "$SNIFFY_TEST_SCENARIO" != auth-cancel ] || return 126
        [ "$SNIFFY_TEST_SCENARIO" != auth-denied ] || return 127
        [ "$1" = /usr/bin/env ] && [ "$2" = DEBIAN_FRONTEND=noninteractive ] || exit 96
        shift 2
        DEBIAN_FRONTEND=noninteractive "$@"
    }
    [ "$SNIFFY_TEST_SCENARIO" != missing-pkexec ] || unset -f pkexec
    return
fi

set -eu
fixture=$1
runner=$0
if command -v cygpath >/dev/null 2>&1; then
    fixture=$(cygpath -u "$fixture")
    runner=$(cygpath -u "$runner")
fi
runner=$(cd "$(dirname "$runner")" && pwd)/$(basename "$runner")
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
mkdir "$test_root/empty"
export SNIFFY_TEST_ROOT="$test_root"
export SNIFFY_TEST_RM="$(command -v rm)"
export SNIFFY_TEST_RUNNER="$runner"
shell=${SHELL_UNDER_TEST:-/bin/sh}
"$shell" -n "$fixture"
cat > "$test_root/apt-mock" <<'MOCK'
#!/bin/sh
. "$SNIFFY_TEST_RUNNER"
apt_get_mock "$@"
MOCK
chmod +x "$test_root/apt-mock"

for scenario in success root installer-failure auth-cancel auth-denied missing-apt missing-pkexec parent-timeout cancelled missing-app; do
    helper="$test_root/$scenario.sh"
    export SNIFFY_TEST_HELPER="$helper"
    rm -f "$test_root/empty/apt-get"
    [ "$scenario" = missing-apt ] || cp "$test_root/apt-mock" "$test_root/empty/apt-get"
    cp "$fixture" "$helper"
    [ "$scenario" != cancelled ] || : > "$helper.cancel"
    status=0
    SNIFFY_TEST_MODE=mock SNIFFY_TEST_SCENARIO="$scenario" \
        "$shell" -c '. "$1"; . "$0"' "$helper" "$runner" > "$helper.log" 2>&1 || status=$?
    case "$scenario" in
        success|root) expected=0 ;;
        installer-failure) expected=100 ;;
        auth-cancel) expected=126 ;;
        auth-denied) expected=127 ;;
        *) expected=1 ;;
    esac
    if [ "$status" -ne "$expected" ]; then
        cat "$helper.log"
        printf 'FAIL %s: expected exit %s, got %s\n' "$scenario" "$expected" "$status" >&2
        exit 1
    fi
    case "$scenario" in
        missing-apt|missing-pkexec|parent-timeout|cancelled)
            ! grep -q 'Elevation requested\|APT invoked\|Relaunching' "$helper.log"
            ;;
        missing-app)
            ! grep -q 'Relaunching' "$helper.log"
            grep -q 'Notification:' "$helper.log"
            ;;
        *) grep -q 'Relaunching' "$helper.log" ;;
    esac
    if [ "$expected" -eq 0 ]; then
        [ ! -f "$helper" ]
        [ ! -f "$helper.error" ]
        grep -q 'APT invoked' "$helper.log"
        ! grep -q 'Notification:' "$helper.log"
    else
        [ -f "$helper" ]
        [ -s "$helper.error" ]
    fi
    [ ! -f "$helper.ready" ]
    [ ! -f "$helper.cancel" ]
    printf 'PASS Linux script: %s\n' "$scenario"
done