//! Native OpenGL compatibility spike. No browser, Wine, or production game linkage.
//! Load the actual movie and report typed host calls and SWF-to-host FSCommands as JSON.

use ruffle_core::backend::navigator::{NullExecutor, NullNavigatorBackend};
use ruffle_core::external::{FsCommandProvider, Value};
use ruffle_core::limits::ExecutionLimit;
use ruffle_core::tag_utils::movie_from_path;
use ruffle_core::PlayerBuilder;
use ruffle_core::primeworld_loader::GameNavigator;
use ruffle_render_wgpu::{backend::WgpuRenderBackend, target::TextureTarget, wgpu};
use serde_json::{json, Value as Json};
use std::cell::RefCell;
use std::path::Path;
use std::rc::Rc;
use std::sync::{Arc, atomic::{AtomicUsize, Ordering}};
use std::time::Duration;
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt, Layer};

/// Count runtime errors even when ActionScript event dispatch catches the exception.
struct RuntimeErrors(Arc<AtomicUsize>);
impl<S: tracing::Subscriber> Layer<S> for RuntimeErrors {
	fn on_event(&self, event: &tracing::Event<'_>, _context: tracing_subscriber::layer::Context<'_, S>) {
		if *event.metadata().level() == tracing::Level::ERROR {
			self.0.fetch_add(1, Ordering::Relaxed);
		}
	}
}

/// Keep callback data owned by Rust; no AVM2 atoms or GC references cross the host boundary.
struct Commands(Rc<RefCell<Vec<(String, String)>>>);
impl FsCommandProvider for Commands {
	fn on_fs_command(&self, command: &str, args: &str) -> bool {
		self.0.borrow_mut().push((command.to_string(), args.to_string()));
		true
	}
}

/// Preserve JSON scalar/list values through Ruffle's existing ExternalValue conversion.
fn argument(value: &Json) -> Value {
	match value {
		Json::Null => Value::Null,
		Json::Bool(v) => Value::Bool(*v),
		Json::Number(v) => Value::Number(v.as_f64().expect("JSON number")),
		Json::String(v) => Value::String(v.clone()),
		Json::Array(v) => Value::List(v.iter().map(argument).collect()),
		Json::Object(v) => Value::Object(v.iter().map(|(key, value)| (key.clone(), argument(value))).collect()),
	}
}

/// Compare actual scalar return values rather than counting non-throwing calls as parity.
fn scalar(value: &Value) -> (&'static str, Json) {
	match value {
		Value::Undefined => ("undefined", Json::Null),
		Value::Null => ("null", Json::Null),
		Value::Bool(value) => ("boolean", json!(value)),
		Value::Number(value) => ("number", json!(value)),
		Value::String(value) => ("string", json!(value)),
		_ => ("object", Json::Null),
	}
}

/// Match fixture values independently of the AVM2 Number representation.
fn scalar_matches(value: &Value, expected: &Json) -> bool {
	match value {
		Value::Number(actual) => expected.as_f64().is_some_and(|expected| expected == *actual),
		_ => scalar(value).1 == *expected,
	}
}

