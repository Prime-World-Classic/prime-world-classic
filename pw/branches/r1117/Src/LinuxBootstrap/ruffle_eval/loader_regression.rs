//! Executable native Loader regressions for the pinned Ruffle revision.
//!
//! Stage as `core/src/avm2/loader_regression.rs`; `host.patch`
//! declares this test-only module. Run `cargo test --locked -p ruffle_core
//! --lib avm2::loader_regression`. These tests require no GPU or external AS3
//! compiler: Ruffle's SWF writer builds the movie and actual bytecode listeners.

use crate::avm2::method::MethodAssociation;
use crate::avm2::object::{FunctionObject, ScriptObject, TObject};
use crate::avm2::scope::ScopeChain;
use crate::avm2::script::TranslationUnit;
use crate::avm2::{Activation, FunctionArgs, Object, QName, Value};
use crate::backend::navigator::NullExecutor;
use crate::primeworld_loader::GameNavigator;
use crate::string::AvmString;
use crate::tag_utils::SwfMovie;
use crate::{Player, PlayerBuilder};
use std::any::Any;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use swf::avm2::types::{self as abc, Index, Op};

static NEXT_FIXTURE: AtomicU64 = AtomicU64::new(0);

/// A private local asset directory and a headless, initialized AVM2 Player.
struct Fixture {
	player: Arc<Mutex<Player>>,
	_executor: NullExecutor,
	path: PathBuf,
}

impl Fixture {
	fn new() -> Self {
		let path = std::env::temp_dir().join(format!(
			"pw-loader-regression-{}-{}",
			std::process::id(),
			NEXT_FIXTURE.fetch_add(1, Ordering::Relaxed)
		));
		std::fs::create_dir(&path).unwrap();
		let mut movie_bytes = Vec::new();
		let mut header = swf::Header::default_with_swf_version(19);
		header.num_frames = 1;
		swf::write_swf(
			&header,
			&[
				swf::Tag::FileAttributes(swf::FileAttributes::IS_ACTION_SCRIPT_3),
				swf::Tag::ShowFrame,
				swf::Tag::End,
			],
			&mut movie_bytes,
		)
		.unwrap();
		let movie_path = path.join("fixture.swf");
		std::fs::write(&movie_path, &movie_bytes).unwrap();
		for (name, width) in [("first.png", 2), ("second.png", 5)] {
			image::RgbaImage::from_pixel(width, 3, image::Rgba([40, 80, 120, 255]))
				.save(path.join(name))
				.unwrap();
		}
		std::fs::write(path.join("broken.dds"), b"DDS ").unwrap();
		let executor = NullExecutor::new();
		let navigator = GameNavigator::new(&path, &movie_path, &executor).unwrap();
		let movie = SwfMovie::from_data(&movie_bytes, "file:///fixture.swf".into(), None).unwrap();
		let player = PlayerBuilder::new()
			.with_movie(movie)
			.with_navigator(navigator)
			.build();
		Self {
			player,
			_executor: executor,
			path,
		}
	}

	/// Keep GC objects inside a genuine Player mutation, including callback execution.
	fn run(&self, test: impl for<'gc> FnOnce(&mut Activation<'_, 'gc>)) {
		self.player
			.lock()
			.unwrap()
			.mutate_with_update_context(|context| {
				let domain = context.avm2.stage_domain();
				let mut activation = Activation::from_domain(context, domain);
				test(&mut activation);
				let navigator = (activation.context.navigator as &dyn Any)
					.downcast_ref::<GameNavigator>()
					.unwrap();
				assert_eq!(navigator.active_operations(), 0, "leaked in-flight roots");
			});
	}
}

impl Drop for Fixture {
	fn drop(&mut self) {
		let _ = std::fs::remove_dir_all(&self.path);
	}
}

/// Call an actual public AVM2 method, including its native Loader hook.
fn call<'gc>(
	activation: &mut Activation<'_, 'gc>,
	object: Object<'gc>,
	name: &str,
	args: &[Value<'gc>],
) -> Value<'gc> {
	let name = AvmString::new_utf8(activation.gc(), name);
	Value::from(object)
		.call_public_property(name, FunctionArgs::from_slice(args), activation)
		.unwrap()
}

