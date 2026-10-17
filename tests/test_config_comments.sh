#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I. \
   tests/config_comments.c -o "$bin"
"$bin"
echo "config block-comment lexer: OK"
