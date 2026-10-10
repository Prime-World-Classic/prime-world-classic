//! Bounded, stateless JSON input decoding for the native Ruffle host.

use ruffle_core::events::{
	KeyDescriptor, KeyLocation, LogicalKey, MouseButton, MouseWheelDelta, NamedKey, PhysicalKey,
	PlayerEvent,
};
use serde_json::{Map, Value};

const MAX_COORDINATE: f64 = 1_000_000.0;
const MAX_WHEEL_LINES: f64 = 1_000.0;
const MAX_TEXT_BYTES: usize = 1024;
const MAX_KEY_BYTES: usize = 16;

/// Decode one event object completely before the caller dispatches any events.
///
/// Accepted schemas (all fields required, no additional fields):
/// - `{"type":"mouse_move","x":0,"y":0}`
/// - `{"type":"mouse_down","x":0,"y":0,"button":"left"}`
/// - `{"type":"mouse_up","x":0,"y":0,"button":"left"}`
/// - `{"type":"mouse_leave"}`
/// - `{"type":"wheel","lines":1}`
/// - `{"type":"key_down","key":"ArrowLeft"}`
/// - `{"type":"key_up","key":"ArrowLeft"}`
/// - `{"type":"text","text":"hello"}`
/// - `{"type":"focus","focused":true}`
///
/// Coordinates are viewport pixels, passed to Ruffle without rescaling, and must
/// be finite within +/-1,000,000 inclusive. Negative/off-viewport positions are
/// allowed. Wheel lines must be finite within +/-1,000 inclusive; their sign is
/// preserved. Buttons are exactly `left`, `right`, or `middle`. Click counts are
/// left to Ruffle (`MouseDown.index` is `None`). Focus requires a JSON boolean.
///
/// Text is at most 1,024 UTF-8 bytes and produces one `TextInput` per Unicode
/// scalar, in order, without normalization. Empty text produces no events.
/// Thus one request produces at most 1,024 events. Key events do not insert text.
///
/// Keys are case-sensitive names or one non-control Unicode scalar. Names are
/// `Backspace`, `Tab`, `Enter`, `Escape`, `Space`, `Insert`, `Delete`, `Home`,
/// `End`, `PageUp`, `PageDown`, `ArrowLeft`, `ArrowRight`, `ArrowUp`, `ArrowDown`,
/// `Shift`, `Control`, `Alt`, `Meta`, `Super`, `CapsLock`, `NumLock`, `ScrollLock`,
/// `ContextMenu`, `Pause`, `PrintScreen`, and `F1` through `F24`. `Meta` aliases
/// `Super`; modifiers use the left physical key and location. Other keys use
/// the standard location. ASCII characters use an explicit US keyboard mapping,
/// including shifted punctuation; other scalars use `PhysicalKey::Unknown`
/// while preserving the logical character. No modifier presses are synthesized.
///
/// This parser does not track held keys/buttons, translate text-editing commands
/// or IME, or handle focus-loss releases. The transport must bound the JSON
/// payload before constructing a `Value`; duplicate fields are already lost
/// at that stage and cannot be rejected here.
///
/// # Errors
/// Returns a bounded diagnostic for non-objects, missing/extra fields, incorrect
/// JSON types, out-of-range numbers, oversized text, or unsupported values.
pub fn events(input: &Value) -> Result<Vec<PlayerEvent>, String> {
	let object = input.as_object().ok_or("Input event must be an object")?;
	let kind = string(object, "type")?;
	let fields: &[&str] = match kind {
		"mouse_move" => &["type", "x", "y"],
		"mouse_down" | "mouse_up" => &["type", "x", "y", "button"],
		"mouse_leave" => &["type"],
		"wheel" => &["type", "lines"],
		"key_down" | "key_up" => &["type", "key"],
		"text" => &["type", "text"],
		"focus" => &["type", "focused"],
		_ => return Err("Unknown input event type".into()),
	};
	if object.len() != fields.len() || object.keys().any(|field| !fields.contains(&field.as_str()))
	{
		return Err("Input event has missing or unexpected fields".into());
	}

	let event = match kind {
		"mouse_move" | "mouse_down" | "mouse_up" => {
			let x = number(object, "x", MAX_COORDINATE)?;
			let y = number(object, "y", MAX_COORDINATE)?;
			if kind == "mouse_move" {
				PlayerEvent::MouseMove { x, y }
			} else {
				let button = match string(object, "button")? {
					"left" => MouseButton::Left,
					"right" => MouseButton::Right,
					"middle" => MouseButton::Middle,
					_ => return Err("Unknown mouse button".into()),
				};
				if kind == "mouse_down" {
					PlayerEvent::MouseDown {
						x,
						y,
						button,
						index: None,
					}
				} else {
					PlayerEvent::MouseUp { x, y, button }
				}
			}
		}
		"mouse_leave" => PlayerEvent::MouseLeave,
		"wheel" => PlayerEvent::MouseWheel {
			delta: MouseWheelDelta::Lines(number(object, "lines", MAX_WHEEL_LINES)?),
		},
		"key_down" | "key_up" => {
			let key = key(string(object, "key")?)?;
			if kind == "key_down" {
				PlayerEvent::KeyDown { key }
			} else {
				PlayerEvent::KeyUp { key }
			}
		}
		"text" => {
			let text = string(object, "text")?;
			if text.len() > MAX_TEXT_BYTES {
				return Err("Text exceeds 1024 UTF-8 bytes".into());
			}
			return Ok(text
				.chars()
				.map(|codepoint| PlayerEvent::TextInput { codepoint })
				.collect());
		}
		"focus" => {
			if object["focused"]
				.as_bool()
				.ok_or("Field 'focused' must be a boolean")?
			{
				PlayerEvent::FocusGained
			} else {
				PlayerEvent::FocusLost
			}
		}
		_ => return Err("Unknown input event type".into()),
	};
	Ok(vec![event])
}

