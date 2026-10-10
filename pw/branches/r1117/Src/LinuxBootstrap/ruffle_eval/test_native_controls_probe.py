'''Mock the driver without opening a window or sending host input.'''

from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import native_controls_probe as probe


class NativeControlsTests(unittest.TestCase):
	'''Pin action order, key release cleanup, focus isolation and fixed setup boundaries.'''

	def test_command(self):
		args = probe.command(Path('/binary with spaces'), Path('/bridge.so'), Path('/capture.png'))
		self.assertEqual(args[:4], ['stdbuf', '-oL', '-eL', '/binary with spaces'])
		self.assertEqual(args[args.index('--seconds') + 1], '60')
		self.assertEqual(args[args.index('--bootstrap-click-script') + 1], '1119,971;-156,835;1119,971')

	def test_long_run_configuration(self):
		args = probe.command(Path('/client'), Path('/bridge'), Path('/capture'), 90)
		self.assertEqual(args[args.index('--seconds') + 1], '90')
		self.assertEqual(args[args.index('--bootstrap-frame-capture-after') + 1], '75')
		probe.check_timing(90, 50)
		for seconds, delay in ((59, 0), (601, 0), (90.5, 0), (90, -1), (90, 51),
			(90, float('nan')), (90, float('inf'))):
			with self.subTest(seconds=seconds, delay=delay), self.assertRaises(probe.EvidenceError):
				probe.check_timing(seconds, delay)

	def test_alias_keys_are_window_addressed(self):
		with patch.object(probe, 'tool', return_value='42') as tool:
			probe.probe_alias_keys('42')
			keys = [call.args for call in tool.call_args_list if call.args[0] == 'key']
			self.assertEqual([args[3] for args in keys],
				['F4', 'F5', 'F6', 'KP_1', 'KP_3', 'Delete', 'Insert', 'F2', 'F9'])
			self.assertTrue(all(args[1:3] == ('--window', '42') for args in keys))
		with patch.object(probe, 'tool', return_value='99') as tool:
			with self.assertRaises(probe.EvidenceError):
				probe.probe_alias_keys('42')
			self.assertEqual(tool.call_count, 1)

	def test_minimized_window_restored_before_viewport_check(self):
		process = Mock(pid=123)
		with patch.object(probe, 'tool', side_effect=['42', '', 'WIDTH=1280\nHEIGHT=720']) as tool:
			self.assertEqual(probe.owned_window(process, 100), '42')
			self.assertEqual([call.args for call in tool.call_args_list], [
				('search', '--pid', 123), ('windowactivate', '--sync', '42'),
				('getwindowgeometry', '--shell', '42')])

	def test_ambiguous_pid_never_activates_window(self):
		with patch.object(probe, 'tool', return_value='42\n43') as tool:
			with self.assertRaises(probe.EvidenceError):
				probe.owned_window(Mock(pid=123), 100)
			self.assertEqual(tool.call_count, 1)

	def test_sequence(self):
		with patch.object(probe, 'tool', return_value='42') as tool, patch.object(probe.time, 'sleep'):
			wait = Mock()
			events = Mock()
			events.attach_mock(tool, 'tool')
			events.attach_mock(wait, 'wait')
			probe.drive('42', wait)
			keys = [call.args for call in tool.call_args_list if call.args[0] in ('key', 'keydown', 'keyup')]
			self.assertEqual(keys, [('keydown', '--window', '42', '1')] * 4 +
				[('keyup', '--window', '42', '1'), ('key', '--window', '42', '1'),
				('key', '--window', '42', 'Escape'), ('key', '--window', '42', '1')])
			self.assertEqual(wait.call_args_list[-1].args, ('Ruffle ground target: submitted', 1))
			sequence = [(name, args) for name, args, _ in events.mock_calls
				if name == 'wait' or (name == 'tool' and args[0] in ('mousemove', 'click', 'key'))]
			self.assertEqual(sequence, [
				('wait', ('Ruffle ground target armed:', 1)),
				('tool', ('mousemove', '--window', '42', 640, 100)),
				('tool', ('click', '--window', '42', 1)),
				('wait', ('Ruffle ground target: rejected', 1)),
				('tool', ('mousemove', '--window', '42', 640, 360)),
				('tool', ('click', '--window', '42', 1)),
				('wait', ('Ruffle ground target: rejected', 2)),
				('tool', ('mousemove', '--window', '42', 864, 519)),
				('tool', ('click', '--window', '42', 3)),
				('tool', ('key', '--window', '42', '1')),
				('wait', ('Ruffle ground target armed:', 2)),
				('tool', ('key', '--window', '42', 'Escape')),
				('tool', ('key', '--window', '42', '1')),
				('wait', ('Ruffle ground target armed:', 3)),
				('tool', ('mousemove', '--window', '42', 864, 519)),
				('tool', ('click', '--window', '42', 1)),
				('wait', ('Ruffle ground target: submitted', 1))])
			moves = [call.args for call in tool.call_args_list if call.args[0] == 'mousemove']
			self.assertEqual(moves[-1], moves[-2])
			self.assertTrue(all('--sync' not in args for args in moves))

	def test_missing_range_rejection_stops_before_cancel_or_cast(self):
		'''The center click must produce the second rejection while the first arm persists.'''
		def wait(needle, count):
			if (needle, count) == ('Ruffle ground target: rejected', 2):
				raise probe.EvidenceError('second rejection missing')
		with patch.object(probe, 'tool', return_value='42') as tool, patch.object(probe.time, 'sleep'):
			with self.assertRaisesRegex(probe.EvidenceError, 'second rejection missing'):
				probe.drive('42', wait)
			self.assertEqual([call.args for call in tool.call_args_list if call.args[0] == 'click'],
				[('click', '--window', '42', 1)] * 2)
			self.assertFalse(any(call.args[0] == 'key' for call in tool.call_args_list))

	def test_startup_exit_never_drives_window_or_validates_partial_log(self):
		'''An exited process cannot proceed to controls or partial-log validation.'''
		with tempfile.TemporaryDirectory() as directory, \
			patch.object(probe.subprocess, 'Popen') as start, patch.object(probe, 'tool') as tool, \
			patch.object(probe, 'drive') as drive, patch.object(probe, 'validate_interactive') as validate:
			start.return_value.poll.return_value = 1
			with self.assertRaisesRegex(probe.EvidenceError, 'Client exited'):
				probe.run(Path('/mock-client'), Path('/mock-library'), Path(directory), Path(directory) / 'out')
			tool.assert_not_called()
			drive.assert_not_called()
			validate.assert_not_called()

	def test_startup_failure_never_sends_input(self):
		'''Mock launch failures require neither a display nor a cleanup command to another PID.'''
		with tempfile.TemporaryDirectory() as directory, \
			patch.object(probe.subprocess, 'Popen', side_effect=OSError('mock launch failed')), \
			patch.object(probe, 'tool') as tool:
			with self.assertRaisesRegex(OSError, 'mock launch failed'):
				probe.run(Path('/mock-client'), Path('/mock-library'), Path(directory), Path(directory) / 'out')
			tool.assert_not_called()

	def test_focus_loss_never_sends_input(self):
		with patch.object(probe, 'tool', return_value='99') as tool:
			with self.assertRaises(probe.EvidenceError):
				probe.drive('42', Mock())
			self.assertEqual([call.args for call in tool.call_args_list], [('getwindowfocus',)])

	def test_held_key_released_on_interruption(self):
		with patch.object(probe, 'tool', return_value='42') as tool, \
			patch.object(probe.time, 'sleep', side_effect=RuntimeError('interrupted')):
			with self.assertRaises(RuntimeError):
				probe.drive('42', Mock())
			self.assertEqual(tool.call_args_list[-1].args, ('keyup', '--window', '42', '1'))

	def test_matching_evidence_does_not_revive_exited_client(self):
		output = Mock()
		output.read_text.return_value = 'expected\n'
		process = Mock()
		process.poll.return_value = 0
		with patch.object(probe.time, 'monotonic', return_value=1):
			with self.assertRaises(probe.EvidenceError):
				probe.wait_for(process, output, 'expected', 1, 10)

	def test_early_exit(self):
		output = Mock()
		output.read_text.return_value = 'Incomplete\n'
		process = Mock()
		process.poll.return_value = 1
		with patch.object(probe.time, 'monotonic', return_value=1):
			with self.assertRaises(probe.EvidenceError):
				probe.wait_for(process, output, 'missing', 1, 10)

	def test_focus_loss_during_hold_releases_only_game_window(self):
		focus_checks = iter(('42', '42', '99'))
		def result(*args, **kwargs):
			return next(focus_checks) if args[0] == 'getwindowfocus' else ''
		with patch.object(probe, 'tool', side_effect=result) as tool, patch.object(probe.time, 'sleep'):
			with self.assertRaises(probe.EvidenceError):
				probe.drive('42', Mock())
			self.assertEqual(tool.call_args_list[-1].args, ('keyup', '--window', '42', '1'))
			self.assertEqual(sum(call.args[0] == 'keydown' for call in tool.call_args_list), 1)

	def test_expired_deadline_never_sends_or_sleeps(self):
		with patch.object(probe.time, 'monotonic', return_value=10), \
			patch.object(probe.subprocess, 'run') as run, patch.object(probe.time, 'sleep') as sleep:
			with self.assertRaises(probe.EvidenceError):
				probe.tool('key', '--window', '42', '1', deadline=9)
			with self.assertRaises(probe.EvidenceError):
				probe.pause(2, deadline=11)
			run.assert_not_called()
			sleep.assert_not_called()


if __name__ == '__main__':
	unittest.main()
