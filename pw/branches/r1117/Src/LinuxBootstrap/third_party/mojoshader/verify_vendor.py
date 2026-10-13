#!/usr/bin/env python3
'''Verify the pinned nine-file MojoShader subset and replay its two local patches.

Uses Python's standard library and the system patch utility. Only selected regular
archive members are copied into temporary storage; no vendor tree is modified.
'''

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile


PIN = 'ad5dff84830c2863c841f4b1f4e3df78c705b383'
ARCHIVE_SHA256 = '728ff9098115059c4012895b7a85065688c857fdadf9fbc04484fec8c7be3081'
FILES = (
	'LICENSE.txt', 'mojoshader.c', 'mojoshader.h', 'mojoshader_internal.h',
	'mojoshader_common.c', 'mojoshader_opengl.c', 'profiles/mojoshader_profile.h',
	'profiles/mojoshader_profile_common.c', 'profiles/mojoshader_profile_glsl.c',
)
PATCHES = (
	('0001-gl-link-stage-ownership.patch', 'mojoshader_opengl.c',
		'78860acc2f7bf44b78f1495926b502901d94f7f33f3a4a919d9f0865d8a6af40'),
	('0002-reglist-allocation-guards.patch', 'mojoshader.c',
		'8ed96f594cb73c53b089aacee30b7304d7f389b8db1aa5e36a9bd5d9ca4f9c69'),
)


def require(condition: bool, message: str) -> None:
	'''Fail closed on a provenance or exact replay mismatch.'''
	if not condition:
		raise ValueError(message)


def file_hash(path: Path) -> str:
	'''Hash an archive without loading the entire compressed file into memory.'''
	digest = hashlib.sha256()
	with path.open('rb') as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b''):
			digest.update(block)
	return digest.hexdigest()


def originals(archive: Path) -> dict:
	'''Read only the selected regular members from the verified official tarball.'''
	require(file_hash(archive) == ARCHIVE_SHA256, 'official archive SHA-256 mismatch')
	result = {}
	with tarfile.open(archive, 'r:gz') as source:
		members = source.getmembers()
		for name in FILES:
			full_name = 'mojoshader-' + PIN + '/' + name
			matches = [entry for entry in members if entry.name == full_name]
			require(len(matches) == 1 and matches[0].isfile(), 'invalid archive member: ' + name)
			stream = source.extractfile(matches[0])
			require(stream is not None, 'unreadable archive member: ' + name)
			with stream:
				result[name] = stream.read()
	return result


def replay(before: dict, patches: list) -> dict:
	'''Apply hash-pinned patches with the existing utility in disposable originals.'''
	tool = shutil.which('patch')
	require(tool is not None, 'system patch utility is required')
	with tempfile.TemporaryDirectory(prefix='mojoshader-verify-') as directory:
		root = Path(directory) / 'vendor' / 'mojoshader'
		for name, contents in before.items():
			path = root / name
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(contents)
		for name, contents in patches:
			result = subprocess.run([tool, '-p1', '--batch', '--fuzz=0'], input=contents,
				cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
			require(result.returncode == 0, 'patch replay failed: ' + name + '\n' +
				result.stdout.decode('utf-8', errors='replace'))
		actual = {path.relative_to(root).as_posix() for path in root.rglob('*') if path.is_file()}
		require(actual == set(FILES), 'patch replay changed the selected file inventory')
		return {name: (root / name).read_bytes() for name in FILES}


def verify(archive: Path, root: Path, patch_dir: Path) -> None:
	'''Check source inventory, pinned patch hashes, exact output and retained notices.'''
	require(root.is_dir(), 'vendor root is not a directory')
	require(not (root / 'GL').exists(), 'bundled GL directory is not permitted')
	allowed = set(FILES) | {'README.md', 'verify_vendor.py'} | {
		'patches/' + name for name, _, _ in PATCHES
	}
	for entry in root.rglob('*'):
		require(not entry.is_symlink(), 'symlink in vendor tree: ' + str(entry))
		if entry.is_file():
			require(entry.relative_to(root).as_posix() in allowed, 'unexpected vendor file: ' + str(entry))

	before = originals(archive)
	patches = []
	for name, target, digest in PATCHES:
		path = patch_dir / name
		require(not path.is_symlink(), 'patch must not be a symlink: ' + name)
		patch = path.read_bytes()
		require(hashlib.sha256(patch).hexdigest() == digest, 'local patch SHA-256 mismatch: ' + name)
		patches.append((name, patch))
	after = replay(before, patches)

	changed = {target for _, target, _ in PATCHES}
	for name in FILES:
		actual = (root / name).read_bytes()
		require(actual == after[name], 'source differs from official bytes plus recorded patches: ' + name)
		if name in changed:
			require(actual.splitlines(keepends=True)[:9] == before[name].splitlines(keepends=True)[:9],
				'original notice changed: ' + name)
			require(actual[-128:] == before[name][-128:], 'original EOF suffix changed: ' + name)
			for data in (before[name], actual):
				require(data.endswith(b'\n\n') and not data.endswith(b'\n\n\n'),
				'expected exactly two trailing LF bytes: ' + name)
		label = 'patched' if name in changed else 'official'
		print('PASS ' + label + ' ' + name + ' sha256=' + hashlib.sha256(actual).hexdigest())
	print('PASS pin=' + PIN + ' files=9 official=7 patched=2 exact_replay=2 notices=retained eof=retained')


def main() -> int:
	'''Accept a caller-supplied archive and optional vendor/patch roots.'''
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('archive', type=Path, help='official tar.gz for the pinned commit')
	parser.add_argument('--vendor-root', type=Path, default=Path(__file__).resolve().parent,
		help='defaults to the directory containing this verifier')
	parser.add_argument('--patch-dir', type=Path, help='defaults to vendor-root/patches')
	args = parser.parse_args()
	try:
		verify(args.archive, args.vendor_root, args.patch_dir or args.vendor_root / 'patches')
	except (OSError, ValueError, tarfile.TarError) as error:
		print('FAIL ' + str(error), file=sys.stderr)
		return 1
	return 0


if __name__ == '__main__':
	raise SystemExit(main())
