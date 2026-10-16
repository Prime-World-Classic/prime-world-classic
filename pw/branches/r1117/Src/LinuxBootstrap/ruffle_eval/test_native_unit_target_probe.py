'''Exercise the optional Healer driver and evidence parser without opening a display.'''

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import gameplay_gate as gate
import native_unit_target_probe as probe


PURCHASE = ('Native local talent purchase: hero=7502 player=2 user=1984 activated=1 '
	'slot=0,0 progress=1->2 dev=1->2 gold=400->100\n')
CAST = ('Native local talent cast: hero=7502 player=2 user=1984 slot=0,0 '
	'target=0/7502/2 talentState=-1->680/1->2/0.00->4.00\n')
PAYMENT = 'Native talent formula payment: slot=0,0 range=14 cost=80 pool=mana before=695 after=615 step=680\n'
ARM = 'Ruffle unit target armed: row=0 column=0\n'
REJECT = 'Ruffle unit target: rejected object=-1 position=100.000,100.000\n'
SUBMIT = 'Ruffle unit target: submitted object=7502 position=200.000,200.000\n'


def fixture():
	'''Return synthetic Healer evidence; these strings are not a native test result.'''
	counts = dict.fromkeys(gate.RUFFLE_COUNTERS.split(), 0)
	counts.update(attempted=1, frames=569, receivedCallbacks=8, gameplayRequests=2,
		talentCommands=2, needsTarget=1, targetArmed=1, targetCasts=1, targetRejected=1,
		shortcutCalls=1, hudCalls=70, actionCalls=177, minimapUploads=172,
		pointerEvents=581, consumedPointerEvents=6)
	ruffle = ' '.join(f'{key}:{value}' for key, value in counts.items())
	client = ('finalClientTimingMs=frames:575 input:1 update:2 assets:0 draw:3\n'
		'finalGameTransceiverWorldAttached=yes\nfinalGameTransceiverWorldStep=875\n'
		'finalNativeWorldPresentationFrames=569\n'
		f'finalRuffleInspection=ready {ruffle} minimapTarget:0,0 prime:400->212 error:\n'
		'finalLiveHeroReady=yes\nfinalLiveHeroObjectId=7502\nfinalLiveHeroPlayerId=2\n'
		'finalLiveHeroUserId=1984\nfinalLiveHeroFaction=2\nfinalLiveHeroGold=212\n'
		'finalLiveHeroDead=no\nfinalLiveHeroUnitDbid=Heroes/Healer/_.HROB.xdb\n'
		'finalInteractiveClock=ticks:876 pumps:575 pendingSeconds:0.05 discardedSeconds:2.35\n'
		'finalLocalMapCamera=active:1 follow:1 updates:569 focusHeight:6.68912\n'
		'mapTacticalBounds=0,0 -> 400,400\n')
	stdout = ('Logs: logs/mock\nSelected lineup hero: Healer (healer)\n' +
		''.join(f'Session effects {name} runtime: {value}\n' for name, value in gate.SESSION_EFFECTS.items()) +
		'Ruffle talent command: buy row=0 column=0 prime=400 step=122\n' + PURCHASE + ARM + REJECT + SUBMIT + CAST + PAYMENT +
		'Final game transceiver runtime: ready=yes world=yes step=875 commands=130 replayCommands=130 replayBytes=7409 '
		'heroMoveCommands=0 heroStopCommands=0 heroAttackCommands=0 heroCancelCommands=0 '
		'heroUseUnitCommands=0 heroUseTalentCommands=0\n'
		'Final replay capture validation: valid=yes magic=yes bounds=yes bytes=yes commands=yes statuses=yes '
		'records=49 steps=49 commandBlocks=130/130 statusBlocks=3/3 active=2 away=1 disconnected=0 leaver=0 '
		'bytesRead=7409/7409 largest=71 startStep=0 firstStep=1 lastStep=874 error=none\n'
		'Final replay readback: decoded=yes commands=130/130 failures=0 bytes=4611 distinctTypes=12 '
		'clients=1984..1984 first=0x9E6A1193/1984/211271519 last=0x7B13C109/1984/211363113 countMatch=yes\n'
		'Final replay storage: valid=yes segments=49/49 commands=130/130 statuses=3/3 failures=0 steps=1..874 '
		'segmentMatch=yes commandMatch=yes statusMatch=yes header=yes/10 client=1984 stepLength=100 '
		'map=Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb error=none source=ReplayStorage2\n'
		'Native unit animation shutdown: created=27 unsupported=13 samples=7839 moving=6566 failed=0 casts=3\n'
		'Native BasicFX shutdown: loads=1 draws=188339 rejected=0 liveBytes=0\n'
		'Native terrain shutdown: draws=137129 rejected=0 natureUploads=1 channels=7\n'
		'Prime World Linux client shell finished.\n')
	return stdout, client


