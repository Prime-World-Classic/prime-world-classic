//! Host-side held-input lifecycle; Ruffle's FocusLost alone only clears focus.
use ruffle_core::events::{KeyDescriptor, MouseButton, PhysicalKey, PlayerEvent};

/// Track a bounded set of held controls and synthesize releases before losing focus.
#[derive(Default)]
pub struct InputState {
	keys: Vec<KeyDescriptor>,
	buttons: Vec<MouseButton>,
}

fn same_key(a: &KeyDescriptor, b: &KeyDescriptor) -> bool {
	if a.physical_key == PhysicalKey::Unknown || b.physical_key == PhysicalKey::Unknown {
		a.logical_key == b.logical_key
	} else {
		a.physical_key == b.physical_key
	}
}

impl InputState {
	/// Called after complete JSON validation, once per parsed event.
	pub fn expand(&mut self, event: PlayerEvent) -> Result<Vec<PlayerEvent>, String> {
		match &event {
			PlayerEvent::KeyDown { key } => {
				if !self.keys.iter().any(|held| same_key(held, key)) {
					if self.keys.len() >= 128 {
						return Err("Held-key limit exceeded".into());
					}
					self.keys.push(*key);
				}
			}
			PlayerEvent::KeyUp { key } => {
				self.keys.retain(|held| !same_key(held, key));
			}
			PlayerEvent::MouseDown { button, .. } => {
				if !self.buttons.contains(button) {
					self.buttons.push(*button);
				}
			}
			PlayerEvent::MouseUp { button, .. } => {
				self.buttons.retain(|held| held != button);
			}
			PlayerEvent::FocusLost => {
				let mut events = vec![PlayerEvent::MouseLeave];
				// Release outside the movie so focus loss cannot accidentally click a button.
				events.extend(self.buttons.drain(..).map(|button| PlayerEvent::MouseUp {
					x: -1_000_000.0,
					y: -1_000_000.0,
					button,
				}));
				events.extend(self.keys.drain(..).map(|key| PlayerEvent::KeyUp { key }));
				events.push(event);
				return Ok(events);
			}
			_ => {}
		}
		Ok(vec![event])
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use ruffle_core::events::{KeyLocation, LogicalKey};
	fn key(c: char) -> KeyDescriptor {
		KeyDescriptor {
			physical_key: PhysicalKey::Unknown,
			logical_key: LogicalKey::Character(c),
			key_location: KeyLocation::Standard,
		}
	}
	#[test]
	fn focus_loss_releases_each_held_control_once() {
		let mut state = InputState::default();
		for _ in 0..2 {
			state
				.expand(PlayerEvent::KeyDown { key: key('x') })
				.unwrap();
		}
		state
			.expand(PlayerEvent::MouseDown {
				x: 10.0,
				y: 20.0,
				button: MouseButton::Left,
				index: None,
			})
			.unwrap();
		let releases = state.expand(PlayerEvent::FocusLost).unwrap();
		assert_eq!(releases.len(), 4);
		assert!(matches!(releases[1],PlayerEvent::MouseUp {x,y,..} if x<0.0 && y<0.0));
		assert!(matches!(releases[2], PlayerEvent::KeyUp { .. }));
		assert_eq!(state.expand(PlayerEvent::FocusLost).unwrap().len(), 2);
	}
	#[test]
	fn explicit_release_and_capacity_are_bounded() {
		let mut state = InputState::default();
		for i in 0..128 {
			state
				.expand(PlayerEvent::KeyDown {
					key: key(char::from_u32(256 + i).unwrap()),
				})
				.unwrap();
		}
		assert!(
			state
				.expand(PlayerEvent::KeyDown { key: key('z') })
				.is_err()
		);
		state
			.expand(PlayerEvent::KeyUp {
				key: key('\u{100}'),
			})
			.unwrap();
		state
			.expand(PlayerEvent::KeyDown { key: key('z') })
			.unwrap();
		assert_eq!(state.expand(PlayerEvent::FocusLost).unwrap().len(), 130);
	}
}