/// Read through the public getter rather than inspecting LoaderInfo internals.
fn get<'gc>(activation: &mut Activation<'_, 'gc>, object: Object<'gc>, name: &str) -> Value<'gc> {
	let name = AvmString::new_utf8(activation.gc(), name);
	Value::from(object)
		.get_public_property(name, activation)
		.unwrap()
}

/// Initialize a dynamic test-state property used by the bytecode listeners.
fn set<'gc>(
	activation: &mut Activation<'_, 'gc>,
	object: Object<'gc>,
	name: &str,
	value: Value<'gc>,
) {
	let name = AvmString::new_utf8(activation.gc(), name);
	object.set_dynamic_property(name, value, activation.gc());
}

/// Construct a normal builtin using its package-qualified class definition.
fn construct<'gc>(
	activation: &mut Activation<'_, 'gc>,
	class: &str,
	args: &[Value<'gc>],
) -> Object<'gc> {
	let name = AvmString::new_utf8(activation.gc(), class);
	let name = QName::from_qualified_name(name, activation.context);
	let class = activation
		.avm2()
		.stage_domain()
		.get_defined_value(activation, name)
		.unwrap();
	class
		.as_object()
		.unwrap()
		.as_class_object()
		.unwrap()
		.construct(activation, args)
		.unwrap()
		.as_object()
		.unwrap()
}

fn request<'gc>(activation: &mut Activation<'_, 'gc>, path: &str) -> Object<'gc> {
	let path = AvmString::new_utf8(activation.gc(), path);
	construct(activation, "flash.net.URLRequest", &[path.into()])
}

fn load<'gc>(activation: &mut Activation<'_, 'gc>, loader: Object<'gc>, path: &str) {
	let request = request(activation, path);
	call(activation, loader, "load", &[request.into()]);
}

/// Bytecode actions run only on the listener's first invocation. Later invocations
/// are still counted, making recursion regressions fail without overflowing a stack.
#[derive(Clone, Copy)]
enum Action {
	Count,
	Domain,
	Unload,
	Load,
}

/// Serialize instructions with Ruffle's existing ABC writer, not hand-coded bytes.
fn encode(ops: &[Op]) -> Vec<u8> {
	let mut bytes = Vec::new();
	let mut writer = swf::avm2::write::Writer::new(&mut bytes);
	for op in ops {
		writer.write_op(op).unwrap();
	}
	bytes
}

