'''Mock menu evidence and input ownership; the real display gate remains opt-in.'''

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import native_menu_probe as probe
from test_gameplay_gate import fixture


def evidence():
	'''Extend the ordinary-session fixture with a manual ten-player lobby and nine bots.'''
	stdout, client = fixture('default')
	# Nine AI commands are separate from the single startup keepalive.
	stdout = stdout.replace('commands=1 ', 'commands=10 ').replace('replayCommands=1 ', 'replayCommands=10 ')
	stdout = stdout.replace('commandBlocks=1/1', 'commandBlocks=10/10').replace('commands=1/1', 'commands=10/10')
	client += '''bootstrapCreateGame=no
bootstrapLegacyLobbyOverlay=no
bootstrapLegacyHeroOverlay=no
bootstrapLegacySessionOverlay=no
finalBootstrapInputScript=0/0/0.00
finalGameReplayInputPath=inactive
finalProductionLobbyCreatedGame=Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb/10
finalProductionLobbyCallbacks=create:1 createMulti:1 createSingle:0 join:0 spectate:0 reconnect:0 lastAction:create-multiplayer
finalProductionHeroCallbacks=ready:1 lastAction:ready
finalProductionLobbyTransition=1/create-multiplayer hero:yes active:no
finalProductionHeroLoadingTransition=1/ready loading:yes
finalStartingLobbyPresentation=production-xdb-lua productionFrames:10 legacyFrames:0
finalHeroLobbyPresentation=production-xdb-lua productionFrames:10 legacyFrames:0
finalGameWorldPlayers=10
finalGameWorldPresentPlayers=1
finalGameWorldPlayersWithHeroes=10
finalLinuxAIControllers=auto:9/9 add:9/9 bots:1/1 active:9 steps:1000 commands:9/9 fallback:0 combat:9
'''
	return stdout, client


class NativeMenuTests(unittest.TestCase):
	'''Reject automatic startup, silent bots, direct mutations and unsafe window input.'''

	def test_manual_session(self):
		result = probe.validate_menu(*evidence())
		self.assertEqual((result['bots'], result['ai_commands'], result['replay_commands']), (9, 9, 10))

	def test_missing_and_duplicate_records(self):
		stdout, client = evidence()
		for line in client.splitlines():
			if line.startswith(('bootstrapCreate', 'bootstrapLegacy', 'finalProduction', 'finalStarting',
				'finalHeroLobby', 'finalLinuxAI', 'finalGameWorldPlayers', 'finalGameWorldPresent', 'finalBootstrap', 'finalGameReplayInput')):
				for text in (client.replace(line + '\n', ''), client + line + '\n'):
					with self.subTest(line=line), self.assertRaises(probe.EvidenceError):
						probe.validate_menu(stdout, text)

	def test_bad_menu_or_ai_evidence(self):
		stdout, client = evidence()
		for before, after in (
			('bootstrapCreateGame=no', 'bootstrapCreateGame=yes'),
			('bootstrapLegacyLobbyOverlay=no', 'bootstrapLegacyLobbyOverlay=yes'),
			('finalBootstrapInputScript=0/0/0.00', 'finalBootstrapInputScript=2/0/0.00'),
			('finalGameReplayInputPath=inactive', 'finalGameReplayInputPath=replay.pwrp'),
			('_.ADMPDSCR.xdb/10', '_.ADMPDSCR.xdb/2'), ('create:1', 'create:0'),
			('createMulti:1', 'createMulti:2'), ('join:0', 'join:1'), ('ready:1', 'ready:0'),
			('1/ready loading:yes', '0/none loading:yes'), ('productionFrames:10', 'productionFrames:0'),
			('legacyFrames:0', 'legacyFrames:1'), ('finalGameWorldPlayersWithHeroes=10', 'finalGameWorldPlayersWithHeroes=9'),
			('auto:9/9', 'auto:0/0'), ('add:9/9', 'add:9/8'), ('bots:1/1', 'bots:1/0'),
			('active:9', 'active:0'), ('steps:1000', 'steps:0'), ('commands:9/9', 'commands:9/8'),
			('commands:9/9', 'commands:10/10'), ('commands:9/9', 'commands:nan/nan'),
			('fallback:0', 'fallback:9'), ('combat:9', 'combat:0'),
			('bootstrapDiagnosticActions=0', 'bootstrapDiagnosticActions=1')):
			with self.subTest(before=before), self.assertRaises(probe.EvidenceError):
				probe.validate_menu(stdout, client.replace(before, after))

	def test_launch_has_no_automation(self):
		args = probe.command(Path('/client'), Path('/frame.png'), 120)
		flags = [arg for arg in args if arg.startswith('--bootstrap-')]
		self.assertEqual(flags, ['--bootstrap-frame-capture', '--bootstrap-frame-capture-after'])
		for seconds in (0, 119, 601, True, float('nan')):
			with self.subTest(seconds=seconds), self.assertRaises(probe.EvidenceError):
				probe.command(Path('/client'), Path('/frame.png'), seconds)

	def test_clicks_are_window_addressed(self):
		with patch.object(probe, 'tool', return_value='42') as tool, patch.object(probe, 'pause'):
			probe.drive('42', 100)
			self.assertEqual([call.args for call in tool.call_args_list if call.args[0] == 'click'],
				[('click', '--window', '42', 1)] * 3)
			self.assertEqual([call.args for call in tool.call_args_list if call.args[0] == 'mousemove'],
				[('mousemove', '--window', '42', 425, 360), ('mousemove', '--window', '42', 551, 618),
					('mousemove', '--window', '42', 640, 477)])

	def test_focus_loss_sends_no_input(self):
		with patch.object(probe, 'tool', return_value='99') as tool:
			with self.assertRaises(probe.EvidenceError):
				probe.drive('42', 100)
			self.assertEqual(tool.call_count, 1)

	def test_failure_reaps_child(self):
		for timeout in (False, True):
			process = Mock()
			process.poll.return_value = None
			process.wait.side_effect = [subprocess.TimeoutExpired('client', 10), 0] if timeout else [0]
			with tempfile.TemporaryDirectory() as directory, \
				patch.object(probe.subprocess, 'Popen', return_value=process), \
				patch.object(probe, 'wait_for_window', side_effect=probe.EvidenceError('no window')):
				with self.assertRaisesRegex(probe.EvidenceError, 'no window'):
					probe.run(Path('/client'), Path('/bin'), Path(directory) / 'output.log')
				process.terminate.assert_called_once()
				self.assertEqual(process.kill.call_count, int(timeout))
				self.assertEqual(process.wait.call_count, 1 + int(timeout))

	def test_driver_uses_the_real_evidence_gate(self):
		with tempfile.TemporaryDirectory() as directory:
			base = Path(directory)
			stdout, client = evidence()
			(base / 'linux-client-shell.log').write_text(client, encoding='utf-8')
			process = Mock()
			process.poll.return_value = process.wait.return_value = 0
			def launch(*args, **kwargs):
				kwargs['stdout'].write(stdout + f'Logs: {base}\n')
				return process
			with patch.object(probe.subprocess, 'Popen', side_effect=launch), \
				patch.object(probe, 'wait_for_window'), patch.object(probe, 'wait_for'), \
				patch.object(probe, 'pause'), patch.object(probe, 'owned_window', return_value='42'), \
				patch.object(probe, 'drive'):
				result, _ = probe.run(Path('/client'), base, base / 'out.log')
				self.assertEqual(result['ai_commands'], 9)
				process.terminate.assert_not_called()


if __name__ == '__main__':
	unittest.main()