class UnitTargetEvidenceTests(unittest.TestCase):
	'''Bind unique input, execution, payment and shutdown records to the same human hero.'''

	def test_complete_healer_evidence(self):
		result = probe.validate_unit_target(*fixture(), 90, 600)
		self.assertEqual((result['hero_object_id'], result['cast_step'], result['clock_ticks']), (7502, 680, 876))
		self.assertEqual((result['world_step'], result['replay_commands']), (875, 130))

	def test_arm_record_does_not_evaluate_target_dependent_range(self):
		'''Only the selected cast/payment receipt carries the evaluated range.'''
		stdout, client = fixture()
		probe.validate_unit_target(stdout, client)
		with self.assertRaises(gate.EvidenceError):
			probe.validate_unit_target(stdout.replace(ARM, ARM.rstrip() + ' range=14.000\n'), client)

	def test_authored_passive_instance_is_not_an_earlier_cast(self):
		'''Healer A1 purchase owns its shipped MiniRefresher trigger before active casting.'''
		stdout, client = fixture()
		probe.validate_unit_target(stdout, client)
		for counts in ('0->0', '0->1', '1->1', '2->3', '-1->2'):
			with self.subTest(counts=counts), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(stdout.replace('/1->2/', '/' + counts + '/'), client)

	def test_sparse_replay_omits_idle_world_ticks(self):
		'''Retain original gate semantics: no missing commands and no future replay step.'''
		stdout, client = fixture()
		probe.validate_unit_target(stdout.replace('world=yes step=875', 'world=yes step=876'),
			client.replace('WorldStep=875', 'WorldStep=876'))
		with self.assertRaises(gate.EvidenceError):
			probe.validate_unit_target(stdout.replace('world=yes step=875', 'world=yes step=873'),
				client.replace('WorldStep=875', 'WorldStep=873'))

	def test_missing_or_duplicate_execution_cannot_pass_on_submission(self):
		stdout, client = fixture()
		for line in (PURCHASE, CAST, PAYMENT, ARM, REJECT, SUBMIT):
			for replacement in ('', line + line, line.replace(': ', ':'), line.rstrip() + ' extra=1\n'):
				with self.subTest(line=line, replacement=replacement), self.assertRaises(gate.EvidenceError):
					probe.validate_unit_target(stdout.replace(line, replacement), client)

	def test_log_only_or_preactivation_evidence_is_not_execution(self):
		stdout, client = fixture()
		for changed in (stdout.replace(CAST, '').replace(PAYMENT, ''),
			stdout.replace(PURCHASE, ''), stdout.replace('activated=1', 'activated=0'),
			stdout.replace('-1->680/', '-1->-1/'), stdout.replace('-1->680/', '679->680/'),
			stdout.replace('0.00->4.00', '0.00->0.00'), stdout.replace('0.00->4.00', '0.00->10.00'),
			stdout.replace('dev=1->2', 'dev=1->1'), stdout.replace('gold=400->100', 'gold=400->400')):
			with self.subTest(changed=changed), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(changed, client)

	def test_exact_actor_endpoint_and_talent(self):
		stdout, client = fixture()
		for line in (PURCHASE, CAST):
			for old, new in (('hero=7502', 'hero=7503'), ('player=2', 'player=3'),
				('user=1984', 'user=1985'), ('slot=0,0', 'slot=0,1')):
				with self.subTest(line=line, old=old), self.assertRaises(gate.EvidenceError):
					probe.validate_unit_target(stdout.replace(line, line.replace(old, new)), client)
		for endpoint in ('2/-1/-1', '1/7502/2', '0/7503/2', '0/7502/1', '0/-1/2', '0/7502/2/0'):
			with self.subTest(endpoint=endpoint), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(stdout.replace('target=0/7502/2', 'target=' + endpoint), client)
		for changed in (stdout.replace(SUBMIT, SUBMIT.replace('object=7502', 'object=7503')),
			stdout.replace(REJECT, REJECT.replace('object=-1', 'object=7503')),
			stdout.replace(REJECT, REJECT.replace('100.000,100.000', '0.000,0.000')),
			stdout.replace(REJECT, REJECT.replace('100.000,100.000', '900.000,900.000')),
			stdout.replace('(healer)', '(plane)')):
			with self.subTest(changed=changed), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(changed, client)

	def test_payment_amount_balances_schema_and_step(self):
		stdout, client = fixture()
		for old, new in (('cost=80', 'cost=70'), ('cost=80', 'cost=80.000000000000001'),
			('range=14', 'range=0'), ('pool=mana', 'pool=life'), ('after=615', 'after=695'),
			('after=615', 'after=615.02'), ('before=695', 'before=-1e-999'),
			('before=695', 'before=nan'), ('after=615', 'after=inf'), ('step=680', 'step=679'),
			('cost=80', 'cost=80 cost=80'), ('slot=0,0', 'slot=0,1')):
			with self.subTest(old=old, new=new), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(stdout.replace(PAYMENT, PAYMENT.replace(old, new)), client)
		for before, after in (('695.25', '615.25'), ('80', '0'), ('695', '615.01'), ('6.95e2', '6.15e2')):
			with self.subTest(before=before, after=after):
				probe.validate_unit_target(stdout.replace('before=695 after=615', f'before={before} after={after}'), client)

	def test_input_and_execution_order(self):
		stdout, client = fixture()
		for first, second in ((PURCHASE, ARM), (ARM, REJECT), (REJECT, SUBMIT), (SUBMIT, CAST), (CAST, PAYMENT)):
			with self.subTest(first=first), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(stdout.replace(first, '').replace(second, second + first), client)

	def test_replay_clock_drain_and_shutdown(self):
		stdout, client = fixture()
		for old, new in (('replayCommands=130', 'replayCommands=129'), ('decoded=yes', 'decoded=no'),
			('countMatch=yes', 'countMatch=no'), ('clients=1984..1984', 'clients=1984..1985'),
			('segmentMatch=yes', 'segmentMatch=no'), ('client=1984', 'client=1985'),
			('failures=0', 'failures=1'), ('heroMoveCommands=0', 'heroMoveCommands=1'),
			('liveBytes=0', 'liveBytes=8'), ('rejected=0', 'rejected=1'), ('failed=0', 'failed=1'),
			('shell finished.', 'shell failed.')):
			self.assertIn(old, stdout)
			with self.subTest(old=old), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(stdout.replace(old, new), client)
		for old, new in (('ticks:876', 'ticks:600'), ('pumps:575', 'pumps:0'),
			('pendingSeconds:0.05', 'pendingSeconds:0.6'), ('discardedSeconds:2.35', 'discardedSeconds:-1'),
			('follow:1', 'follow:0'), ('WorldStep=875', 'WorldStep=900')):
			with self.subTest(old=old), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(stdout, client.replace(old, new))
		for failure in ('Local scheduler drain timed out: world=875 barrier=876\n', 'runtime error: mock\n', 'AddressSanitizer: mock\n'):
			for which in ('stdout', 'client'):
				with self.subTest(failure=failure, which=which), self.assertRaises(gate.EvidenceError):
					probe.validate_unit_target(stdout + failure if which == 'stdout' else stdout,
						client + failure if which == 'client' else client)

	def test_required_record_cardinality_and_complete_text(self):
		stdout, client = fixture()
		for line in stdout.splitlines(keepends=True):
			for replacement in ('', line + line):
				with self.subTest(line=line, replacement=replacement), self.assertRaises(gate.EvidenceError):
					probe.validate_unit_target(stdout.replace(line, replacement), client)
		for line in client.splitlines(keepends=True):
			for replacement in ('', line + line):
				with self.subTest(line=line, replacement=replacement), self.assertRaises(gate.EvidenceError):
					probe.validate_unit_target(stdout, client.replace(line, replacement))
		for changed_stdout, changed_client in ((stdout.rstrip(), client), (stdout, client.rstrip()), (stdout + '\0\n', client)):
			with self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(changed_stdout, changed_client)

	def test_hud_counts_and_live_identity(self):
		stdout, client = fixture()
		for old, new in (('targetArmed:1', 'targetArmed:2'), ('targetCasts:1', 'targetCasts:0'),
			('targetRejected:1', 'targetRejected:0'), ('targetCanceled:0', 'targetCanceled:1'),
			('targetPending:0', 'targetPending:1'), ('shortcutCalls:1', 'shortcutCalls:2'),
			('talentCommands:2', 'talentCommands:3'), ('matchEndCalls:0', 'matchEndCalls:1'),
			('priorGlErrors:0', 'priorGlErrors:1'), ('prime:400->212', 'prime:400->213'),
			('finalLiveHeroReady=yes', 'finalLiveHeroReady=no'), ('finalLiveHeroDead=no', 'finalLiveHeroDead=yes'),
			('Heroes/Healer/_.HROB.xdb', 'Heroes/Plane/_.HROB.xdb')):
			with self.subTest(old=old), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(stdout, client.replace(old, new))

	def test_cast_step_bounds(self):
		stdout, client = fixture()
		with self.assertRaises(gate.EvidenceError):
			probe.validate_unit_target(stdout, client, 90, 681)
		for step in (875, 876, 9999):
			with self.subTest(step=step), self.assertRaises(gate.EvidenceError):
				probe.validate_unit_target(stdout.replace('->680/', f'->{step}/').replace('step=680', f'step={step}'), client)


