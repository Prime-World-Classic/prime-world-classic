//! Shared native host used by the live handle test and experimental C ABI.

mod input;
mod input_state;

use ruffle_core::backend::navigator::NullExecutor;
use ruffle_core::external::{FsCommandProvider, Value};
use ruffle_core::limits::ExecutionLimit;
use ruffle_core::primeworld_loader::GameNavigator;
use ruffle_core::pw_handles::ObjectHandleStore;
use ruffle_core::tag_utils::movie_from_path;
use ruffle_core::{HostValue, Player, PlayerBuilder, ViewportDimensions};
use ruffle_render_wgpu::{backend::WgpuRenderBackend, target::TextureTarget, wgpu};
use serde_json::{Value as Json, json};
use std::cell::RefCell;
use std::path::Path;
use std::rc::Rc;
use std::sync::{
	Arc, Mutex,
	atomic::{AtomicUsize, Ordering},
};
use tracing_subscriber::{Layer, layer::SubscriberExt};

struct Errors(Arc<AtomicUsize>);
impl<S: tracing::Subscriber> Layer<S> for Errors {
	fn on_event(&self, event: &tracing::Event<'_>, _: tracing_subscriber::layer::Context<'_, S>) {
		if *event.metadata().level() == tracing::Level::ERROR {
			self.0.fetch_add(1, Ordering::Relaxed);
		}
	}
}

struct Commands(Rc<RefCell<Vec<(String, String)>>>);
impl FsCommandProvider for Commands {
	fn on_fs_command(&self, command: &str, argument: &str) -> bool {
		let mut queue = self.0.borrow_mut();
		if queue.len() >= 1024 {
			tracing::error!("FSCommand queue limit exceeded");
			return false;
		}
		queue.push((command.to_owned(), argument.to_owned()));
		true
	}
}

/// Decode data values using Ruffle's existing conversion; handles have an explicit tag.
fn value(input: &Json) -> Value {
	match input {
		Json::Null => Value::Null,
		Json::Bool(v) => Value::Bool(*v),
		Json::Number(v) => Value::Number(v.as_f64().unwrap()),
		Json::String(v) => Value::String(v.clone()),
		Json::Array(v) => Value::List(v.iter().map(value).collect()),
		Json::Object(v) => Value::Object(v.iter().map(|(k, v)| (k.clone(), value(v))).collect()),
	}
}

/// IDs are decimal strings so no caller can round a u64 through JSON/AVM2 doubles.
fn id(value: &Json) -> Result<u64, String> {
	value
		.as_str()
		.ok_or("Handle must be a decimal string")?
		.parse()
		.map_err(|_| "Invalid handle".into())
}

/// Bound both texture dimensions and total readback allocation before touching the GPU.
fn dimensions(request: &Json) -> Result<(u32, u32), String> {
	let width = request["width"]
		.as_u64()
		.filter(|n| (1..=4096).contains(n))
		.ok_or("Invalid surface width")?;
	let height = request["height"]
		.as_u64()
		.filter(|n| (1..=4096).contains(n))
		.ok_or("Invalid surface height")?;
	if width * height > 8 * 1024 * 1024 {
		return Err("Surface exceeds 32 MiB RGBA limit".into());
	}
	Ok((width as u32, height as u32))
}

/// Millisecond time slices are finite and bounded; callers subdivide long pauses.
fn delta_ms(request: &Json) -> Result<f64, String> {
	request["delta_ms"]
		.as_f64()
		.filter(|dt| dt.is_finite() && (0.0..=250.0).contains(dt))
		.ok_or("delta_ms must be between 0 and 250".into())
}

/// Thread-confined Player and its host-owned roots, callbacks, and diagnostic counter.
pub struct Host {
	input: input_state::InputState,
	handles: ObjectHandleStore,
	player: Arc<Mutex<Player>>,
	executor: NullExecutor,
	commands: Rc<RefCell<Vec<(String, String)>>>,
	errors: Arc<AtomicUsize>,
	dispatch: tracing::Dispatch,
}