/// Run finite frames; fail on mismatched assertions, runtime errors, or a blank frame.
fn main() -> Result<(), Box<dyn std::error::Error>> {
	let args: Vec<_> = std::env::args().collect();
	if args.len() != 4 && args.len() != 5 {
		return Err("Usage: primeworld MOVIE.swf CALLS.json FRAME.png [DATA_ROOT]".into());
	}
	let errors = Arc::new(AtomicUsize::new(0));
	tracing_subscriber::registry().with(tracing_subscriber::filter::LevelFilter::INFO)
		.with(tracing_subscriber::fmt::layer().with_ansi(false).with_writer(std::io::stderr))
		.with(RuntimeErrors(errors.clone())).init();
	let movie_path = Path::new(&args[1]);
	let calls: Json = serde_json::from_slice(&std::fs::read(&args[2])?)?;
	let movie = movie_from_path(movie_path, None)?;
	let commands = Rc::new(RefCell::new(Vec::new()));
	let mut executor = NullExecutor::new();
	let renderer = WgpuRenderBackend::<TextureTarget>::for_offscreen(
		(1280, 720), wgpu::Backends::GL, wgpu::PowerPreference::HighPerformance)?;
	let builder = PlayerBuilder::new()
		.with_movie(movie)
		.with_renderer(renderer)
		.with_viewport_dimensions(1280, 720, 1.0)
		.with_fs_commands(Box::new(Commands(commands.clone())))
		.with_max_execution_duration(Duration::from_secs(10));
	let builder = if let Some(data) = args.get(4) {
		builder.with_navigator(GameNavigator::new(Path::new(data), movie_path, &executor)?)
	} else {
		builder.with_navigator(NullNavigatorBackend::with_base_path(movie_path.parent().unwrap(), &executor)?)
	};
	let player_handle = builder.build();
	{
		let mut player = player_handle.lock().unwrap();
		player.preload(&mut ExecutionLimit::none());
		player.run_frame();
	}
	let mut failures = 0;
	for call in calls.as_array().ok_or("Calls must be a JSON array")? {
		let frames = call.get("frames_before").map_or(Ok(0), |value| value.as_u64().ok_or("Invalid frame count"))?;
		if frames > 120 { return Err("Frame count exceeds probe limit".into()); }
		for _ in 0..frames {
			executor.run();
			player_handle.lock().unwrap().run_frame();
		}
		let path = call["path"].as_str().ok_or("Missing interface path")?;
		let method = call["method"].as_str().ok_or("Missing method")?;
		let arguments = call["args"].as_array().ok_or("Arguments must be an array")?.iter().map(argument).collect();
		let operation = call["op"].as_str().unwrap_or("call");
		let result = player_handle.lock().unwrap().pw_invoke(path, operation, method, arguments);
		let passed = match &result {
			Ok(value) => {
				let (kind, _) = scalar(value);
				call.get("expect_error").is_none()
					&& call.get("expect").is_none_or(|expected| scalar_matches(value, expected))
					&& call.get("expect_type").is_none_or(|expected| expected.as_str() == Some(kind))
			},
			Err(error) => call["expect_error"].as_str().is_some_and(|expected| error.contains(expected)),
		};
		if !passed { failures += 1; }
		println!("{}", json!({"path": path, "method": method, "op": operation,
			"passed": passed, "ok": result.is_ok(), "result": format!("{result:?}")}));
	}
	// Asset futures lock the Player themselves; never pump them while holding its mutex.
	for _ in 0..20 {
		executor.run();
		player_handle.lock().unwrap().run_frame();
	}
	let mut player = player_handle.lock().unwrap();
	player.render();
	let renderer = <dyn std::any::Any>::downcast_mut::<WgpuRenderBackend<TextureTarget>>(player.renderer_mut())
		.ok_or("Unexpected render backend")?;
	let frame = renderer.capture_frame().ok_or("OpenGL capture failed")?;
	let background = frame.get_pixel(0, 0);
	let different_pixels = frame.pixels().filter(|pixel| *pixel != background).count();
	frame.save(&args[3])?;
	println!("{}", json!({"fscommands": *commands.borrow(), "call_failures": failures,
		"runtime_errors": errors.load(Ordering::Relaxed),
		"frame": args[3], "dimensions": [frame.width(), frame.height()],
		"non_background_pixels": different_pixels, "game_integration_verified": false}));
	if different_pixels == 0 { return Err("Blank OpenGL frame".into()); }
	if failures != 0 { return Err("Host calls failed".into()); }
	if errors.load(Ordering::Relaxed) != 0 { return Err("Runtime errors occurred; inspect stderr".into()); }
	Ok(())
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn fixture_numbers_match_avm2_numbers() {
		assert!(scalar_matches(&Value::Number(1920.0), &json!(1920)));
		assert!(scalar_matches(&Value::Number(4294967295.0), &json!(4294967295_u32)));
		assert!(!scalar_matches(&Value::Number(1920.0), &json!(1080)));
		assert!(!scalar_matches(&Value::Number(1.0), &json!(true)));
	}

	#[test]
	fn runtime_errors_are_separate_from_call_results() {
		let errors = Arc::new(AtomicUsize::new(0));
		let subscriber = tracing_subscriber::registry().with(RuntimeErrors(errors.clone()));
		tracing::subscriber::with_default(subscriber, || {
			tracing::warn!("Unsupported feature");
			tracing::error!("Caught event-dispatch exception");
		});
		assert_eq!(errors.load(Ordering::Relaxed), 1);
	}
}
