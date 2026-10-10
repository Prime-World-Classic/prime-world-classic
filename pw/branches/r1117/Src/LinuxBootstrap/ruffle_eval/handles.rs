//! Host-owned, rooted AVM2 object handles for Ruffle revision `1b24dd3a`.
//!
//! Integrate as `core/src/avm2/pw_handles.rs`, declare `pub mod pw_handles;`
//! in `core/src/avm2.rs`, and export `pub use avm2::pw_handles;` from
//! `core/src/lib.rs`. The tests use private AVM2 fixture-building APIs, so the
//! module belongs under `avm2`, not at the crate root.
//!
//! Keep one [`ObjectHandleStore`] in each host Player wrapper, outside the GC
//! arena. Construct it inside `Player::mutate_with_update_context`, passing
//! `context.gc()` and `context.dynamic_root`. Pass those same context fields
//! when retaining an object return or resolving an object receiver/argument.
//! Only integer handles, never `Object<'gc>`, may escape that mutation closure.
//!
//! The store is permanently bound to its original dynamic root set, including
//! while empty. Clear it before replacing the movie in the same Player; create
//! a new store when replacing the Player/arena. Drop or clear it at teardown.
//! Dropping a store after its Player is also safe under DynamicRoot's contract.
//! Neither the store nor its roots should be cloned or sent to another thread.
//!
//! IDs are nonzero, monotonically allocated across stores in this loaded copy
//! of the bridge, and never reused, including after clear or store destruction.
//! They are process-local identifiers, not persistent IDs or secret capability
//! tokens. Transfer them through a 64-bit integer ABI, not an AVM2/JSON Number.
//!
//! After integration, run from the Ruffle checkout:
//! `cargo test --locked -p ruffle_core --lib avm2::pw_handles::tests`.

#![forbid(unsafe_code)]

use crate::avm2::Object;
use gc_arena::{DynamicRoot, DynamicRootSet, Gc, Mutation, Rootable};
use std::collections::BTreeMap;
use std::fmt;
use std::sync::atomic::{AtomicU64, Ordering};

/// Opaque host identifier. Zero is invalid; only the issuing store accepts it.
pub type ObjectHandle = u64;

/// A rejected handle operation; failures leave existing handles unchanged.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HandleError {
	/// The supplied context belongs to a different Player/dynamic root set.
	WrongRootSet,
	/// The ID is zero, unknown, released, cleared, or owned by another store.
	InvalidHandle,
	/// The configured maximum number of live handles has been reached.
	CapacityExceeded,
	/// All nonzero `u64` IDs have been issued; IDs must never wrap or be reused.
	IdExhausted,
}

impl fmt::Display for HandleError {
	fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
		formatter.write_str(match self {
			Self::WrongRootSet => "object handle store belongs to another root set",
			Self::InvalidHandle => "invalid or stale object handle",
			Self::CapacityExceeded => "object handle capacity exceeded",
			Self::IdExhausted => "object handle IDs exhausted",
		})
	}
}

impl std::error::Error for HandleError {}

static LAST_HANDLE: AtomicU64 = AtomicU64::new(0);

/// Allocate without wrapping. The argument permits isolated exhaustion tests.
fn allocate_handle(last: &AtomicU64) -> Result<ObjectHandle, HandleError> {
	last.fetch_update(Ordering::Relaxed, Ordering::Relaxed, |value| {
		value.checked_add(1)
	})
	.map(|previous| previous + 1)
	.map_err(|_| HandleError::IdExhausted)
}

/// A bounded set of strong roots owned by one host Player wrapper.
///
/// Each successful retain consumes one slot, even for an already-retained
/// object. Release each returned ID independently. The capacity limits live
/// handles, not the size of the object graphs they keep alive. There is one
/// additional unit-valued root for domain identity; no raw GC pointers or
/// forged lifetimes are stored here.
///
/// Normal field destruction drops all DynamicRoots. No explicit GC context is
/// needed for [`clear`](Self::clear) or destruction, and neither forces GC.
pub struct ObjectHandleStore {
	// Keep a separate domain marker so releasing the last object cannot rebind us.
	domain: DynamicRoot<Rootable![()]>,
	objects: BTreeMap<ObjectHandle, DynamicRoot<Rootable![Object<'_>]>>,
	capacity: usize,
}

impl ObjectHandleStore {
	/// Bind an empty store to the owning Player's `context.dynamic_root`.
	///
	/// Use `context.gc()` for `mc`. A zero capacity is valid and rejects all
	/// retains. Capacity is not preallocated; memory grows with live handles.
	pub fn new<'gc>(mc: &Mutation<'gc>, roots: DynamicRootSet<'gc>, capacity: usize) -> Self {
		Self {
			domain: roots.stash(mc, Gc::new(mc, ())),
			objects: BTreeMap::new(),
			capacity,
		}
	}

