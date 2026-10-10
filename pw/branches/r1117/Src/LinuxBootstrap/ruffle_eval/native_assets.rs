//! Bounded, local Prime World image provider for the isolated native Ruffle host.
//! Uses maintained dds/image codecs; no network, Python process, or asset rewriting.

use image::{ImageFormat, ImageReader, Limits};
use std::cell::RefCell;
use std::collections::HashMap;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};
use std::rc::Rc;

const MAX_FILE: usize = 32 * 1024 * 1024;
const MAX_DECODED: usize = 64 * 1024 * 1024;
const MAX_CACHE: usize = 128 * 1024 * 1024;
const MAX_CACHE_ENTRIES: usize = 512;

/// Straight-alpha RGBA pixels. The renderer bridge performs premultiplication once.
pub struct AssetImage {
	pub width: u32,
	pub height: u32,
	pub rgba: Vec<u8>,
	pub encoded_size: usize,
}

/// Canonical game root and movie-relative base with a bounded decoded-image cache.
pub struct Assets {
	root: PathBuf,
	base: PathBuf,
	cache: RefCell<HashMap<PathBuf, Rc<AssetImage>>>,
	bytes: RefCell<usize>,
}

impl Assets {
	/// Both paths must exist and the movie must be inside the supplied Data root.
	pub fn new(root: &Path, movie: &Path) -> Result<Self, String> {
		let root = root.canonicalize().map_err(|e| e.to_string())?;
		let movie = movie.canonicalize().map_err(|e| e.to_string())?;
		if !root.is_dir() || !movie.is_file() || !movie.starts_with(&root) {
			return Err("Movie must be a file inside Data".into());
		}
		Ok(Self {
			root,
			base: movie.parent().unwrap().to_path_buf(),
			cache: RefCell::new(HashMap::new()),
			bytes: RefCell::new(0),
		})
	}

	/// Resolve `:/UI/...` or movie-relative paths; reject URLs and filesystem escapes.
	/// This is a trusted local game-data provider, not a race-proof filesystem sandbox.
	pub fn resolve(&self, name: &str) -> Result<PathBuf, String> {
		if name.is_empty() || name.len() > 4096 || name.contains('\0') {
			return Err("Invalid game asset name".into());
		}
		let name = name.replace('\\', "/");
		let (base, name) = match name.strip_prefix(':') {
			Some(name) => (&self.root, name.trim_start_matches('/')),
			None => (&self.base, name.as_str()),
		};
		if name.is_empty() || name.contains(':') || Path::new(name).is_absolute() {
			return Err("Only game-relative resource paths are supported".into());
		}
		let path = base.join(name).canonicalize().map_err(|e| e.to_string())?;
		if !path.starts_with(&self.root) || !path.is_file() {
			return Err("Asset must be a file inside Data".into());
		}
		Ok(path)
	}

