'''Mock integration evidence, including successful submission without execution.'''

import contextlib
import io
from pathlib import Path
import re
import tempfile
import unittest

import gameplay_gate as gate


SESSION_EFFECTS = '''Session effects ability-applicator runtime: db=yes units=yes passive=yes/yes/1/1/1 removed=yes/0/0 active=yes/yes/yes/1/1/1 removed=yes/0/0
Session effects unit-combat runtime: db=yes units=yes slot=yes/yes/yes/1/1 removed=yes/0/0 external=yes/yes/1/1 removed=yes/0/0 baseattack=yes/yes/yes/yes/1/1 removed=yes/0/0
Session effects unit-instant runtime: formula=yes/0.25 db=yes units=yes damage=yes/yes/1.00->0.75 heal=yes/yes/0.50->0.75 energy=yes/yes/0.50->0.75 kill=yes/yes nafta=yes/yes/0->3 baseattack=yes/yes/yes/yes/1.00->0.80
Session effects unit-chain runtime: db=yes units=yes spell=yes/yes/1.00->0.85 proxy=yes/yes/yes/0.12/1.00->0.88 dispell=yes/yes/yes/1->0 refresh=yes/yes/4.00->0.00 abilityend=yes/yes periodic=yes/yes/1.00->0.94 probability=yes/yes/1.00->0.93
'''

REGRESSED_SESSION_EFFECTS = '''Session effects ability-applicator runtime: db=yes units=yes passive=yes/yes/1/1/1 removed=yes/0/0 active=yes/no/no/0/0/0 removed=yes/0/0
Session effects unit-combat runtime: db=yes units=yes slot=no/no/no/0/0 removed=no/0/0 external=no/no/0/0 removed=yes/0/0 baseattack=yes/yes/no/no/0/0 removed=yes/0/0
Session effects unit-instant runtime: formula=yes/0.25 db=yes units=yes damage=no/no/1.00->1.00 heal=no/no/0.50->0.50 energy=no/no/0.50->0.50 kill=no/no nafta=no/no/0->0 baseattack=yes/yes/no/no/1.00->1.00
Session effects unit-chain runtime: db=yes units=yes spell=no/no/1.00->1.00 proxy=yes/yes/yes/0.12/1.00->0.88 dispell=no/no/no/0->0 refresh=yes/yes/4.00->0.00 abilityend=no/no periodic=no/no/1.00->1.00 probability=no/no/1.00->1.00
'''

PAYMENT = 'Native talent formula payment: slot=0,0 range=14 cost=70 pool=mana before=725 after=655 step=496\n'
LOCAL_PURCHASE = ('Native local talent purchase: hero=7502 player=2 user=1984 activated=1 '
	'slot=0,0 progress=1->2 dev=1->2 gold=400->100\n')
LOCAL_CAST = ('Native local talent cast: hero=7502 player=2 user=1984 slot=0,0 '
	'target=2/-1/-1 talentState=-1->496/0->0/0.00->10.00\n')


def startup_fixture(session_mode='ordinary', player_commands=0):
	'''Model the policy counters, not production execution or native evidence.'''
	diagnostics = session_mode != 'ordinary'
	interactive = session_mode != 'diagnostics'
	sent = 1 + player_commands + 2 * diagnostics + (12 if not interactive else 0)
	return (f'bootstrapDiagnosticActions={int(diagnostics)}\n'
		f'bootstrapInteractiveWorld={int(interactive)}\n'
		f'finalGameWorldBootstrapRuntimeCommands={int(diagnostics)}\n'
		f'finalGameWorldTimeScale={"1.05" if diagnostics else "1"}\n'
		f'finalGameTransceiverTimescaleCommandsSent={int(diagnostics)}\n'
		f'finalGameTransceiverRuntimeCommandsSent={sent}\n'
		f'finalGameTransceiverProductionCommandsSent={sent - diagnostics}\n'
		f'finalGameTransceiverStatusScript=queued:{5 if diagnostics else 0} applied:{5 if diagnostics else 0} completed:{"yes" if diagnostics else "no"}\n'
		'finalLinuxBotCommandDriverActive=no\nfinalLinuxBotMoveCommandsSent=0\nfinalLinuxBotAttackCommandsSent=0\n'
		'finalLiveHudActivateTalentProof=no\nfinalLiveHotkeyCommandProof=no\n'
		'finalLiveMapPreviewCommandProof=no\nfinalLiveMinimapCommandProof=no\n')