	/// Root an object result and return a fresh ID owned by this store.
	///
	/// Both GC arguments and `object` must come from the same mutation context;
	/// their invariant `'gc` lifetimes enforce this. Runtime validation also
	/// rejects a context different from the one used to construct the store.
	/// Domain, capacity, and ID exhaustion are checked before allocating a root.
	pub fn retain<'gc>(
		&mut self,
		mc: &Mutation<'gc>,
		roots: DynamicRootSet<'gc>,
		object: Object<'gc>,
	) -> Result<ObjectHandle, HandleError> {
		self.validate_domain(roots)?;
		if self.objects.len() >= self.capacity {
			return Err(HandleError::CapacityExceeded);
		}
		let handle = allocate_handle(&LAST_HANDLE)?;
		let root = roots.stash(mc, Gc::new(mc, object));
		self.objects.insert(handle, root);
		Ok(handle)
	}

	/// Resolve a live ID for a call, property access, or object-valued argument.
	///
	/// The returned object is valid only within the supplied arena's mutation
	/// lifetime. No borrow of this store is retained, so callers can subsequently
	/// release a handle or retain a call result within that same mutation.
	pub fn resolve<'gc>(
		&self,
		roots: DynamicRootSet<'gc>,
		handle: ObjectHandle,
	) -> Result<Object<'gc>, HandleError> {
		self.validate_domain(roots)?;
		let root = self
			.objects
			.get(&handle)
			.ok_or(HandleError::InvalidHandle)?;
		roots
			.try_fetch(root)
			.map(|object| *object)
			.map_err(|_| HandleError::WrongRootSet)
	}

	/// Invalidate one ID and immediately drop its strong root.
	///
	/// Releasing twice is an error. Other retained IDs for the same object remain
	/// valid. The object becomes collectible only when no other roots reach it.
	pub fn release(
		&mut self,
		roots: DynamicRootSet<'_>,
		handle: ObjectHandle,
	) -> Result<(), HandleError> {
		self.validate_domain(roots)?;
		self.objects
			.remove(&handle)
			.map(|_| ())
			.ok_or(HandleError::InvalidHandle)
	}

	/// Invalidate all IDs and drop all object roots, without entering the Player.
	///
	/// Idempotent. Preserves capacity and the original root-set binding, and does
	/// not reset the ID sequence. The identity root itself is dropped on teardown.
	pub fn clear(&mut self) {
		self.objects.clear();
	}

	/// Number of live handles, counting independent retains of the same object.
	pub fn len(&self) -> usize {
		self.objects.len()
	}

	/// Whether there are no retained object handles.
	pub fn is_empty(&self) -> bool {
		self.objects.is_empty()
	}

	/// Maximum number of simultaneously retained object handles.
	pub fn capacity(&self) -> usize {
		self.capacity
	}

	/// Check ownership even when the object map is empty.
	pub(crate) fn validate_domain(&self, roots: DynamicRootSet<'_>) -> Result<(), HandleError> {
		if roots.contains(&self.domain) {
			Ok(())
		} else {
			Err(HandleError::WrongRootSet)
		}
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::avm2::object::{ScriptObject, WeakObject};
	use crate::avm2::vtable::VTable;
	use crate::avm2::{Class, Namespace, QName};
	use crate::string::AvmString;
	use gc_arena::{Arena, Collect, RefLock};

	#[derive(Collect)]
	#[collect(no_drop)]
	struct TestRoot<'gc> {
		roots: DynamicRootSet<'gc>,
		other_roots: DynamicRootSet<'gc>,
		weak: Gc<'gc, RefLock<Vec<WeakObject<'gc>>>>,
	}

	type TestArena = Arena<Rootable![TestRoot<'_>]>;

	/// A minimal Player root domain with weak observers that do not retain objects.
	fn arena() -> TestArena {
		Arena::new(|mc| TestRoot {
			roots: DynamicRootSet::new(mc),
			other_roots: DynamicRootSet::new(mc),
			weak: Gc::new(mc, RefLock::new(Vec::new())),
		})
	}

	/// Allocate a real AVM2 ScriptObject without a movie, backend, or VM bootstrap.
	fn object<'gc>(mc: &Mutation<'gc>, root: &TestRoot<'gc>) -> Object<'gc> {
		let name = QName::new(Namespace::any(), AvmString::new_utf8(mc, "HandleFixture"));
		let class = Class::custom_new(name, None, None, Box::default(), mc);
		let object = ScriptObject::custom_object(mc, class, None, VTable::empty(mc));
		root.weak.unlock(mc).borrow_mut().push(object.downgrade());
		object
	}

	/// Inspect weak references only inside an arena mutation.
	fn live_objects(arena: &TestArena) -> usize {
		arena.mutate(|mc, root| {
			root.weak
				.borrow()
				.iter()
				.filter(|weak| weak.upgrade(mc).is_some())
				.count()
		})
	}

	#[test]
	fn roots_survive_collection_and_duplicate_retains_release_independently() {
		let mut arena = arena();
		let (mut store, first, second) = arena.mutate(|mc, root| {
			let mut store = ObjectHandleStore::new(mc, root.roots, 2);
			let object = object(mc, root);
			let first = store.retain(mc, root.roots, object).unwrap();
			let second = store.retain(mc, root.roots, object).unwrap();
			assert!(first > 0 && second > first);
			(store, first, second)
		});
		for _ in 0..3 {
			arena.finish_cycle();
			assert_eq!(live_objects(&arena), 1);
			arena.mutate(|mc, root| {
				let original = root.weak.borrow()[0].upgrade(mc).unwrap();
				assert!(Object::ptr_eq(
					store.resolve(root.roots, first).unwrap(),
					original
				));
				assert!(Object::ptr_eq(
					store.resolve(root.roots, second).unwrap(),
					original
				));
			});
		}
		arena.mutate(|_, root| {
			store.release(root.roots, first).unwrap();
			assert!(matches!(
				store.resolve(root.roots, first),
				Err(HandleError::InvalidHandle)
			));
			assert_eq!(
				store.release(root.roots, first),
				Err(HandleError::InvalidHandle)
			);
		});
		arena.finish_cycle();
		assert_eq!(live_objects(&arena), 1);
		arena.mutate(|_, root| store.release(root.roots, second).unwrap());
		arena.finish_cycle();
		assert_eq!(live_objects(&arena), 0);
		assert!(store.is_empty());
	}

	#[test]
	fn clear_invalidates_ids_without_reuse_or_rebinding() {
		let mut arena = arena();
		let (mut store, old) = arena.mutate(|mc, root| {
			let mut store = ObjectHandleStore::new(mc, root.roots, 1);
			let handle = store.retain(mc, root.roots, object(mc, root)).unwrap();
			(store, handle)
		});
		store.clear();
		store.clear();
		arena.finish_cycle();
		assert_eq!(live_objects(&arena), 0);
		assert!(store.is_empty());
		arena.mutate(|mc, root| {
			assert!(matches!(
				store.resolve(root.roots, old),
				Err(HandleError::InvalidHandle)
			));
			assert_eq!(
				store.release(root.roots, old),
				Err(HandleError::InvalidHandle)
			);
			assert_eq!(
				store.retain(mc, root.other_roots, object(mc, root)),
				Err(HandleError::WrongRootSet)
			);
			let new = store.retain(mc, root.roots, object(mc, root)).unwrap();
			assert!(new > old);
			assert_eq!(store.capacity(), 1);
			assert_eq!(store.len(), 1);
		});
		drop(store);
		arena.finish_cycle();
		assert_eq!(live_objects(&arena), 0);
	}

	#[test]
	fn capacity_failure_does_not_root_objects_and_release_frees_capacity() {
		let mut arena = arena();
		let (mut store, first) = arena.mutate(|mc, root| {
			let mut store = ObjectHandleStore::new(mc, root.roots, 1);
			let first = store.retain(mc, root.roots, object(mc, root)).unwrap();
			for _ in 0..4 {
				assert_eq!(
					store.retain(mc, root.roots, object(mc, root)),
					Err(HandleError::CapacityExceeded)
				);
			}
			assert_eq!(store.len(), 1);
			(store, first)
		});
		arena.finish_cycle();
		assert_eq!(live_objects(&arena), 1);
		arena.mutate(|mc, root| {
			store.release(root.roots, first).unwrap();
			let next = store.retain(mc, root.roots, object(mc, root)).unwrap();
			assert!(next > first);
			assert!(matches!(
				store.resolve(root.roots, first),
				Err(HandleError::InvalidHandle)
			));
		});
		arena.finish_cycle();
		assert_eq!(live_objects(&arena), 1);
	}

	#[test]
	fn zero_capacity_and_unknown_ids_are_rejected() {
		let mut arena = arena();
		arena.mutate(|mc, root| {
			let mut store = ObjectHandleStore::new(mc, root.roots, 0);
			assert_eq!(
				store.retain(mc, root.roots, object(mc, root)),
				Err(HandleError::CapacityExceeded)
			);
			for handle in [0, 1, u64::MAX] {
				assert!(matches!(
					store.resolve(root.roots, handle),
					Err(HandleError::InvalidHandle)
				));
				assert_eq!(
					store.release(root.roots, handle),
					Err(HandleError::InvalidHandle)
				);
			}
			assert_eq!(store.capacity(), 0);
			assert!(store.is_empty());
		});
		arena.finish_cycle();
		assert_eq!(live_objects(&arena), 0);
	}

	#[test]
	fn separate_stores_never_accept_each_others_ids() {
		let arena = arena();
		arena.mutate(|mc, root| {
			let mut first = ObjectHandleStore::new(mc, root.roots, 1);
			let mut second = ObjectHandleStore::new(mc, root.roots, 1);
			let a = first.retain(mc, root.roots, object(mc, root)).unwrap();
			let b = second.retain(mc, root.roots, object(mc, root)).unwrap();
			assert!(b > a);
			assert!(matches!(
				second.resolve(root.roots, a),
				Err(HandleError::InvalidHandle)
			));
			assert!(matches!(
				first.resolve(root.roots, b),
				Err(HandleError::InvalidHandle)
			));
			assert_eq!(
				second.release(root.roots, a),
				Err(HandleError::InvalidHandle)
			);
			drop(first);
			let mut replacement = ObjectHandleStore::new(mc, root.roots, 1);
			let c = replacement
				.retain(mc, root.roots, object(mc, root))
				.unwrap();
			assert!(c > b);
			assert!(matches!(
				replacement.resolve(root.roots, a),
				Err(HandleError::InvalidHandle)
			));
		});
	}

	#[test]
	fn wrong_root_sets_and_players_cannot_resolve_release_or_retain() {
		let first_arena = arena();
		let second_arena = arena();
		let (mut store, handle) = first_arena.mutate(|mc, root| {
			let mut store = ObjectHandleStore::new(mc, root.roots, 2);
			let handle = store.retain(mc, root.roots, object(mc, root)).unwrap();
			assert!(matches!(
				store.resolve(root.other_roots, handle),
				Err(HandleError::WrongRootSet)
			));
			assert_eq!(
				store.release(root.other_roots, handle),
				Err(HandleError::WrongRootSet)
			);
			(store, handle)
		});
		second_arena.mutate(|mc, root| {
			assert!(matches!(
				store.resolve(root.roots, handle),
				Err(HandleError::WrongRootSet)
			));
			assert_eq!(
				store.release(root.roots, handle),
				Err(HandleError::WrongRootSet)
			);
			assert_eq!(
				store.retain(mc, root.roots, object(mc, root)),
				Err(HandleError::WrongRootSet)
			);
			assert_eq!(store.len(), 1);
		});
		first_arena.mutate(|_, root| {
			assert!(store.resolve(root.roots, handle).is_ok());
			store.release(root.roots, handle).unwrap();
		});
		second_arena.mutate(|mc, root| {
			assert_eq!(
				store.retain(mc, root.roots, object(mc, root)),
				Err(HandleError::WrongRootSet)
			);
		});
	}

	#[test]
	fn store_can_outlive_its_player_but_cannot_bind_to_a_new_one() {
		let (mut store, handle) = {
			let arena = arena();
			arena.mutate(|mc, root| {
				let mut store = ObjectHandleStore::new(mc, root.roots, 1);
				let handle = store.retain(mc, root.roots, object(mc, root)).unwrap();
				(store, handle)
			})
		};
		let replacement = arena();
		replacement.mutate(|mc, root| {
			assert!(matches!(
				store.resolve(root.roots, handle),
				Err(HandleError::WrongRootSet)
			));
			assert_eq!(
				store.release(root.roots, handle),
				Err(HandleError::WrongRootSet)
			);
			store.clear();
			assert_eq!(
				store.retain(mc, root.roots, object(mc, root)),
				Err(HandleError::WrongRootSet)
			);
		});
		drop(store);
	}

	#[test]
	fn id_exhaustion_never_wraps_or_reissues_the_last_id() {
		let last = AtomicU64::new(u64::MAX - 1);
		assert_eq!(allocate_handle(&last), Ok(u64::MAX));
		assert_eq!(allocate_handle(&last), Err(HandleError::IdExhausted));
		assert_eq!(allocate_handle(&last), Err(HandleError::IdExhausted));
		assert_eq!(last.load(Ordering::Relaxed), u64::MAX);
	}
}
