//! Experimental native C ABI. No Rust references, AVM2 objects, or GL pointers escape.
//! The header documents pointer validity/ownership and single-threaded host lifetime.

#[path = "pw_runtime/mod.rs"]
mod runtime;

use serde_json::{Value, json};
use std::cell::RefCell;
use std::collections::HashMap;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};

const BAD_ARGUMENT: i32 = 1;
const BAD_HOST: i32 = 2;
const ERROR: i32 = 3;
const BUSY: i32 = 4;
const PANIC: i32 = 5;
static LAST_HOST: AtomicU64 = AtomicU64::new(0);
thread_local! { static HOSTS: RefCell<HashMap<u64, runtime::Host>> = RefCell::new(HashMap::new()); }

/// Owned, length-delimited bytes. Caller must release with this library, not libc.
#[repr(C)]
pub struct Buffer {
	pub data: *mut u8,
	pub len: usize,
}

/// C-owned frame metadata; its pixel allocation uses the same buffer-free function.
#[repr(C)]
pub struct Frame {
	pub width: u32,
	pub height: u32,
	pub stride: u32,
	pub rgba: Buffer,
}

/// Render one top-down straight-alpha RGBA frame without advancing the movie.
/// # Safety
/// Both outputs must be valid, initially unowned, writable, and non-aliasing.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pw_ruffle_render(
	host: u64,
	output: *mut Frame,
	diagnostic: *mut Buffer,
) -> i32 {
	if output.is_null() || diagnostic.is_null() {
		return BAD_ARGUMENT;
	}
	unsafe {
		*output = Frame {
			width: 0,
			height: 0,
			stride: 0,
			rgba: Buffer {
				data: std::ptr::null_mut(),
				len: 0,
			},
		};
		*diagnostic = Buffer {
			data: std::ptr::null_mut(),
			len: 0,
		};
	}
	boundary(|| {
		HOSTS.with(|hosts| {
			let Ok(mut hosts) = hosts.try_borrow_mut() else {
				return BUSY;
			};
			let Some(host) = hosts.get_mut(&host) else {
				return BAD_HOST;
			};
			match host.frame() {
				Ok(frame) => {
					let (width, height) = frame.dimensions();
					let bytes = frame.into_raw().into_boxed_slice();
					let len = bytes.len();
					unsafe {
						*output = Frame {
							width,
							height,
							stride: width * 4,
							rgba: Buffer {
								data: Box::into_raw(bytes) as *mut u8,
								len,
							},
						};
					}
					0
				}
				Err(error) => unsafe { response(diagnostic, Err(error)) },
			}
		})
	})
}

/// Convert panics to a status; foreign callers must then close the affected host.
fn boundary(action: impl FnOnce() -> i32) -> i32 {
	catch_unwind(AssertUnwindSafe(action)).unwrap_or(PANIC)
}

/// Read bounded JSON. The ABI caller guarantees pointer validity for `len` bytes.
unsafe fn parse(data: *const u8, len: usize) -> Result<Value, String> {
	if data.is_null() || len == 0 || len > 64 * 1024 {
		return Err("Invalid JSON input buffer".into());
	}
	let bytes = unsafe { std::slice::from_raw_parts(data, len) };
	serde_json::from_slice(bytes).map_err(|e| e.to_string())
}

/// Transfer fresh byte ownership; `output` is valid writable non-aliasing memory.
unsafe fn response(output: *mut Buffer, result: Result<Value, String>) -> i32 {
	let (status, json) = match result {
		Ok(value) => (0, value),
		Err(error) => (ERROR, json!({"error":error})),
	};
	let bytes = serde_json::to_vec(&json)
		.expect("Serializable response")
		.into_boxed_slice();
	let len = bytes.len();
	unsafe {
		*output = Buffer {
			data: Box::into_raw(bytes) as *mut u8,
			len,
		};
	}
	status
}

/// Exact protocol version; no host state is needed.
#[unsafe(no_mangle)]
pub extern "C" fn pw_ruffle_abi_version() -> u32 {
	1
}

