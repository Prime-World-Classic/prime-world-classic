//! Opt-in Prime World Loader semantics. Stock Ruffle navigators retain stock behavior.

use crate::avm2::globals::slots::{
	flash_display_loader as loader_slots, flash_net_url_request as request_slots,
	flash_system_loader_context as context_slots,
};
use crate::avm2::object::{BitmapDataObject, EventObject, LoaderInfoObject, LoaderStream, TObject};
use crate::avm2::{Activation, Avm2, Domain, Error, Object};
use crate::backend::navigator::*;
use crate::bitmap::bitmap_data::{BitmapData, Color};
use crate::context::UpdateContext;
use crate::display_object::{DisplayObject, TDisplayObject};
use crate::loader::{ContentType, Error as LoadError};
use crate::prelude::TDisplayObjectContainer;
use crate::primeworld_assets::Assets;
use crate::socket::{ConnectionState, SocketAction, SocketHandle};
use crate::tag_utils::SwfMovie;
use async_channel::{Receiver, Sender};
use gc_arena::{DynamicRoot, DynamicRootSet, Gc, Rootable};
use indexmap::IndexMap;
use std::any::Any;
use std::cell::{Cell, RefCell};
use std::path::Path;
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;
use url::{ParseError, Url};

/// Native local-image host. Other fetches, navigation, and sockets fail closed.
pub struct GameNavigator {
	pub assets: Assets,
	spawner: NullSpawner,
	operations: Operations,
}

impl GameNavigator {
	/// Activate only for an explicitly configured game Data root and movie.
	pub fn new(data: &Path, movie: &Path, executor: &NullExecutor) -> Result<Self, String> {
		Ok(Self {
			assets: Assets::new(data, movie)?,
			spawner: executor.spawner(),
			operations: Rc::new(RefCell::new(Vec::new())),
		})
	}

	/// Verify that synchronous operations do not leave permanent Loader roots.
	#[cfg(test)]
	pub(crate) fn active_operations(&self) -> usize {
		self.operations.borrow().len()
	}
}

/// Only in-flight calls are rooted; completed loads do not retain their Loaders.
type Operations = Rc<RefCell<Vec<Rc<Operation>>>>;

/// Each call is a generation token. Cancellation cannot wrap or reuse an ID.
struct Operation {
	info: DynamicRoot<Rootable![LoaderInfoObject<'_>]>,
	unloading: bool,
	removing: Option<DynamicRoot<Rootable![DisplayObject<'_>]>>,
	cancelled: Cell<bool>,
}

impl Operation {
	/// Match the LoaderInfo within the active Player's root domain, without pointers.
	fn matches<'gc>(&self, roots: DynamicRootSet<'gc>, info: LoaderInfoObject<'gc>) -> bool {
		roots
			.try_fetch(&self.info)
			.is_ok_and(|ours| Object::ptr_eq(Object::from(*ours), Object::from(info)))
	}
}

/// Remove temporary roots on success, early return, VM error, or unwinding.
struct OperationGuard {
	operations: Operations,
	operation: Rc<Operation>,
}

impl OperationGuard {
	fn new<'gc>(
		context: &UpdateContext<'gc>,
		operations: &Operations,
		info: LoaderInfoObject<'gc>,
		unloading: bool,
		removing: Option<DisplayObject<'gc>>,
	) -> Self {
		let operation = Rc::new(Operation {
			info: context
				.dynamic_root
				.stash(context.gc(), Gc::new(context.gc(), info)),
			unloading,
			removing: removing.map(|child| {
				context
					.dynamic_root
					.stash(context.gc(), Gc::new(context.gc(), child))
			}),
			cancelled: Cell::new(false),
		});
		operations.borrow_mut().push(operation.clone());
		Self {
			operations: operations.clone(),
			operation,
		}
	}

	fn is_current(&self) -> bool {
		!self.operation.cancelled.get()
	}
}

impl Drop for OperationGuard {
	fn drop(&mut self) {
		self.operations
			.borrow_mut()
			.retain(|entry| !Rc::ptr_eq(entry, &self.operation));
	}
}

/// Obtain lifecycle state only for the explicitly selected game navigator.
fn operations(context: &UpdateContext<'_>) -> Option<Operations> {
	(context.navigator as &dyn Any)
		.downcast_ref::<GameNavigator>()
		.map(|navigator| navigator.operations.clone())
}

/// A newer load or explicit unload supersedes all older calls for this Loader.
fn cancel_loads<'gc>(
	context: &UpdateContext<'gc>,
	operations: &Operations,
	info: LoaderInfoObject<'gc>,
) {
	for operation in operations.borrow().iter() {
		if !operation.unloading && operation.matches(context.dynamic_root, info) {
			operation.cancelled.set(true);
		}
	}
}

/// Run stock removal once. Its `removed` listeners can synchronously load or unload.
/// The outer removal still removes the old child after a nested load returns.
fn unload_contents<'gc>(
	context: &mut UpdateContext<'gc>,
	operations: &Operations,
	info: LoaderInfoObject<'gc>,
) {
	let child = info
		.loader()
		.expect("LoaderInfo must have been created by Loader")
		.display_object()
		.as_container()
		.unwrap()
		.child_by_index(0);
	// Suppress removal of the same child, not of a replacement inserted by a callback.
	let already_removing = child.is_some_and(|child| {
		operations.borrow().iter().any(|operation| {
			operation.matches(context.dynamic_root, info)
				&& operation.removing.as_ref().is_some_and(|removing| {
					context
						.dynamic_root
						.try_fetch(removing)
						.is_ok_and(|ours| DisplayObject::ptr_eq(*ours, child))
				})
		})
	});
	if already_removing {
		return;
	}
	let _guard = OperationGuard::new(context, operations, info, true, child);
	info.pw_unload_inner(context);
}

