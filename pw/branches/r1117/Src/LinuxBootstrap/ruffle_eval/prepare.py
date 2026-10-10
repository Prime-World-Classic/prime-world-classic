#!/usr/bin/env python3
'''Stage the evaluation host in a separate, pinned official Ruffle checkout.'''

import argparse
from pathlib import Path
import subprocess


REVISION = '1b24dd3a6925eecdd1d7fa49e165814e7ed2163d'
HERE = Path(__file__).resolve().parent
FILES = {'host_api.rs': 'core/src/primeworld.rs', 'primeworld.rs': 'core/examples/primeworld.rs',
	'native_assets.rs': 'core/src/primeworld_assets.rs', 'native_loader.rs': 'core/src/primeworld_loader.rs',
	'loader_regression.rs': 'core/src/avm2/loader_regression.rs',
	'handles.rs': 'core/src/avm2/pw_handles.rs', 'runtime.rs': 'core/examples/pw_runtime/mod.rs',
	'input.rs': 'core/examples/pw_runtime/input.rs',
	'input_state.rs': 'core/examples/pw_runtime/input_state.rs',
	'handles_probe.rs': 'core/examples/handles_probe.rs', 'ffi.rs': 'core/examples/pw_bridge.rs'}


def git(source, *args):
	'''Run Git directly, keeping evaluation paths out of the shell parser.'''
	return subprocess.run(['git', '-C', str(source), *args], capture_output=True, text=True)


def patch_needed(source, patch):
	'''Accept an already applied patch, but reject conflicting local edits.'''
	if git(source, 'apply', '--reverse', '--check', str(patch)).returncode == 0:
		return False
	result = git(source, 'apply', '--check', str(patch))
	if result.returncode:
		raise RuntimeError('Patch conflicts with checkout: ' + result.stderr)
	return True


def stage_plan(source, files=FILES):
	'''Preflight every destination before writing; never overwrite a different local file.'''
	plan = []
	for name, relative in files.items():
		destination = source / relative
		if not destination.resolve().is_relative_to(source):
			raise RuntimeError('Destination escapes checkout: ' + str(destination))
		content = (HERE / name).read_bytes()
		if destination.exists() and destination.read_bytes() != content:
			raise RuntimeError('Refusing to overwrite local file: ' + str(destination))
		if not destination.exists():
			plan.append((destination, content))
	return plan


def prepare(source, user_input=False):
	'''Apply only the named evaluation patches at the exact audited revision.'''
	source = source.resolve(strict=True)
	revision = git(source, 'rev-parse', 'HEAD')
	if revision.returncode or revision.stdout.strip() != REVISION:
		raise RuntimeError('Expected Ruffle revision ' + REVISION)
	# Untracked source and optional builtin modifications must not be silently clobbered.
	files = stage_plan(source)
	patches = [HERE / 'host.patch', HERE / 'deps.patch', HERE / 'mouse_events.patch']
	if user_input:
		patches.append(HERE / 'user_input.patch')
	pending = [patch for patch in patches if patch_needed(source, patch)]
	for patch in pending:
		result = git(source, 'apply', str(patch))
		if result.returncode:
			raise RuntimeError(result.stderr)
	for destination, content in files:
		destination.parent.mkdir(parents=True, exist_ok=True)
		destination.write_bytes(content)
	print('Prepared native Ruffle evaluation at ' + REVISION)


def main():
	'''Do not clone, fetch, build, alter game assets, or switch the client backend.'''
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('source', type=Path)
	parser.add_argument('--user-input', action='store_true', help='Apply the incomplete text compatibility experiment')
	args = parser.parse_args()
	prepare(args.source, args.user_input)


if __name__ == '__main__':
	main()
