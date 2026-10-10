#!/usr/bin/env python3
'''Validate bounded native gameplay evidence offline; never launch the client.'''

import argparse
import math
from pathlib import Path
import re
import sys


class EvidenceError(ValueError):
	'''A missing, malformed, contradictory, or unsuccessful evidence record.'''


def require(condition, message):
	'''Reject failed evidence without relying on removable Python assertions.'''
	if not condition:
		raise EvidenceError(message)


def record(text, name, separator, optional=False):
	'''Find exactly one complete named record; reject malformed duplicate prefixes.'''
	lines = [line.lstrip() for line in text.splitlines() if line.lstrip().startswith(name)]
	if optional and not lines:
		return None
	require(len(lines) == 1, f'{name}: expected one record, found {len(lines)}')
	require(lines[0].startswith(name + separator), f'{name}: malformed record')
	return lines[0][len(name + separator):]


def fields(value, separator, expected=None):
	'''Parse unique whitespace-delimited fields, optionally enforcing an exact schema.'''
	result = {}
	for token in value.split():
		key, found, item = token.partition(separator)
		require(found and key and key not in result, f'duplicate/malformed field: {token}')
		result[key] = item
	if expected is not None:
		require(set(result) == set(expected.split()), f'bad fields: expected {expected}; got {" ".join(result)}')
	return result


def integer(value, signed=False):
	'''Accept canonical decimal counters, not floats, booleans, or nonfinite values.'''
	require(re.fullmatch(r'-?[0-9]+' if signed else r'[0-9]+', value) is not None, f'bad integer: {value}')
	return int(value)


def number(value):
	'''Accept only finite decimal numbers, including exponent-form timing values.'''
	require(re.fullmatch(r'-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?', value) is not None,
		f'bad number: {value}')
	result = float(value)
	require(math.isfinite(result), f'nonfinite number: {value}')
	return result


def pair(value, separator='/', parser=integer):
	'''Parse a complete two-value record, preserving both sides for comparison.'''
	parts = value.split(separator)
	require(len(parts) == 2, f'bad pair: {value}')
	return tuple(parser(part) for part in parts)


RUFFLE_COUNTERS = '''attempted frames discardedCallbacks receivedCallbacks pendingCallbacks
gameplayRequests talentCommands rejectedRequests needsTarget targetArmed targetCasts
targetRejected targetCanceled targetPending shortcutCalls minimapMoves minimapCameras
priorGlErrors hudCalls actionCalls minimapUploads pointerEvents consumedPointerEvents'''
CAPTURE_FIELDS = '''valid magic bounds bytes commands statuses records steps commandBlocks
statusBlocks active away disconnected leaver bytesRead largest startStep firstStep lastStep error'''
STORAGE_FIELDS = '''valid segments commands statuses failures steps segmentMatch commandMatch
statusMatch header client stepLength map error source'''