/// Install an actual AVM2 Function as an event listener, bound to test state.
fn listen<'gc>(
	activation: &mut Activation<'_, 'gc>,
	target: Object<'gc>,
	state: Object<'gc>,
	event: &str,
	counter: &str,
	action: Action,
) {
	let names = [
		counter,
		"loader",
		"request",
		"load",
		"unload",
		"target",
		"applicationDomain",
		"observedDomain",
	];
	let property = |index| Op::GetProperty {
		index: Index::new(index),
	};
	let body = match action {
		Action::Count => Vec::new(),
		Action::Domain => vec![
			Op::GetLocal { index: 0 },
			Op::GetLocal { index: 1 },
			property(6),
			property(7),
			Op::SetProperty {
				index: Index::new(8),
			},
		],
		Action::Unload => vec![
			Op::GetLocal { index: 0 },
			property(2),
			Op::CallPropVoid {
				index: Index::new(5),
				num_args: 0,
			},
		],
		Action::Load => vec![
			Op::GetLocal { index: 0 },
			property(2),
			Op::GetLocal { index: 0 },
			property(3),
			Op::CallPropVoid {
				index: Index::new(4),
				num_args: 1,
			},
		],
	};
	let tail = encode(&body);
	let mut code = encode(&[
		Op::GetLocal { index: 0 },
		Op::Dup,
		property(1),
		Op::Increment,
		Op::SetProperty {
			index: Index::new(1),
		},
		Op::GetLocal { index: 0 },
		property(1),
		Op::PushByte { value: 1 },
		Op::IfGt {
			offset: tail.len() as i32,
		},
	]);
	code.extend(tail);
	code.extend(encode(&[Op::ReturnVoid]));
	let abc = abc::AbcFile {
		major_version: 46,
		minor_version: 16,
		constant_pool: abc::ConstantPool {
			ints: Vec::new(),
			uints: Vec::new(),
			doubles: Vec::new(),
			strings: names.iter().map(|name| name.as_bytes().to_vec()).collect(),
			namespaces: vec![abc::Namespace::Package(Index::new(0))],
			namespace_sets: Vec::new(),
			multinames: (1..=names.len())
				.map(|index| abc::Multiname::QName {
					namespace: Index::new(1),
					name: Index::new(index as u32),
				})
				.collect(),
		},
		methods: vec![abc::Method {
			name: Index::new(0),
			params: vec![abc::MethodParam {
				name: None,
				kind: Index::new(0),
				default_value: None,
			}],
			return_type: Index::new(0),
			flags: abc::MethodFlags::empty(),
			body: Some(Index::new(0)),
		}],
		metadata: Vec::new(),
		instances: Vec::new(),
		classes: Vec::new(),
		scripts: Vec::new(),
		method_bodies: vec![abc::MethodBody {
			method: Index::new(0),
			max_stack: 4,
			num_locals: 2,
			init_scope_depth: 0,
			max_scope_depth: 0,
			code,
			exceptions: Vec::new(),
			traits: Vec::new(),
		}],
	};
	let domain = activation.avm2().stage_domain();
	let unit = TranslationUnit::from_abc(
		abc,
		domain,
		None,
		activation.context.root_swf.clone(),
		activation.gc(),
	);
	let method = unit.load_method(Index::new(0), true, activation).unwrap();
	method
		.associate(activation, MethodAssociation::freestanding())
		.unwrap();
	let function = FunctionObject::from_method(
		activation.context,
		method,
		ScopeChain::new(domain),
		Some(state.into()),
		None,
	);
	set(activation, state, counter, 0.into());
	let event = AvmString::new_utf8(activation.gc(), event);
	call(
		activation,
		target,
		"addEventListener",
		&[event.into(), function.into()],
	);
}

/// Loader, LoaderInfo and callback state, all local to the active GC mutation.
fn setup<'gc>(activation: &mut Activation<'_, 'gc>) -> (Object<'gc>, Object<'gc>, Object<'gc>) {
	let loader = construct(activation, "flash.display.Loader", &[]);
	let info = get(activation, loader, "contentLoaderInfo")
		.as_object()
		.unwrap();
	let state = ScriptObject::new_object(activation.context);
	set(activation, state, "loader", loader.into());
	let request = request(activation, "second.png");
	set(activation, state, "request", request.into());
	(loader, info, state)
}

fn number<'gc>(activation: &mut Activation<'_, 'gc>, object: Object<'gc>, name: &str) -> f64 {
	get(activation, object, name)
		.coerce_to_number(activation)
		.unwrap()
}

#[test]
fn domain_getter_is_available_inside_init_and_honors_loader_context() {
	Fixture::new().run(|activation| {
		for explicit in [false, true] {
			let (loader, info, state) = setup(activation);
			listen(activation, info, state, "init", "inits", Action::Domain);
			listen(
				activation,
				info,
				state,
				"complete",
				"completes",
				Action::Count,
			);
			let request = request(activation, "first.png");
			let chosen = activation.avm2().stage_domain();
			if explicit {
				let domain = crate::avm2::object::DomainObject::from_domain(activation, chosen);
				let context = construct(
					activation,
					"flash.system.LoaderContext",
					&[false.into(), domain.into()],
				);
				call(
					activation,
					loader,
					"load",
					&[request.into(), context.into()],
				);
			} else {
				call(activation, loader, "load", &[request.into()]);
			}
			assert_eq!(number(activation, state, "inits"), 1.0);
			assert_eq!(number(activation, state, "completes"), 1.0);
			let observed = get(activation, state, "observedDomain")
				.as_object()
				.unwrap()
				.as_application_domain()
				.unwrap();
			if explicit {
				assert!(observed == chosen);
			} else {
				assert!(observed.parent_domain() == Some(chosen));
			}
		}
	});
}

