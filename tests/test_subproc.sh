#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. \
   librustyaxe/tests/subproc.c -L. -Wl,-rpath,"$PWD" -lrustyaxe -o "$work/subproc"
"$work/subproc"
