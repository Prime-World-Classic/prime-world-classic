# Pinned MojoShader Subset

This directory contains a **locally patched** subset of
[official MojoShader](https://github.com/icculus/mojoshader) at unsigned commit
[`ad5dff84830c2863c841f4b1f4e3df78c705b383`](https://github.com/icculus/mojoshader/commit/ad5dff84830c2863c841f4b1f4e3df78c705b383).
The original implementation is by Ryan C. Gordon and upstream contributors, not
the local adapter authors. The root [zlib license](LICENSE.txt) and source notices
are retained unchanged.

## Upstream Bytes

Official archive:
[mojoshader-ad5dff84830c2863c841f4b1f4e3df78c705b383.tar.gz](https://codeload.github.com/icculus/mojoshader/tar.gz/ad5dff84830c2863c841f4b1f4e3df78c705b383)

Archive SHA-256:
`728ff9098115059c4012895b7a85065688c857fdadf9fbc04484fec8c7be3081`.
The hash pins the retained archive, not a signature or an upstream endorsement.
The subset has exactly nine upstream files:

```text
LICENSE.txt
mojoshader.c
mojoshader.h
mojoshader_internal.h
mojoshader_common.c
mojoshader_opengl.c
profiles/mojoshader_profile.h
profiles/mojoshader_profile_common.c
profiles/mojoshader_profile_glsl.c
```

Only `mojoshader.c` and `mojoshader_opengl.c` are locally modified. The other seven
files remain byte-identical to the official archive. No upstream GL headers,
build scripts, tools, effects framework or unused profiles are included. Relevant
upstream API documentation is in [mojoshader.h](mojoshader.h).

## Local Patches

Apply these two changes in order, and no historical candidate patch:

1. [0001-gl-link-stage-ownership.patch](patches/0001-gl-link-stage-ownership.patch)
   defers VS/PS reference acquisition until all fallible link initialization has
   succeeded. A PS-uniform allocation failure must not orphan a VS reference.
   SHA-256: `78860acc2f7bf44b78f1495926b502901d94f7f33f3a4a919d9f0865d8a6af40`.
2. [0002-reglist-allocation-guards.patch](patches/0002-reglist-allocation-guards.patch)
   guards NULL `reglist_insert` results in `add_attribute_register` and
   `add_sampler`, retaining the existing OOM reporting instead of dereferencing
   the failed allocation.
   SHA-256: `8ed96f594cb73c53b089aacee30b7304d7f389b8db1aa5e36a9bd5d9ca4f9c69`.

These are local changes, not claimed upstream releases. Their unified-diff paths
retain `a/vendor/mojoshader` and `b/vendor/mojoshader`; the verifier recreates
that layout in a temporary directory. Both modified sources retain their original
notice prefixes, original EOF suffixes and exactly two trailing LF bytes. Keep
source and patch files byte-for-byte, including line endings; normalizing them
will intentionally fail verification.

## Portable Verification

From the repository root, supply the already-downloaded official archive:

```sh
python3 pw/branches/r1117/Src/LinuxBootstrap/third_party/mojoshader/verify_vendor.py /path/to/mojoshader-ad5dff84830c2863c841f4b1f4e3df78c705b383.tar.gz
```

The verifier uses Python 3's standard library and the system `patch` utility,
makes no network/GL calls and does not build anything. The vendor root defaults
to its own directory; `--vendor-root` and `--patch-dir` support alternate staging
locations. It verifies the archive and both patch hashes, reads only the nine
selected regular tar members, and copies them into a `TemporaryDirectory`.
It runs `patch -p1 --batch --fuzz=0` there, then compares all nine resulting files.
The original archive and working vendor are never extracted over or modified.
Notices and EOF bytes are checked explicitly. The seven unchanged files must
match official bytes. Extra files are rejected except this README, the verifier
and the two recorded patch files; symlinks and bundled GL are not accepted.

Exit 0 means provenance/replay passed, not that renderer runtime tests passed.
Failure prints a reason and exits 1. This audit needs no engine headers, private
build directories, system GL headers, git program or test runner. The system
`patch` utility must be available on PATH and support the flags shown above.
The supplied archive is read in binary mode; the local source files are never
silently repaired.
