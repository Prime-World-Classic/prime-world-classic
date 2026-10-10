//! Evaluation-only direct AVM2 bridge for the pinned Ruffle revision.
//! Rooted handles never expose GC objects or Tamarin Atoms to the native host.

use crate::Player;
use crate::avm2::pw_handles::ObjectHandleStore;
use crate::avm2::{Activation, FunctionArgs, Multiname, Value};
use crate::display_object::TDisplayObject;
use crate::external::Value as ExternalValue;
use crate::string::AvmString;

/// Owned host value or opaque rooted object ID. IDs must not travel as floating point.
#[derive(Debug)]
pub enum HostValue {
	Scalar(ExternalValue),
	Handle(u64),
}

impl Player {
	/// Access a named public interface without leaking GC objects to the host.
	/// Supports synchronous call/get/set; object-valued results fail explicitly.
	pub fn pw_invoke(
		&mut self,
		path: &str,
		operation: &str,
		member: &str,
		args: Vec<ExternalValue>,
	) -> Result<ExternalValue, String> {
		match self.pw_invoke_impl(
			None,
			None,
			path,
			operation,
			member,
			args.into_iter().map(HostValue::Scalar).collect(),
		)? {
			HostValue::Scalar(value) => Ok(value),
			HostValue::Handle(_) => unreachable!("No handle store supplied"),
		}
	}

	/// Access a root interface or retained receiver and retain any object-valued result.
	/// An empty path is permitted only with a live receiver handle.
	pub fn pw_invoke_owned(
		&mut self,
		handles: &mut ObjectHandleStore,
		receiver: Option<u64>,
		path: &str,
		operation: &str,
		member: &str,
		args: Vec<HostValue>,
	) -> Result<HostValue, String> {
		self.pw_invoke_impl(Some(handles), receiver, path, operation, member, args)
	}

	fn pw_invoke_impl(
		&mut self,
		mut handles: Option<&mut ObjectHandleStore>,
		target: Option<u64>,
		path: &str,
		operation: &str,
		member: &str,
		args: Vec<HostValue>,
	) -> Result<HostValue, String> {
		self.mutate_with_update_context(|context| {
			if let Some(store) = handles.as_ref() {
				store
					.validate_domain(context.dynamic_root)
					.map_err(|e| e.to_string())?;
			}
			let root = context
				.stage
				.root_clip()
				.and_then(|clip| clip.object2())
				.ok_or_else(|| "No AVM2 root movie".to_string())?;
			let domain = context
				.library
				.library_for_movie(context.root_swf.clone())
				.ok_or_else(|| "No movie library".to_string())?
				.avm2_domain();
			let mut activation = Activation::from_domain(context, domain);
			let mut receiver = if let Some(id) = target {
				Value::from(
					handles
						.as_ref()
						.ok_or("No handle store")?
						.resolve(activation.context.dynamic_root, id)
						.map_err(|e| e.to_string())?,
				)
			} else {
				Value::from(root)
			};
			for component in path
				.split('.')
				.filter(|_| !(target.is_some() && path.is_empty()))
			{
				if component.is_empty() || receiver.as_object().is_none() {
					return Err("Invalid interface path".to_string());
				}
				let name = AvmString::new_utf8(activation.gc(), component);
				receiver = receiver
					.get_public_property(name, &mut activation)
					.map_err(|e| format!("{e:?}"))?;
			}
			if receiver.as_object().is_none() {
				return Err("Interface is not an object".to_string());
			}
			let arguments: Vec<_> = args
				.into_iter()
				.map(|value| match value {
					HostValue::Scalar(value) => Ok(value.into_avm2(activation.context)),
					HostValue::Handle(id) => handles
						.as_ref()
						.ok_or("No handle store".to_string())?
						.resolve(activation.context.dynamic_root, id)
						.map(Value::from)
						.map_err(|e| e.to_string()),
				})
				.collect::<Result<_, String>>()?;
			let name = AvmString::new_utf8(activation.gc(), member);
			let name = Multiname::new(activation.avm2().find_public_namespace(), name);
			let result = match operation {
				"call" => receiver.call_property(
					&name,
					FunctionArgs::from_slice(&arguments),
					&mut activation,
				),
				"get" if arguments.is_empty() => receiver.get_property(&name, &mut activation),
				"set" if arguments.len() == 1 => receiver
					.set_property(&name, arguments[0], &mut activation)
					.map(|()| Value::Undefined),
				_ => return Err("Invalid operation or argument count".to_string()),
			}
			.map_err(|e| format!("{e:?}"))?;
			if let Some(object) = result.as_object() {
				let store = handles
					.as_mut()
					.ok_or("Object return requires a rooted handle adapter")?;
				return store
					.retain(activation.gc(), activation.context.dynamic_root, object)
					.map(HostValue::Handle)
					.map_err(|e| e.to_string());
			}
			ExternalValue::from_avm2(&mut activation, result)
				.map(HostValue::Scalar)
				.map_err(|e| format!("{e:?}"))
		})
	}
}
