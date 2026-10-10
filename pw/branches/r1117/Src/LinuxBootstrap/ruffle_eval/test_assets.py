'''Fixture-driven path, DDS, and data-preservation tests; no GPU required.'''

from pathlib import Path
import tempfile
import unittest

from PIL import Image

from assets import resolve_asset, transcode


class AssetTests(unittest.TestCase):
	def test_root_and_relative_game_paths(self):
		with tempfile.TemporaryDirectory() as temporary:
			data = Path(temporary)
			movie = data / 'movie.swf'
			movie.touch()
			asset = data / 'icon.dds'
			asset.touch()
			for name in (':/icon.dds', ':icon.dds', ':\\icon.dds', 'icon.dds'):
				self.assertEqual(resolve_asset(data, movie, name), asset.resolve())
			for name in ('', '/', 'file:///etc/passwd', 'https://example.test/icon.dds'):
				with self.assertRaises(ValueError):
					resolve_asset(data, movie, name)

	def test_symlink_and_parent_escape(self):
		with tempfile.TemporaryDirectory() as temporary:
			root = Path(temporary)
			data = root / 'Data'
			data.mkdir()
			movie = data / 'movie.swf'
			movie.touch()
			outside = root / 'outside.dds'
			outside.touch()
			(data / 'link.dds').symlink_to(outside)
			for name in ('link.dds', '../outside.dds'):
				with self.assertRaisesRegex(ValueError, 'outside Data'):
					resolve_asset(data, movie, name)

	def test_dds_rgba_round_trip_and_no_overwrite(self):
		'''A small mock DDS exercises dimensions, transparency, and exact decoded pixels.'''
		with tempfile.TemporaryDirectory() as temporary:
			source = Path(temporary) / 'input.dds'
			destination = Path(temporary) / 'output.png'
			Image.new('RGBA', (4, 8), (20, 40, 60, 127)).save(source, format='DDS')
			original = source.read_bytes()
			metadata = transcode(source, destination)
			self.assertEqual(metadata['size'], [4, 8])
			with Image.open(destination) as png, Image.open(source) as dds:
				self.assertEqual(png.convert('RGBA').tobytes(), dds.convert('RGBA').tobytes())
			with self.assertRaises(FileExistsError):
				transcode(source, destination)
			self.assertEqual(source.read_bytes(), original)

	def test_non_dds_rejected(self):
		with tempfile.TemporaryDirectory() as temporary:
			source = Path(temporary) / 'input.png'
			Image.new('RGBA', (1, 1)).save(source)
			with self.assertRaisesRegex(ValueError, 'Expected a DDS'):
				transcode(source, Path(temporary) / 'out.png')


if __name__ == '__main__':
	unittest.main()