impl Host {
	/// Construct an isolated 1280x720 native GL player; fail on constructor errors.
	pub fn new(data: &Path, movie: &Path) -> Result<Self, String> {
		let errors = Arc::new(AtomicUsize::new(0));
		let dispatch = tracing::Dispatch::new(
			tracing_subscriber::registry()
				.with(tracing_subscriber::filter::LevelFilter::INFO)
				.with(
					tracing_subscriber::fmt::layer()
						.with_ansi(false)
						.with_writer(std::io::stderr),
				)
				.with(Errors(errors.clone())),
		);
		tracing::dispatcher::with_default(&dispatch.clone(), || {
			let executor = NullExecutor::new();
			let navigator = GameNavigator::new(data, movie, &executor)?;
			let movie = movie_from_path(movie, None).map_err(|e| e.to_string())?;
			let renderer = WgpuRenderBackend::<TextureTarget>::for_offscreen(
				(1280, 720),
				wgpu::Backends::GL,
				wgpu::PowerPreference::HighPerformance,
			)
			.map_err(|e| e.to_string())?;
			let commands = Rc::new(RefCell::new(Vec::new()));
			let player = PlayerBuilder::new()
				.with_autoplay(true)
				.with_movie(movie)
				.with_renderer(renderer)
				.with_navigator(navigator)
				.with_fs_commands(Box::new(Commands(commands.clone())))
				.with_viewport_dimensions(1280, 720, 1.0)
				.with_max_execution_duration(std::time::Duration::from_secs(5))
				.build();
			let handles = {
				let mut locked = player.lock().map_err(|e| e.to_string())?;
				locked.preload(&mut ExecutionLimit::none());
				locked.run_frame();
				locked.mutate_with_update_context(|context| {
					ObjectHandleStore::new(context.gc(), context.dynamic_root, 1024)
				})
			};
			if errors.load(Ordering::Relaxed) != 0 {
				return Err("SWF construction reported runtime errors".into());
			}
			Ok(Self {
				input: input_state::InputState::default(),
				handles,
				player,
				executor,
				commands,
				errors,
				dispatch,
			})
		})
	}

	/// Execute once; callbacks are queued for polling instead of reentering the host.
	pub fn request(&mut self, request: &Json) -> Result<Json, String> {
		let dispatch = self.dispatch.clone();
		tracing::dispatcher::with_default(&dispatch, || self.execute(request))
	}

	/// Return tightly packed, top-down straight-alpha RGBA owned by the caller.
	/// Ruffle's capture_frame removes GPU row padding and unmultiples alpha.
	pub fn frame(&mut self) -> Result<image::RgbaImage, String> {
		let dispatch = self.dispatch.clone();
		tracing::dispatcher::with_default(&dispatch, || {
			let mut player = self.player.lock().map_err(|e| e.to_string())?;
			player.render();
			let renderer = <dyn std::any::Any>::downcast_mut::<WgpuRenderBackend<TextureTarget>>(
				player.renderer_mut(),
			)
			.ok_or("Wrong renderer")?;
			renderer.capture_frame().ok_or("Capture failed".into())
		})
	}

	/// Full native pixel replacement with no JSON/base64 copies or AVM2 pointers.
	pub fn upload_bitmap(
		&mut self,
		id: u64,
		width: u32,
		height: u32,
		rgba: &[u8],
	) -> Result<(), String> {
		self.player
			.lock()
			.map_err(|e| e.to_string())?
			.pw_upload_bitmap(&self.handles, id, width, height, rgba)
	}

