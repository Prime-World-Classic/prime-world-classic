'''Mock the driver without opening a window or sending host input.'''

from pathlib import Path
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
			probe.drive('42', wait)
			keys = [call.args for call in tool.call_args_list if call.args[0] in ('key', 'keydown', 'keyup')]
			self.assertEqual(keys, [('keydown', '--window', '42', '1')] * 4 +
				[('keyup', '--window', '42', '1'), ('key', '--window', '42', '1'),
				('key', '--window', '42', 'Escape'), ('key', '--window', '42', '1')])
			self.assertEqual(wait.call_args_list[-1].args, ('Ruffle ground target: submitted', 1))
			moves = [call.args for call in tool.call_args_list if call.args[0] == 'mousemove']
			self.assertEqual(moves[-1], moves[-2])
			self.assertTrue(all('--sync' not in args for args in moves))

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