	/// Decode DDS/PNG through image's maintained codecs, rejecting oversized resources.
	pub fn load(&self, name: &str) -> Result<Rc<AssetImage>, String> {
		let path = self.resolve(name)?;
		if let Some(image) = self.cache.borrow().get(&path) {
			return Ok(image.clone());
		}
		let file = std::fs::File::open(&path).map_err(|e| e.to_string())?;
		let mut bytes = Vec::new();
		file.take((MAX_FILE + 1) as u64)
			.read_to_end(&mut bytes)
			.map_err(|e| e.to_string())?;
		if bytes.len() > MAX_FILE {
			return Err("Encoded image exceeds limit".into());
		}
		let format = image::guess_format(&bytes).map_err(|e| e.to_string())?;
		if !matches!(format, ImageFormat::Dds | ImageFormat::Png) {
			return Err("Only DDS and PNG image resources are supported".into());
		}
		let image = if format == ImageFormat::Dds {
			let mut decoder = dds::Decoder::new(Cursor::new(&bytes)).map_err(|e| e.to_string())?;
			if !decoder.layout().is_texture() {
				return Err("Only 2D DDS textures are supported".into());
			}
			let size = decoder.main_size();
			if size.width > 8192 || size.height > 8192 {
				return Err("RGBA image exceeds limit".into());
			}
			let length = u64::from(size.width)
				.checked_mul(u64::from(size.height))
				.and_then(|pixels| pixels.checked_mul(4))
				.ok_or("RGBA size overflow")?;
			if length > MAX_DECODED as u64 {
				return Err("RGBA image exceeds limit".into());
			}
			// dds normalizes legacy DXT2/4, but DX10's extra alpha mode is not normalized.
			if let dds::header::Header::Dx10(header) = decoder.header() {
				if !matches!(
					header.alpha_mode,
					dds::header::AlphaMode::Unknown | dds::header::AlphaMode::Straight
				) {
					return Err("Unsupported DX10 alpha mode".into());
				}
			}
			let mut rgba = vec![0; length as usize];
			let view = dds::ImageViewMut::new(&mut rgba, size, dds::ColorFormat::RGBA_U8)
				.ok_or("Invalid DDS dimensions")?;
			decoder.read_surface(view).map_err(|e| e.to_string())?;
			Rc::new(AssetImage {
				width: size.width,
				height: size.height,
				rgba,
				encoded_size: bytes.len(),
			})
		} else {
			let mut reader = ImageReader::with_format(Cursor::new(&bytes), format);
			let mut limits = Limits::default();
			limits.max_image_width = Some(8192);
			limits.max_image_height = Some(8192);
			limits.max_alloc = Some(MAX_DECODED as u64);
			reader.limits(limits);
			let decoded = reader.decode().map_err(|e| e.to_string())?;
			if u64::from(decoded.width()) * u64::from(decoded.height()) * 4 > MAX_DECODED as u64 {
				return Err("RGBA image exceeds limit".into());
			}
			Rc::new(AssetImage {
				width: decoded.width(),
				height: decoded.height(),
				rgba: decoded.to_rgba8().into_raw(),
				encoded_size: bytes.len(),
			})
		};
		// Eviction drops only the cache's references; existing AVM2 bitmaps own their pixels.
		if *self.bytes.borrow() + image.rgba.len() > MAX_CACHE
			|| self.cache.borrow().len() >= MAX_CACHE_ENTRIES
		{
			self.clear();
		}
		*self.bytes.borrow_mut() += image.rgba.len();
		self.cache.borrow_mut().insert(path, image.clone());
		Ok(image)
	}

