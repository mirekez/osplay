#!/usr/bin/env bash
# Tribe-compatible entry point. This platform currently runs native Verilator.
exec bash "$(dirname "$0")/prepare.sh" "$@"
