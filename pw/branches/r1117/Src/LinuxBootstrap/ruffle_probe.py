#!/usr/bin/env python3
"""Capture a bounded native Ruffle compatibility run; this is not a game backend."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


MOVIES = {
	'loading': 'UI/Screens/Loading/Flash/pwl.swf',
	'combat': 'UI/Screens/Combat/Flash/main.swf',
}


def build_command(binary, movie, data, output, width, height, dummy_interface=False):
	"""Use native GL, temporary state, and deny side effects from standalone movie code."""
	command = [binary, str(movie), '--graphics', 'gl', '--width', str(width),
		'--height', str(height), '--base', data.as_uri() + '/', '--storage', 'memory',
		'--config', str(output / 'config'), '--cache-directory', str(output / 'cache'),
		'--volume', '0', '--gamemode', 'off', '--tcp-connections', 'deny',
		'--open-url-mode', 'deny', '--filesystem-access-mode', 'deny',
		'--proxy', 'http://127.0.0.1:9', '--no-gui']
	if dummy_interface:
		command.append('--dummy-external-interface')
	return command


def diagnostics(log):
	"""Retain unique actionable diagnostics, not a misleading boolean playback verdict."""
	return sorted({line.strip() for line in log.splitlines()
		if any(word in line for word in ('ERROR', 'WARN', 'Error #', 'panicked', 'Unknown fscommand'))})


def choose_window(pid_windows, before, current):
	"""Account for PID namespaces without capturing an unrelated pre-existing player."""
	owned = set(pid_windows) - set(before)
	if len(owned) == 1:
		return owned.pop()
	created = set(current) - set(before)
	return created.pop() if len(created) == 1 else None


def search_windows(*arguments):
	"""Include minimized test windows, without interpreting user text as shell commands."""
	result = subprocess.run(['xdotool', 'search', *arguments],
		capture_output=True, text=True, timeout=5)
	return result.stdout.splitlines()


def run_probe(args):
	"""Run only the selected local SWF and terminate only the process we started."""
	binary = shutil.which(args.ruffle)
	if not binary:
		raise RuntimeError('Ruffle executable not found: ' + args.ruffle)
	data = args.data.resolve(strict=True)
	movie = (data / MOVIES[args.movie]).resolve(strict=True)
	if not movie.is_relative_to(data):
		raise ValueError('Movie must remain within the selected Data directory')
	output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='pw-ruffle-'))
	output.mkdir(parents=True, exist_ok=True)
	command = build_command(binary, movie, data, output, args.width, args.height, args.dummy_interface)
	version = subprocess.run([binary, '--version'], capture_output=True, text=True, check=True).stdout.strip()
	environment = dict(os.environ, RUST_LOG='info', RUST_BACKTRACE='1', NO_COLOR='1')
	environment.pop('WAYLAND_DISPLAY', None)
	for key in list(environment):
		if key.lower() in ('http_proxy', 'https_proxy', 'all_proxy', 'no_proxy'):
			environment.pop(key)
	window = None
	captured = False
	terminated = False
	started = time.monotonic()
	before = search_windows('--class', 'ruffle')
	with (output / 'player.log').open('w') as log:
		process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=environment)
		try:
			while process.poll() is None and time.monotonic() - started < args.seconds:
				if not window:
					window = choose_window(search_windows('--pid', str(process.pid)), before,
						search_windows('--class', 'ruffle'))
				if window and not captured and time.monotonic() - started >= args.seconds / 2:
					subprocess.run(['xdotool', 'windowactivate', '--sync', window],
						capture_output=True, timeout=5)
					capture = subprocess.run(['maim', '-i', window, str(output / 'frame.png')],
						capture_output=True, text=True, timeout=10)
					captured = capture.returncode == 0
				time.sleep(0.25)
		finally:
			if process.poll() is None:
				terminated = True
				process.terminate()
				try:
					process.wait(timeout=10)
				except subprocess.TimeoutExpired:
					process.kill()
					process.wait(timeout=5)
	result = {
		'ruffle_version': version, 'movie': str(movie),
		'sha256': hashlib.sha256(movie.read_bytes()).hexdigest(),
		'command': command, 'window_found': bool(window), 'captured': captured,
		'bounded_termination': terminated, 'returncode': process.returncode,
		'elapsed_seconds': round(time.monotonic() - started, 2),
		'diagnostics': diagnostics((output / 'player.log').read_text(errors='replace')),
		'game_integration_verified': False,
	}
	(output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
	print(json.dumps({'output': str(output), **result}, indent=2))
	return 0 if window and captured and terminated else 1


def main():
	"""Standalone desktop probe; intentionally not registered as a headless CTest."""
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('movie', choices=MOVIES)
	parser.add_argument('--data', type=Path, default=Path(__file__).resolve().parents[2] / 'Data')
	parser.add_argument('--ruffle', default='ruffle')
	parser.add_argument('--output', type=Path)
	parser.add_argument('--seconds', type=float, default=45)
	parser.add_argument('--width', type=int, default=1280)
	parser.add_argument('--height', type=int, default=720)
	parser.add_argument('--dummy-interface', action='store_true')
	args = parser.parse_args()
	if not 5 <= args.seconds <= 300 or not 320 <= args.width <= 3840 or not 240 <= args.height <= 2160:
		parser.error('Use 5..300 seconds and a 320x240..3840x2160 viewport')
	return run_probe(args)


if __name__ == '__main__':
	raise SystemExit(main())
