#!/usr/bin/env python3
'''Opt-in Healer A1 HUD smoke: purchase, reject empty ground, then click the hero.

Supply explicit client, bridge, Bin directory and new output paths. This
probe never changes world state through a debug command, swaps assets, or invokes
an ability directly. It shares the existing purchase setup and owned X11 window
helpers, but validates Healer's unit endpoint, 80 mana and four-second cooldown.
Default duration/control delay are 90/45 seconds; shorter runs need a smaller
--control-delay. All evidence is retained, and the launched process is reaped.
Full-health self-casting proves targeting/execution, not a positive healing delta.
'''

import argparse
from decimal import Decimal, InvalidOperation
from pathlib import Path
import re
import subprocess
import time

from gameplay_gate import EvidenceError, RUFFLE_COUNTERS, fields, integer, number, pair, record, replay, require, session_effects, session_policy
from native_controls_probe import check_timing, command as controls_command, owned_window, pause, tool, wait_for, wait_for_window


def command(binary, library, capture, seconds=90):
	'''Use the existing native HUD setup, changing only the ordinary hero selection.'''
	return controls_command(binary, library, capture, seconds) + ['--hero', 'healer']


def decimal(value):
	'''Validate finite decimal syntax, retaining exact logged payment precision.'''
	number(value)
	try:
		result = Decimal(value)
	except InvalidOperation as error:
		raise EvidenceError(f'Bad decimal: {value}') from error
	require(result.is_finite(), f'Nonfinite decimal: {value}')
	return result


def ordered(stdout, prefixes):
	'''Require one record per prefix and strict input-to-execution ordering.'''
	positions = []
	for prefix in prefixes:
		matches = [i for i, line in enumerate(stdout.splitlines()) if line.lstrip().startswith(prefix)]
		require(len(matches) == 1, f'{prefix}: missing or duplicate ordered record')
		positions.append(matches[0])
	require(all(a < b for a, b in zip(positions, positions[1:])), 'Input/execution records are out of order')