def fixture(mode='targeting', session_mode='ordinary', expected_moves=0):
	'''Generate a complete shape-matched mock; never claim this is native evidence.'''
	counts = dict.fromkeys(gate.RUFFLE_COUNTERS.split(), 0)
	if mode == 'targeting':
		counts.update(attempted=1, frames=20, talentCommands=2, targetArmed=3,
			targetCanceled=2, targetCasts=1, targetRejected=2, shortcutCalls=3)
	ruffle = ' '.join(f'{key}:{value}' for key, value in counts.items())
	status = 'ready' if mode == 'targeting' else 'inactive'
	client = ('  finalClientTimingMs=frames:21 input:1 update:2 assets:0 draw:3\n'
		'  finalGameTransceiverWorldAttached=yes\n  finalGameTransceiverWorldStep=1000\n'
		'  finalNativeWorldPresentationFrames=20\n'
		f'  finalRuffleInspection={status} {ruffle} minimapTarget:0,0 prime:400->100 error:\n')
	startup = startup_fixture(session_mode, (2 if mode == 'targeting' else 0) + expected_moves)
	policy = dict(line.split('=', 1) for line in startup.splitlines())
	commands = int(policy['finalGameTransceiverRuntimeCommandsSent'])
	production = policy['finalGameTransceiverProductionCommandsSent']
	client += startup
	stdout = (f'Final game transceiver runtime: ready=yes world=yes step=1000 commands={commands} '
		f'replayCommands={commands} replayBytes=50 runtimeSent={commands} productionSent={production}\n'
		'Final replay capture validation: valid=yes magic=yes bounds=yes bytes=yes commands=yes statuses=yes '
		f'records=2 steps=2 commandBlocks={commands}/{commands} statusBlocks=1/1 active=1 away=0 disconnected=0 leaver=0 '
		'bytesRead=50/50 largest=30 startStep=0 firstStep=1 lastStep=90 error=none\n'
		f'Final replay storage: valid=yes segments=2/2 commands={commands}/{commands} statuses=1/1 failures=0 steps=1..90 '
		'segmentMatch=yes commandMatch=yes statusMatch=yes header=yes/10 client=1984 stepLength=100 '
		'map=Maps/Map.xdb error=none source=ReplayStorage2\n'
		'Final hero gameplay command execution: useUnit=0/0/0/0 can=0 '
		'activate=2/2/1/1 can=1 slot=0,0 progress=1->2 dev=1->2 gold=400->100 '
		'useTalent=2/2/1/1 can=1 target=2/-1/-1 talentState=-1->496/0->0/0.00->10.00 '
		'portal=0/0/0/0 can=0 consumable=0/0/0/0 can=0 buy=0/0/0/0 can=0 '
		'raise=0/0/0/0 can=0 init=0/0/0/0 can=0 pickup=0/0/0/0 can=0 object=0\n'
		'Prime World Linux client shell finished.\n')
	if mode == 'targeting':
		client += 'finalLiveHeroObjectId=7502\nfinalLiveHeroPlayerId=2\nfinalLiveHeroUserId=1984\nfinalLiveHeroGold=100\n'
	stdout = SESSION_EFFECTS + ('Selected lineup hero: Plane (plane)\n' + PAYMENT + LOCAL_PURCHASE + LOCAL_CAST if mode == 'targeting' else '') + stdout
	return stdout, client