	/// Drop decoded cache entries without invalidating already-created Flash bitmaps.
	pub fn clear(&self) {
		self.cache.borrow_mut().clear();
		*self.bytes.borrow_mut() = 0;
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use std::sync::atomic::{AtomicU64, Ordering};
	static NEXT: AtomicU64 = AtomicU64::new(0);

	struct Fixture(PathBuf);
	impl Fixture {
		fn new() -> Self {
			let path = std::env::temp_dir().join(format!(
				"pw-assets-{}-{}",
				std::process::id(),
				NEXT.fetch_add(1, Ordering::Relaxed)
			));
			std::fs::create_dir(&path).unwrap();
			std::fs::create_dir(path.join("Data")).unwrap();
			std::fs::write(path.join("Data/movie.swf"), []).unwrap();
			Self(path)
		}
		fn assets(&self) -> Assets {
			Assets::new(&self.0.join("Data"), &self.0.join("Data/movie.swf")).unwrap()
		}
	}
	impl Drop for Fixture {
		fn drop(&mut self) {
			let _ = std::fs::remove_dir_all(&self.0);
		}
	}

	#[test]
	fn game_paths_and_cache() {
		let fixture = Fixture::new();
		let path = fixture.0.join("Data/icon.png");
		image::RgbaImage::from_pixel(4, 8, image::Rgba([20, 40, 60, 127]))
			.save(&path)
			.unwrap();
		let assets = fixture.assets();
		let image = assets.load(":/icon.png").unwrap();
		assert_eq!((image.width, image.height), (4, 8));
		assert_eq!(&image.rgba[..4], &[20, 40, 60, 127]);
		for name in ["icon.png", ":icon.png", ":\\icon.png"] {
			assert!(Rc::ptr_eq(&image, &assets.load(name).unwrap()));
		}
		assets.clear();
		assert!(!Rc::ptr_eq(&image, &assets.load("icon.png").unwrap()));
		assert_eq!(image.rgba.len(), 128);
	}

	#[test]
	fn reject_escapes_and_unsupported_data() {
		let fixture = Fixture::new();
		std::fs::write(fixture.0.join("outside.png"), [1, 2, 3]).unwrap();
		std::fs::write(fixture.0.join("Data/broken.dds"), b"DDS ").unwrap();
		let assets = fixture.assets();
		for name in [
			"",
			":",
			"../outside.png",
			"https://host/image.png",
			"file:///etc/passwd",
			"/etc/passwd",
			"broken.dds",
		] {
			assert!(assets.load(name).is_err(), "{name}");
		}
		#[cfg(unix)]
		{
			std::os::unix::fs::symlink(
				fixture.0.join("outside.png"),
				fixture.0.join("Data/link.png"),
			)
			.unwrap();
			assert!(assets.load("link.png").is_err());
		}
	}

	#[test]
	fn uncompressed_bgra_with_alpha() {
		let fixture = Fixture::new();
		let mut header = [0_u32; 31];
		header[0] = 124;
		header[1] = 0x100f;
		header[2] = 1;
		header[3] = 2;
		header[4] = 8;
		header[18] = 32;
		header[19] = 0x41;
		header[21] = 32;
		header[22] = 0xff0000;
		header[23] = 0xff00;
		header[24] = 0xff;
		header[25] = 0xff000000;
		header[26] = 0x1000;
		let mut bytes = b"DDS ".to_vec();
		for value in header {
			bytes.extend(value.to_le_bytes());
		}
		bytes.extend([60, 40, 20, 127, 3, 2, 1, 0]);
		std::fs::write(fixture.0.join("Data/bgra.dds"), bytes).unwrap();
		let image = fixture.assets().load("bgra.dds").unwrap();
		assert_eq!((image.width, image.height), (2, 1));
		assert_eq!(image.rgba, [20, 40, 60, 127, 1, 2, 3, 0]);
	}

	#[test]
	fn huge_dds_dimensions_fail_without_overflow() {
		let fixture = Fixture::new();
		let mut header = [0_u32; 31];
		header[0] = 124;
		header[1] = 0x81007;
		header[2] = 0x80000000;
		header[3] = 0x80000000;
		header[18] = 32;
		header[19] = 4;
		header[20] = u32::from_le_bytes(*b"DXT1");
		header[26] = 0x1000;
		let mut bytes = b"DDS ".to_vec();
		for value in header {
			bytes.extend(value.to_le_bytes());
		}
		std::fs::write(fixture.0.join("Data/huge.dds"), bytes).unwrap();
		assert!(fixture.assets().load("huge.dds").is_err());
	}

	#[test]
	fn premultiplied_dx10_is_rejected_not_multiplied_twice() {
		let fixture = Fixture::new();
		let mut header = [0_u32; 31];
		header[0] = 124;
		header[1] = 0x100f;
		header[2] = 1;
		header[3] = 1;
		header[4] = 4;
		header[18] = 32;
		header[19] = 4;
		header[20] = u32::from_le_bytes(*b"DX10");
		header[26] = 0x1000;
		let mut bytes = b"DDS ".to_vec();
		for value in header {
			bytes.extend(value.to_le_bytes());
		}
		for value in [28_u32, 3, 0, 1, 2] {
			bytes.extend(value.to_le_bytes());
		}
		bytes.extend([20, 40, 60, 127]);
		std::fs::write(fixture.0.join("Data/premultiplied.dds"), bytes).unwrap();
		assert!(fixture.assets().load("premultiplied.dds").is_err());
	}

	#[test]
	fn small_images_cannot_grow_cache_metadata_without_bound() {
		let fixture = Fixture::new();
		let path = fixture.0.join("Data/source.png");
		image::RgbaImage::from_pixel(1, 1, image::Rgba([0, 0, 0, 0]))
			.save(&path)
			.unwrap();
		let assets = fixture.assets();
		for index in 0..513 {
			let name = format!("{index}.png");
			std::fs::hard_link(&path, fixture.0.join("Data").join(&name)).unwrap();
			assets.load(&name).unwrap();
		}
		assert!(assets.cache.borrow().len() <= 512);
	}

	#[test]
	fn dxt1_decode_and_alpha() {
		let fixture = Fixture::new();
		// Minimal 4x4 DXT1 mock: one red block, decoded by image, not a custom codec.
		let mut header = [0_u32; 31];
		header[0] = 124;
		header[1] = 0x81007;
		header[2] = 4;
		header[3] = 4;
		header[4] = 8;
		header[18] = 32;
		header[19] = 4;
		header[20] = u32::from_le_bytes(*b"DXT1");
		header[26] = 0x1000;
		let mut bytes = b"DDS ".to_vec();
		for value in header {
			bytes.extend(value.to_le_bytes());
		}
		bytes.extend([0, 248, 224, 7, 0, 0, 0, 0]);
		std::fs::write(fixture.0.join("Data/icon.dds"), bytes).unwrap();
		let image = fixture.assets().load("icon.dds").unwrap();
		assert_eq!((image.width, image.height), (4, 4));
		assert!(
			image
				.rgba
				.chunks_exact(4)
				.all(|pixel| pixel == [255, 0, 0, 255])
		);
	}
}
