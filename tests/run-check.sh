#!/usr/bin/env bash
# Run all standalone library tests, preserving failures after cleanup.
set -u

build_dir=$1
shift
failed=0

for test in "$@"; do
   case "$test" in
      /*) "$test" ;;
      *) "./$test" ;;
   esac || failed=1
done

python3 socks_tcp_tls.py "$build_dir/test_transport_tcp" || failed=1
make clean "TEST_BUILD_DIR=$build_dir" || failed=1
exit "$failed"