class GameplayGateTests(unittest.TestCase):
	'''Reject malformed counters, false success flags, omissions and duplicate evidence.'''

	def test_startup_policy_is_explicit_and_independent_of_hud(self):
		'''A diagnostic run cannot satisfy ordinary acceptance, even with identical HUD receipts.'''
		for mode in ('default', 'targeting'):
			for actual in ('ordinary', 'diagnostics', 'diagnostics-interactive'):
				stdout, client = fixture(mode, actual)
				for expected in ('ordinary', 'diagnostics', 'diagnostics-interactive'):
					with self.subTest(mode=mode, actual=actual, expected=expected):
						if actual == expected:
							self.assertEqual(gate.validate(stdout, client, mode, session_mode=expected)['session_mode'], expected)
						else:
							with self.assertRaises(gate.EvidenceError):
								gate.validate(stdout, client, mode, session_mode=expected)
		for actual in ('diagnostics', 'diagnostics-interactive'):
			with self.assertRaises(gate.EvidenceError):
				gate.validate(*fixture('default', actual), 'default')

	def test_startup_policy_records_fail_closed(self):
		'''Require all policy records and exact finite numeric values in every policy.'''
		for mode in ('ordinary', 'diagnostics', 'diagnostics-interactive'):
			stdout, client = fixture('default', mode)
			for line in startup_fixture(mode).splitlines(True):
				key, value = line.rstrip().split('=')
				for replacement in ('', line + line, f'{key}=nan\n', f'{key}=-1\n',
					f'{key}=true\n', f'{key}={value} extra=0\n'):
					with self.subTest(mode=mode, line=line, replacement=replacement), self.assertRaises(gate.EvidenceError):
						gate.validate(stdout, client.replace(line, replacement), 'default', session_mode=mode)
		stdout, client = fixture()
		for key, bad in (('bootstrapDiagnosticActions', '1'), ('bootstrapInteractiveWorld', '0'),
			('bootstrapInteractiveWorld', '01'), ('finalGameWorldBootstrapRuntimeCommands', '1'),
			('finalGameTransceiverTimescaleCommandsSent', '1'), ('finalGameWorldTimeScale', '1.05'),
			('finalGameWorldTimeScale', '1.000000000000000001')):
			changed = re.sub(r'(?m)^' + key + r'=.*$', key + '=' + bad, client)
			with self.subTest(key=key), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout, changed, 'targeting')

	def test_keepalive_is_the_only_ordinary_injected_command(self):
		'''Require one keepalive plus exactly the independently expected player inputs.'''
		for mode in ('default', 'targeting'):
			stdout, client = fixture(mode)
			sent = 3 if mode == 'targeting' else 1
			for changed_count in (sent - 1, sent + 1, sent + 2):
				changed_client = client.replace(f'CommandsSent={sent}\n', f'CommandsSent={changed_count}\n')
				changed_stdout = stdout.replace(f'runtimeSent={sent}', f'runtimeSent={changed_count}')
				changed_stdout = changed_stdout.replace(f'productionSent={sent}', f'productionSent={changed_count}')
				with self.subTest(mode=mode, sent=changed_count), self.assertRaises(gate.EvidenceError):
					gate.validate(changed_stdout, changed_client, mode)
			for key in ('runtimeSent', 'productionSent'):
				with self.subTest(key=key), self.assertRaises(gate.EvidenceError):
					gate.validate(stdout.replace(f'{key}={sent}', f'{key}={sent + 1}'), client, mode)

	def test_movement_allowance_is_bound_to_driver_scenario(self):
		'''No-HUD does not mean diagnostic: one or two actual moves supplement the keepalive.'''
		for moves in (1, 2):
			stdout, client = fixture('default', expected_moves=moves)
			self.assertEqual(gate.validate(stdout, client, 'default', session_mode='ordinary',
				expected_moves=moves)['replay_commands'], moves + 1)
			with self.assertRaises(gate.EvidenceError):
				gate.validate(stdout, client, 'default')
		for moves in (-1, True, 1.5):
			with self.assertRaises(gate.EvidenceError):
				gate.validate(*fixture('default'), 'default', expected_moves=moves)
		with self.assertRaises(gate.EvidenceError):
			gate.validate(*fixture(), 'targeting', expected_moves=1)

	def test_ordinary_mode_rejects_status_and_player_proofs(self):
		'''Real AI and connection updates are distinct from the bootstrap demonstration driver.'''
		stdout, client = fixture()
		for before, after in (('queued:0', 'queued:1'), ('completed:no', 'completed:yes'),
			('finalLinuxBotCommandDriverActive=no', 'finalLinuxBotCommandDriverActive=yes'),
			('finalLinuxBotMoveCommandsSent=0', 'finalLinuxBotMoveCommandsSent=1'),
			('finalLinuxBotAttackCommandsSent=0', 'finalLinuxBotAttackCommandsSent=1'),
			('finalLiveHudActivateTalentProof=no', 'finalLiveHudActivateTalentProof=yes'),
			('finalLiveHotkeyCommandProof=no', 'finalLiveHotkeyCommandProof=yes'),
			('finalLiveMapPreviewCommandProof=no', 'finalLiveMapPreviewCommandProof=yes'),
			('finalLiveMinimapCommandProof=no', 'finalLiveMinimapCommandProof=yes')):
			with self.subTest(before=before), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout, client.replace(before, after), 'targeting')
		gate.validate(stdout, client.replace('applied:0', 'applied:1'), 'targeting')

	def test_diagnostics_cannot_omit_its_startup_actions(self):
		'''Opt-in expectations stay strict; accepting ordinary counters is not compatibility.'''
		for mode in ('diagnostics', 'diagnostics-interactive'):
			stdout, client = fixture('default', mode)
			for old, new in (('finalGameWorldBootstrapRuntimeCommands=1', 'finalGameWorldBootstrapRuntimeCommands=0'),
				('finalGameTransceiverTimescaleCommandsSent=1', 'finalGameTransceiverTimescaleCommandsSent=0'),
				('finalGameWorldTimeScale=1.05', 'finalGameWorldTimeScale=1')):
				with self.subTest(mode=mode, old=old), self.assertRaises(gate.EvidenceError):
					gate.validate(stdout, client.replace(old, new), 'default', session_mode=mode)
		with self.assertRaises(gate.EvidenceError):
			gate.validate(*fixture(), 'targeting', session_mode='auto')
		with self.assertRaises(gate.EvidenceError):
			gate.validate_interactive(*fixture('targeting', 'diagnostics'), 100, session_mode='diagnostics')

	def test_cli_requires_policy_and_legacy_smoke_uses_diagnostics(self):
		'''An omitted or wrong expectation cannot silently reinterpret a legacy smoke log.'''
		with tempfile.TemporaryDirectory() as directory:
			out, log = Path(directory) / 'stdout', Path(directory) / 'client.log'
			stdout, client = fixture('default', 'diagnostics')
			out.write_text(stdout, encoding='utf-8')
			log.write_text(client, encoding='utf-8')
			args = ['--stdout', str(out), '--client-log', str(log), '--mode', 'default']
			with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
				with self.assertRaises(SystemExit) as error:
					gate.main(args)
				self.assertEqual(error.exception.code, 2)
				self.assertEqual(gate.main(args + ['--session-mode', 'ordinary']), 1)
				self.assertEqual(gate.main(args + ['--session-mode', 'diagnostics']), 0)

	def test_live_prime_is_not_the_immutable_purchase_receipt(self):
		'''Background salary changes the live HUD, but cannot replace the actual 400-to-100 purchase.'''
		stdout, client = fixture()
		for gold in (100, 104, 212):
			current = client.replace('prime:400->100', f'prime:400->{gold}').replace('finalLiveHeroGold=100', f'finalLiveHeroGold={gold}')
			with self.subTest(gold=gold):
				self.assertEqual(gate.validate(stdout, current, 'targeting')['world_step'], 1000)
			with self.subTest(gold=gold, receipt='wrong'), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace(LOCAL_PURCHASE, LOCAL_PURCHASE.replace('gold=400->100', 'gold=400->200')), current, 'targeting')

	def test_live_prime_requires_unique_matching_bounded_hero_balance(self):
		'''Missing snapshots and inconsistent live balances cannot be explained away as salary.'''
		stdout, client = fixture()
		for gold in ('-1', '2147483648', 'nan', '1.0', '1e2'):
			with self.subTest(gold=gold), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout, client.replace('prime:400->100', f'prime:400->{gold}')
					.replace('finalLiveHeroGold=100', f'finalLiveHeroGold={gold}'), 'targeting')
		for changed in (client.replace('finalLiveHeroGold=100\n', ''), client + 'finalLiveHeroGold=100\n',
			client.replace('finalLiveHeroGold=100', 'finalLiveHeroGold=104'),
			client.replace('prime:400->100', 'prime:401->100')):
			with self.subTest(changed=changed), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout, changed, 'targeting')

	def test_failed_drain_cannot_be_hidden_by_completion(self):
		'''The client emits its ordinary completion record even when timed draining exits nonzero.'''
		for mode in ('targeting', 'default'):
			stdout, client = fixture(mode)
			failure = 'Local scheduler drain timed out: world=425 barrier=426\n'
			for position in ('before', 'after', 'client'):
				with self.subTest(mode=mode, position=position), self.assertRaises(gate.EvidenceError):
					gate.validate(failure + stdout if position == 'before' else stdout + failure if position == 'after' else stdout,
						client + failure if position == 'client' else client, mode)

	def test_running_match_cannot_report_terminal_ui(self):
		'''New counters are required, but a forced finish cannot pass ordinary gameplay acceptance.'''
		for mode in ('targeting', 'default'):
			stdout, client = fixture(mode)
			for key in ('matchEndCalls', 'matchExitRequests'):
				if key + ':' not in client:
					client = client.replace(' error:', f' {key}:0 error:')
			self.assertEqual(gate.validate(stdout, client, mode)['world_step'], 1000)
			for key in ('matchEndCalls', 'matchExitRequests'):
				with self.subTest(mode=mode, key=key), self.assertRaises(gate.EvidenceError):
					gate.validate(stdout, client.replace(key + ':0', key + ':1'), mode)
				with self.subTest(mode=mode, missing=key), self.assertRaises(gate.EvidenceError):
					gate.validate(stdout, client.replace(' ' + key + ':0', ''), mode)

	def test_bot_commands_cannot_overwrite_local_execution(self):
		'''Local event identity replaces the obsolete assumption of a bot-free world.'''
		stdout, client = fixture()
		changed = stdout.replace('activate=2/2/1/1 can=1 slot=0,0 progress=1->2 dev=1->2 gold=400->100',
			'activate=10/10/9/9 can=0 slot=2,3 progress=3->4 dev=5->6 gold=900->400')
		changed = changed.replace('useTalent=2/2/1/1 can=1 target=2/-1/-1 talentState=-1->496/0->0/0.00->10.00',
			'useTalent=76/40/39/3 can=1 target=0/7887/2 talentState=703->813/0->1/0.00->10.00')
		self.assertEqual(gate.validate(changed, client, 'targeting')['world_step'], 1000)
		for event in (LOCAL_PURCHASE, LOCAL_CAST):
			for replacement in ('', event + event, event.replace('hero=7502', 'hero=7887'),
				event.replace('player=2', 'player=9'), event.replace('user=1984', 'user=-9'),
				event.rstrip() + ' extra=1\n'):
				with self.subTest(event=event, replacement=replacement), self.assertRaises(gate.EvidenceError):
					gate.validate(changed.replace(event, replacement), client, 'targeting')

	def test_local_execution_identity_and_outcomes_fail_closed(self):
		'''Bot success, missing local events and contradictory HUD ownership never prove a cast.'''
		stdout, client = fixture()
		for event in (LOCAL_PURCHASE, LOCAL_CAST):
			for token in event.split(': ', 1)[1].split():
				for replacement in ('', token + ' ' + token):
					with self.subTest(token=token, replacement=replacement), self.assertRaises(gate.EvidenceError):
						gate.validate(stdout.replace(event, event.replace(token, replacement)), client, 'targeting')
		for before, after in (('activated=1', 'activated=0'), ('hero=7502', 'hero=-1'),
			('player=2', 'player=-1'), ('user=1984', 'user=0'), ('hero=7502', 'hero=nan')):
			with self.subTest(before=before, after=after), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace(before, after), client, 'targeting')

	def test_startup_failure_cannot_be_masked_by_successful_cast(self):
		'''Pinned mock records reproduce cost-native's regression with an otherwise good tail.'''
		for mode in ('default', 'targeting'):
			stdout, client = fixture(mode)
			for good, bad in zip(SESSION_EFFECTS.splitlines(True), REGRESSED_SESSION_EFFECTS.splitlines(True)):
				with self.subTest(mode=mode, record=good), self.assertRaisesRegex(gate.EvidenceError, 'Session effects'):
					gate.validate(stdout.replace(good, bad), client, mode)
			with self.assertRaisesRegex(gate.EvidenceError, 'Session effects'):
				gate.validate(stdout.replace(SESSION_EFFECTS, REGRESSED_SESSION_EFFECTS), client, mode)

	def test_session_records_missing_duplicate_or_malformed_in_both_modes(self):
		'''Even a failed record followed by a successful duplicate must be rejected.'''
		for mode in ('default', 'targeting'):
			stdout, client = fixture(mode)
			for good in SESSION_EFFECTS.splitlines(True):
				bad = good.replace('yes', 'no', 1)
				for replacement in ('', good + good, bad + good, good + bad,
					good.replace(': ', ':'), good.rstrip() + ' unexpected=yes\n'):
					with self.subTest(mode=mode, record=good, replacement=replacement), \
						self.assertRaisesRegex(gate.EvidenceError, 'Session effects'):
						gate.validate(stdout.replace(good, replacement), client, mode)

	def test_every_session_flag_and_counter_is_checked(self):
		'''Mutate individual success flags and numeric results, not just db/units readiness.'''
		for mode in ('default', 'targeting'):
			stdout, client = fixture(mode)
			for good in SESSION_EFFECTS.splitlines(True):
				for match in re.finditer(r'yes|[0-9]+(?:\.[0-9]+)?', good):
					bad = good[:match.start()] + ('no' if match[0] == 'yes' else '999') + good[match.end():]
					with self.subTest(mode=mode, record=bad), self.assertRaisesRegex(gate.EvidenceError, 'Session effects'):
						gate.validate(stdout.replace(good, bad), client, mode)

	def test_payment_variable_balances_and_tolerance(self):
		'''Actual mana balances may vary, but the finite nonnegative deduction stays 70.'''
		stdout, client = fixture()
		for before, after in (('725', '655'), ('1000.25', '930.25'), ('70', '0'),
			('725', '655.01'), ('725', '654.99'), ('70.01', '0'), ('69.99', '0'), ('7.25e2', '6.55e2')):
			with self.subTest(before=before, after=after):
				changed = stdout.replace('before=725 after=655', f'before={before} after={after}')
				gate.validate(changed, client, 'targeting')

	def test_payment_schema_and_plane_values(self):
		'''One complete payment record is required; another talent/hero cannot substitute.'''
		stdout, client = fixture()
		mutations = (('slot=0,0', 'slot=0,1'), ('slot=0,0', 'slot=00,0'), ('range=14', 'range=0'),
			('range=14', 'range=14.01'), ('cost=70', 'cost=0'), ('cost=70', 'cost=71'),
			('range=14', 'range=14.000000000000001'), ('cost=70', 'cost=70.000000000000001'),
			('pool=mana', 'pool=energy'), ('pool=mana', 'pool=health'), ('step=496', 'step=495'),
			('step=496', 'step=-1'), ('step=496', 'step=496.0'), ('range=14', 'range=nan'),
			('cost=70', 'cost=inf'), ('step=496', 'step=496x'))
		for old, new in mutations:
			with self.subTest(old=old, new=new), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace(PAYMENT, PAYMENT.replace(old, new)), client, 'targeting')
		for token in PAYMENT.split(': ', 1)[1].split():
			for replacement in ('', token + ' ' + token):
				with self.subTest(token=token, replacement=replacement), self.assertRaises(gate.EvidenceError):
					gate.validate(stdout.replace(PAYMENT, PAYMENT.replace(token, replacement)), client, 'targeting')
		for replacement in ('', PAYMENT + PAYMENT, PAYMENT.replace(': ', ':'), PAYMENT.rstrip() + ' extra=1\n',
			PAYMENT.replace('before=725', 'before=0') + PAYMENT):
			with self.subTest(replacement=replacement), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace(PAYMENT, replacement), client, 'targeting')
		for old, new in (('(plane)', '(other)'), ('activated=1 slot=0,0', 'activated=1 slot=0,1')):
			with self.subTest(old=old, new=new), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace(old, new), client, 'targeting')

	def test_payment_rejects_bad_balances_and_wrong_deduction(self):
		'''No hardcoded pool sizes, nonfinite values, negative balances or relative tolerance.'''
		stdout, client = fixture()
		for field in ('before', 'after'):
			for invalid in ('nan', 'inf', '-inf', '1e309', '-1', '-1e-999', '', '725x', '1e-9999999999999999999999999999'):
				changed = re.sub(r'\b' + field + r'=[0-9]+', field + '=' + invalid, PAYMENT)
				with self.subTest(field=field, invalid=invalid), self.assertRaises(gate.EvidenceError):
					gate.validate(stdout.replace(PAYMENT, changed), client, 'targeting')
		for before, after in (('725', '725'), ('725', '655.02'), ('725', '654.98'), ('0', '70'),
			('70.010001', '0'), ('69.989999', '0'), ('1000000070', '1000000001')):
			with self.subTest(before=before, after=after), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace('before=725 after=655', f'before={before} after={after}'), client, 'targeting')

	def test_cast_step_cannot_exceed_world(self):
		'''A recorded future command is not an executed cast in this world.'''
		stdout, client = fixture()
		with self.assertRaises(gate.EvidenceError):
			gate.validate(stdout.replace('-1->496/', '-1->100000/'), client, 'targeting')

	def test_interactive_clock_and_late_cast(self):
		'''Reject frozen, accelerated, future-cast and alias-contaminated mock sessions.'''
		stdout, client = fixture()
		stdout = stdout.replace('replayBytes=50', 'replayBytes=50 heroStopCommands=0 heroAttackCommands=0 '
			'heroCancelCommands=0 heroUseUnitCommands=0 heroUseTalentCommands=0').replace('-1->496/', '-1->750/').replace('step=496', 'step=750')
		client += ('finalInteractiveClock=ticks:1001 pumps:1800 pendingSeconds:0.05 discardedSeconds:0.1\n'
			'finalMap3DPreviewBaseYaw=-42\nfinalMap3DPreviewPitch=56\nfinalMap3DPreviewZoom=1.18\n')
		self.assertEqual(gate.validate_interactive(stdout, client, 100, 650)['cast_step'], 750)
		for before, after in (('ticks:1001', 'ticks:650'), ('ticks:1001', 'ticks:3000'),
			('pumps:1800', 'pumps:0'), ('pendingSeconds:0.05', 'pendingSeconds:0.51'),
			('discardedSeconds:0.1', 'discardedSeconds:nan'), ('discardedSeconds:0.1', 'discardedSeconds:-1'),
			('BaseYaw=-42', 'BaseYaw=-57'), ('Pitch=56', 'Pitch=52'), ('Zoom=1.18', 'Zoom=1.3452')):
			with self.subTest(before=before, after=after), self.assertRaises(gate.EvidenceError):
				gate.validate_interactive(stdout, client.replace(before, after), 100, 650)
		for before, after in (('-1->750/', '-1->649/'), ('-1->750/', '-1->1001/'),
			('heroStopCommands=0', 'heroStopCommands=1'), ('heroAttackCommands=0', 'heroAttackCommands=1'),
			('heroCancelCommands=0', 'heroCancelCommands=1'),
			('heroUseUnitCommands=0', 'heroUseUnitCommands=1'), ('heroUseTalentCommands=0', 'heroUseTalentCommands=1')):
			with self.subTest(before=before, after=after), self.assertRaises(gate.EvidenceError):
				changed = stdout.replace(before, after)
				if before == '-1->750/':
					changed = changed.replace('step=750', 'step=' + after[len('-1->'):-1])
				gate.validate_interactive(changed, client, 100, 650)
		with self.assertRaises(gate.EvidenceError):
			gate.validate_interactive(stdout, client, 60, 650)
		for line in client.splitlines(keepends=True):
			if 'finalInteractiveClock' not in line and 'finalMap3DPreview' not in line:
				continue
			for replacement in ('', line + line):
				with self.subTest(line=line, replacement=replacement), self.assertRaises(gate.EvidenceError):
					gate.validate_interactive(stdout, client.replace(line, replacement), 100, 650)
		for seconds, minimum in ((float('nan'), 0), (float('inf'), 0), (0, 0), (100, -1), (100, 0.5)):
			with self.assertRaises(gate.EvidenceError):
				gate.validate_interactive(stdout, client, seconds, minimum)

	def test_valid_modes(self):
		'''Default supports builds without a Ruffle record.'''
		for mode in ('targeting', 'default'):
			stdout, client = fixture(mode)
			self.assertEqual(gate.validate(stdout, client, mode)['world_step'], 1000)
		stdout, client = fixture('default')
		client = '\n'.join(line for line in client.splitlines() if 'finalRuffleInspection' not in line) + '\n'
		gate.validate(stdout, client, 'default')

	def test_local_camera_interactive_contract(self):
		'''The new hero view must remain unchanged by special-key alias tests.'''
		stdout, client = fixture()
		stdout = stdout.replace('replayBytes=50', 'replayBytes=50 heroStopCommands=0 heroAttackCommands=0 '
			'heroCancelCommands=0 heroUseUnitCommands=0 heroUseTalentCommands=0')
		client += ('finalInteractiveClock=ticks:1001 pumps:1800 pendingSeconds:0.05 discardedSeconds:0.1\n'
			'finalMap3DPreviewBaseYaw=-42\nfinalMap3DPreviewPitch=56\nfinalMap3DPreviewZoom=8.14331\n'
			'finalLocalMapCamera=active:1 follow:1 updates:447 focusHeight:7.28\n'
			'mapTacticalBounds=19.0107,36.9693 -> 390.989,371.106\n'
			'finalMap3DPreviewInputCount=0\nfinalMap3DPreviewLastAction=local-hero-camera\n')
		self.assertEqual(gate.validate_interactive(stdout, client, 100)['cast_step'], 496)
		for before, after in (('active:1', 'active:0'), ('follow:1', 'follow:0'), ('updates:447', 'updates:0'),
			('focusHeight:7.28', 'focusHeight:nan'), ('Zoom=8.14331', 'Zoom=9.28'),
			('InputCount=0', 'InputCount=1'), ('LastAction=local-hero-camera', 'LastAction=zoom-in'),
			('390.989,371.106', '19,36'), ('390.989,371.106', 'nan,371.106')):
			with self.subTest(before=before), self.assertRaises(gate.EvidenceError):
				gate.validate_interactive(stdout, client.replace(before, after), 100)
		for line in client.splitlines(keepends=True):
			if not line.startswith(('finalLocal', 'mapTacticalBounds', 'finalMap3D')):
				continue
			for replacement in ('', line + line):
				with self.subTest(line=line, replacement=replacement), self.assertRaises(gate.EvidenceError):
					gate.validate_interactive(stdout, client.replace(line, replacement), 100)

	def test_every_required_record_missing_or_duplicated(self):
		'''A repeated final record cannot silently override an earlier failure.'''
		original = fixture()
		for source in range(2):
			for line in original[source].splitlines(keepends=True):
				for replacement in ('', line + line):
					with self.subTest(source=source, line=line, duplicate=bool(replacement)):
						texts = list(original)
						texts[source] = texts[source].replace(line, replacement)
						with self.assertRaises(gate.EvidenceError):
							gate.validate(*texts, 'targeting')

	def test_bad_targeting_fields(self):
		'''All exact expectations and every numeric field fail closed.'''
		stdout, client = fixture()
		for field in gate.RUFFLE_COUNTERS.split():
			for invalid in ('nan', 'inf', '-1', '1.0', '', '1x'):
				with self.subTest(field=field, invalid=invalid):
					changed = re.sub(r'\b' + field + r':[0-9]+', field + ':' + invalid, client)
					with self.assertRaises(gate.EvidenceError):
						gate.validate(stdout, changed, 'targeting')
		for before, after in (('targetArmed:3', 'targetArmed:4'), ('targetCanceled:2', 'targetCanceled:1'),
			('targetCasts:1', 'targetCasts:0'), ('targetRejected:2', 'targetRejected:0'),
			('targetRejected:2', 'targetRejected:1'), ('targetRejected:2', 'targetRejected:3'),
			('shortcutCalls:3', 'shortcutCalls:2'), ('talentCommands:2', 'talentCommands:1'),
			('prime:400->100', 'prime:400->200'), ('pendingCallbacks:0', 'pendingCallbacks:1'),
			('targetPending:0', 'targetPending:1'), ('minimapMoves:0', 'minimapMoves:1'),
			('priorGlErrors:0', 'priorGlErrors:1'), ('error:', 'error:failure'),
			('shortcutCalls:3', ''), ('shortcutCalls:3', 'shortcutCalls:3 shortcutCalls:3'),
			('minimapTarget:0,0', 'minimapTarget:1e309,0'), ('draw:3', 'draw:nan'),
			('update:2', 'update:1e309'), ('input:1', 'input:-1'), ('assets:0', 'assets:inf'),
			('frames:21', 'frames:0'), ('draw:3', 'draw:3 unknown:0')):
			with self.subTest(before=before, after=after), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout, client.replace(before, after), 'targeting')

	def test_replay_and_execution_failures(self):
		'''Submission logs alone cannot satisfy actual engine execution evidence.'''
		stdout, client = fixture()
		for before, after in (('valid=yes', 'valid=no'), ('failures=0', 'failures=1'),
			('commandBlocks=3/3', 'commandBlocks=1/3'), ('commands=3/3', 'commands=4/4'),
			('statuses=1/1', 'statuses=2/2'), ('segments=2/2', 'segments=1/2'),
			('bytesRead=50/50', 'bytesRead=49/50'), ('commandMatch=yes', 'commandMatch=no'),
			('error=none', 'error=bad'), ('steps=1..90', 'steps=1..89'),
			('useTalent=2/2/1/1', 'useTalent=2/2/0/0'), ('0.00->10.00', '0.00->0.00'),
			('0.00->10.00', '0.00->nan'), ('0.00->10.00', '0.00->1e309'),
			('gold=400->100', 'gold=400->400'), ('step=1000', 'step=0'),
			('replayBytes=50', 'replayBytes=51'), ('records=2', 'records=2 records=2')):
			with self.subTest(before=before, after=after), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace(before, after), client, 'targeting')

	def test_ground_execution_markers(self):
		'''Correct cooldown alone cannot prove a ground cast or a new last-use step.'''
		stdout, client = fixture()
		for target in ('0/-1/-1', '1/-1/-1', '3/-1/-1', '2/7406/-1', '2/-1/0',
			'02/-1/-1', '2/-01/-1', '2/-1/-1x', '2/-1', '2/-1/-1/0'):
			with self.subTest(target=target), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace('target=2/-1/-1', 'target=' + target), client, 'targeting')
		for step in ('-1->-1', '0->0', '496->496', '0->496', '-2->496', '-1->-2',
			'-1->nan', '-1->496x', '-1->496->497', '->496'):
			with self.subTest(step=step), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace('talentState=-1->496/', 'talentState=' + step + '/'),
					client, 'targeting')

	def test_ground_execution_active_instance_pair(self):
		'''The second talentState pair counts active instances, not mana payment.'''
		stdout, client = fixture()
		for step in (0, 1, 496):
			for instances in ('0->0', '1->0', '-1->5', '5->-1'):
				with self.subTest(step=step, instances=instances):
					changed = stdout.replace('talentState=-1->496/0->0/',
						f'talentState=-1->{step}/{instances}/').replace('step=496', f'step={step}')
					gate.validate(changed, client, 'targeting')
		for instances in ('nan->0', '0->inf', '0->1x', '0.5->1', '0', '0->1->2'):
			with self.subTest(instances=instances), self.assertRaises(gate.EvidenceError):
				gate.validate(stdout.replace('talentState=-1->496/0->0/',
					'talentState=-1->496/' + instances + '/'), client, 'targeting')

	def test_truncation_and_wrong_mode(self):
		'''Incomplete files and targeting activity in default mode are rejected.'''
		stdout, client = fixture()
		for texts in ((stdout[:-1], client), (stdout, client[:-1]), (stdout, client + '\0\n')):
			with self.assertRaises(gate.EvidenceError):
				gate.validate(*texts, 'targeting')
		with self.assertRaises(gate.EvidenceError):
			gate.validate(stdout, client, 'default')
		stdout, client = fixture('default')
		client = '\n'.join(line for line in client.splitlines() if 'finalRuffleInspection' not in line) + '\n'
		with self.assertRaises(gate.EvidenceError):
			gate.validate('Ruffle talent command: submitted\n' + stdout, client, 'default')

	def test_cli(self):
		'''CLI returns a failed verdict for malformed evidence and inaccessible files.'''
		with tempfile.TemporaryDirectory() as directory:
			out, log = Path(directory) / 'stdout', Path(directory) / 'client.log'
			stdout, client = fixture()
			out.write_text(stdout, encoding='utf-8')
			log.write_text(client, encoding='utf-8')
			args = ['--stdout', str(out), '--client-log', str(log), '--session-mode', 'ordinary', '--mode', 'targeting']
			with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
				self.assertEqual(gate.main(args), 0)
				self.assertEqual(gate.main(args + ['--min-cast-step', '650']), 1)
				self.assertEqual(gate.main(args + ['--interactive-seconds', 'nan']), 1)
				self.assertEqual(gate.main(args + ['--interactive-seconds', '100']), 1)
				default_stdout, default_client = fixture('default')
				out.write_text(default_stdout, encoding='utf-8')
				log.write_text(default_client, encoding='utf-8')
				self.assertEqual(gate.main(args[:-1] + ['default', '--interactive-seconds', '100']), 1)
				log.write_text(client, encoding='utf-8')
				out.write_text(stdout[:-1], encoding='utf-8')
				self.assertEqual(gate.main(args), 1)
				out.unlink()
				self.assertEqual(gate.main(args), 1)


if __name__ == '__main__':
	unittest.main()
