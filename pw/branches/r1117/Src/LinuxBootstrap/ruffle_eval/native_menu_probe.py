#!/usr/bin/env python3
'''Start a local match through the authored lobby, without auto-create or proof flags.'''

import argparse
from pathlib import Path
import subprocess
import time

from gameplay_gate import EvidenceError, fields, integer, pair, record, require, validate
from native_controls_probe import owned_window, pause, tool, wait_for, wait_for_window


def command(binary, capture, seconds):
	'''Pin the shipped ten-player MOBA menu; no gameplay or UI script is injected.'''
	require(type(seconds) is int and 120 <= seconds <= 600, 'Require 120..600 seconds for menu startup')
	return ['stdbuf', '-oL', '-eL', str(binary), '--seconds', str(seconds), '--width', '1280',
		'--height', '720', '--map', 'MOBA', '--hero', 'plane',
		'--bootstrap-frame-capture', str(capture), '--bootstrap-frame-capture-after', str(seconds - 15)]


def validate_menu(stdout, client_log):
	'''Require actual Lua callbacks, live AI and queued commands, not a static menu capture.

	The existing gameplay gate checks ordinary startup, world progress and complete
	replay accounting. This driver submits no player gameplay commands: every replay
	command beyond the single keepalive must come from the real AI sender.
	'''
	result = validate(stdout, client_log, 'default', session_mode='ordinary')
	for key in ('bootstrapCreateGame', 'bootstrapLegacyLobbyOverlay', 'bootstrapLegacyHeroOverlay',
		'bootstrapLegacySessionOverlay'):
		require(record(client_log, key, '=') == 'no', f'{key}: automated/legacy startup is not menu evidence')
	require(record(client_log, 'finalBootstrapInputScript', '=') == '0/0/0.00', 'UI script bypassed real input')
	require(record(client_log, 'finalGameReplayInputPath', '=') == 'inactive', 'Replay is not a local match')
	require(record(client_log, 'finalProductionLobbyCreatedGame', '=') ==
		'Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb/10', 'Unexpected map or player count')
	lobby = fields(record(client_log, 'finalProductionLobbyCallbacks', '='), ':')
	for key, expected in (('create', 1), ('createMulti', 1), ('createSingle', 0), ('join', 0), ('spectate', 0), ('reconnect', 0)):
		require(integer(lobby.get(key, '')) == expected, f'Unexpected lobby callback: {key}')
	require(lobby.get('lastAction') == 'create-multiplayer', 'Wrong lobby action')
	hero = fields(record(client_log, 'finalProductionHeroCallbacks', '='), ':')
	require(integer(hero.get('ready', '')) == 1 and hero.get('lastAction') == 'ready', 'Missing Ready callback')
	require(record(client_log, 'finalProductionLobbyTransition', '=') ==
		'1/create-multiplayer hero:yes active:no', 'Missing authored hero-selection transition')
	require(record(client_log, 'finalProductionHeroLoadingTransition', '=') ==
		'1/ready loading:yes', 'Missing manual Ready-to-loading transition')
	for key in ('finalStartingLobbyPresentation', 'finalHeroLobbyPresentation'):
		mode, separator, counters = record(client_log, key, '=').partition(' ')
		require(separator and mode == 'production-xdb-lua', f'{key}: wrong presentation')
		counts = fields(counters, ':', 'productionFrames legacyFrames')
		require(integer(counts['productionFrames']) > 0 and integer(counts['legacyFrames']) == 0,
			f'{key}: authored screen was not rendered')
	for key, expected in (('finalGameWorldPlayers', 10), ('finalGameWorldPresentPlayers', 1), ('finalGameWorldPlayersWithHeroes', 10)):
		# Players and PlayersWithHeroes are distinct adjacent records, not duplicates.
		require(integer(record(client_log, key + '=', '')) == expected, f'{key}: incomplete lineup')
	ai = fields(record(client_log, 'finalLinuxAIControllers', '='), ':')
	require(pair(ai.get('auto', '')) == pair(ai.get('add', '')) == (9, 9), 'Menu match did not start all nine bots')
	require(pair(ai.get('bots', '')) == (1, 1), 'Authored bot policy was not enabled')
	require(integer(ai.get('active', '')) == 9 and integer(ai.get('steps', '')) > 100, 'Bot controllers did not tick')
	attempted, sent = pair(ai.get('commands', ''))
	require(attempted == sent and sent > 0, 'AI commands were not sent through the transceiver')
	require(integer(ai.get('fallback', '')) == 0, 'AI mutated the world without the command sender')
	require(integer(ai.get('combat', '')) > 0, 'Bots did not issue lane orders')
	require(result['replay_commands'] == sent + 1, 'Replay must contain AI commands plus one keepalive')
	return dict(result, bots=9, ai_commands=sent)


def drive(window, deadline):
	'''Select the authored MOBA row, then click Create and Ready at 1280x720.'''
	for x, y, delay in ((425, 360, 2), (551, 618, 12), (640, 477, 12)):
		require(tool('getwindowfocus', deadline=deadline) == window, 'Game lost focus')
		tool('mousemove', '--window', window, x, y, deadline=deadline)
		tool('click', '--window', window, 1, deadline=deadline)
		pause(delay, deadline)


def run(binary, bin_dir, output, seconds=120):
	'''Own and reap one client; keep screenshots and logs even when the regression fails.'''
	capture = output.with_suffix('.png')
	args = command(binary, capture, seconds)
	require(not capture.exists(), f'Refusing to replace capture: {capture}')
	deadline = time.monotonic() + seconds + 120
	with output.open('x', encoding='utf-8') as stream:
		process = subprocess.Popen(args, cwd=bin_dir, stdout=stream, stderr=subprocess.STDOUT)
		try:
			wait_for_window(process, deadline)
			wait_for(process, output, 'Native scene window:', 1, deadline)
			pause(3, deadline)
			drive(owned_window(process, deadline), deadline)
			require(process.wait(timeout=max(1, deadline - time.monotonic())) == 0, 'Native client failed')
		finally:
			if process.poll() is None:
				process.terminate()
				try:
					process.wait(timeout=10)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=10)
	stdout = output.read_text(encoding='utf-8')
	client_log = Path(record(stdout, 'Logs', ': ')) / 'linux-client-shell.log'
	return validate_menu(stdout, client_log.read_text(encoding='utf-8')), client_log


def main():
	'''This display test is opt-in; CTest runs only the mock evidence/driver tests.'''
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--binary', type=Path, required=True)
	parser.add_argument('--bin-dir', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--seconds', type=int, default=120)
	args = parser.parse_args()
	try:
		result, client_log = run(args.binary.resolve(strict=True), args.bin_dir.resolve(strict=True),
			args.output.absolute(), args.seconds)
	except (OSError, ValueError, subprocess.SubprocessError) as error:
		parser.exit(1, f'Native menu FAILED: {error}\n')
	print(f'Native menu PASSED: {result}\nClient log: {client_log}')


if __name__ == '__main__':
	main()
