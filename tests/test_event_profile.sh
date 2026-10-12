#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} ${EVENT_PROFILE_CFLAGS:-} \
   librustyaxe/tests/event_profile.c -L. -Wl,-rpath,"$PWD" -lrustyaxe -pthread \
   -o "$work/event_profile"
"$work/event_profile"
