//! Real-SWF receiver/argument handle checks, complementary to the GC arena unit tests.
#[path = "pw_runtime/mod.rs"]
mod runtime;
use serde_json::json;
use std::path::Path;

fn main() -> Result<(), String> {
	let args: Vec<_> = std::env::args().collect();
	if args.len() != 3 {
		return Err("Usage: handles_probe DATA_ROOT COMBAT.swf".into());
	}
	let mut host = runtime::Host::new(Path::new(&args[1]), Path::new(&args[2]))?;
	let retained = host.request(
		&json!({"path":"mainInterface", "method":"GetActionBarItemDisplayObject", "args":[0, false]}),
	)?;
	let id = retained["id"]
		.as_str()
		.ok_or("Expected object handle")?
		.to_string();
	assert_eq!(retained["type"], "handle");
	let property = |op, args| json!({"receiver":id, "op":op, "method":"visible", "args":args});
	host.request(&property("set", json!([false])))?;
	host.request(&json!({"action":"step", "frames":120}))?;
	assert_eq!(host.request(&property("get", json!([])))?["value"], false);
	host.request(&property("set", json!([true])))?;
	assert_eq!(host.request(&property("get", json!([])))?["value"], true);
	assert_eq!(
		host.request(&json!({"receiver":id, "method":"contains", "args":[{"$handle":id}]}))?["value"],
		true
	);
	host.request(&json!({"action":"release", "id":id}))?;
	assert!(
		host.request(&property("get", json!([])))
			.unwrap_err()
			.contains("stale")
	);
	assert!(host.request(&json!({"action":"release", "id":id})).is_err());
	assert_eq!(
		host.request(&json!({"action":"stats"}))?,
		json!({"handles":0,"runtime_errors":0})
	);
	assert!(
		!host
			.request(&json!({"action":"events"}))?
			.as_array()
			.unwrap()
			.is_empty()
	);
	assert_eq!(host.request(&json!({"action":"events"}))?, json!([]));
	println!(
		"Live SWF handles: retain, 120 frames, get/set, object argument, release, stale rejection, callbacks PASS"
	);
	Ok(())
}
