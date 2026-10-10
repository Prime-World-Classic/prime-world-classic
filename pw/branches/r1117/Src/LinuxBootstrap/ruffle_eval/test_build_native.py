'''Mock the build tools to pin the offline optimized FFI build contract.'''

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class NativeBuildTests(unittest.TestCase):
	def run_script(self, arguments, failure=0):
		'''Never invoke Rust or modify a checkout from this policy regression.'''
		with tempfile.TemporaryDirectory() as directory:
			root = Path(directory)
			log = root / 'calls.jsonl'
			tool = '''#!/usr/bin/env python3
import json, os, sys
with open(os.environ['BUILD_LOG'], 'a') as output:
	output.write(json.dumps({'name': os.path.basename(sys.argv[0]), 'args': sys.argv[1:],
		'panic': os.environ.get('CARGO_PROFILE_RELEASE_PANIC'),
		'overflow': os.environ.get('CARGO_PROFILE_RELEASE_OVERFLOW_CHECKS')}) + '\\n')
sys.exit(int(os.environ.get('PREPARE_FAILURE', '0')) if os.path.basename(sys.argv[0]) != 'cargo' else 0)
'''
			# Use an absolute interpreter to avoid recursively calling the mock python3.
			import sys
			tool = tool.replace('#!/usr/bin/env python3', '#!' + sys.executable)
			for name in ('prepare-python', 'cargo'):
				path = root / name
				path.write_text(tool)
				path.chmod(0o755)
			(root / 'python3').symlink_to(root / 'prepare-python')
			env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
				BUILD_LOG=str(log), PREPARE_FAILURE=str(failure),
				CARGO_PROFILE_RELEASE_PANIC='abort', CARGO_PROFILE_RELEASE_OVERFLOW_CHECKS='false')
			result = subprocess.run(['bash', str(Path(__file__).with_name('build_native.sh')), *arguments],
				env=env, capture_output=True, text=True)
			return result, [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []

	def test_optimized_unwind_build(self):
		result, calls = self.run_script(['/mock checkout', '3'])
		self.assertEqual(result.returncode, 0, result.stderr)
		self.assertEqual(len(calls), 2)
		self.assertEqual(calls[0]['args'][-2:], ['/mock checkout', '--user-input'])
		self.assertEqual(calls[1]['args'], ['build', '--manifest-path', '/mock checkout/Cargo.toml',
			'--locked', '--offline', '--release', '-p', 'ruffle_core', '--example', 'pw_bridge',
			'--features', 'default_font,primeworld_mouse_events', '-j', '3'])
		self.assertEqual((calls[1]['panic'], calls[1]['overflow']), ('unwind', 'true'))

	def test_invalid_jobs_fail_before_preparation(self):
		for arguments in ([], ['a', '0'], ['a', '-1'], ['a', '65'], ['a', 'x'], ['a', '1', 'extra']):
			result, calls = self.run_script(arguments)
			self.assertEqual(result.returncode, 2)
			self.assertEqual(calls, [])

	def test_prepare_failure_stops_build(self):
		result, calls = self.run_script(['a'], failure=7)
		self.assertEqual(result.returncode, 7)
		self.assertEqual(len(calls), 1)


if __name__ == '__main__':
	unittest.main()
