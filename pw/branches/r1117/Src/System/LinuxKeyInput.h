#ifndef SYSTEM_LINUX_KEY_INPUT_H_INCLUDED
#define SYSTEM_LINUX_KEY_INPUT_H_INCLUDED

#include "VirtualKey.h"
#include <X11/keysym.h>

namespace NMainFrame
{
/// Keep the existing VK payload separate from the original XLookupString identity.
struct LinuxKeyInput
{
	int virtualKey;
	unsigned long nativeKeySym;
};

/**
 * Convert a key edge without losing its original keysym. Text is not a key edge.
 * Pass an unshifted keysym only for opt-in digit stabilization; other bases are ignored.
 * Unknown symbols retain the historical int cast. Never infer raw identity from VK.
 */
inline LinuxKeyInput NormalizeLinuxKeyInput(unsigned long nativeKeySym, unsigned long unshiftedKeySym = 0)
{
	namespace Key = EVirtualKeyCode;
	const unsigned long keySym = unshiftedKeySym >= XK_0 && unshiftedKeySym <= XK_9 ?
		unshiftedKeySym : nativeKeySym;

	if (keySym >= XK_0 && keySym <= XK_9)
		return {static_cast<int>(keySym), nativeKeySym};
	if (keySym >= XK_A && keySym <= XK_Z)
		return {static_cast<int>(keySym), nativeKeySym};
	if (keySym >= XK_a && keySym <= XK_z)
		return {static_cast<int>('A' + keySym - XK_a), nativeKeySym};
	if (keySym >= XK_F1 && keySym <= XK_F24)
		return {Key::F1 + static_cast<int>(keySym - XK_F1), nativeKeySym};
	if (keySym >= XK_KP_0 && keySym <= XK_KP_9)
		return {Key::Numpad0 + static_cast<int>(keySym - XK_KP_0), nativeKeySym};

	switch (keySym)
	{
		case XK_BackSpace: return {Key::Backspace, nativeKeySym};
		case XK_Tab:
		case XK_ISO_Left_Tab: return {Key::Tab, nativeKeySym};
		case XK_Return:
		case XK_KP_Enter: return {Key::Enter, nativeKeySym};
		case XK_Shift_L:
		case XK_Shift_R: return {Key::Shift, nativeKeySym};
		case XK_Control_L:
		case XK_Control_R: return {Key::Control, nativeKeySym};
		case XK_Alt_L:
		case XK_Alt_R:
		case XK_Meta_L:
		case XK_Meta_R: return {Key::Alt, nativeKeySym};
		case XK_Pause: return {Key::Pause, nativeKeySym};
		case XK_Caps_Lock: return {Key::CapsLock, nativeKeySym};
		case XK_Escape: return {Key::Escape, nativeKeySym};
		case XK_space:
		case XK_KP_Space: return {Key::Space, nativeKeySym};
		case XK_Page_Up:
		case XK_KP_Page_Up: return {Key::PageUp, nativeKeySym};
		case XK_Page_Down:
		case XK_KP_Page_Down: return {Key::PageDown, nativeKeySym};
		case XK_End:
		case XK_KP_End: return {Key::End, nativeKeySym};
		case XK_Home:
		case XK_KP_Home: return {Key::Home, nativeKeySym};
		case XK_Left:
		case XK_KP_Left: return {Key::Left, nativeKeySym};
		case XK_Up:
		case XK_KP_Up: return {Key::Up, nativeKeySym};
		case XK_Right:
		case XK_KP_Right: return {Key::Right, nativeKeySym};
		case XK_Down:
		case XK_KP_Down: return {Key::Down, nativeKeySym};
		case XK_Insert:
		case XK_KP_Insert: return {Key::Insert, nativeKeySym};
		case XK_Delete:
		case XK_KP_Delete: return {Key::Delete, nativeKeySym};
		case XK_Super_L: return {Key::LeftSystem, nativeKeySym};
		case XK_Super_R: return {Key::RightSystem, nativeKeySym};
		case XK_Menu: return {Key::ContextMenu, nativeKeySym};
		case XK_KP_Multiply: return {Key::Multiply, nativeKeySym};
		case XK_KP_Add: return {Key::Add, nativeKeySym};
		case XK_KP_Separator: return {Key::Separator, nativeKeySym};
		case XK_KP_Subtract: return {Key::Subtract, nativeKeySym};
		case XK_KP_Decimal: return {Key::Decimal, nativeKeySym};
		case XK_KP_Divide: return {Key::Divide, nativeKeySym};
		case XK_Num_Lock: return {Key::NumLock, nativeKeySym};
		case XK_Scroll_Lock: return {Key::ScrollLock, nativeKeySym};
		default: return {static_cast<int>(keySym), nativeKeySym};
	}
}
}

#endif // SYSTEM_LINUX_KEY_INPUT_H_INCLUDED