def replay(stdout):
	'''Cross-check successful capture/storage flags, counts, byte totals and step ranges.'''
	capture = fields(record(stdout, 'Final replay capture validation', ': '), '=', CAPTURE_FIELDS)
	storage = fields(record(stdout, 'Final replay storage', ': '), '=', STORAGE_FIELDS)
	for key in ('valid', 'magic', 'bounds', 'bytes', 'commands', 'statuses'):
		require(capture[key] == 'yes', f'capture {key} failed')
	for key in ('valid', 'segmentMatch', 'commandMatch', 'statusMatch'):
		require(storage[key] == 'yes', f'storage {key} failed')
	require(capture['error'] == storage['error'] == 'none', 'replay error')
	require(storage['source'] == 'ReplayStorage2', 'unexpected replay storage source')
	require(integer(storage['failures']) == 0, 'replay storage failures')
	for key in ('records', 'steps', 'active', 'away', 'disconnected', 'leaver', 'largest', 'startStep', 'firstStep', 'lastStep'):
		capture[key] = integer(capture[key])
	for key in ('commandBlocks', 'statusBlocks', 'bytesRead'):
		capture[key] = pair(capture[key])
		require(capture[key][0] == capture[key][1], f'capture {key} mismatch')
	for key in ('segments', 'commands', 'statuses'):
		storage[key] = pair(storage[key])
		require(storage[key][0] == storage[key][1], f'storage {key} mismatch')
	require(capture['records'] == capture['steps'] == storage['segments'][0] > 0, 'replay segment counts')
	require(capture['commandBlocks'] == storage['commands'], 'cross-record command counts')
	require(capture['statusBlocks'] == storage['statuses'], 'cross-record status counts')
	require(sum(capture[key] for key in ('active', 'away', 'disconnected', 'leaver')) == storage['statuses'][0],
		'replay status category counts')
	require(capture['bytesRead'][0] > 0 and 0 < capture['largest'] <= capture['bytesRead'][0], 'replay byte counts')
	first, last = pair(storage['steps'], '..')
	require((first, last) == (capture['firstStep'], capture['lastStep']) and
		capture['startStep'] <= first <= last and last > 0, 'replay step ranges')
	header = storage['header'].split('/')
	require(len(header) == 2 and header[0] == 'yes' and integer(header[1]) > 0, 'replay header')
	integer(storage['client'], signed=True)
	require(integer(storage['stepLength']) > 0 and storage['map'] not in ('', '<none>', 'none'), 'replay map/step length')
	return storage['commands'][0], capture['bytesRead'][0], last


def hero_execution(stdout):
	'''Require ground identity, a new last-use step and cooldown, not submission messages.'''
	line = record(stdout, 'Final hero gameplay command execution', ': ')
	names = ('useUnit', 'activate', 'useTalent', 'portal', 'consumable', 'buy', 'raise', 'init', 'pickup')
	parts = re.split(r'(?<!\S)(' + '|'.join(names) + r')=', line)
	require(parts[0] == '' and tuple(parts[1::2]) == names, 'hero execution sections missing/duplicated')
	sections = {}
	for name, body in zip(parts[1::2], parts[2::2]):
		count, space, rest = body.partition(' ')
		require(space and re.fullmatch(r'[0-9]+/[0-9]+/[0-9]+/[0-9]+', count), f'{name}: malformed counts')
		sections[name] = (count, fields(rest, '='))
	count, bought = sections['activate']
	require(set(bought) == {'can', 'slot', 'progress', 'dev', 'gold'}, 'purchase fields')
	require(count == '2/2/1/1' and bought['can'] == '1' and pair(bought['gold'], '->') == (400, 100), 'purchase execution')
	require(all(0 <= value < 6 for value in pair(bought['slot'], ',')), 'purchase slot')
	for key in ('progress', 'dev'):
		pair(bought[key], '->')
	count, used = sections['useTalent']
	require(set(used) == {'can', 'target', 'talentState'}, 'talent execution fields')
	require(count == '2/2/1/1' and used['can'] == '1', 'useTalent execution counts')
	target = used['target'].split('/')
	require(target == ['2', '-1', '-1'], 'talent target must be a ground position without unit identity')
	state = used['talentState'].split('/')
	require(len(state) == 3, 'talentState fields')
	before, after = pair(state[0], '->', lambda item: integer(item, signed=True))
	require(before == -1 and after >= 0, 'talent last-use step must change from -1 to a nonnegative step')
	pair(state[1], '->', lambda item: integer(item, signed=True))
	require(pair(state[2], '->', number) == (0, 10), 'actual talent cooldown must change 0->10')


