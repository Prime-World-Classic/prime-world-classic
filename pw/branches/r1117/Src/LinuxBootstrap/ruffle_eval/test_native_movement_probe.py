'''Test movement evidence and focus safety with mock records; no GUI or host input.'''

import unittest
from pathlib import Path
import subprocess
import tempfile
from unittest.mock import Mock, patch

import native_movement_probe as probe


def evidence(scenario='obstructed'):
	'''Mirror the two independent actual-client records with a valid fixed point.'''
	x, y = probe.SCENARIOS[scenario][-1]
	count = len(probe.SCENARIOS[scenario])
	return (f'Final map command move: commands={count} tracking=yes moving=no moved=yes reached=yes '
		'target=380.2,175.4 remaining=1.00 maxDistance=12.00 input=live-map-preview-move\n'
		f'Final live 3D map commands: surface=yes proof=no input={count} move={count} projection=yes '
		f'lastHit=command at={x},{y} target=380.2,175.4 world=380.2,175.4 action=live-map-preview-move\n')


class NativeMovementTests(unittest.TestCase):
	'''Do not let rewritten, synthetic, missing or nonfinite evidence pass as real input.'''

	def test_preserved_destination(self):
		for scenario in probe.SCENARIOS:
			self.assertEqual(probe.validate_movement(evidence(scenario), scenario)['remaining'], '1.00')

	def test_rewritten_destination(self):
		with self.assertRaisesRegex(probe.EvidenceError, 'replaced'):
			probe.validate_movement(evidence().replace('target=380.2,175.4', 'target=370.2,175.4'), 'obstructed')

	def test_missing_duplicate_or_bad_projection(self):
		for text in ('', evidence() * 2, evidence().replace('projection=yes', 'projection=no'),
			evidence().replace('world=380.2,175.4', 'world=nan,175.4'),
			evidence().replace('world=380.2,175.4', 'world=380.2,175.4,1')):
			with self.subTest(text=text), self.assertRaises(probe.EvidenceError):
				probe.validate_movement(text, 'obstructed')

	def test_synthetic_or_different_input(self):
		for before, after in (('proof=no', 'proof=yes'), ('input=1', 'input=2'),
			('lastHit=command', 'lastHit=none'), ('at=780,410', 'at=779,410'),
			('live-map-preview-move', 'bootstrap-map-click-proof'), ('tracking=yes', 'tracking=no')):
			with self.subTest(before=before), self.assertRaises(probe.EvidenceError):
				probe.validate_movement(evidence().replace(before, after), 'obstructed')

	def test_reissue_requires_arrival_but_blocked_goal_does_not(self):
		for scenario in probe.SCENARIOS:
			text = evidence(scenario).replace('reached=yes', 'reached=no').replace('remaining=1.00', 'remaining=12.00')
			if scenario == 'reissue':
				with self.assertRaises(probe.EvidenceError):
					probe.validate_movement(text, scenario)
			else:
				probe.validate_movement(text, scenario)

	def test_input_is_window_addressed(self):
		with patch.object(probe, 'tool', return_value='42') as tool, patch.object(probe, 'pause'):
			probe.drive('42', 'reissue', 100)
			self.assertEqual([call.args for call in tool.call_args_list if call.args[0] == 'click'],
				[('click', '--window', '42', 3)] * 2)

	def test_focus_loss_sends_no_mouse_event(self):
		with patch.object(probe, 'tool', return_value='99') as tool:
			with self.assertRaises(probe.EvidenceError):
				probe.drive('42', 'obstructed', 100)
			self.assertEqual(tool.call_count, 1)

	def test_input_failure_reaps_child_even_when_termination_times_out(self):
		for timeout in (False, True):
			process = Mock()
			process.poll.return_value = None
			process.wait.side_effect = [subprocess.TimeoutExpired('client', 10), 0] if timeout else [0]
			with tempfile.TemporaryDirectory() as directory, \
				patch.object(probe.subprocess, 'Popen', return_value=process) as launch, \
				patch.object(probe, 'wait_for_window'), patch.object(probe, 'wait_for'), \
				patch.object(probe, 'pause'), patch.object(probe, 'owned_window', return_value='42'), \
				patch.object(probe, 'drive', side_effect=probe.EvidenceError('lost focus')):
				with self.assertRaisesRegex(probe.EvidenceError, 'lost focus'):
					probe.run(Path('/client'), Path('/bin'), Path(directory)/'output.log', 'obstructed')
				self.assertIn('--bootstrap-interactive-world', launch.call_args.args[0])
				self.assertNotIn('--bootstrap-click-script', launch.call_args.args[0])
				process.terminate.assert_called_once()
				self.assertEqual(process.kill.call_count, int(timeout))
				self.assertEqual(process.wait.call_count, 1 + int(timeout))


if __name__ == '__main__':
	unittest.main()
