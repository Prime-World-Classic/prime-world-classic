#!/usr/bin/env bash
# Build the pinned opt-in adapter offline; keep Rust unwinding inside its FFI guards.
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || ! ${2:-4} =~ ^[1-9][0-9]*$ || ${2:-4} -gt 64 ]]; then
	printf 'Usage: %s RUFFLE_CHECKOUT [JOBS:1..64]\n' "$0" >&2
	exit 2
fi

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python3 -B "$here/prepare.py" "$1" --user-input
# Upstream release defaults to abort, which bypasses catch_unwind at the C ABI.
export CARGO_PROFILE_RELEASE_PANIC=unwind
export CARGO_PROFILE_RELEASE_OVERFLOW_CHECKS=true
cargo build --manifest-path "$1/Cargo.toml" --locked --offline --release \
	-p ruffle_core --example pw_bridge --features default_font,primeworld_mouse_events -j "${2:-4}"
