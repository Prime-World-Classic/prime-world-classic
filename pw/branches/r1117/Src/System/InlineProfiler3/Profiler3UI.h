#ifndef INLINEPROFILER3UI_H_INCLUDED
#define INLINEPROFILER3UI_H_INCLUDED

namespace profiler3ui
{

void Init();
void Shutdown();

bool Show( HWND hParentWnd );
bool Hide();

} //namespace profiler3ui

// –еализаци€ (Profiler3UI.cpp) под x64 не собираетс€ (WTL/ATL нет в x64-тулчейне),
// а вызовы profiler3ui::Init/Shutdown/Show остаютс€ в PW_Client/Game.cpp,
// PF/UniServer/main.cpp, тестах Ч здесь no-op'ы, иначе LNK2019.
#if defined(_M_X64)
namespace profiler3ui
{
  inline void Init() {}
  inline void Shutdown() {}
  inline bool Show( HWND /*hParentWnd*/ ) { return false; }
  inline bool Hide() { return false; }
}
#endif

#endif //INLINEPROFILER3UI_H_INCLUDED
