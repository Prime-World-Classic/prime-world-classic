#!/usr/bin/env python3
'''Exercise native map orders with real X11 input and exact projected destinations.'''

import argparse
from pathlib import Path
import subprocess
import time

from gameplay_gate import EvidenceError, fields, integer, number, pair, record, require, validate
from native_controls_probe import owned_window, pause, tool, wait_for, wait_for_window


SCENARIOS = {'reissue': ((500, 300), (500, 360)), 'obstructed': ((780, 410),)}


def validate_movement(stdout, scenario):
	'''Compare independently recorded projection and command positions at logged precision.

	An obstructed destination need not be reachable, but its order must retire
	without replacing the goal in the UI. The engine owns routing and arrival.
	'''
	move = fields(record(stdout, 'Final map command move', ': '), '=')
	live = fields(record(stdout, 'Final live 3D map commands', ': '), '=')
	expected = len(SCENARIOS[scenario])
	require(integer(move['commands']) == integer(live['move']) == integer(live['input']) == expected,
		'Expected only the actual mouse movement commands')
	require(live['proof'] == 'no' and live['surface'] == live['projection'] == 'yes',
		'Require actual projected map input, not a scripted demonstration')
	require(move['input'] == live['action'] == 'live-map-preview-move' and live['lastHit'] == 'command',
		'Unexpected movement source')
	require(pair(live['at'], ',', integer) == SCENARIOS[scenario][-1], 'Unexpected final mouse coordinates')
	requested = pair(live['world'], ',', number)
	require(pair(move['target'], ',', number) == pair(live['target'], ',', number) == requested,
		'UI replaced the projected destination before queuing movement')
	require(move['tracking'] == 'yes', 'Movement command was not tracked')
	require(move['moving'] == 'no', 'Movement order did not settle')
	if scenario == 'reissue':
		require(move['moved'] == move['reached'] == 'yes', 'Reissued move did not settle')
		require(number(move['remaining']) <= 1.01 and number(move['maxDistance']) > 1, 'Move did not reach the second goal')
	return move


def drive(window, scenario, deadline):
	'''Address only the owned focused window; never send global button events.'''
	for x, y in SCENARIOS[scenario]:
		require(tool('getwindowfocus', deadline=deadline) == window, 'Game lost focus')
		tool('mousemove', '--window', window, x, y, deadline=deadline)
		tool('click', '--window', window, 3, deadline=deadline)
		pause(2, deadline)


def run(binary, bin_dir, output, scenario):
	'''Launch one bounded interactive scene, reap it on all paths, then check stored replay evidence.'''
	deadline = time.monotonic() + 180
	with output.open('x') as stream:
		process = subprocess.Popen(['stdbuf', '-oL', '-eL', str(binary), '--seconds', '60',
			'--width', '1280', '--height', '720', '--bootstrap-create-game', '--bootstrap-interactive-world',
			'--bootstrap-frame-capture', str(output.with_suffix('.png')), '--bootstrap-frame-capture-after', '45'],
			cwd=bin_dir, stdout=stream, stderr=subprocess.STDOUT)
		try:
			wait_for_window(process, deadline)
			wait_for(process, output, 'Native scene window:', 1, deadline)
			pause(10, deadline)
			drive(owned_window(process, deadline), scenario, deadline)
			require(process.wait(timeout=max(1, deadline-time.monotonic())) == 0, 'Native client failed')
		finally:
			if process.poll() is None:
				process.terminate()
				try:
					process.wait(timeout=10)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=10)
	stdout = output.read_text(encoding='utf-8', errors='replace')
	log = Path(record(stdout, 'Logs', ': ')) / 'linux-client-shell.log'
	result = validate(stdout, log.read_text(), 'default')
	return result, validate_movement(stdout, scenario), log


def main():
	'''Require explicit paths and scenario; never start a GUI merely by importing the module.'''
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--binary', type=Path, required=True)
	parser.add_argument('--bin-dir', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--scenario', choices=SCENARIOS, required=True)
	args = parser.parse_args()
	try:
		result, movement, log = run(args.binary.resolve(), args.bin_dir.resolve(), args.output.resolve(), args.scenario)
	except (EvidenceError, KeyError, OSError, subprocess.SubprocessError) as error:
		parser.exit(1, f'Native movement FAILED: {error}\n')
	print(f'Native movement PASSED: {result}\nMovement: {movement}\nClient log: {log}')


if __name__ == '__main__':
	main()