/// Read a required string without coercion or copying untrusted field contents.
fn string<'a>(object: &'a Map<String, Value>, field: &str) -> Result<&'a str, String> {
	object
		.get(field)
		.and_then(Value::as_str)
		.ok_or_else(|| format!("Field '{field}' must be a string"))
}

/// Accept integer or fractional JSON numbers only within the inclusive bound.
fn number(object: &Map<String, Value>, field: &str, bound: f64) -> Result<f64, String> {
	object
		.get(field)
		.and_then(Value::as_f64)
		.filter(|value| value.is_finite() && value.abs() <= bound)
		.ok_or_else(|| format!("Field '{field}' must be a finite number within +/-{bound}"))
}

/// Construct descriptors without depending on enum discriminants or host layout.
fn key(value: &str) -> Result<KeyDescriptor, String> {
	if value.is_empty() || value.len() > MAX_KEY_BYTES {
		return Err("Key must be one character or a supported name".into());
	}
	let mut chars = value.chars();
	let first = chars.next().ok_or("Key must not be empty")?;
	if chars.next().is_none() {
		if first.is_control() {
			return Err("Control characters require a named key".into());
		}
		return Ok(KeyDescriptor {
			physical_key: character_key(first),
			logical_key: LogicalKey::Character(first),
			key_location: KeyLocation::Standard,
		});
	}
	if value == "Space" {
		return Ok(KeyDescriptor {
			physical_key: PhysicalKey::Space,
			logical_key: LogicalKey::Character(' '),
			key_location: KeyLocation::Standard,
		});
	}
	let (physical_key, named) = match value {
		"Backspace" => (PhysicalKey::Backspace, NamedKey::Backspace),
		"Tab" => (PhysicalKey::Tab, NamedKey::Tab),
		"Enter" => (PhysicalKey::Enter, NamedKey::Enter),
		"Escape" => (PhysicalKey::Escape, NamedKey::Escape),
		"Insert" => (PhysicalKey::Insert, NamedKey::Insert),
		"Delete" => (PhysicalKey::Delete, NamedKey::Delete),
		"Home" => (PhysicalKey::Home, NamedKey::Home),
		"End" => (PhysicalKey::End, NamedKey::End),
		"PageUp" => (PhysicalKey::PageUp, NamedKey::PageUp),
		"PageDown" => (PhysicalKey::PageDown, NamedKey::PageDown),
		"ArrowLeft" => (PhysicalKey::ArrowLeft, NamedKey::ArrowLeft),
		"ArrowRight" => (PhysicalKey::ArrowRight, NamedKey::ArrowRight),
		"ArrowUp" => (PhysicalKey::ArrowUp, NamedKey::ArrowUp),
		"ArrowDown" => (PhysicalKey::ArrowDown, NamedKey::ArrowDown),
		"Shift" => (PhysicalKey::ShiftLeft, NamedKey::Shift),
		"Control" => (PhysicalKey::ControlLeft, NamedKey::Control),
		"Alt" => (PhysicalKey::AltLeft, NamedKey::Alt),
		"Meta" | "Super" => (PhysicalKey::SuperLeft, NamedKey::Super),
		"CapsLock" => (PhysicalKey::CapsLock, NamedKey::CapsLock),
		"NumLock" => (PhysicalKey::NumLock, NamedKey::NumLock),
		"ScrollLock" => (PhysicalKey::ScrollLock, NamedKey::ScrollLock),
		"ContextMenu" => (PhysicalKey::ContextMenu, NamedKey::ContextMenu),
		"Pause" => (PhysicalKey::Pause, NamedKey::Pause),
		"PrintScreen" => (PhysicalKey::PrintScreen, NamedKey::PrintScreen),
		"F1" => (PhysicalKey::F1, NamedKey::F1),
		"F2" => (PhysicalKey::F2, NamedKey::F2),
		"F3" => (PhysicalKey::F3, NamedKey::F3),
		"F4" => (PhysicalKey::F4, NamedKey::F4),
		"F5" => (PhysicalKey::F5, NamedKey::F5),
		"F6" => (PhysicalKey::F6, NamedKey::F6),
		"F7" => (PhysicalKey::F7, NamedKey::F7),
		"F8" => (PhysicalKey::F8, NamedKey::F8),
		"F9" => (PhysicalKey::F9, NamedKey::F9),
		"F10" => (PhysicalKey::F10, NamedKey::F10),
		"F11" => (PhysicalKey::F11, NamedKey::F11),
		"F12" => (PhysicalKey::F12, NamedKey::F12),
		"F13" => (PhysicalKey::F13, NamedKey::F13),
		"F14" => (PhysicalKey::F14, NamedKey::F14),
		"F15" => (PhysicalKey::F15, NamedKey::F15),
		"F16" => (PhysicalKey::F16, NamedKey::F16),
		"F17" => (PhysicalKey::F17, NamedKey::F17),
		"F18" => (PhysicalKey::F18, NamedKey::F18),
		"F19" => (PhysicalKey::F19, NamedKey::F19),
		"F20" => (PhysicalKey::F20, NamedKey::F20),
		"F21" => (PhysicalKey::F21, NamedKey::F21),
		"F22" => (PhysicalKey::F22, NamedKey::F22),
		"F23" => (PhysicalKey::F23, NamedKey::F23),
		"F24" => (PhysicalKey::F24, NamedKey::F24),
		_ => return Err("Unknown key name".into()),
	};
	let key_location = match physical_key {
		PhysicalKey::ShiftLeft
		| PhysicalKey::ControlLeft
		| PhysicalKey::AltLeft
		| PhysicalKey::SuperLeft => KeyLocation::Left,
		_ => KeyLocation::Standard,
	};
	Ok(KeyDescriptor {
		physical_key,
		logical_key: LogicalKey::Named(named),
		key_location,
	})
}