def validate_unit_target(stdout, client_log, seconds=90, min_cast_step=0):
	'''Validate real execution, independent hero snapshots and native session health.

	Reuse the generic replay and startup gates, never the Plane-specific targeting
	validator or rewritten log records. UI submission is insufficient: the original
	ability must produce an activation receipt, payment and cooldown for this hero.
	'''
	check_timing(seconds, 0)
	require(type(min_cast_step) is int and min_cast_step >= 0, 'Invalid minimum cast step')
	for name, text in (('stdout', stdout), ('client log', client_log)):
		require(text.endswith('\n') and '\0' not in text, f'{name}: truncated or invalid text')
		for failure in ('Local scheduler drain timed out:', 'AddressSanitizer', 'runtime error:'):
			require(failure not in text, f'{name}: {failure}')
	require(record(stdout, 'Logs', ': ').strip(), 'Missing client log directory')
	require(re.fullmatch(r'.+ \(healer\)', record(stdout, 'Selected lineup hero', ': ')) is not None,
		'Require the shipped Healer hero')
	session_effects(stdout)
	finish = 'Prime World Linux client shell finished.'
	require(record(stdout, finish, '') == '', 'Malformed native completion')
	timing = fields(record(client_log, 'finalClientTimingMs', '='), ':', 'frames input update assets draw')
	require(integer(timing['frames']) > 0, 'No profiled frames')
	for key in ('input', 'update', 'assets', 'draw'):
		require(number(timing[key]) >= 0, f'Negative {key} timing')
	require(number(timing['update']) > 0 and number(timing['draw']) > 0, 'No update/draw progress')
	require(record(client_log, 'finalGameTransceiverWorldAttached', '=') == 'yes', 'World not attached')
	step = integer(record(client_log, 'finalGameTransceiverWorldStep', '='))
	require(step > 0 and integer(record(client_log, 'finalNativeWorldPresentationFrames', '=')) > 0, 'No world progress')
	commands, byte_count, replay_step = replay(stdout)
	readback = fields(record(stdout, 'Final replay readback', ': '), '=',
		'decoded commands failures bytes distinctTypes clients first last countMatch')
	require(readback['decoded'] == readback['countMatch'] == 'yes' and integer(readback['failures']) == 0,
		'Replay command decoding failed')
	require(pair(readback['commands']) == (commands, commands) and integer(readback['bytes']) > 0 and
		integer(readback['distinctTypes']) > 0, 'Replay decoded command counts disagree')
	world = fields(record(stdout, 'Final game transceiver runtime', ': '), '=')
	require(world.get('ready') == world.get('world') == 'yes', 'Transceiver not ready')
	# Like the existing gate, accept idle ticks absent from sparse replay storage.
	require(integer(world.get('step', '')) == step and replay_step <= step, 'World/replay steps disagree')
	require(integer(world.get('commands', '')) == integer(world.get('replayCommands', '')) == commands >= 2,
		'World/replay command counts disagree')
	require(integer(world.get('replayBytes', '')) == byte_count, 'World/replay byte counts disagree')
	sent = session_policy(stdout, client_log, 'ordinary', 2)
	require(commands >= sent, 'Replay is missing submitted commands')
	for key in ('heroMoveCommands', 'heroStopCommands', 'heroAttackCommands', 'heroCancelCommands', 'heroUseUnitCommands', 'heroUseTalentCommands'):
		require(integer(world.get(key, '')) == 0, f'Unexpected legacy command: {key}')
	clock = fields(record(client_log, 'finalInteractiveClock', '='), ':', 'ticks pumps pendingSeconds discardedSeconds')
	ticks, pumps = integer(clock['ticks']), integer(clock['pumps'])
	pending, discarded = number(clock['pendingSeconds']), number(clock['discardedSeconds'])
	require(ticks > 0 and pumps > 0 and 0 <= ticks - step <= 3, 'Interactive clock/world mismatch')
	require(0 <= pending <= 0.5 and discarded >= 0, 'Invalid interactive clock debt')
	require(abs(ticks * 0.1 + pending + discarded - seconds) <= 2, 'Interactive clock is not wall paced')
	camera = fields(record(client_log, 'finalLocalMapCamera', '='), ':', 'active follow updates focusHeight')
	require(integer(camera['active']) == integer(camera['follow']) == 1 and integer(camera['updates']) > 0,
		'Local camera lost hero follow')
	number(camera['focusHeight'])
	bounds = pair(record(client_log, 'mapTacticalBounds', '='), ' -> ', lambda value: pair(value, ',', number))
	require(all(bounds[0][i] < bounds[1][i] for i in (0, 1)), 'Invalid map bounds')

	require(record(client_log, 'finalLiveHeroReady', '=') == 'yes' and record(client_log, 'finalLiveHeroDead', '=') == 'no',
		'Controlled hero is unavailable/dead')
	require(record(client_log, 'finalLiveHeroUnitDbid', '=') == 'Heroes/Healer/_.HROB.xdb', 'Controlled hero asset mismatch')
	identity = tuple(integer(record(client_log, 'finalLiveHero' + key, '=')) for key in ('ObjectId', 'PlayerId', 'UserId'))
	faction = integer(record(client_log, 'finalLiveHeroFaction', '='))
	require(faction in (1, 2), 'Controlled hero has no playing faction')
	storage = fields(record(stdout, 'Final replay storage', ': '), '=')
	require(integer(storage['client']) == identity[2], 'Replay client differs from the controlled human')
	require(pair(readback['clients'], '..') == (identity[2], identity[2]), 'Decoded replay client mismatch')
	for key in ('first', 'last'):
		parts = readback[key].split('/')
		require(len(parts) == 3 and re.fullmatch(r'0x[0-9a-fA-F]+', parts[0]) is not None,
			f'Malformed decoded {key} command')
		require(integer(parts[1]) == identity[2], f'Decoded {key} command client mismatch')
		integer(parts[2])

	status, space, rest = record(client_log, 'finalRuffleInspection', '=').partition(' ')
	require(space and status == 'ready', 'Ruffle is not ready')
	ruffle = fields(rest, ':', RUFFLE_COUNTERS + ' minimapTarget prime error')
	for key in RUFFLE_COUNTERS.split():
		ruffle[key] = integer(ruffle[key])
	require(ruffle['error'] == '' and ruffle['attempted'] == 1 and ruffle['frames'] > 0, 'Ruffle initialization failed')
	for key in ('hudCalls', 'actionCalls', 'pointerEvents', 'consumedPointerEvents'):
		require(ruffle[key] > 0, f'No Ruffle {key}')
	expected = {'pendingCallbacks': 0, 'priorGlErrors': 0, 'gameplayRequests': 2, 'rejectedRequests': 0,
		'needsTarget': 1, 'targetArmed': 1, 'targetCasts': 1, 'targetRejected': 1, 'targetCanceled': 0,
		'targetPending': 0, 'shortcutCalls': 1, 'talentCommands': 2, 'minimapMoves': 0, 'minimapCameras': 0,
		'matchEndCalls': 0, 'matchExitRequests': 0}
	for key, value in expected.items():
		require(ruffle[key] == value, f'Ruffle {key}: expected {value}, got {ruffle[key]}')
	pair(ruffle['minimapTarget'], ',', number)
	gold = integer(record(client_log, 'finalLiveHeroGold', '='))
	require(gold <= 2147483647 and pair(ruffle['prime'], '->') == (400, gold), 'HUD/live prime mismatch')
	request = record(stdout, 'Ruffle talent command', ': ')
	action, space, rest = request.partition(' ')
	require(space and action == 'buy', 'Expected exactly one HUD purchase request')
	buy = fields(rest, '=', 'row column prime step')
	require(buy['row'] == buy['column'] == '0' and integer(buy['prime']) == 400, 'HUD purchase slot/prime mismatch')
	buy_step = integer(buy['step'])

	def local_event(name, schema):
		'''Reject bot success, wrong slots and duplicate local activation records.'''
		result = fields(record(stdout, 'Native local talent ' + name, ': '), '=', 'hero player user ' + schema)
		require(tuple(integer(result[key]) for key in ('hero', 'player', 'user')) == identity, 'Local event actor mismatch')
		require(result['slot'] == '0,0', 'Local event talent mismatch')
		return result

	bought = local_event('purchase', 'activated slot progress dev gold')
	require(bought['activated'] == '1' and pair(bought['gold'], '->') == (400, 100), 'Purchase did not execute/deduct 300 prime')
	before, after = pair(bought['dev'], '->')
	require(after == before + 1, 'Purchase did not add one development point')
	before, after = pair(bought['progress'], '->')
	require(after >= before, 'Purchase level regressed')
	used = local_event('cast', 'slot target talentState')
	require(used['target'] == f'0/{identity[0]}/{faction}', 'Cast endpoint is not the controlled hero UNIT')
	state = used['talentState'].split('/')
	require(len(state) == 3, 'Malformed cast state')
	before, cast_step = pair(state[0], '->', lambda value: integer(value, signed=True))
	require(before == -1 and buy_step < cast_step <= replay_step and cast_step >= min_cast_step,
		'Cast was not a new activation inside the recorded session')
	# The authored MiniRefresher passive owns one instance from purchase onward.
	require(pair(state[1], '->') == (1, 2), 'Healer passive/active instance transition mismatch')
	require(pair(state[2], '->', decimal) == (Decimal(0), Decimal(4)), 'Healer cooldown must change 0->4')
	paid = fields(record(stdout, 'Native talent formula payment', ': '), '=', 'slot range cost pool before after step')
	require(paid['slot'] == '0,0' and paid['pool'] == 'mana' and integer(paid['step']) == cast_step, 'Payment slot/pool/step mismatch')
	values = {key: decimal(paid[key]) for key in ('range', 'cost', 'before', 'after')}
	require(values['range'] == 14 and values['cost'] == 80, 'Healer formula range/cost mismatch')
	require(values['before'] >= 0 and values['after'] >= 0 and
		abs(values['before'] - values['after'] - 80) <= Decimal('0.01'), 'Healer did not pay 80 mana')
	arm = fields(record(stdout, 'Ruffle unit target armed', ': '), '=', 'row column')
	require(arm['row'] == arm['column'] == '0', 'Wrong talent armed')
	reject = fields(record(stdout, 'Ruffle unit target: rejected', ' '), '=', 'object position')
	submit = fields(record(stdout, 'Ruffle unit target: submitted', ' '), '=', 'object position')
	require(integer(reject['object'], signed=True) == -1 and integer(submit['object']) == identity[0], 'HUD target identity mismatch')
	point = pair(reject['position'], ',', number)
	require(point != (0, 0) and all(bounds[0][i] <= point[i] < bounds[1][i] for i in (0, 1)),
		'Rejection was not a projected empty-ground click')
	pair(submit['position'], ',', number)
	require(sum(line.lstrip().startswith('Ruffle unit target:') for line in stdout.splitlines()) == 2, 'Unexpected unit target attempts')
	ordered(stdout, ('Ruffle talent command:', 'Native local talent purchase:', 'Ruffle unit target armed:',
		'Ruffle unit target: rejected', 'Ruffle unit target: submitted', 'Native local talent cast:',
		'Native talent formula payment:', finish))
	for name in ('Native BasicFX shutdown', 'Native terrain shutdown'):
		shutdown = fields(record(stdout, name, ': '), '=')
		require(integer(shutdown.get('draws', '')) > 0 and integer(shutdown.get('rejected', '')) == 0, f'{name}: no draws or rejected work')
		if name == 'Native BasicFX shutdown':
			require(integer(shutdown.get('loads', '')) > 0 and integer(shutdown.get('liveBytes', '')) == 0, 'BasicFX shutdown retained resources')
	animation = fields(record(stdout, 'Native unit animation shutdown', ': '), '=')
	require(integer(animation.get('created', '')) > 0 and integer(animation.get('samples', '')) > 0 and
		integer(animation.get('failed', '')) == 0, 'Native animation failed or did not run')
	return {'hero_object_id': identity[0], 'cast_step': cast_step, 'world_step': step,
		'replay_commands': commands, 'clock_ticks': ticks}