#[test]
fn missing_and_corrupt_assets_dispatch_io_error_without_init_or_complete() {
	Fixture::new().run(|activation| {
		for path in ["missing.png", "broken.dds"] {
			let (loader, info, state) = setup(activation);
			listen(activation, info, state, "ioError", "errors", Action::Count);
			listen(activation, info, state, "init", "inits", Action::Count);
			listen(
				activation,
				info,
				state,
				"complete",
				"completes",
				Action::Count,
			);
			load(activation, loader, path);
			assert_eq!(number(activation, state, "errors"), 1.0);
			assert_eq!(number(activation, state, "inits"), 0.0);
			assert_eq!(number(activation, state, "completes"), 0.0);
			assert!(matches!(get(activation, loader, "content"), Value::Null));
			assert_eq!(number(activation, loader, "numChildren"), 0.0);
			load(activation, loader, "first.png");
			assert_eq!(number(activation, state, "inits"), 1.0);
			assert_eq!(number(activation, state, "completes"), 1.0);
		}
	});
}

#[test]
fn added_callback_unload_cancels_init_and_complete() {
	Fixture::new().run(|activation| {
		let (loader, info, state) = setup(activation);
		listen(activation, loader, state, "added", "added", Action::Unload);
		listen(activation, info, state, "init", "inits", Action::Count);
		listen(
			activation,
			info,
			state,
			"complete",
			"completes",
			Action::Count,
		);
		load(activation, loader, "first.png");
		assert_eq!(number(activation, state, "added"), 1.0);
		assert_eq!(number(activation, state, "inits"), 0.0);
		assert_eq!(number(activation, state, "completes"), 0.0);
		assert!(matches!(get(activation, loader, "content"), Value::Null));
		assert_eq!(number(activation, loader, "numChildren"), 0.0);
	});
}

#[test]
fn init_callback_unload_suppresses_complete() {
	Fixture::new().run(|activation| {
		let (loader, info, state) = setup(activation);
		listen(activation, info, state, "init", "inits", Action::Unload);
		listen(
			activation,
			info,
			state,
			"complete",
			"completes",
			Action::Count,
		);
		load(activation, loader, "first.png");
		assert_eq!(number(activation, state, "inits"), 1.0);
		assert_eq!(number(activation, state, "completes"), 0.0);
		assert!(matches!(get(activation, loader, "content"), Value::Null));
		assert_eq!(number(activation, loader, "numChildren"), 0.0);
	});
}

#[test]
fn removed_callback_load_supersedes_the_outer_load() {
	Fixture::new().run(|activation| {
		let (loader, info, state) = setup(activation);
		load(activation, loader, "first.png");
		listen(
			activation,
			loader,
			state,
			"removed",
			"removed",
			Action::Load,
		);
		listen(activation, info, state, "init", "inits", Action::Count);
		listen(
			activation,
			info,
			state,
			"complete",
			"completes",
			Action::Count,
		);
		load(activation, loader, "first.png");
		assert_eq!(number(activation, state, "removed"), 1.0);
		assert_eq!(number(activation, state, "inits"), 1.0);
		assert_eq!(number(activation, state, "completes"), 1.0);
		assert_eq!(number(activation, loader, "numChildren"), 1.0);
		let content = get(activation, loader, "content").as_object().unwrap();
		assert_eq!(number(activation, content, "width"), 5.0);
	});
}