	fn execute(&mut self, request: &Json) -> Result<Json, String> {
		match request["action"].as_str().unwrap_or("invoke") {
			"bitmap_create" => {
				let (width, height) = dimensions(request)?;
				let handle = self
					.player
					.lock()
					.map_err(|e| e.to_string())?
					.pw_create_bitmap(&mut self.handles, width, height)?;
				Ok(json!({"type":"handle","id":handle.to_string()}))
			}
			"input" => {
				let events = input::events(&request["event"])?;
				let count = events.len();
				let mut player = self.player.lock().map_err(|e| e.to_string())?;
				let mut handled = false;
				for event in events {
					for event in self.input.expand(event)? {
						handled |= player.handle_event(event);
					}
				}
				Ok(json!({"events":count,"handled":handled}))
			}
			"tick" => {
				let dt = delta_ms(request)?;
				self.executor.run();
				self.player.lock().map_err(|e| e.to_string())?.tick(dt);
				self.executor.run();
				Ok(json!({"delta_ms":dt}))
			}
			"surface" => {
				let (width, height) = dimensions(request)?;
				let transparent = request["transparent"]
					.as_bool()
					.ok_or("Missing transparent boolean")?;
				let mut player = self.player.lock().map_err(|e| e.to_string())?;
				player.set_window_mode(if transparent { "transparent" } else { "opaque" });
				player.set_viewport_dimensions(ViewportDimensions {
					width,
					height,
					scale_factor: 1.0,
				});
				Ok(json!({"width":width,"height":height,"transparent":transparent}))
			}
			"invoke" => {
				let receiver = request.get("receiver").map(id).transpose()?;
				let path = request["path"].as_str().unwrap_or("");
				let op = request["op"].as_str().unwrap_or("call");
				let method = request["method"].as_str().ok_or("Missing method")?;
				let args = request["args"]
					.as_array()
					.ok_or("Arguments must be an array")?;
				if args.len() > 64 {
					return Err("Argument limit exceeded".into());
				}
				let args = args
					.iter()
					.map(|argument| {
						if let Some(handle) = argument.get("$handle") {
							Ok(HostValue::Handle(id(handle)?))
						} else {
							Ok(HostValue::Scalar(value(argument)))
						}
					})
					.collect::<Result<_, String>>()?;
				let result = self
					.player
					.lock()
					.map_err(|e| e.to_string())?
					.pw_invoke_owned(&mut self.handles, receiver, path, op, method, args)?;
				Ok(match result {
					HostValue::Handle(handle) => json!({"type":"handle", "id":handle.to_string()}),
					HostValue::Scalar(Value::Undefined) => json!({"type":"undefined"}),
					HostValue::Scalar(Value::Null) => json!({"type":"null"}),
					HostValue::Scalar(Value::Bool(v)) => json!({"type":"boolean", "value":v}),
					HostValue::Scalar(Value::Number(v)) if v.is_finite() => {
						json!({"type":"number", "value":v})
					}
					HostValue::Scalar(Value::String(v)) => json!({"type":"string", "value":v}),
					_ => return Err("Unsupported return value".into()),
				})
			}
			"release" => {
				let handle = id(&request["id"])?;
				self.player
					.lock()
					.map_err(|e| e.to_string())?
					.mutate_with_update_context(|context| {
						self.handles
							.release(context.dynamic_root, handle)
							.map_err(|e| e.to_string())
					})?;
				Ok(json!({"released":true}))
			}
			"clear" => {
				self.handles.clear();
				Ok(json!({"cleared":true}))
			}
			"events" => Ok(json!(std::mem::take(&mut *self.commands.borrow_mut()))),
			"step" => {
				let frames = request["frames"]
					.as_u64()
					.filter(|n| *n <= 120)
					.ok_or("Invalid frame count")?;
				for _ in 0..frames {
					self.executor.run();
					self.player.lock().map_err(|e| e.to_string())?.run_frame();
				}
				Ok(json!({"frames":frames}))
			}
			// Inspect VM focus by display type, not a class-name string or guessed UI path.
			"focus_state" => {
				let text = self.player.lock().map_err(|e| e.to_string())?
					.mutate_with_update_context(|context| context.focus_tracker.get_as_edit_text().is_some());
				Ok(json!({"text": text}))
			}
			"stats" => Ok(
				json!({"handles":self.handles.len(), "runtime_errors":self.errors.load(Ordering::Relaxed),
					"primeworld_mouse_events":cfg!(feature = "primeworld_mouse_events")}),
			),
			"capture" => {
				let output = request["path"].as_str().ok_or("Missing output path")?;
				let frame = self.frame()?;
				let first = frame.get_pixel(0, 0);
				let pixels = frame.pixels().filter(|pixel| *pixel != first).count();
				if pixels == 0 {
					return Err("Blank framebuffer".into());
				}
				frame.save(output).map_err(|e| e.to_string())?;
				Ok(
					json!({"width":frame.width(), "height":frame.height(), "non_background_pixels":pixels}),
				)
			}
			_ => Err("Unknown host action".into()),
		}
	}
}

#[cfg(test)]
mod surface_tests {
	use super::*;
	#[test]
	fn reject_invalid_time_slices() {
		for dt in [json!(null), json!("16"), json!(-1), json!(251)] {
			assert!(delta_ms(&json!({"delta_ms":dt})).is_err());
		}
		for dt in [0.0, 16.25, 250.0] {
			assert_eq!(delta_ms(&json!({"delta_ms":dt})).unwrap(), dt);
		}
	}
	#[test]
	fn reject_invalid_or_oversized_surfaces() {
		for request in [
			json!({}),
			json!({"width":0,"height":1}),
			json!({"width":1.5,"height":1}),
			json!({"width":-1,"height":10}),
			json!({"width":4097,"height":1}),
			json!({"width":4096,"height":4096}),
		] {
			assert!(dimensions(&request).is_err());
		}
		for (w, h) in [(1, 1), (257, 193), (1280, 720), (3840, 2160)] {
			assert_eq!(dimensions(&json!({"width":w,"height":h})).unwrap(), (w, h));
		}
	}
}