def drive(window, wait, deadline):
	'''Send only focused, window-addressed keys/clicks; stop on missing rejection.'''
	def focused():
		require(tool('getwindowfocus', deadline=deadline) == window, 'Game lost focus; refusing input')

	def click(x, y):
		focused()
		tool('mousemove', '--window', window, x, y, deadline=deadline)
		focused()
		tool('click', '--window', window, 1, deadline=deadline)

	focused()
	tool('key', '--window', window, '1', deadline=deadline)
	wait('Ruffle unit target armed:', 1)
	click(350, 250)
	wait('Ruffle unit target: rejected', 1)
	click(640, 296)
	wait('Ruffle unit target: submitted', 1)
	wait('Native local talent cast:', 1)
	wait('Native talent formula payment:', 1)


def run(binary, library, bin_dir, output, seconds=90, control_delay=45, min_cast_step=0):
	'''Launch one opt-in client, preserve evidence and reap on every post-launch path.'''
	check_timing(seconds, control_delay)
	require(type(min_cast_step) is int and min_cast_step >= 0, 'Invalid minimum cast step')
	deadline = time.monotonic() + seconds + 120
	capture = output.with_suffix('.png')
	require(capture != output and not capture.exists(), f'Refusing to replace capture: {capture}')
	with output.open('x', encoding='utf-8') as stream:
		process = subprocess.Popen(command(binary, library, capture, seconds), cwd=bin_dir,
			stdout=stream, stderr=subprocess.STDOUT)
		try:
			wait = lambda needle, count: wait_for(process, output, needle, count, deadline)
			wait_for_window(process, deadline)
			wait('Ruffle talent command: buy row=0 column=0', 1)
			pause(4, deadline)
			pause(control_delay, deadline)
			drive(owned_window(process, deadline), wait, deadline)
			require(process.wait(timeout=max(1, deadline - time.monotonic())) == 0, 'Native client exited unsuccessfully')
		finally:
			if process.poll() is None:
				process.terminate()
				try:
					process.wait(timeout=10)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=10)
	stdout = output.read_text(encoding='utf-8')
	log_dir = Path(record(stdout, 'Logs', ': '))
	if not log_dir.is_absolute():
		log_dir = bin_dir / log_dir
	client_log = log_dir / 'linux-client-shell.log'
	return validate_unit_target(stdout, client_log.read_text(encoding='utf-8'), seconds, min_cast_step), client_log


def main():
	'''Require explicit paths; importing or running unit tests never launches a GUI.'''
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--binary', type=Path, required=True)
	parser.add_argument('--library', type=Path, required=True)
	parser.add_argument('--bin-dir', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--seconds', type=int, default=90)
	parser.add_argument('--control-delay', type=float, default=45)
	parser.add_argument('--min-cast-step', type=int, default=0)
	args = parser.parse_args()
	try:
		result, client_log = run(args.binary.resolve(strict=True), args.library.resolve(strict=True),
			args.bin_dir.resolve(strict=True), args.output.absolute(), args.seconds, args.control_delay, args.min_cast_step)
	except (OSError, ValueError, subprocess.SubprocessError) as error:
		parser.exit(1, f'Native unit targeting FAILED: {error}\n')
	print(f'Native unit targeting PASSED: {result}\nClient log: {client_log}')


if __name__ == '__main__':
	main()