#[test]
fn explicit_unload_guards_recursive_removed_callback() {
	Fixture::new().run(|activation| {
		let (loader, _, state) = setup(activation);
		load(activation, loader, "first.png");
		listen(
			activation,
			loader,
			state,
			"removed",
			"removed",
			Action::Unload,
		);
		call(activation, loader, "unload", &[]);
		assert_eq!(number(activation, state, "removed"), 1.0);
		assert!(matches!(get(activation, loader, "content"), Value::Null));
		assert_eq!(number(activation, loader, "numChildren"), 0.0);
		load(activation, loader, "second.png");
		assert_eq!(number(activation, loader, "numChildren"), 1.0);
	});
}

#[test]
fn io_error_callback_can_load_a_replacement_synchronously() {
	Fixture::new().run(|activation| {
		let (loader, info, state) = setup(activation);
		listen(activation, info, state, "ioError", "errors", Action::Load);
		listen(activation, info, state, "init", "inits", Action::Count);
		listen(
			activation,
			info,
			state,
			"complete",
			"completes",
			Action::Count,
		);
		load(activation, loader, "missing.png");
		assert_eq!(number(activation, state, "errors"), 1.0);
		assert_eq!(number(activation, state, "inits"), 1.0);
		assert_eq!(number(activation, state, "completes"), 1.0);
		let content = get(activation, loader, "content").as_object().unwrap();
		assert_eq!(number(activation, content, "width"), 5.0);
		assert_eq!(number(activation, loader, "numChildren"), 1.0);
	});
}

#[test]
fn removed_callback_replacement_can_be_unloaded_by_its_added_callback() {
	Fixture::new().run(|activation| {
		let (loader, info, state) = setup(activation);
		load(activation, loader, "first.png");
		listen(
			activation,
			loader,
			state,
			"removed",
			"removed",
			Action::Load,
		);
		listen(activation, loader, state, "added", "added", Action::Unload);
		listen(activation, info, state, "init", "inits", Action::Count);
		listen(
			activation,
			info,
			state,
			"complete",
			"completes",
			Action::Count,
		);
		load(activation, loader, "first.png");
		assert_eq!(number(activation, state, "removed"), 2.0);
		assert_eq!(number(activation, state, "added"), 1.0);
		assert_eq!(number(activation, state, "inits"), 0.0);
		assert_eq!(number(activation, state, "completes"), 0.0);
		assert!(matches!(get(activation, loader, "content"), Value::Null));
		assert_eq!(number(activation, loader, "numChildren"), 0.0);
	});
}

#[test]
fn added_and_init_callbacks_can_replace_the_active_load() {
	Fixture::new().run(|activation| {
		for event in ["added", "init"] {
			let (loader, info, state) = setup(activation);
			let target = if event == "added" { loader } else { info };
			listen(activation, target, state, event, "callbacks", Action::Load);
			listen(
				activation,
				info,
				state,
				"complete",
				"completes",
				Action::Count,
			);
			load(activation, loader, "first.png");
			assert_eq!(number(activation, state, "callbacks"), 2.0);
			assert_eq!(number(activation, state, "completes"), 1.0);
			assert_eq!(number(activation, loader, "numChildren"), 1.0);
			let content = get(activation, loader, "content").as_object().unwrap();
			assert_eq!(number(activation, content, "width"), 5.0);
		}
	});
}

#[test]
fn callback_loading_another_loader_does_not_cancel_this_loader() {
	Fixture::new().run(|activation| {
		let (loader, info, state) = setup(activation);
		let (other, _, _) = setup(activation);
		set(activation, state, "loader", other.into());
		listen(activation, loader, state, "added", "added", Action::Load);
		listen(activation, info, state, "init", "inits", Action::Count);
		listen(
			activation,
			info,
			state,
			"complete",
			"completes",
			Action::Count,
		);
		load(activation, loader, "first.png");
		assert_eq!(number(activation, state, "added"), 1.0);
		assert_eq!(number(activation, state, "inits"), 1.0);
		assert_eq!(number(activation, state, "completes"), 1.0);
		for (target, width) in [(loader, 2.0), (other, 5.0)] {
			let content = get(activation, target, "content").as_object().unwrap();
			assert_eq!(number(activation, content, "width"), width);
			assert_eq!(number(activation, target, "numChildren"), 1.0);
		}
	});
}
