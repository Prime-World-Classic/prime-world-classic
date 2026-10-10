#include "TamarinPCH.h"

#include "adventure_flash_probe.h"
// Load formula declarations after Tamarin to keep FormulaPars' random macro out of the VM headers.
#include "PF_GameLogic/StringExecutorBootstrap.h"
#include "PF_GameLogic/AdventureFlashInterface.h"
#include "System/FileSystem/TestFileSystem.h"
#include "UI/FlashContainer2.h"
#include "UI/Flash/GameSWFIntegration/FlashEnterFunction.h"

#include <cstdio>

namespace
{
/// A callable VM object records atoms after production FVar conversion and method lookup.
class CapturedCall : public avmplus::ScriptObject
{
public:
	CapturedCall(avmplus::VTable* table, avmplus::ScriptObject* owner)
		: ScriptObject(table, owner->toplevel()->objectClass->prototype), receiver(owner),
		arguments(0), calls(0), receiverMatches(false), returnValue(avmplus::AtomConstants::undefinedAtom)
	{
	}

	virtual avmplus::Atom call(int argc, avmplus::Atom* argv)
	{
		++calls;
		receiverMatches = argv[0] == receiver->atom();
		arguments = toplevel()->arrayClass->newArray(argc);
		for (int i = 0; i < argc; ++i)
			arguments->setIntProperty(i, argv[i + 1]);
		return returnValue;
	}

	bool Matches(unsigned count, unsigned expectedCalls = 1) const
	{
		return calls == expectedCalls && receiverMatches && arguments && arguments->get_length() == count;
	}

	avmplus::Atom Argument(unsigned index) const
	{
		return arguments ? arguments->getIntProperty(index) : avmplus::AtomConstants::undefinedAtom;
	}