/// Create a thread-confined host, leaving no live ID after failure.
/// # Safety
/// Inputs/outputs must satisfy the ownership, bounds, and aliasing contract in bridge.h.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pw_ruffle_open(
	config: *const u8,
	len: usize,
	output: *mut u64,
	diagnostic: *mut Buffer,
) -> i32 {
	if output.is_null() || diagnostic.is_null() {
		return BAD_ARGUMENT;
	}
	unsafe {
		*output = 0;
		*diagnostic = Buffer {
			data: std::ptr::null_mut(),
			len: 0,
		};
	}
	let status = boundary(|| {
		let result = (|| {
			let config = unsafe { parse(config, len) }?;
			let data = config["data"].as_str().ok_or("Missing Data path")?;
			let movie = config["movie"].as_str().ok_or("Missing movie path")?;
			HOSTS.with(|hosts| {
				let mut hosts = hosts
					.try_borrow_mut()
					.map_err(|_| "Host registry is busy")?;
				if hosts.len() >= 8 {
					return Err("Host limit exceeded".into());
				}
				let host = runtime::Host::new(Path::new(data), Path::new(movie))?;
				let id = LAST_HOST
					.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |id| id.checked_add(1))
					.map_err(|_| "Host IDs exhausted")?
					+ 1;
				hosts.insert(id, host);
				unsafe {
					*output = id;
				}
				Ok(json!({"abi":1}))
			})
		})();
		unsafe { response(diagnostic, result) }
	});
	if status != 0 {
		let id = unsafe { *output };
		if id != 0 {
			pw_ruffle_close(id);
		}
		unsafe {
			*output = 0;
		}
	}
	status
}

/// Invoke exactly once and return owned JSON; no retry-size API can duplicate effects.
/// # Safety
/// Request and response storage must satisfy bridge.h; response must be initially unowned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pw_ruffle_request(
	host: u64,
	input: *const u8,
	len: usize,
	output: *mut Buffer,
) -> i32 {
	if output.is_null() {
		return BAD_ARGUMENT;
	}
	unsafe {
		*output = Buffer {
			data: std::ptr::null_mut(),
			len: 0,
		};
	}
	boundary(|| {
		HOSTS.with(|hosts| {
			let Ok(mut hosts) = hosts.try_borrow_mut() else {
				return BUSY;
			};
			let Some(host) = hosts.get_mut(&host) else {
				return BAD_HOST;
			};
			let result = unsafe { parse(input, len) }.and_then(|request| host.request(&request));
			unsafe { response(output, result) }
		})
	})
}

/// Drop the complete host on its creating thread; never dereference a caller-owned ID.
#[unsafe(no_mangle)]
pub extern "C" fn pw_ruffle_close(host: u64) -> i32 {
	boundary(|| {
		HOSTS.with(|hosts| {
			let Ok(mut hosts) = hosts.try_borrow_mut() else {
				return BUSY;
			};
			if hosts.remove(&host).is_some() {
				0
			} else {
				BAD_HOST
			}
		})
	})
}

/// Return ownership to this allocator and zero the caller's buffer.
/// # Safety
/// The buffer must be valid and either cleared or an unmodified buffer from this library.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pw_ruffle_buffer_free(buffer: *mut Buffer) {
	if buffer.is_null() {
		return;
	}
	let buffer = unsafe { &mut *buffer };
	if !buffer.data.is_null() {
		unsafe {
			drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
				buffer.data,
				buffer.len,
			)));
		}
	}
	buffer.data = std::ptr::null_mut();
	buffer.len = 0;
}

#[cfg(test)]
mod tests {
	use super::*;
	#[test]
	fn buffers_and_boundary_validation() {
		let mut buffer = Buffer {
			data: std::ptr::null_mut(),
			len: 0,
		};
		assert_eq!(
			unsafe { response(&mut buffer, Ok(json!({"text":"hello"}))) },
			0
		);
		assert_eq!(
			unsafe { parse(buffer.data, buffer.len) }.unwrap(),
			json!({"text":"hello"})
		);
		unsafe {
			pw_ruffle_buffer_free(&mut buffer);
			pw_ruffle_buffer_free(&mut buffer);
		}
		assert!(buffer.data.is_null());
		assert_eq!(buffer.len, 0);
		assert!(unsafe { parse(std::ptr::null(), 1) }.is_err());
		assert!(unsafe { parse(b"{}".as_ptr(), 65537) }.is_err());
		assert_eq!(pw_ruffle_close(0), BAD_HOST);
		let mut frame = Frame {
			width: 99,
			height: 99,
			stride: 99,
			rgba: Buffer {
				data: std::ptr::null_mut(),
				len: 0,
			},
		};
		assert_eq!(
			unsafe { pw_ruffle_render(0, &mut frame, &mut buffer) },
			BAD_HOST
		);
		assert_eq!(
			(frame.width, frame.height, frame.stride, frame.rgba.len),
			(0, 0, 0, 0)
		);
		assert_eq!(
			unsafe { pw_ruffle_render(0, std::ptr::null_mut(), &mut buffer) },
			BAD_ARGUMENT
		);
		assert_eq!(boundary(|| panic!("controlled FFI boundary test")), PANIC);
	}
}
