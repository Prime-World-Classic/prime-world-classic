'''Headless safety checks for staging an isolated Ruffle source experiment.'''

from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import prepare


class PrepareTests(unittest.TestCase):
	@patch('prepare.patch_needed', return_value=False)
	@patch('prepare.git')
	def test_mouse_feature_definition_staged_without_enabling_it(self, git, needed):
		'''The compatibility feature exists in prepared checkouts but stays opt-in.'''
		git.return_value = subprocess.CompletedProcess([], 0, stdout=prepare.REVISION + '\n', stderr='')
		with tempfile.TemporaryDirectory() as temporary:
			prepare.prepare(Path(temporary))
		self.assertIn(prepare.HERE / 'mouse_events.patch', [call.args[1] for call in needed.call_args_list])
		self.assertNotIn(prepare.HERE / 'user_input.patch', [call.args[1] for call in needed.call_args_list])

	def test_missing_and_identical_destinations(self):
		'''Repeated staging is idempotent without overwriting the source checkout.'''
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			plan = prepare.stage_plan(root)
			self.assertEqual(len(plan), len(prepare.FILES))
			for destination, content in plan:
				destination.parent.mkdir(parents=True, exist_ok=True)
				destination.write_bytes(content)
			self.assertEqual(prepare.stage_plan(root), [])

	def test_conflicting_file_remains_untouched(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary).resolve()
			file = root / 'core/src/primeworld.rs'
			file.parent.mkdir(parents=True)
			file.write_text('local changes')
			with self.assertRaisesRegex(RuntimeError, 'Refusing to overwrite'):
				prepare.stage_plan(root)
			self.assertEqual(file.read_text(), 'local changes')
			self.assertFalse((root / 'core/examples/primeworld.rs').exists())

	def test_symlink_escape_rejected(self):
		with tempfile.TemporaryDirectory() as temporary, tempfile.TemporaryDirectory() as outside:
			root = Path(temporary).resolve()
			(root / 'core').symlink_to(outside, target_is_directory=True)
			with self.assertRaisesRegex(RuntimeError, 'escapes checkout'):
				prepare.stage_plan(root)

	@patch('prepare.git')
	def test_revision_mismatch_makes_no_edits(self, git):
		git.return_value = subprocess.CompletedProcess([], 0, stdout='wrong revision\n', stderr='')
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			with self.assertRaisesRegex(RuntimeError, 'Expected Ruffle revision'):
				prepare.prepare(root)
			self.assertEqual(list(root.iterdir()), [])

	@patch('prepare.git')
	def test_patch_idempotence_and_conflict(self, git):
		ok = subprocess.CompletedProcess([], 0, stdout='', stderr='')
		bad = subprocess.CompletedProcess([], 1, stdout='', stderr='conflict')
		git.return_value = ok
		self.assertFalse(prepare.patch_needed(Path('/unused'), Path('/patch')))
		git.side_effect = [bad, ok]
		self.assertTrue(prepare.patch_needed(Path('/unused'), Path('/patch')))
		git.side_effect = [bad, bad]
		with self.assertRaisesRegex(RuntimeError, 'conflict'):
			prepare.patch_needed(Path('/unused'), Path('/patch'))


if __name__ == '__main__':
	unittest.main()