/// Opt-in hook called by `LoaderInfoObject::unload`, including explicit Loader.unload.
/// Stock navigators return false and retain Ruffle's original path unchanged.
pub(crate) fn try_unload<'gc>(
	context: &mut UpdateContext<'gc>,
	info: LoaderInfoObject<'gc>,
) -> bool {
	let Some(operations) = operations(context) else {
		return false;
	};
	cancel_loads(context, &operations, info);
	unload_contents(context, &operations, info);
	true
}

impl NavigatorBackend for GameNavigator {
	fn navigate_to_url(
		&self,
		_url: &str,
		_target: &str,
		_vars: Option<(NavigationMethod, IndexMap<String, String>)>,
	) {
	}
	fn fetch(&self, request: Request) -> OwnedFuture<Box<dyn SuccessResponse>, ErrorResponse> {
		let url = request.url().to_string();
		Box::pin(async move {
			Err(ErrorResponse {
				url,
				error: LoadError::FetchError("Only synchronous game images are supported".into()),
			})
		})
	}
	fn resolve_url(&self, url: &str) -> Result<Url, ParseError> {
		let path = self
			.assets
			.resolve(url)
			.map_err(|_| ParseError::RelativeUrlWithoutBase)?;
		Url::from_file_path(path).map_err(|_| ParseError::RelativeUrlWithoutBase)
	}
	fn spawn_future(&mut self, future: OwnedFuture<(), LoadError>) {
		self.spawner.spawn_local(future);
	}
	fn pre_process_url(&self, url: Url) -> Url {
		url
	}
	fn connect_socket(
		&mut self,
		_host: String,
		_port: u16,
		_timeout: Duration,
		handle: SocketHandle,
		_receiver: Receiver<Vec<u8>>,
		sender: Sender<SocketAction>,
	) {
		let _ = sender.try_send(SocketAction::Connect(handle, ConnectionState::Failed));
	}
}

/// Complete a local bitmap synchronously before Loader.load returns, as PW expects.
/// No GC references leave this activation; bitmap pixels are independently owned.
pub(crate) fn try_load<'gc>(
	activation: &mut Activation<'_, 'gc>,
	loader: Object<'gc>,
	request: Object<'gc>,
	loader_context: Option<Object<'gc>>,
) -> Result<bool, Error<'gc>> {
	let Some(operations) = operations(activation.context) else {
		return Ok(false);
	};
	let url = request
		.get_slot(request_slots::_URL)
		.coerce_to_string(activation)?
		.to_string();
	let info = loader
		.get_slot(loader_slots::_CONTENT_LOADER_INFO)
		.as_object()
		.and_then(|object| object.as_loader_info_object())
		.ok_or_else(|| Error::rust_error("Missing LoaderInfo".into()))?;
	cancel_loads(activation.context, &operations, info);
	let guard = OperationGuard::new(activation.context, &operations, info, false, None);
	unload_contents(activation.context, &operations, info);
	if !guard.is_current() {
		return Ok(true);
	}
	let image = (activation.context.navigator as &dyn Any)
		.downcast_ref::<GameNavigator>()
		.unwrap()
		.assets
		.load(&url);
	let image = match image {
		Ok(image) => image,
		Err(error) => {
			info.set_errored(true);
			let event =
				EventObject::io_error_event(activation, &format!("Game image {url}: {error}"), 0);
			Avm2::dispatch_event(activation.context, event, info.into());
			return Ok(true);
		}
	};
	// Match Ruffle's normal Loader domain choice, including an explicit LoaderContext.
	let domain = match loader_context
		.map(|context| context.get_slot(context_slots::APPLICATION_DOMAIN))
		.and_then(|value| value.as_object())
		.and_then(|object| object.as_application_domain())
	{
		Some(domain) => domain,
		None => {
			let parent = activation
				.caller_domain()
				.ok_or_else(|| Error::rust_error("Missing caller domain in Loader.load".into()))?;
			Domain::movie_domain(activation.context, parent)
		}
	};
	let pixels = image
		.rgba
		.chunks_exact(4)
		.map(|pixel| {
			Color::rgba(pixel[0], pixel[1], pixel[2], pixel[3]).to_premultiplied_alpha(true)
		})
		.collect();
	let data =
		BitmapData::new_with_pixels(activation.gc(), image.width, image.height, true, pixels);
	let data_object = BitmapDataObject::from_bitmap_data(activation.context, data);
	let bitmap = activation
		.avm2()
		.classes()
		.bitmap
		.construct(activation, &[data_object.into()])?
		.as_object()
		.and_then(|object| object.as_display_object())
		.ok_or_else(|| Error::rust_error("Bitmap construction failed".into()))?;
	let movie = Arc::new(SwfMovie::fake_with_compressed_len(
		activation.context.root_swf.version(),
		Some("file:///".into()),
		image.encoded_size,
	));
	activation
		.context
		.library
		.library_for_movie_mut(movie.clone())
		.set_avm2_domain(domain);
	info.set_loader_stream(LoaderStream::Swf(movie, bitmap), activation.gc());
	info.set_content_type(ContentType::Png);
	info.set_expose_content();
	let mut container = loader
		.as_display_object()
		.and_then(|object| object.as_container())
		.ok_or_else(|| Error::rust_error("Loader is not a container".into()))?;
	container.insert_at_index(activation.context, bitmap, 0);
	if guard.is_current() {
		info.fire_init_and_complete_events(activation.context, 0, false);
	}
	Ok(true)
}