/// Map printable ASCII to physical US keys; Unicode has no inferred location.
fn character_key(value: char) -> PhysicalKey {
	match value.to_ascii_lowercase() {
		'a' => PhysicalKey::KeyA,
		'b' => PhysicalKey::KeyB,
		'c' => PhysicalKey::KeyC,
		'd' => PhysicalKey::KeyD,
		'e' => PhysicalKey::KeyE,
		'f' => PhysicalKey::KeyF,
		'g' => PhysicalKey::KeyG,
		'h' => PhysicalKey::KeyH,
		'i' => PhysicalKey::KeyI,
		'j' => PhysicalKey::KeyJ,
		'k' => PhysicalKey::KeyK,
		'l' => PhysicalKey::KeyL,
		'm' => PhysicalKey::KeyM,
		'n' => PhysicalKey::KeyN,
		'o' => PhysicalKey::KeyO,
		'p' => PhysicalKey::KeyP,
		'q' => PhysicalKey::KeyQ,
		'r' => PhysicalKey::KeyR,
		's' => PhysicalKey::KeyS,
		't' => PhysicalKey::KeyT,
		'u' => PhysicalKey::KeyU,
		'v' => PhysicalKey::KeyV,
		'w' => PhysicalKey::KeyW,
		'x' => PhysicalKey::KeyX,
		'y' => PhysicalKey::KeyY,
		'z' => PhysicalKey::KeyZ,
		'0' | ')' => PhysicalKey::Digit0,
		'1' | '!' => PhysicalKey::Digit1,
		'2' | '@' => PhysicalKey::Digit2,
		'3' | '#' => PhysicalKey::Digit3,
		'4' | '$' => PhysicalKey::Digit4,
		'5' | '%' => PhysicalKey::Digit5,
		'6' | '^' => PhysicalKey::Digit6,
		'7' | '&' => PhysicalKey::Digit7,
		'8' | '*' => PhysicalKey::Digit8,
		'9' | '(' => PhysicalKey::Digit9,
		'`' | '~' => PhysicalKey::Backquote,
		'-' | '_' => PhysicalKey::Minus,
		'=' | '+' => PhysicalKey::Equal,
		'[' | '{' => PhysicalKey::BracketLeft,
		']' | '}' => PhysicalKey::BracketRight,
		'\\' | '|' => PhysicalKey::Backslash,
		';' | ':' => PhysicalKey::Semicolon,
		'\'' | '"' => PhysicalKey::Quote,
		',' | '<' => PhysicalKey::Comma,
		'.' | '>' => PhysicalKey::Period,
		'/' | '?' => PhysicalKey::Slash,
		' ' => PhysicalKey::Space,
		_ => PhysicalKey::Unknown,
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use serde_json::json;

	/// Require exactly one decoded event for non-text fixtures.
	fn one(input: Value) -> PlayerEvent {
		let mut parsed = events(&input).expect("valid input fixture");
		assert_eq!(parsed.len(), 1);
		parsed.pop().unwrap()
	}

	/// Require the same descriptor and event direction for a press/release pair.
	fn assert_key(value: &str, expected: KeyDescriptor) {
		for kind in ["key_down", "key_up"] {
			let event = one(json!({"type": kind, "key": value}));
			let actual = match (kind, event) {
				("key_down", PlayerEvent::KeyDown { key }) => key,
				("key_up", PlayerEvent::KeyUp { key }) => key,
				_ => panic!("wrong key event direction"),
			};
			assert_eq!(actual, expected, "{kind}: {value:?}");
		}
	}

	#[test]
	fn requires_one_object_and_a_known_string_type() {
		for input in [
			Value::Null,
			json!(false),
			json!(1),
			json!("mouse_leave"),
			json!([]),
			json!([{"type": "mouse_leave"}]),
			json!({}),
			json!({"type": null}),
			json!({"type": 1}),
			json!({"type": true}),
			json!({"type": []}),
			json!({"type": {}}),
			json!({"type": ""}),
			json!({"type": "MouseMove"}),
			json!({"type": "unknown"}),
		] {
			assert!(events(&input).is_err(), "{input}");
		}
	}

	#[test]
	fn requires_exact_fields_and_rejects_nulls_for_all_schemas() {
		for input in [
			json!({"type": "mouse_move", "x": 0, "y": 0}),
			json!({"type": "mouse_down", "x": 0, "y": 0, "button": "left"}),
			json!({"type": "mouse_up", "x": 0, "y": 0, "button": "right"}),
			json!({"type": "mouse_leave"}),
			json!({"type": "wheel", "lines": 0}),
			json!({"type": "key_down", "key": "a"}),
			json!({"type": "key_up", "key": "a"}),
			json!({"type": "text", "text": ""}),
			json!({"type": "focus", "focused": false}),
		] {
			assert!(events(&input).is_ok(), "{input}");
			let mut extra = input.clone();
			extra["unexpected"] = json!(false);
			assert!(events(&extra).is_err(), "{extra}");
			for field in input.as_object().unwrap().keys() {
				let mut missing = input.clone();
				missing.as_object_mut().unwrap().remove(field);
				assert!(events(&missing).is_err(), "{missing}");
				missing["unexpected"] = json!(0);
				assert!(events(&missing).is_err(), "{missing}");
				let mut null = input.clone();
				null[field] = Value::Null;
				assert!(events(&null).is_err(), "{null}");
			}
		}
	}

	#[test]
	fn mouse_movement_preserves_fractional_and_boundary_coordinates() {
		for (x, y) in [(-1.25, 2.5), (-MAX_COORDINATE, MAX_COORDINATE), (0.0, -0.0)] {
			match one(json!({"type": "mouse_move", "x": x, "y": y})) {
				PlayerEvent::MouseMove {
					x: actual_x,
					y: actual_y,
				} => {
					assert_eq!(actual_x, x);
					assert_eq!(actual_y, y);
				}
				_ => panic!("expected mouse movement"),
			}
		}
		assert!(matches!(
			one(json!({"type": "mouse_leave"})),
			PlayerEvent::MouseLeave
		));
	}

	#[test]
	fn rejects_bad_coordinates_on_each_mouse_event() {
		for kind in ["mouse_move", "mouse_down", "mouse_up"] {
			for field in ["x", "y"] {
				for value in [
					json!(MAX_COORDINATE + 0.25),
					json!(-MAX_COORDINATE - 0.25),
					json!(f64::MAX),
					json!(u64::MAX),
					json!(null),
					json!("NaN"),
					json!("Infinity"),
					json!("1"),
					json!(true),
					json!([]),
					json!({}),
				] {
					let mut input = json!({"type": kind, "x": 0, "y": 0});
					if kind != "mouse_move" {
						input["button"] = json!("left");
					}
					input[field] = value;
					assert!(events(&input).is_err(), "{input}");
				}
			}
		}
	}

	#[test]
	fn maps_all_buttons_and_leaves_click_count_to_ruffle() {
		for (name, expected) in [
			("left", MouseButton::Left),
			("right", MouseButton::Right),
			("middle", MouseButton::Middle),
		] {
			for kind in ["mouse_down", "mouse_up"] {
				match (
					kind,
					one(json!({"type": kind, "x": -5, "y": 7.5, "button": name})),
				) {
					(
						"mouse_down",
						PlayerEvent::MouseDown {
							x,
							y,
							button,
							index: None,
						},
					)
					| ("mouse_up", PlayerEvent::MouseUp { x, y, button }) => {
						assert_eq!((x, y), (-5.0, 7.5));
						assert_eq!(button, expected);
					}
					_ => panic!("expected matching button event"),
				}
			}
		}
	}

	#[test]
	fn rejects_unknown_or_malformed_buttons() {
		for kind in ["mouse_down", "mouse_up"] {
			for button in [
				json!("Left"),
				json!("back"),
				json!(""),
				json!(0),
				json!(false),
				json!(null),
				json!([]),
				json!({}),
			] {
				assert!(events(&json!({"type": kind, "x": 0, "y": 0, "button": button})).is_err());
			}
		}
	}

	#[test]
	fn wheel_lines_preserve_sign_and_fraction_with_inclusive_bounds() {
		for lines in [-MAX_WHEEL_LINES, -0.25, 0.0, 0.5, MAX_WHEEL_LINES] {
			match one(json!({"type": "wheel", "lines": lines})) {
				PlayerEvent::MouseWheel {
					delta: MouseWheelDelta::Lines(actual),
				} => assert_eq!(actual, lines),
				_ => panic!("expected line-based wheel event"),
			}
		}
		for lines in [
			json!(MAX_WHEEL_LINES + 0.25),
			json!(-MAX_WHEEL_LINES - 0.25),
			json!(f64::MAX),
			json!("1"),
			json!(false),
			json!(null),
			json!([]),
			json!({}),
		] {
			assert!(events(&json!({"type": "wheel", "lines": lines})).is_err());
		}
	}

	#[test]
	fn focus_requires_a_boolean_and_maps_both_states() {
		assert!(matches!(
			one(json!({"type": "focus", "focused": true})),
			PlayerEvent::FocusGained
		));
		assert!(matches!(
			one(json!({"type": "focus", "focused": false})),
			PlayerEvent::FocusLost
		));
		for focused in [
			json!(0),
			json!(1),
			json!("true"),
			json!("false"),
			json!(null),
			json!([]),
			json!({}),
		] {
			assert!(events(&json!({"type": "focus", "focused": focused})).is_err());
		}
	}

	#[test]
	fn text_preserves_unicode_scalars_and_order() {
		let text = "A\u{e9}\u{416}\u{4e2d}\u{1f600}e\u{301}\n\t\0";
		let parsed = events(&json!({"type": "text", "text": text})).unwrap();
		let actual: String = parsed
			.into_iter()
			.map(|event| match event {
				PlayerEvent::TextInput { codepoint } => codepoint,
				_ => panic!("expected text input only"),
			})
			.collect();
		assert_eq!(actual, text);
		assert!(
			events(&json!({"type": "text", "text": ""}))
				.unwrap()
				.is_empty()
		);
	}

	#[test]
	fn text_limit_counts_utf8_bytes_not_characters() {
		for unit in ["a", "\u{e9}", "\u{1f600}"] {
			let text = unit.repeat(MAX_TEXT_BYTES / unit.len());
			assert_eq!(text.len(), MAX_TEXT_BYTES);
			let parsed = events(&json!({"type": "text", "text": text})).unwrap();
			assert_eq!(parsed.len(), MAX_TEXT_BYTES / unit.len());
			assert!(events(&json!({"type": "text", "text": text + "a"})).is_err());
		}
		for text in [json!(null), json!(0), json!(true), json!([]), json!({})] {
			assert!(events(&json!({"type": "text", "text": text})).is_err());
		}
	}

	#[test]
	fn character_keys_use_explicit_us_positions_and_preserve_logical_case() {
		for (characters, physical_key) in [
			("aA", PhysicalKey::KeyA),
			("bB", PhysicalKey::KeyB),
			("cC", PhysicalKey::KeyC),
			("dD", PhysicalKey::KeyD),
			("eE", PhysicalKey::KeyE),
			("fF", PhysicalKey::KeyF),
			("gG", PhysicalKey::KeyG),
			("hH", PhysicalKey::KeyH),
			("iI", PhysicalKey::KeyI),
			("jJ", PhysicalKey::KeyJ),
			("kK", PhysicalKey::KeyK),
			("lL", PhysicalKey::KeyL),
			("mM", PhysicalKey::KeyM),
			("nN", PhysicalKey::KeyN),
			("oO", PhysicalKey::KeyO),
			("pP", PhysicalKey::KeyP),
			("qQ", PhysicalKey::KeyQ),
			("rR", PhysicalKey::KeyR),
			("sS", PhysicalKey::KeyS),
			("tT", PhysicalKey::KeyT),
			("uU", PhysicalKey::KeyU),
			("vV", PhysicalKey::KeyV),
			("wW", PhysicalKey::KeyW),
			("xX", PhysicalKey::KeyX),
			("yY", PhysicalKey::KeyY),
			("zZ", PhysicalKey::KeyZ),
			("0)", PhysicalKey::Digit0),
			("1!", PhysicalKey::Digit1),
			("2@", PhysicalKey::Digit2),
			("3#", PhysicalKey::Digit3),
			("4$", PhysicalKey::Digit4),
			("5%", PhysicalKey::Digit5),
			("6^", PhysicalKey::Digit6),
			("7&", PhysicalKey::Digit7),
			("8*", PhysicalKey::Digit8),
			("9(", PhysicalKey::Digit9),
			("`~", PhysicalKey::Backquote),
			("-_", PhysicalKey::Minus),
			("=+", PhysicalKey::Equal),
			("[{", PhysicalKey::BracketLeft),
			("]}", PhysicalKey::BracketRight),
			("\\|", PhysicalKey::Backslash),
			(";:", PhysicalKey::Semicolon),
			("'\"", PhysicalKey::Quote),
			(",<", PhysicalKey::Comma),
			(".>", PhysicalKey::Period),
			("/?", PhysicalKey::Slash),
			(" ", PhysicalKey::Space),
			("\u{e9}\u{416}\u{4e2d}\u{1f600}", PhysicalKey::Unknown),
		] {
			for character in characters.chars() {
				assert_key(
					&character.to_string(),
					KeyDescriptor {
						physical_key,
						logical_key: LogicalKey::Character(character),
						key_location: KeyLocation::Standard,
					},
				);
			}
		}
	}

	#[test]
	fn named_keys_map_to_matching_logical_and_physical_keys() {
		for (name, physical_key, named) in [
			("Backspace", PhysicalKey::Backspace, NamedKey::Backspace),
			("Tab", PhysicalKey::Tab, NamedKey::Tab),
			("Enter", PhysicalKey::Enter, NamedKey::Enter),
			("Escape", PhysicalKey::Escape, NamedKey::Escape),
			("Insert", PhysicalKey::Insert, NamedKey::Insert),
			("Delete", PhysicalKey::Delete, NamedKey::Delete),
			("Home", PhysicalKey::Home, NamedKey::Home),
			("End", PhysicalKey::End, NamedKey::End),
			("PageUp", PhysicalKey::PageUp, NamedKey::PageUp),
			("PageDown", PhysicalKey::PageDown, NamedKey::PageDown),
			("ArrowLeft", PhysicalKey::ArrowLeft, NamedKey::ArrowLeft),
			("ArrowRight", PhysicalKey::ArrowRight, NamedKey::ArrowRight),
			("ArrowUp", PhysicalKey::ArrowUp, NamedKey::ArrowUp),
			("ArrowDown", PhysicalKey::ArrowDown, NamedKey::ArrowDown),
			("CapsLock", PhysicalKey::CapsLock, NamedKey::CapsLock),
			("NumLock", PhysicalKey::NumLock, NamedKey::NumLock),
			("ScrollLock", PhysicalKey::ScrollLock, NamedKey::ScrollLock),
			(
				"ContextMenu",
				PhysicalKey::ContextMenu,
				NamedKey::ContextMenu,
			),
			("Pause", PhysicalKey::Pause, NamedKey::Pause),
			(
				"PrintScreen",
				PhysicalKey::PrintScreen,
				NamedKey::PrintScreen,
			),
			("F1", PhysicalKey::F1, NamedKey::F1),
			("F2", PhysicalKey::F2, NamedKey::F2),
			("F3", PhysicalKey::F3, NamedKey::F3),
			("F4", PhysicalKey::F4, NamedKey::F4),
			("F5", PhysicalKey::F5, NamedKey::F5),
			("F6", PhysicalKey::F6, NamedKey::F6),
			("F7", PhysicalKey::F7, NamedKey::F7),
			("F8", PhysicalKey::F8, NamedKey::F8),
			("F9", PhysicalKey::F9, NamedKey::F9),
			("F10", PhysicalKey::F10, NamedKey::F10),
			("F11", PhysicalKey::F11, NamedKey::F11),
			("F12", PhysicalKey::F12, NamedKey::F12),
			("F13", PhysicalKey::F13, NamedKey::F13),
			("F14", PhysicalKey::F14, NamedKey::F14),
			("F15", PhysicalKey::F15, NamedKey::F15),
			("F16", PhysicalKey::F16, NamedKey::F16),
			("F17", PhysicalKey::F17, NamedKey::F17),
			("F18", PhysicalKey::F18, NamedKey::F18),
			("F19", PhysicalKey::F19, NamedKey::F19),
			("F20", PhysicalKey::F20, NamedKey::F20),
			("F21", PhysicalKey::F21, NamedKey::F21),
			("F22", PhysicalKey::F22, NamedKey::F22),
			("F23", PhysicalKey::F23, NamedKey::F23),
			("F24", PhysicalKey::F24, NamedKey::F24),
		] {
			assert_key(
				name,
				KeyDescriptor {
					physical_key,
					logical_key: LogicalKey::Named(named),
					key_location: KeyLocation::Standard,
				},
			);
		}
		assert_key(
			"Space",
			KeyDescriptor {
				physical_key: PhysicalKey::Space,
				logical_key: LogicalKey::Character(' '),
				key_location: KeyLocation::Standard,
			},
		);
	}

	#[test]
	fn modifier_names_use_left_location_without_synthetic_events() {
		for (name, physical_key, named) in [
			("Shift", PhysicalKey::ShiftLeft, NamedKey::Shift),
			("Control", PhysicalKey::ControlLeft, NamedKey::Control),
			("Alt", PhysicalKey::AltLeft, NamedKey::Alt),
			("Meta", PhysicalKey::SuperLeft, NamedKey::Super),
			("Super", PhysicalKey::SuperLeft, NamedKey::Super),
		] {
			assert_key(
				name,
				KeyDescriptor {
					physical_key,
					logical_key: LogicalKey::Named(named),
					key_location: KeyLocation::Left,
				},
			);
		}
	}

	#[test]
	fn rejects_unknown_names_multicharacter_keys_and_wrong_types() {
		for kind in ["key_down", "key_up"] {
			for value in [
				json!(""),
				json!("ab"),
				json!("e\u{301}"),
				json!("\u{1f1e7}\u{1f1fe}"),
				json!("enter"),
				json!("Unknown"),
				json!("Unidentified"),
				json!("F0"),
				json!("F25"),
				json!("\n"),
				json!("\t"),
				json!("\0"),
				json!("\u{7f}"),
				json!("\u{85}"),
				json!("a".repeat(MAX_KEY_BYTES + 1)),
				json!(null),
				json!(1),
				json!(false),
				json!([]),
				json!({}),
			] {
				let input = json!({"type": kind, "key": value});
				assert!(events(&input).is_err(), "{input}");
			}
		}
	}

	#[test]
	fn errors_do_not_echo_unbounded_input() {
		let large = "x".repeat(64 * 1024);
		for input in [
			json!({"type": large}),
			json!({"type": "key_down", "key": large}),
			json!({"type": "text", "text": large}),
			json!({"type": "mouse_down", "x": 0, "y": 0, "button": large}),
			json!({"type": "mouse_leave", (large): true}),
		] {
			assert!(events(&input).unwrap_err().len() < 128);
		}
	}
}
