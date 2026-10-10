#include "../System/LinuxKeyInput.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

#ifdef PW_KEY_INPUT_NATIVE_PUMP
// Section garbage collection keeps this optional probe independent of window/GL libraries.
#include "../System/MainFrame_linux.cpp"
#else
// Mock only unrelated engine declarations; exercise the real message layout below.
struct IBaseInterfaceST {};
using DWORD = unsigned long;
namespace nstl { class string; }
class wstring;
template <typename T> class vector;
#define _interface struct
#define NI_DECLARE_CLASS_1(...)
#include "../System/MainFrame.h"
#undef NI_DECLARE_CLASS_1
#undef _interface
#endif

#ifdef PW_KEY_INPUT_NATIVE_PUMP
namespace
{
KeySym lookupSymbol = NoSymbol;
KeySym lookupBase = NoSymbol;
unsigned char lookupText = 0;
bool lookupHasText = false;
}

/// Mock Xlib's lookup only; ProcessKeyEvent and the message queue remain production code.
extern "C" int XLookupString(XKeyEvent*, char* buffer, int size, KeySym* symbol, XComposeStatus*)
{
	*symbol = lookupSymbol;
	if (!lookupHasText || size <= 0) return 0;
	buffer[0] = static_cast<char>(lookupText);
	return 1;
}

/// Supply a physical base symbol without requiring a display or a keyboard layout.
extern "C" KeySym XLookupKeysym(XKeyEvent*, int)
{
	return lookupBase;
}

/// Deterministic clock for the production queue's message timestamps.
void NHPTimer::GetTime(NHPTimer::STime& time)
{
	time = 123;
}
#endif