	DRC(avmplus::ScriptObject*) receiver;
	DRC(avmplus::ArrayObject*) arguments;
	unsigned calls;
	bool receiverMatches;
	avmplus::Atom returnValue;
};

/// Attach a native callable to the real movie root, leaving the production binding path intact.
CapturedCall* Capture(avmplus::ScriptObject* root, const char* method)
{
	avmplus::VTable* table = root->toplevel()->objectClass->ivtable();
	CapturedCall* result = new (root->gc(), table->getExtraSize()) CapturedCall(table, root);
	root->setStringProperty(root->core()->internConstantStringLatin1(method), result->atom());
	return result;
}

bool IsNumber(avmplus::Atom value, double expected)
{
	return avmplus::AvmCore::isNumber(value) && avmplus::AvmCore::number(value) == expected;
}

bool IsBool(avmplus::Atom value, bool expected)
{
	return value == (expected ? avmplus::AtomConstants::trueAtom : avmplus::AtomConstants::falseAtom);
}

bool IsText(avmplus::Atom value, const char* expected)
{
	if (!avmplus::AvmCore::isString(value))
		return false;
	avmplus::StUTF8String text(avmplus::AvmCore::atomToString(value));
	return string(text.c_str(), text.length()) == expected;
}

/// Compare VM array contents directly; expected values never pass through FVar.
template<class T>
bool IsArray(avmplus::Atom value, const vector<T>& expected)
{
	if (!avmplus::AvmCore::isObject(value))
		return false;
	avmplus::ArrayObject* array = dynamic_cast<avmplus::ArrayObject*>(avmplus::AvmCore::atomToScriptObject(value));
	if (!array || array->get_length() != static_cast<unsigned>(expected.size()))
		return false;
	for (int i = 0; i < expected.size(); ++i)
		if (!IsNumber(array->getIntProperty(i), expected[i]))
			return false;
	return true;
}

void Check(bool passed, const char* name, unsigned& failures)
{
	std::printf("Adventure Flash probe: %s=%s\n", name, passed ? "yes" : "NO");
	if (!passed)
		++failures;
}

/// Run real wrapper calls while the host owns and roots the mock movie and its callbacks.
bool ProbeCalls(UI::FlashContainer2* host)
{
	avmplus::ScriptObject* root = host->GetFlashObject("");
	if (!root)
		return false;
	FLASH_ENTER_FUNCTION_RETURN(root->gc(), false);
	Strong<NGameX::AdventureFlashInterface> ui = new NGameX::AdventureFlashInterface(host, "");
	unsigned failures = 0;
	Check(ui->IsBound(), "bound", failures);

	const wstring name = L"\u0413\u0435\u0440\u043e\u0439 \U0001F600";
	const char* nameUtf8 = "\xD0\x93\xD0\xB5\xD1\x80\xD0\xBE\xD0\xB9 \xF0\x9F\x98\x80";
	vector<int> places;
	places.push_back(2);
	places.push_back(11);
	places.push_back(40);

	CapturedCall* own = Capture(root, "SetOurHeroIdententity");
	ui->SetOurHeroIdententity(7, NDb::FACTION_FREEZE, NDb::APPLICATORDAMAGETYPE_ENERGY, 3, 11, places);
	Check(own->Matches(6) && IsNumber(own->Argument(0), 7) && IsNumber(own->Argument(1), NDb::FACTION_FREEZE) &&
		IsNumber(own->Argument(2), NDb::APPLICATORDAMAGETYPE_ENERGY) && IsArray(own->Argument(3), places) &&
		IsNumber(own->Argument(4), 3) && IsNumber(own->Argument(5), 11), "own-identity", failures);

	string flagIcon = "flag.dds";
	wstring flagTooltip = name;
	for (int enemy = 0; enemy < 2; ++enemy)
	{
		const char* method = enemy ? "SetEnemyHeroIdentity" : "SetFriendlyHeroIdentity";
		CapturedCall* identity = Capture(root, method);
		ui->SetHeroIdentity(name, L"Mage", 7, "hero.dds", true, enemy != 0, false, 123,
			NDb::FACTION_FREEZE, NDb::FACTION_BURN, 1500.5f, "rank.dds", name, true,
			42, flagIcon, flagTooltip, false, 3, 11, places);
		Check(identity->Matches(20) && IsNumber(identity->Argument(0), 7) && IsText(identity->Argument(1), nameUtf8) &&
			IsText(identity->Argument(2), "Mage") && IsText(identity->Argument(3), "hero.dds") &&
			IsBool(identity->Argument(4), true) && IsBool(identity->Argument(5), false) &&
			IsNumber(identity->Argument(6), 123) && IsNumber(identity->Argument(7), NDb::FACTION_FREEZE) &&
			IsNumber(identity->Argument(8), NDb::FACTION_BURN) && IsNumber(identity->Argument(9), 1500.5) &&
			IsArray(identity->Argument(10), places) && IsText(identity->Argument(11), "rank.dds") &&
			IsText(identity->Argument(12), nameUtf8) && IsBool(identity->Argument(13), true) &&
			IsNumber(identity->Argument(14), 42) && IsText(identity->Argument(15), "flag.dds") &&
			IsText(identity->Argument(16), nameUtf8) && IsBool(identity->Argument(17), false) &&
			IsNumber(identity->Argument(18), 3) && IsNumber(identity->Argument(19), 11), method, failures);
	}

	NGameX::HeroInfoParams params = {};
	params.level = 9;
	params.curHealth = 400;
	params.maxHealth = 900;
	params.curMana = 120;
	params.maxMana = 350;
	params.isVisible = true;
	params.isPickable = false;
	params.timeToRessurect = 17;
	params.channgeling = 0.25f;
	params.healthRegen = 1.5f;
	params.manaRegen = 2.75f;
	params.isCameraLocked = true;
	params.ultimateCoolDown = 6.5f;
	CapturedCall* hero = Capture(root, "SetHeroParams");
	ui->SetHeroParams(7, params);
	Check(hero->Matches(14) && IsNumber(hero->Argument(0), 7) && IsNumber(hero->Argument(1), 9) &&
		IsNumber(hero->Argument(2), 400) && IsNumber(hero->Argument(3), 900) &&
		IsNumber(hero->Argument(4), 120) && IsNumber(hero->Argument(5), 350) &&
		IsBool(hero->Argument(6), true) && IsBool(hero->Argument(7), false) && IsNumber(hero->Argument(8), 17) &&
		IsNumber(hero->Argument(9), 0.25) && IsNumber(hero->Argument(10), 1.5) &&
		IsNumber(hero->Argument(11), 2.75) && IsBool(hero->Argument(12), true) &&
		IsNumber(hero->Argument(13), 6.5), "hero-params", failures);

	CapturedCall* talent = Capture(root, "SetTalentStatus");
	ui->SetTalentStatus(2, 4, ActionBarSlotState::NotEnoughMana, 1.25f, 8.5f, true);
	Check(talent->Matches(6) && IsNumber(talent->Argument(0), 2) && IsNumber(talent->Argument(1), 4) &&
		IsNumber(talent->Argument(2), ActionBarSlotState::NotEnoughMana) && IsNumber(talent->Argument(3), 1.25) &&
		IsNumber(talent->Argument(4), 8.5) && IsBool(talent->Argument(5), true), "talent-status", failures);

	vector<int> forces;
	forces.push_back(-1);
	forces.push_back(123);
	vector<uint> colors;
	colors.push_back(0xff102030u);
	colors.push_back(0xffffffffu);
	CapturedCall* force = Capture(root, "SetForceColors");
	ui->SetForceColorTable(forces, colors);
	Check(force->Matches(2) && IsArray(force->Argument(0), forces) && IsArray(force->Argument(1), colors),
		"force-arrays", failures);
	forces.clear();
	colors.clear();
	ui->SetForceColorTable(forces, colors);
	Check(force->Matches(2, 2) && IsArray(force->Argument(0), forces) && IsArray(force->Argument(1), colors),
		"empty-arrays", failures);

	CapturedCall* energy = Capture(root, "SetHeroCustomEnergy");
	ui->SetHeroCustomEnergy(7, name, 0xff102030u);
	Check(energy->Matches(3) && IsNumber(energy->Argument(0), 7) && IsText(energy->Argument(1), nameUtf8) &&
		IsNumber(energy->Argument(2), 0xff102030u), "unsigned-color", failures);

	CapturedCall* chat = Capture(root, "AddMessageEx");
	ui->AddMessage(NDb::CHATCHANNEL_GLOBAL, name, L"", -1);
	Check(chat->Matches(4) && IsNumber(chat->Argument(0), NDb::CHATCHANNEL_GLOBAL) &&
		IsText(chat->Argument(1), nameUtf8) && IsText(chat->Argument(2), "") && IsNumber(chat->Argument(3), -1),
		"chat-text", failures);

	CapturedCall* minimap = Capture(root, "SetMinimapEffect");
	ui->SetMinimapEffect(5, NDb::MINIMAPEFFECTS_CHAT);
	Check(minimap->Matches(2) && IsNumber(minimap->Argument(0), 5) &&
		IsNumber(minimap->Argument(1), NDb::MINIMAPEFFECTS_CHAT), "minimap-enum", failures);

	CapturedCall* escape = Capture(root, "OnEscape");
	escape->returnValue = avmplus::AtomConstants::trueAtom;
	Check(ui->OnEscape() && escape->Matches(0), "escape-true", failures);
	escape->returnValue = avmplus::AtomConstants::falseAtom;
	Check(!ui->OnEscape() && escape->Matches(0, 2), "escape-false", failures);

	Strong<NGameX::AdventureFlashInterface> unbound = new NGameX::AdventureFlashInterface(0, "");
	unbound->SetHeroParams(7, params);
	unbound->SetHeroExperience(1, 2, places);
	Check(!unbound->IsBound() && !unbound->OnEscape() && hero->calls == 1 && escape->calls == 2,
		"unbound", failures);
	return failures == 0;
}
}

bool RunPrimeWorldLinuxAdventureFlashProbe()
{
	// One empty AS3 frame: FWS v9, zero RECT, 24 fps, FileAttributes, ShowFrame, End.
	// Native callable properties supply the mock receiver; no alternate argument serializer is involved.
	static const unsigned char movie[] = {
		'F', 'W', 'S', 9, 24, 0, 0, 0, 8, 0, 0, 24, 1, 0,
		0x44, 0x11, 8, 0, 0, 0, 0x40, 0, 0, 0
	};
	const char* path = "__adventure_flash_probe__/receiver.swf";
	CObj<TestFileSystem> files = new TestFileSystem("", false);
	files->SetFileWithContents(path, string(reinterpret_cast<const char*>(movie), sizeof(movie)));
	RootFileSystem::RegisterFileSystem(files);
	bool passed = false;
	{
		Strong<UI::FlashContainer2> host = new UI::FlashContainer2;
		host->LoadOnly(path, 0);
		passed = host->IsRuntimeReadyForBootstrapProbe() && ProbeCalls(host);
	}
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("Adventure Flash probe: calls=%s teardown=yes\n", passed ? "yes" : "NO");
	return passed;
}
