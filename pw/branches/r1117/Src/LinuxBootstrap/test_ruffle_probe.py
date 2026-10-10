"""Headless regressions for the Ruffle evaluation harness; no installed player required."""

from pathlib import Path
import unittest

from ruffle_probe import build_command, choose_window, diagnostics


class RuffleProbeTests(unittest.TestCase):
	def test_native_isolated_command(self):
		"""Never inherit user preferences or enable external network/navigation commands."""
		command = build_command('ruffle', Path('/assets/a movie.swf'), Path('/assets'),
			Path('/tmp/probe'), 1280, 720)
		for option, value in (('--graphics', 'gl'), ('--storage', 'memory'),
			('--tcp-connections', 'deny'), ('--open-url-mode', 'deny'),
			('--filesystem-access-mode', 'deny'), ('--proxy', 'http://127.0.0.1:9')):
			self.assertEqual(command[command.index(option) + 1], value)
		self.assertEqual(command[1], '/assets/a movie.swf')
		self.assertNotIn('--dummy-external-interface', command)

	def test_dummy_interface_is_explicit(self):
		command = build_command('ruffle', Path('/assets/a.swf'), Path('/assets'), Path('/tmp/probe'), 960, 768, True)
		self.assertIn('--dummy-external-interface', command)

	def test_pid_namespace_fallback(self):
		"""The initial live run exposed differing process IDs across the X11 host boundary."""
		self.assertEqual(choose_window([], ['10'], ['10', '20']), '20')
		self.assertEqual(choose_window(['20'], ['10'], ['10', '20', '30']), '20')
		self.assertIsNone(choose_window([], ['10'], ['10']))
		self.assertIsNone(choose_window([], ['10'], ['10', '20', '30']))

	def test_runtime_errors_are_not_hidden(self):
		messages = diagnostics('INFO running\nERROR Error #1056: userInput\nWARN stub\nWARN stub\n')
		self.assertEqual(messages, ['ERROR Error #1056: userInput', 'WARN stub'])


if __name__ == '__main__':
	unittest.main()