class UnitTargetDriverTests(unittest.TestCase):
	'''Mock all subprocess/input boundaries; no test may launch the game or xdotool.'''

	def test_original_command_with_only_hero_selection_added(self):
		args = probe.command(Path('/client with spaces'), Path('/bridge'), Path('/capture'), 90)
		self.assertEqual(args, probe.controls_command(Path('/client with spaces'), Path('/bridge'), Path('/capture'), 90) + ['--hero', 'healer'])
		self.assertEqual(args[args.index('--bootstrap-frame-capture-after') + 1], '75')

	def test_timing_is_checked_before_launch(self):
		with patch.object(probe.subprocess, 'Popen') as start:
			for seconds, delay in ((59, 0), (601, 0), (90.5, 0), (90, -1), (90, 51), (90, float('nan'))):
				with self.subTest(seconds=seconds, delay=delay), self.assertRaises(gate.EvidenceError):
					probe.run(Path('/client'), Path('/bridge'), Path('/bin'), Path('/out'), seconds, delay)
			start.assert_not_called()

	def test_owned_input_sequence(self):
		with patch.object(probe, 'tool', return_value='42') as tool:
			wait = Mock()
			probe.drive('42', wait, 100)
			actions = [call.args for call in tool.call_args_list if call.args[0] != 'getwindowfocus']
			self.assertEqual(actions, [('key', '--window', '42', '1'),
				('mousemove', '--window', '42', 350, 250), ('click', '--window', '42', 1),
				('mousemove', '--window', '42', 640, 296), ('click', '--window', '42', 1)])
			self.assertEqual([call.args for call in wait.call_args_list], [
				('Ruffle unit target armed:', 1), ('Ruffle unit target: rejected', 1),
				('Ruffle unit target: submitted', 1), ('Native local talent cast:', 1),
				('Native talent formula payment:', 1)])

	def test_focus_loss_and_missing_rejection_stop_input(self):
		with patch.object(probe, 'tool', return_value='99') as tool:
			with self.assertRaises(gate.EvidenceError):
				probe.drive('42', Mock(), 100)
			self.assertEqual(tool.call_count, 1)
		def wait(needle, count):
			if needle == 'Ruffle unit target: rejected':
				raise gate.EvidenceError('missing rejection')
		with patch.object(probe, 'tool', return_value='42') as tool:
			with self.assertRaisesRegex(gate.EvidenceError, 'missing rejection'):
				probe.drive('42', wait, 100)
			self.assertEqual(sum(call.args[0] == 'click' for call in tool.call_args_list), 1)

	def test_focus_loss_after_motion_never_clicks(self):
		focus = iter(('42', '42', '99'))
		def response(*args, **kwargs):
			return next(focus) if args[0] == 'getwindowfocus' else ''
		with patch.object(probe, 'tool', side_effect=response) as tool:
			with self.assertRaises(gate.EvidenceError):
				probe.drive('42', Mock(), 100)
			self.assertTrue(any(call.args[0] == 'mousemove' for call in tool.call_args_list))
			self.assertFalse(any(call.args[0] == 'click' for call in tool.call_args_list))

	def test_run_uses_exact_log_and_late_controls(self):
		stdout, client = fixture()
		with tempfile.TemporaryDirectory() as directory:
			base = Path(directory)
			log = base / 'logs/mock/linux-client-shell.log'
			log.parent.mkdir(parents=True)
			log.write_text(client, encoding='utf-8')
			process = Mock()
			process.wait.return_value = process.poll.return_value = 0
			def launch(*args, **kwargs):
				kwargs['stdout'].write(stdout)
				return process
			with patch.object(probe.subprocess, 'Popen', side_effect=launch) as start, \
				patch.object(probe, 'wait_for_window', return_value='42') as first_window, \
				patch.object(probe, 'owned_window', return_value='42') as window, \
				patch.object(probe, 'wait_for') as wait, patch.object(probe, 'pause') as pause, \
				patch.object(probe, 'drive') as drive:
				result, actual_log = probe.run(Path('/client'), Path('/bridge'), base, base / 'out.log')
				self.assertEqual(actual_log, log)
				self.assertEqual(result['cast_step'], 680)
				self.assertEqual([call.args[0] for call in pause.call_args_list], [4, 45])
				self.assertEqual(wait.call_args.args[2:4], ('Ruffle talent command: buy row=0 column=0', 1))
				self.assertEqual(drive.call_args.args[0], '42')
				first_window.assert_called_once()
				window.assert_called_once()
				self.assertEqual(start.call_args.kwargs['cwd'], base)
				process.terminate.assert_not_called()

	def test_failure_reaps_owned_process(self):
		with tempfile.TemporaryDirectory() as directory, patch.object(probe.subprocess, 'Popen') as start, \
			patch.object(probe, 'wait_for_window', side_effect=gate.EvidenceError('setup failed')), \
			patch.object(probe, 'drive') as drive, patch.object(probe, 'validate_unit_target') as validate:
			process = start.return_value
			process.poll.return_value = None
			process.wait.side_effect = [subprocess.TimeoutExpired('/client', 10), 0]
			with self.assertRaisesRegex(gate.EvidenceError, 'setup failed'):
				probe.run(Path('/client'), Path('/bridge'), Path(directory), Path(directory) / 'out.log')
			process.terminate.assert_called_once()
			process.kill.assert_called_once()
			self.assertEqual(process.wait.call_count, 2)
			drive.assert_not_called()
			validate.assert_not_called()

	def test_nonzero_exit_never_validates_logs(self):
		with tempfile.TemporaryDirectory() as directory, patch.object(probe.subprocess, 'Popen') as start, \
			patch.object(probe, 'wait_for_window'), patch.object(probe, 'owned_window', return_value='42'), \
			patch.object(probe, 'wait_for'), patch.object(probe, 'pause'), patch.object(probe, 'drive'), \
			patch.object(probe, 'validate_unit_target') as validate:
			start.return_value.wait.return_value = start.return_value.poll.return_value = 1
			with self.assertRaisesRegex(gate.EvidenceError, 'exited unsuccessfully'):
				probe.run(Path('/client'), Path('/bridge'), Path(directory), Path(directory) / 'out.log')
			validate.assert_not_called()
			start.return_value.terminate.assert_not_called()

	def test_existing_outputs_are_never_replaced(self):
		with tempfile.TemporaryDirectory() as directory, patch.object(probe.subprocess, 'Popen') as start:
			output = Path(directory) / 'out.log'
			for path in (output, output.with_suffix('.png')):
				path.write_text('preserve', encoding='utf-8')
				with self.subTest(path=path), self.assertRaises((OSError, gate.EvidenceError)):
					probe.run(Path('/client'), Path('/bridge'), Path(directory), output)
				self.assertEqual(path.read_text(), 'preserve')
				path.unlink()
			with self.assertRaises(gate.EvidenceError):
				probe.run(Path('/client'), Path('/bridge'), Path(directory), output.with_suffix('.png'))
			start.assert_not_called()


if __name__ == '__main__':
	unittest.main()