def validate(stdout, client_log, mode):
	'''Validate complete text evidence; raise EvidenceError on the first failed invariant.'''
	require(mode in ('targeting', 'default'), 'unknown mode')
	if mode == 'default':
		require('Ruffle' not in stdout, 'default stdout contains Ruffle activity')
	for name, text in (('stdout', stdout), ('client log', client_log)):
		require(text.endswith('\n') and '\0' not in text, f'{name}: truncated or invalid text')
	finish = 'Prime World Linux client shell finished.'
	require(record(stdout, finish, '') == '', 'malformed completion record')
	timing = fields(record(client_log, 'finalClientTimingMs', '='), ':', 'frames input update assets draw')
	require(integer(timing['frames']) > 0, 'no profiled frames')
	for key in ('input', 'update', 'assets', 'draw'):
		require(number(timing[key]) >= 0, f'negative {key} timing')
	require(number(timing['update']) > 0 and number(timing['draw']) > 0, 'no update/draw progress')
	require(record(client_log, 'finalGameTransceiverWorldAttached', '=') == 'yes', 'world not attached')
	step = integer(record(client_log, 'finalGameTransceiverWorldStep', '='))
	require(step > 0 and integer(record(client_log, 'finalNativeWorldPresentationFrames', '=')) > 0, 'no world progress')
	commands, byte_count, replay_step = replay(stdout)
	world = fields(record(stdout, 'Final game transceiver runtime', ': '), '=')
	require(world.get('ready') == world.get('world') == 'yes', 'transceiver not ready')
	require(integer(world.get('step', '')) == step and replay_step <= step, 'world/replay steps disagree')
	require(integer(world.get('commands', '')) == integer(world.get('replayCommands', '')) == commands,
		'world/replay command counts disagree')
	require(integer(world.get('replayBytes', '')) == byte_count, 'world/replay byte counts disagree')
	raw = record(client_log, 'finalRuffleInspection', '=', optional=mode == 'default')
	if raw is not None:
		status, space, rest = raw.partition(' ')
		require(space and status in ('ready', 'inactive'), 'Ruffle status malformed')
		ruffle = fields(rest, ':', RUFFLE_COUNTERS + ' minimapTarget prime error')
		for key in RUFFLE_COUNTERS.split():
			ruffle[key] = integer(ruffle[key])
		pair(ruffle['minimapTarget'], ',', number)
		prime = pair(ruffle['prime'], '->', lambda item: integer(item, signed=True))
		require(ruffle['error'] == '', 'Ruffle reported an error')
		if mode == 'default':
			require(status == 'inactive' and all(ruffle[key] == 0 for key in RUFFLE_COUNTERS.split()),
				'default run activated Ruffle')
		else:
			require(status == 'ready' and ruffle['attempted'] == 1 and ruffle['frames'] > 0, 'Ruffle not ready')
			require(prime == (400, 100), 'Ruffle purchase prime mismatch')
			expected = {'priorGlErrors': 0, 'pendingCallbacks': 0, 'targetPending': 0, 'targetArmed': 3,
				'targetCanceled': 2, 'targetCasts': 1, 'targetRejected': 1, 'shortcutCalls': 3,
				'talentCommands': 2, 'minimapMoves': 0}
			for key, value in expected.items():
				require(ruffle[key] == value, f'Ruffle {key}: expected {value}, got {ruffle[key]}')
	if mode == 'targeting':
		hero_execution(stdout)
	return {'mode': mode, 'world_step': step, 'replay_commands': commands}


def main(argv=None):
	'''Read supplied files only; emit a short verdict and conventional exit status.'''
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--stdout', required=True, type=Path)
	parser.add_argument('--client-log', required=True, type=Path)
	parser.add_argument('--mode', required=True, choices=('targeting', 'default'))
	args = parser.parse_args(argv)
	try:
		result = validate(args.stdout.read_text(encoding='utf-8'), args.client_log.read_text(encoding='utf-8'), args.mode)
	except (OSError, UnicodeError, ValueError) as error:
		print(f'Gameplay gate FAILED: {error}', file=sys.stderr)
		return 1
	print(f'Gameplay gate PASSED: {result}')
	return 0


if __name__ == '__main__':
	sys.exit(main())
