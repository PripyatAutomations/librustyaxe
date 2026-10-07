#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -fPIC -shared -I. -Iinc -Ibuild/${PROFILE:-radio} \
   librustyaxe/tests/module_log_fixture.c -L. -Wl,-rpath,"$PWD" -lrustyaxe \
   -o "$work/module_log_fixture.so"
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   librustyaxe/tests/module_log_shutdown.c -L. -Wl,-rpath,"$PWD" \
   -lrustyaxe -ldl -o "$work/module_log_shutdown"
"$work/module_log_shutdown" "$work"
