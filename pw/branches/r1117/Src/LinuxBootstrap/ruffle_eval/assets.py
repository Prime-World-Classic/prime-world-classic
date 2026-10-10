#!/usr/bin/env python3
'''Generate bounded DDS/PNG loading fixtures, not a production asset provider.'''

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


HERE = Path(__file__).resolve().parent
ASSETS = (':/UI/Styles/LoadingBack/PVP.dds', ':/UI/Styles/LoadingBack/PVP_Logo.dds')


def resolve_asset(data, movie, name):
	'''Resolve PW root/relative paths while rejecting URLs and escapes from Data.'''
	data = data.resolve(strict=True)
	movie = movie.resolve(strict=True)
	if not movie.is_relative_to(data):
		raise ValueError('Movie outside Data')
	name = name.replace('\\', '/')
	if name.startswith(':'):
		base, name = data, name[1:].lstrip('/')
	else:
		base = movie.parent
	if not name or ':' in name or name.startswith('/'):
		raise ValueError('Expected a game resource path, not a URL or absolute path')
	asset = (base / name).resolve(strict=True)
	if not asset.is_relative_to(data):
		raise ValueError('Asset outside Data')
	return asset


def transcode(source, destination):
	'''Use Pillow's maintained DDS decoder, retaining dimensions and alpha in PNG.'''
	with Image.open(source) as image:
		if image.format != 'DDS':
			raise ValueError('Expected a DDS resource')
		if image.width * image.height > 16 * 1024 * 1024:
			raise ValueError('Asset exceeds evaluation image limit')
		rgba = image.convert('RGBA')
		with destination.open('xb') as output:
			rgba.save(output, format='PNG')
		return {'size': list(rgba.size), 'rgba_sha256': hashlib.sha256(rgba.tobytes()).hexdigest(),
			'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
			'png_sha256': hashlib.sha256(destination.read_bytes()).hexdigest()}


def create_fixture(data, output):
	'''Compare raw DDS failures with PNG loads through the same unmodified loading SWF.'''
	data = data.resolve(strict=True)
	movie = data / 'UI/Screens/Loading/Flash/pwl.swf'
	sources = [resolve_asset(data, movie, name) for name in ASSETS]
	output = output.resolve()
	if output.is_relative_to(data):
		raise ValueError('Evaluation output must not modify Data')
	output.mkdir(parents=True, exist_ok=False)
	metadata = []
	for index, source in enumerate(sources):
		png = output / (str(index) + '.png')
		metadata.append({'game_path': ASSETS[index], 'dds': source.as_uri(), 'png': png.as_uri(),
			**transcode(source, png)})
	for format in ('dds', 'png'):
		calls = json.loads((HERE / 'loading_calls.json').read_text())
		calls.append({'path': 'LoaderWindowInterface', 'method': 'SetMapBack',
			'args': [item[format] for item in metadata]})
		for target, item in zip(('backGround', 'logo'), metadata):
			for dimension, size in zip(('width', 'height'), item['size']):
				calls.append({'path': target + '.ico_ld.content.bitmapData', 'op': 'get',
					'method': dimension, 'args': [], 'frames_before': 5, 'expect': size})
		(output / (format + '_calls.json')).write_text(json.dumps(calls, indent=2) + '\n')
		if format == 'png':
			# The original Loader completes synchronously before SetMapBack positions images.
			# Keep this separate: a decode success must not hide asynchronous layout drift.
			for target, item in zip(('backGround', 'logo'), metadata):
				for coordinate, viewport, size in zip(('x', 'y'), (1280, 720), item['size']):
					calls.append({'path': target, 'op': 'get', 'method': coordinate,
						'args': [], 'expect': (viewport - size) / 2})
			(output / 'layout_calls.json').write_text(json.dumps(calls, indent=2) + '\n')
	(output / 'assets.json').write_text(json.dumps(metadata, indent=2) + '\n')
	print(json.dumps({'output': str(output), 'assets': metadata}, indent=2))


def main():
	'''Require explicit paths and leave the game's assets and installed player unchanged.'''
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument('data', type=Path)
	parser.add_argument('output', type=Path)
	args = parser.parse_args()
	create_fixture(args.data, args.output)


if __name__ == '__main__':
	main()
