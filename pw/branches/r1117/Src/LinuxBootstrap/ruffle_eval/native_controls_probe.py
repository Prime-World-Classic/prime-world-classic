#!/usr/bin/env python3
'''Run a bounded opt-in gameplay gate with window-addressed X11 events.'''

import argparse
import math
from pathlib import Path
import subprocess
import time

from gameplay_gate import EvidenceError, validate_interactive


def check_timing(seconds, control_delay):
	'''Leave enough time for setup, native controls and a settled replay tail.'''
	if not isinstance(seconds, int) or not 60 <= seconds <= 600 or not math.isfinite(control_delay) or not 0 <= control_delay <= seconds - 40:
		raise EvidenceError('Require 60..600 seconds and a finite control delay in 0..seconds-40')


def command(binary, library, capture, seconds=60):
	'''Keep purchase setup deterministic; gameplay controls use X11, not the script.'''
	check_timing(seconds, 0)
	return ['stdbuf', '-oL', '-eL', str(binary), '--seconds', str(seconds), '--width', '1280',
		'--height', '720', '--bootstrap-create-game', '--bootstrap-interactive-world',
		'--bootstrap-ruffle-library', str(library), '--bootstrap-click-after', '12',
		'--bootstrap-click-interval', '3', '--bootstrap-click-script',
		'1119,971;-156,835;1119,971', '--bootstrap-frame-capture', str(capture),
		'--bootstrap-frame-capture-after', str(seconds - 15)]


def tool(*args, deadline=None):
	'''Invoke xdotool without a shell and with a bounded response time.'''
	timeout = 10 if deadline is None else min(10, deadline - time.monotonic())
	if timeout <= 0:
		raise EvidenceError('Input deadline exceeded')
	return subprocess.run(['xdotool', *map(str, args)], check=True, capture_output=True,
		text=True, timeout=timeout).stdout.strip()


def pause(seconds, deadline):
	'''Do not begin a timed input interval beyond the overall test deadline.'''
	if deadline is not None and time.monotonic() + seconds >= deadline:
		raise EvidenceError('Input deadline exceeded')
	time.sleep(seconds)


def wait_for(process, output, needle, count, deadline):
	'''Wait for flushed real-client evidence; never continue input after an early exit.'''
	while time.monotonic() < deadline:
		if process.poll() is not None:
			raise EvidenceError(f'Client exited before {needle!r}')
		text = output.read_text(encoding='utf-8', errors='replace')
		if text.count(needle) >= count:
			return text
		time.sleep(0.1)
	raise EvidenceError(f'Timed out waiting for {needle!r}')


def drive(window, wait, deadline=None):
	'''Repeat a held Down, reject a click, cancel with right/Escape, then cast once.

	All key/button edges explicitly address the launched window, including cleanup.
	This tests the real X11 message path but does not simulate server repeat pairs;
	those are covered by the engine-free key probe. No global key hold is installed.
	'''
	def focused():
		if tool('getwindowfocus', deadline=deadline) != window:
			raise EvidenceError('Game lost focus; refusing to send keys to another application')

	def click(x, y, button):
		focused()
		# --sync waits for motion even when the pointer is already at the target.
		tool('mousemove', '--window', window, x, y, deadline=deadline)
		tool('click', '--window', window, button, deadline=deadline)

	def key(value):
		focused()
		tool('key', '--window', window, value, deadline=deadline)

	focused()
	try:
		for _ in range(4):
			focused()
			tool('keydown', '--window', window, '1', deadline=deadline)
			pause(0.5, deadline)
	finally:
		tool('keyup', '--window', window, '1')
	wait('Ruffle ground target armed:', 1)
	click(640, 100, 1)
	wait('Ruffle ground target: rejected', 1)
	click(864, 519, 3)
	pause(1.5, deadline)
	key('1')
	wait('Ruffle ground target armed:', 2)
	key('Escape')
	pause(1.5, deadline)
	key('1')
	wait('Ruffle ground target armed:', 3)
	click(864, 519, 1)
	wait('Ruffle ground target: submitted', 1)


