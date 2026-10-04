#!/usr/bin/env bash
exec python3 "$(dirname "$0")/test_tap.py" tcp "$@"
