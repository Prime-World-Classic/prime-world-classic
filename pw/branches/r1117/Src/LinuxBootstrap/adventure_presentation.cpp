#include "../System/systemStdAfx.h"
#include "adventure_presentation.h"
#include "../UI/ScreenLogicBase.h"
#include "../UI/FlashContainer2.h"
#include "../UI/FlashInterface.h"
#include "../UI/User.h"

namespace LinuxBootstrap
{
struct AdventurePresentation::Impl
{
	Strong<UI::ScreenLogicBase> logic;
	Strong<UI::FlashInterface> flash;
};

AdventurePresentation::AdventurePresentation() = default;
AdventurePresentation::~AdventurePresentation() = default;

bool AdventurePresentation::Initialize(UI::User* user)
{
	if (attempted)
		return IsReady();
	attempted = true;
	if (!user)
		return false;
	impl.reset(new Impl);
	impl->logic = new UI::ScreenLogicBase;
	impl->logic->SetUser(user);
	if (!impl->logic->LoadScreenLayout("Combat"))
	{
		Reset();
		return false;
	}
	UI::FlashContainer2* window = impl->logic->GetUIWindow<UI::FlashContainer2>("FlashScreen", true, false);
	if (!window || !window->IsRuntimeReadyForBootstrapProbe())
	{
		Reset();
		return false;
	}
	impl->flash = new UI::FlashInterface(window, "mainInterface");
	if (!impl->flash->IsBound())
	{
		Reset();
		return false;
	}
	impl->flash->CallMethod("HideAllWindows");
	fprintf(stdout, "Native adventure UI: Combat XDB/main.swf interface bound\n");
	return true;
}

bool AdventurePresentation::IsReady() const
{
	return impl && impl->flash && impl->flash->IsBound();
}

void AdventurePresentation::Step(float deltaSeconds)
{
	if (IsReady())
		impl->logic->StepWindows(deltaSeconds);
}

void AdventurePresentation::Render()
{
	if (!IsReady())
		return;
	impl->logic->RenderWindows();
	++frames;
}

void AdventurePresentation::Reset()
{
	impl.reset();
}
}