def owned_window(process, deadline):
	'''Restore this PID's window through the WM; a minimized parent is not directly mappable.'''
	windows = tool('search', '--pid', process.pid, deadline=deadline).splitlines()
	if len(windows) != 1:
		raise EvidenceError(f'Expected one client window, found {windows}')
	window = windows[0]
	tool('windowactivate', '--sync', window, deadline=deadline)
	geometry = dict(line.split('=', 1) for line in tool('getwindowgeometry', '--shell', window, deadline=deadline).splitlines())
	if geometry.get('WIDTH') != '1280' or geometry.get('HEIGHT') != '720':
		raise EvidenceError('Window manager changed the test viewport')
	return window


def probe_alias_keys(window, deadline=None):
	'''Former VK/XK collisions must neither submit gameplay actions nor move the camera.'''
	for key in ('F4', 'F5', 'F6', 'KP_1', 'KP_3', 'Delete', 'Insert', 'F2', 'F9'):
		if tool('getwindowfocus', deadline=deadline) != window:
			raise EvidenceError('Game lost focus during special-key probe')
		tool('key', '--window', window, key, deadline=deadline)


def run(binary, library, bin_dir, output, seconds=60, control_delay=0, min_cast_step=0):
	'''Launch one window, retain logs/capture, validate execution, and reap on every path.'''
	check_timing(seconds, control_delay)
	if not isinstance(min_cast_step, int) or min_cast_step < 0:
		raise EvidenceError('Minimum cast step must be a nonnegative integer')
	deadline = time.monotonic() + seconds + 120
	capture = output.with_suffix('.png')
	if capture.exists():
		raise EvidenceError(f'Refusing to replace capture: {capture}')
	with output.open('x', encoding='utf-8') as stream:
		process = subprocess.Popen(command(binary, library, capture, seconds), cwd=bin_dir,
			stdout=stream, stderr=subprocess.STDOUT)
		try:
			wait = lambda needle, count: wait_for(process, output, needle, count, deadline)
			wait('Ruffle talent command: buy row=0 column=0', 1)
			pause(4, deadline)  # The final setup click closes the authored talent window.
			pause(control_delay, deadline)
			window = owned_window(process, deadline)
			probe_alias_keys(window, deadline)
			drive(window, wait, deadline)
			status = process.wait(timeout=max(1, deadline - time.monotonic()))
			if status != 0:
				raise EvidenceError(f'Client exit status: {status}')
		finally:
			if process.poll() is None:
				process.terminate()
				try:
					process.wait(timeout=10)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=10)
	text = output.read_text(encoding='utf-8')
	paths = [line[6:] for line in text.splitlines() if line.startswith('Logs: ')]
	if len(paths) != 1:
		raise EvidenceError('Missing or ambiguous client log directory')
	client_log = Path(paths[0]) / 'linux-client-shell.log'
	result = validate_interactive(text, client_log.read_text(encoding='utf-8'), seconds, min_cast_step)
	return result, client_log


def main():
	'''Require explicit binaries and output; this is an opt-in display test, not a CTest.'''
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('--binary', type=Path, required=True)
	parser.add_argument('--library', type=Path, required=True)
	parser.add_argument('--bin-dir', type=Path, required=True)
	parser.add_argument('--output', type=Path, required=True)
	parser.add_argument('--seconds', type=int, default=60)
	parser.add_argument('--control-delay', type=float, default=0)
	parser.add_argument('--min-cast-step', type=int, default=0)
	args = parser.parse_args()
	try:
		result, client_log = run(args.binary.resolve(strict=True), args.library.resolve(strict=True),
			args.bin_dir.resolve(strict=True), args.output.absolute(), args.seconds, args.control_delay, args.min_cast_step)
	except (OSError, ValueError, subprocess.SubprocessError) as error:
		parser.exit(1, f'Native controls FAILED: {error}\n')
	print(f'Native controls PASSED: {result}\nClient log: {client_log}')


if __name__ == '__main__':
	main()
