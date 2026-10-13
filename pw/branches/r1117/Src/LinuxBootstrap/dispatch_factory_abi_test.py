#!/usr/bin/env python3
'''Verify the factory ABI using the actual generated caller compile profiles.'''

import json
from pathlib import Path
import shlex
import subprocess
import sys


SOURCES = {
	'PFAbilityInstance.cpp', 'PFBaseAttackData.cpp', 'PFDispatchFactory.cpp',
	'PFApplBounce.cpp', 'PFApplHelper.cpp', 'PFApplMod.cpp', 'PFApplUtils.cpp',
	'PFApplSpecial.cpp', 'PFApplChainLightning.cpp',
}
TARGETS = {'PrimeWorldLinuxClient', 'PrimeWorldLinuxClientRuntimeProbe',
	'PrimeWorldLinuxGameplayProbe'}


def probe_command(entry, probe):
	'''Replace only the compilation input/output, preserving the generated profile.'''
	args = entry.get('arguments') or shlex.split(entry['command'])
	result = []
	iterator = iter(args)
	for arg in iterator:
		if arg in ('-o', '-MF', '-MT', '-MQ'):
			next(iterator)
		elif arg not in ('-c', '-MD', '-MMD', '-MP', entry['file']):
			result.append(arg)
	return result + ['-fsyntax-only', str(probe)]


def main():
	'''Fail on missing callers or any incompatible declaration, including object-only targets.'''
	database = Path(sys.argv[1])
	probe = Path(__file__).with_name('dispatch_factory_abi_probe.cpp').resolve()
	entries = json.loads(database.read_text())
	seen = set()
	profiles = {}
	for entry in entries:
		source = Path(entry['file']).name
		output = entry.get('output', entry.get('command', ''))
		target = next((item for item in TARGETS if f'{item}.dir/' in output), None)
		if source not in SOURCES or target is None:
			continue
		seen.add((target, source))
		command = tuple(probe_command(entry, probe))
		profiles.setdefault((entry['directory'], command), []).append(f'{target}/{source}')
	missing = {(target, source) for target in TARGETS for source in SOURCES} - seen
	if missing:
		print(f'Missing generated factory/caller profiles: {sorted(missing)}', file=sys.stderr)
		return 1
	failures = 0
	for (directory, command), callers in profiles.items():
		result = subprocess.run(command, cwd=directory, capture_output=True, text=True)
		if result.returncode:
			failures += 1
			print(f'Incompatible dispatch ABI: {", ".join(callers)}\n{result.stderr}', file=sys.stderr)
	print(f'Dispatch ABI: {len(seen)} caller builds, {len(profiles)} profiles, {failures} failures')
	return bool(failures)


if __name__ == '__main__':
	sys.exit(main())