namespace
{
std::size_t checks = 0;

/// Keep checks active in release builds and report the offending keysym.
void Check(bool condition, const char* description, unsigned long symbol = 0)
{
	++checks;
	if (condition) return;
	std::fprintf(stderr, "Key input: %s, keysym=0x%lx (check %zu)\n", description, symbol, checks);
	std::exit(1);
}

/// Independent fixtures describe the existing X11-to-VK wire contract, not enum equality.
std::array<int, 65536> ExpectedMapping()
{
	std::array<int, 65536> expected{};
	for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = static_cast<int>(i);
	for (unsigned long key = XK_a; key <= XK_z; ++key) expected[key] = 'A' + key - XK_a;
	for (unsigned long key = XK_F1; key <= XK_F24; ++key) expected[key] = 0x70 + key - XK_F1;
	for (unsigned long key = XK_KP_0; key <= XK_KP_9; ++key) expected[key] = 0x60 + key - XK_KP_0;
	const struct { unsigned long symbol; int value; } special[] = {
		{XK_BackSpace, 0x08}, {XK_Tab, 0x09}, {XK_ISO_Left_Tab, 0x09},
		{XK_Return, 0x0d}, {XK_KP_Enter, 0x0d},
		{XK_Shift_L, 0x10}, {XK_Shift_R, 0x10},
		{XK_Control_L, 0x11}, {XK_Control_R, 0x11},
		{XK_Alt_L, 0x12}, {XK_Alt_R, 0x12}, {XK_Meta_L, 0x12}, {XK_Meta_R, 0x12},
		{XK_Pause, 0x13}, {XK_Caps_Lock, 0x14}, {XK_Escape, 0x1b},
		{XK_space, 0x20}, {XK_KP_Space, 0x20},
		{XK_Page_Up, 0x21}, {XK_KP_Page_Up, 0x21},
		{XK_Page_Down, 0x22}, {XK_KP_Page_Down, 0x22},
		{XK_End, 0x23}, {XK_KP_End, 0x23}, {XK_Home, 0x24}, {XK_KP_Home, 0x24},
		{XK_Left, 0x25}, {XK_KP_Left, 0x25}, {XK_Up, 0x26}, {XK_KP_Up, 0x26},
		{XK_Right, 0x27}, {XK_KP_Right, 0x27}, {XK_Down, 0x28}, {XK_KP_Down, 0x28},
		{XK_Insert, 0x2d}, {XK_KP_Insert, 0x2d}, {XK_Delete, 0x2e}, {XK_KP_Delete, 0x2e},
		{XK_Super_L, 0x5b}, {XK_Super_R, 0x5c}, {XK_Menu, 0x5d},
		{XK_KP_Multiply, 0x6a}, {XK_KP_Add, 0x6b}, {XK_KP_Separator, 0x6c},
		{XK_KP_Subtract, 0x6d}, {XK_KP_Decimal, 0x6e}, {XK_KP_Divide, 0x6f},
		{XK_Num_Lock, 0x90}, {XK_Scroll_Lock, 0x91}
	};
	for (const auto& entry : special) expected[entry.symbol] = entry.value;
	return expected;
}

/// Exhaust the legacy keysym space and every Unicode keysym, including passthroughs.
void Mapping()
{
	const auto expected = ExpectedMapping();
	for (unsigned long symbol = 0; symbol < expected.size(); ++symbol)
	{
		const auto key = NMainFrame::NormalizeLinuxKeyInput(symbol);
		Check(key.virtualKey == expected[symbol], "legacy conversion changed", symbol);
		Check(key.nativeKeySym == symbol, "legacy identity changed", symbol);
	}
	for (unsigned long symbol = 0x01000000; symbol <= 0x0110ffff; ++symbol)
	{
		const auto key = NMainFrame::NormalizeLinuxKeyInput(symbol);
		Check(key.virtualKey == static_cast<int>(symbol), "Unicode passthrough changed", symbol);
		Check(key.nativeKeySym == symbol, "Unicode identity changed", symbol);
	}
	const unsigned long boundaries[] = {0x10000, 0xffffff, 0x01110000, 0x1008ff12,
		static_cast<unsigned long>(std::numeric_limits<int>::max()),
		static_cast<unsigned long>(std::numeric_limits<int>::max()) + 1,
		std::numeric_limits<unsigned long>::max()};
	for (const auto symbol : boundaries)
	{
		const auto key = NMainFrame::NormalizeLinuxKeyInput(symbol);
		Check(key.virtualKey == static_cast<int>(symbol), "fallback cast changed", symbol);
		Check(key.nativeKeySym == symbol, "wide identity truncated", symbol);
	}
}

/// Equal VK payloads must remain distinguishable to Linux keysym consumers.
void Collisions()
{
	const unsigned long pairs[][2] = {
		{XK_Delete, XK_period}, {XK_Insert, XK_minus}, {XK_Super_L, XK_bracketleft},
		{XK_Menu, XK_bracketright}, {XK_KP_0, XK_grave},
		{XK_Return, XK_KP_Enter}, {XK_Tab, XK_ISO_Left_Tab},
		{XK_Shift_L, XK_Shift_R}, {XK_Control_L, XK_Control_R}, {XK_Alt_L, XK_Alt_R}
	};
	for (const auto& pair : pairs)
	{
		const auto first = NMainFrame::NormalizeLinuxKeyInput(pair[0]);
		const auto second = NMainFrame::NormalizeLinuxKeyInput(pair[1]);
		Check(first.virtualKey == second.virtualKey, "fixture must collide in VK", pair[0]);
		Check(first.nativeKeySym != second.nativeKeySym, "collision lost raw identity", pair[0]);
	}
	for (unsigned long symbol = XK_F1; symbol <= XK_F11; ++symbol)
	{
		const auto key = NMainFrame::NormalizeLinuxKeyInput(symbol);
		Check(key.virtualKey == static_cast<int>('p' + symbol - XK_F1), "function/letter collision", symbol);
		Check(key.nativeKeySym != static_cast<unsigned long>(key.virtualKey), "function became letter", symbol);
	}
	for (unsigned long symbol = XK_KP_1; symbol <= XK_KP_9; ++symbol)
	{
		const auto key = NMainFrame::NormalizeLinuxKeyInput(symbol);
		Check(key.virtualKey == static_cast<int>('a' + symbol - XK_KP_1), "keypad/letter collision", symbol);
		Check(key.nativeKeySym != static_cast<unsigned long>(key.virtualKey), "keypad became letter", symbol);
	}
}

/// Only an explicitly supplied top-row base digit changes VK; raw identity never changes.
void StableDigits()
{
	const unsigned long shifted[] = {XK_parenright, XK_exclam, XK_at, XK_numbersign, XK_dollar,
		XK_percent, XK_asciicircum, XK_ampersand, XK_asterisk, XK_parenleft};
	for (unsigned digit = 0; digit < 10; ++digit)
	{
		const unsigned long base = XK_0 + digit;
		const auto ordinary = NMainFrame::NormalizeLinuxKeyInput(shifted[digit]);
		Check(ordinary.virtualKey == static_cast<int>(shifted[digit]), "default shifted digit changed", base);
		for (const unsigned long symbol : {base, shifted[digit]})
		{
			const auto key = NMainFrame::NormalizeLinuxKeyInput(symbol, base);
			Check(key.virtualKey == static_cast<int>(base), "opt-in digit is not stable", symbol);
			Check(key.nativeKeySym == symbol, "stabilization overwrote raw keysym", symbol);
		}
	}
	for (unsigned long base = 0; base <= 0xffff; ++base)
	{
		const auto key = NMainFrame::NormalizeLinuxKeyInput(XK_exclam, base);
		const int expected = base >= XK_0 && base <= XK_9 ? static_cast<int>(base) : XK_exclam;
		Check(key.virtualKey == expected, "non-digit base affected conversion", base);
		Check(key.nativeKeySym == XK_exclam, "base affected raw identity", base);
	}
}

/// Verify real message initialization, edge copies, repeat metadata and separate text payloads.
void Messages()
{
	using Message = NMainFrame::SWindowsMsg;
	static_assert(std::is_trivially_copyable<Message>::value, "Messages must remain copyable as bytes");
	for (int type = Message::MOUSE_WHEEL; type <= Message::CLOSE; ++type)
	{
		alignas(Message) unsigned char storage[sizeof(Message)];
		std::memset(storage, 0xa5, sizeof(storage));
		auto* message = new (storage) Message;
		message->msg = static_cast<Message::EMsg>(type);
		Check(message->nativeKeySym == 0, "default message identity must be zero", type);
		message->~Message();
	}
	for (const auto type : {Message::KEY_DOWN, Message::KEY_UP})
	for (const int repeat : {1, 2, 17})
	for (const unsigned long flags : {0UL, 1UL, 4UL, 8UL, 64UL, 77UL})
	{
		const auto key = NMainFrame::NormalizeLinuxKeyInput(XK_exclam, XK_1);
		Message message = {};
		message.msg = type;
		message.nKey = key.virtualKey;
		message.nativeKeySym = key.nativeKeySym;
		message.nRep = repeat;
		message.dwFlags = flags;
		const Message copy = message;
		Check(copy.nKey == '1' && copy.nativeKeySym == XK_exclam, "edge copy lost either identity");
		Check(copy.msg == type && copy.nRep == repeat && copy.dwFlags == flags, "edge metadata changed");
	}
	for (int value = 0; value <= 255; ++value)
	{
		Message text = {};
		text.msg = Message::KEY_CHAR;
		text.nKey = value;
		Check(text.nativeKeySym == 0 && text.nKey == value, "text acquired a key identity", value);
	}
}

#ifdef PW_KEY_INPUT_NATIVE_PUMP
/// Exercise actual native enqueue/dequeue, text, repeated Down, Up, mouse and TIME paths.
void NativePump()
{
	using Message = NMainFrame::SWindowsMsg;
	const auto expected = ExpectedMapping();
	for (unsigned long symbol = 0; symbol < expected.size(); ++symbol)
	for (const bool down : {false, true})
	{
		lookupSymbol = symbol;
		lookupBase = NoSymbol;
		lookupHasText = false;
		XKeyEvent event = {};
		ProcessKeyEvent(event, down);
		Message message;
		Check(NMainFrame::GetMessage(&message), "native edge missing", symbol);
		Check(message.msg == (down ? Message::KEY_DOWN : Message::KEY_UP), "native edge changed", symbol);
		Check(message.nKey == expected[symbol] && message.nativeKeySym == symbol, "native identities changed", symbol);
		Check(message.nRep == 1 && message.dwFlags == 0 && message.time == 123, "native metadata changed", symbol);
		Check(!NMainFrame::GetMessage(&message), "unexpected extra native event", symbol);
		Check(message.msg == Message::TIME && message.nativeKeySym == 0, "TIME retained stale identity", symbol);
	}
	for (const bool down : {false, true})
	for (const int repeat : {1, 2})
	for (const unsigned long symbol : {XK_1, XK_exclam})
	{
		lookupSymbol = symbol;
		lookupBase = XK_1;
		lookupText = static_cast<unsigned char>(symbol);
		lookupHasText = true;
		XKeyEvent event = {};
		event.state = ShiftMask | ControlMask;
		ProcessKeyEvent(event, down, repeat);
		Message message;
		Check(NMainFrame::GetMessage(&message), "digit edge missing");
#ifdef PW_LINUX_RUFFLE_INSPECTION
		Check(message.nKey == '1' && message.dwFlags == event.state, "opt-in digit/flags changed");
#else
		Check(message.nKey == static_cast<int>(symbol) && message.dwFlags == 0, "default digit/flags changed");
#endif
		Check(message.nativeKeySym == symbol && message.nRep == repeat, "digit identity/repeat changed");
		if (down)
		{
			Check(NMainFrame::GetMessage(&message), "native text missing");
			Check(message.msg == Message::KEY_CHAR && message.nKey == static_cast<int>(symbol), "text was normalized");
			Check(message.nativeKeySym == 0 && message.nRep == repeat, "text acquired raw identity");
		}
		Check(!NMainFrame::GetMessage(&message) && message.nativeKeySym == 0, "queue did not reset identity");
	}
	for (int value = 0; value <= 255; ++value)
	{
		lookupSymbol = XK_a;
		lookupBase = XK_a;
		lookupText = static_cast<unsigned char>(value);
		lookupHasText = true;
		XKeyEvent event = {};
		ProcessKeyEvent(event, true);
		Message message;
		Check(NMainFrame::GetMessage(&message) && message.nKey == 'A' && message.nativeKeySym == XK_a,
			"native letter edge changed", value);
		Check(NMainFrame::GetMessage(&message) && message.msg == Message::KEY_CHAR && message.nKey == value,
			"native text byte changed", value);
		Check(message.nativeKeySym == 0, "native text retained edge identity", value);
		Check(!NMainFrame::GetMessage(&message) && message.nativeKeySym == 0, "native text queue did not drain", value);
	}
	PushMouseMessage(Message::MOUSE_MOVE, 12, 34, 56);
	Message mouse;
	Check(NMainFrame::GetMessage(&mouse), "mouse message missing");
	Check(mouse.nativeKeySym == 0 && mouse.x == 12 && mouse.y == 34 && mouse.dwFlags == 56,
		"mouse payload changed");
	Check(!NMainFrame::GetMessage(&mouse) && mouse.nativeKeySym == 0, "mouse TIME identity changed");
}
#endif
}

/// Standalone native regression probe; no X server, engine libraries or game assets.
int main()
{
	Mapping();
	Collisions();
	StableDigits();
	Messages();
#ifdef PW_KEY_INPUT_NATIVE_PUMP
	NativePump();
#endif
	std::printf("Key input: %zu checks passed\n", checks);
	return 0;
}
