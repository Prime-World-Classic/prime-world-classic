#include "../System/systemStdAfx.h"
#include "text_runtime_probe.h"
#include "../System/Texts.h"
#include "../System/MemoryStream.h"
#include "../System/FileSystem/TestFileSystem.h"
#include "../System/FileSystem/WinFileSystem.h"
#include "../System/FileSystem/FileUtils.h"
#include "../libdb/XmlSaver.h"
#include "../Scripts/Script.h"
#include "../Scripts/LuaComplexTypes.h"
#include "../UI/ImageLabel.h"
#include "../UI/ScreenLogicBase.h"
#include "../Render/TextureManager.h"
#include <filesystem>
#include <fstream>
#include <random>

namespace
{
struct CaptionFixture
{
	CTextRef caption;
	int operator&(IXmlSaver& saver)
	{
		saver.Add("caption", &caption);
		return 0;
	}
};

// Use the production XML reference path, including its leading-slash normalization.
CTextRef ReadReference(const char* path)
{
	const string xml = string("<Probe><caption textref=\"") + path + "\"/></Probe>";
	CObj<MemoryStream> stream = new MemoryStream();
	stream->Write(xml.data(), xml.size());
	stream->Seek(0, SEEKORIGIN_BEGIN);
	CObj<IXmlSaver> saver = CreateXmlSaver(stream, true);
	CaptionFixture fixture;
	saver->Add("Probe", &fixture);
	return fixture.caption;
}

void Check(bool condition, const char* name, int* failures)
{
	printf("Text runtime probe: %s=%s\n", name, condition ? "pass" : "FAIL");
	if (!condition)
		++*failures;
}

/// Cover the native-wide/UTF-8 boundary used by production captions and serializers.
void CheckUnicodeConversions(int* failures)
{
	struct Fixture
	{
		const char* name;
		string utf8;
		wstring wide;
	};
	const Fixture valid[] = {
		{"empty", "", L""},
		{"ascii-markup", "PW <br>", L"PW <br>"},
		{"cyrillic", "\xd0\x91\xd0\xbe\xd0\xb9", L"\u0411\u043e\u0439"},
		{"combining", "e\xcc\x81", L"e\u0301"},
		{"cjk", "\xe4\xb8\xad\xe6\x96\x87", L"\u4e2d\u6587"},
		{"supplementary", "\xf0\x9f\x98\x80", L"\U0001f600"},
		{"scalar-boundaries", "\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf",
			L"\x7f\u0080\u07ff\u0800\ud7ff\ue000\uffff\U00010000\U0010ffff"},
		{"embedded-nul", string("A\0\xf0\x9f\x98\x80", 6), wstring(L"A\0\U0001f600", 3)},
		{"bom-preserved", "\xef\xbb\xbf" "A", L"\ufeff" L"A"}
	};
	for (unsigned int i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
	{
		wstring decoded = L"old caption";
		NStr::UTF8ToUnicode(&decoded, valid[i].utf8);
		Check(decoded == valid[i].wide, NStr::StrFmt("utf8-%s-decode", valid[i].name), failures);
		string encoded = "old caption";
		NStr::UnicodeToUTF8(&encoded, valid[i].wide);
		Check(encoded == valid[i].utf8, NStr::StrFmt("utf8-%s-encode", valid[i].name), failures);
	}
	const wchar_t invalidWide[] = {
		static_cast<wchar_t>(-1), static_cast<wchar_t>(0xd800), static_cast<wchar_t>(0xdfff),
		static_cast<wchar_t>(0x110000), static_cast<wchar_t>(0x7fffffff)
	};
	for (unsigned int i = 0; i < sizeof(invalidWide) / sizeof(invalidWide[0]); ++i)
	{
		wstring invalid = L"prefix";
		invalid += invalidWide[i];
		invalid += L"suffix";
		string encoded = "old caption";
		NStr::UnicodeToUTF8(&encoded, invalid);
		Check(encoded.empty(), NStr::StrFmt("utf8-invalid-wide-%u", i), failures);
	}
	const char* invalidUtf8[] = {
		"\x80", "\xbf", "\xc0\x80", "\xc1\xbf", "\xe0\x80\x80", "\xf0\x80\x80\x80",
		"\xed\xa0\x80", "\xed\xbf\xbf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
		"\xf8\x88\x80\x80\x80", "\xfc\x84\x80\x80\x80\x80", "\xfe", "\xff",
		"\xc2x", "\xe2x\xac", "\xf0\x90x\x80",
		"\xc2", "\xe2", "\xe2\x82", "\xf0", "\xf0\x9f", "\xf0\x9f\x98"
	};
	for (unsigned int i = 0; i < sizeof(invalidUtf8) / sizeof(invalidUtf8[0]); ++i)
	{
		wstring decoded = L"old caption";
		NStr::UTF8ToUnicode(&decoded, string("prefix") + invalidUtf8[i]);
		Check(decoded.empty(), NStr::StrFmt("utf8-invalid-bytes-%u", i), failures);
	}
	// Batch every Unicode scalar to exercise output growth and all four UTF-8 widths.
	wstring scalars;
	scalars.reserve(0x110000 - 0x800);
	for (unsigned int codePoint = 0; codePoint <= 0x10ffff; ++codePoint)
		if (codePoint < 0xd800 || codePoint > 0xdfff)
			scalars += static_cast<wchar_t>(codePoint);
	string encoded;
	wstring decoded;
	NStr::UnicodeToUTF8(&encoded, scalars);
	NStr::UTF8ToUnicode(&decoded, encoded);
	Check(decoded == scalars, "utf8-all-scalars-roundtrip", failures);

	// Rejected input must be empty; accepted input must be canonical, byte-for-byte UTF-8.
	std::mt19937 generator(0x5057);
	bool canonical = true;
	for (int length = 1; length <= 8; ++length)
		for (int sample = 0; sample < 512; ++sample)
		{
			string bytes;
			for (int i = 0; i < length; ++i)
				bytes += static_cast<char>(generator() & 0xff);
			decoded = L"old caption";
			NStr::UTF8ToUnicode(&decoded, bytes);
			if (!decoded.empty())
			{
				NStr::UnicodeToUTF8(&encoded, decoded);
				canonical = (encoded == bytes) && canonical;
			}
		}
	Check(canonical, "utf8-deterministic-byte-corpus", failures);
}

/// Reproduce the map catalog's recursive scan with mock files, not installed data.
void CheckMapFileEnumeration(int* failures)
{
	char folder[] = "/tmp/primeworld-map-enumeration-XXXXXX";
	if (!mkdtemp(folder))
	{
		Check(false, "map-scan-fixture", failures);
		return;
	}
	const std::filesystem::path root(folder);
	std::filesystem::create_directories(root / "Maps/Multiplayer/Mock");
	std::filesystem::create_directories(root / "Maps/.svn");
	std::ofstream(root / "Maps/root.ADMPDSCR.xdb") << "mock";
	std::ofstream(root / "Maps/Multiplayer/Mock/_.ADMPDSCR.xdb") << "mock";
	std::ofstream(root / "Maps/Multiplayer/Mock/unrelated.txt") << "mock";
	std::ofstream(root / "Maps/Multiplayer/Mock/noextension") << "mock";
	std::ofstream(root / "Maps/.svn/ignored.ADMPDSCR.xdb") << "mock";
	CObj<WinFileSystem> files = new WinFileSystem(folder, false);
	vector<string> names;
	files->GetFiles(&names, "Maps\\", "*.ADMPDSCR.xdb", true);
	Check(names.size() == 2 && names[0] == "root.ADMPDSCR.xdb" &&
		names[1] == "Multiplayer/Mock/_.ADMPDSCR.xdb", "map-scan-recursive-backslashes", failures);
	files->GetFiles(&names, "Maps/", "*.ADMPDSCR.xdb", false);
	Check(names.size() == 1 && names[0] == "root.ADMPDSCR.xdb", "map-scan-nonrecursive-basename", failures);
	files->GetFiles(&names, "Maps/Multiplayer\\Mock", "*.ADMPDSCR.xdb", true);
	Check(names.size() == 1 && names[0] == "_.ADMPDSCR.xdb", "map-scan-mixed-separators", failures);
	files->GetFiles(&names, "Maps/Multiplayer/Mock", "*.*", false);
	Check(names.size() == 3, "map-scan-windows-all-files-mask", failures);
	files->GetFiles(&names, "Missing/", "*.*", true);
	Check(names.empty(), "map-scan-missing-folder", failures);
	vector<string> directories;
	files->GetDirectories(&directories, "Maps");
	Check(directories.size() == 1 && directories[0] == "Multiplayer", "map-scan-extensionless-directory", failures);
	{
		NFile::CFileIterator entry(string((root / "Maps/root.ADMPDSCR.xdb").c_str()));
		Check(entry.IsValid() && entry.GetFileName() == "root.ADMPDSCR.xdb" &&
			entry.GetFullName() == (root / "Maps/root.ADMPDSCR.xdb").c_str(), "map-scan-iterator-path", failures);
		++entry;
		Check(entry.IsEnd(), "map-scan-iterator-exhaustion", failures);
	}
	std::filesystem::remove_all(root);
}
}

bool RunPrimeWorldLinuxTextRuntimeProbe()
{
	int failures = 0;
	CObj<TestFileSystem> files = new TestFileSystem("", false);
	RootFileSystem::RegisterFileSystem(files);

	struct Fixture
	{
		const char* path;
		const char* bytes;
		int size;
		const wchar_t* expected;
	};
	const Fixture fixtures[] = {
		{"/text-probe/ascii.txt", "\xff\xfe" "P\0W\0<\0b\0r\0>\0", 14, L"PW<br>"},
		{"/text-probe/cyrillic.txt", "\xff\xfe\x13\x04\x3e\x04\x42\x04\x3e\x04\x32\x04", 12, L"\u0413\u043e\u0442\u043e\u0432"},
		{"/text-probe/supplementary.txt", "\xff\xfe\x3d\xd8\x00\xde", 6, L"\U0001f600"},
		{"/text-probe/empty.txt", "\xff\xfe", 2, L""},
		{"/text-probe/no-bom.txt", "P\0W\0", 4, L""},
		{"/text-probe/odd.txt", "\xff\xfe" "P\0W", 5, L""},
		{"/text-probe/high-surrogate.txt", "\xff\xfe\x3d\xd8", 4, L""},
		{"/text-probe/low-surrogate.txt", "\xff\xfe\x00\xde", 4, L""}
	};
	for (unsigned int i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i)
	{
		const Fixture& fixture = fixtures[i];
		files->SetFileWithContents(fixture.path, string(fixture.bytes, fixture.size));
		const CTextRef reference = ReadReference(fixture.path);
		Check(reference.GetSource() == fixture.path, "xml-reference", &failures);
		Check(reference.GetText() == fixture.expected, fixture.path, &failures);
	}

	CTextRef cached = ReadReference("/text-probe/ascii.txt");
	Check(cached.GetText() == L"PW<br>", "cached-caption", &failures);
	files->SetFileWithContents("/text-probe/ascii.txt", string("\xff\xfe" "N\0", 4));
	Check(cached.GetText() == L"PW<br>" && files->GetUsageCount("/text-probe/ascii.txt") == 1,
		"cache-reuses-file", &failures);
	cached.DropCache();
	Check(cached.GetText() == L"N" && files->GetUsageCount("/text-probe/ascii.txt") == 2,
		"cache-invalidation", &failures);
	Check(ReadReference("/text-probe/missing.txt").GetText().empty(), "missing-file", &failures);
	Check(CTextRef().GetText().empty(), "empty-reference", &failures);
	string supplementaryCaption;
	NStr::UnicodeToUTF8(&supplementaryCaption, ReadReference("/text-probe/supplementary.txt").GetText());
	Check(supplementaryCaption == "\xf0\x9f\x98\x80", "utf16-textref-to-utf8", &failures);

	RootFileSystem::UnregisterFileSystem(files);
	CheckMapFileEnumeration(&failures);

	NScript::Script script;
	lua_State* state = script.GetState();
	const wstring luaCaption = L"\u042f \u0433\u043e\u0442\u043e\u0432! \U0001f600";
	Lua::lua_values<wstring>::put(state, luaCaption);
	lua_getfield(state, -1, "c_wstr");
	const bool completePayload = lua_objlen(state, -1) == luaCaption.size() * sizeof(wchar_t);
	lua_pop(state, 1);
	Check(completePayload, "lua-native-wide-payload", &failures);
	// The old writer truncates the payload; do not let its old reader access beyond it.
	if (completePayload)
	{
		const int stackSize = lua_gettop(state);
		Check(Lua::lua_values<wstring>::get(state, -1) == luaCaption, "lua-caption-roundtrip", &failures);
		Check(lua_gettop(state) == stackSize, "lua-reader-stack", &failures);
		lua_pushlstring(state, reinterpret_cast<const char*>(luaCaption.data()), sizeof(wchar_t));
		lua_setfield(state, -2, "c_wstr");
		Check(Lua::lua_values<wstring>::get(state, -1).empty(), "lua-short-payload-rejected", &failures);
		lua_pushnumber(state, -1);
		lua_setfield(state, -2, "size");
		Check(Lua::lua_values<wstring>::get(state, -1).empty(), "lua-negative-size-rejected", &failures);
		lua_pushnumber(state, 0.5);
		lua_setfield(state, -2, "size");
		Check(Lua::lua_values<wstring>::get(state, -1).empty(), "lua-fractional-size-rejected", &failures);
		Check(lua_gettop(state) == stackSize, "lua-invalid-reader-stack", &failures);
	}
	lua_settop(state, 0);
	Lua::lua_values<wstring>::put(state, wstring());
	Check(Lua::lua_values<wstring>::get(state, -1).empty(), "lua-empty-roundtrip", &failures);
	Check(lua_gettop(state) == 1, "lua-empty-reader-stack", &failures);

	// Exercise the same narrow caption API used by shipped lobby Lua scripts.
	Strong<UI::ScreenLogicBase> context = new UI::ScreenLogicBase();
	Strong<UI::ImageLabel> label = new UI::ImageLabel();
	label->SetContext(context);
	const char* utf8Caption = "Map: \xd0\x91\xd0\xbe\xd0\xb9";
	const wstring wideCaption = L"Map: \u0411\u043e\u0439";
	label->SetCaptionTextA(utf8Caption);
	Check(label->GetCaptionTextW() == wideCaption, "ui-utf8-caption-input", &failures);
	label->SetCaptionTextW(wideCaption);
	Check(label->GetCaptionText() == utf8Caption, "ui-utf8-caption-output", &failures);
	label->SetCaptionTextA("");
	Check(label->GetCaptionTextW().empty() && label->GetCaptionText().empty(), "ui-empty-caption", &failures);
	label->SetCaptionTextA("Hero \xf0\x9f\x98\x80");
	Check(label->GetCaptionTextW() == L"Hero \U0001f600", "ui-supplementary-caption-input", &failures);
	label->SetCaptionTextW(L"Hero \U0001f600");
	Check(label->GetCaptionText() == "Hero \xf0\x9f\x98\x80", "ui-supplementary-caption-output", &failures);
	label->SetCaptionTextA(supplementaryCaption.c_str());
	Check(label->GetCaptionTextW() == L"\U0001f600", "ui-textref-supplementary-caption", &failures);
	label->SetCaptionTextA("prefix\xe2\x82");
	Check(label->GetCaptionTextW().empty() && label->GetCaptionText().empty(), "ui-malformed-caption-rejected", &failures);
	label->SetCaptionTextA(utf8Caption);
	Check(label->GetCaptionTextW() == wideCaption, "ui-caption-recovers-after-malformed", &failures);

	// Reuse dirty allocations so fresh OS pages cannot hide uninitialized atlas padding.
	bool textureStorageZeroed = true;
	bool textureContentsPreserved = true;
	for (int sample = 0; sample < 8; ++sample)
	{
		Render::Texture2DRef texture = Render::CreateTexture2D(64, 64, 1,
			Render::RENDER_POOL_MANAGED, Render::FORMAT_A8);
		Render::LockedRect pixels = texture->LockRect(0, Render::LOCK_DEFAULT);
		if (!pixels.data || pixels.pitch != 64)
		{
			textureStorageZeroed = false;
			break;
		}
		for (int i = 0; i < 64 * 64; ++i)
			textureStorageZeroed = (pixels.data[i] == 0) && textureStorageZeroed;
		memset(pixels.data, 0xa5, 64 * 64);
		texture->UnlockRect(0);
		pixels = texture->LockRect(0, Render::LOCK_READONLY);
		if (!pixels.data || pixels.pitch != 64)
		{
			textureContentsPreserved = false;
			break;
		}
		for (int i = 0; i < 64 * 64; ++i)
			textureContentsPreserved = (pixels.data[i] == 0xa5) && textureContentsPreserved;
		texture->UnlockRect(0);
	}
	Check(textureStorageZeroed, "fresh-font-texture-transparent", &failures);
	Check(textureContentsPreserved, "font-texture-lock-preserves-pixels", &failures);
	CheckUnicodeConversions(&failures);
	printf("Text runtime probe: failures=%d\n", failures);
	return failures == 0;
}
