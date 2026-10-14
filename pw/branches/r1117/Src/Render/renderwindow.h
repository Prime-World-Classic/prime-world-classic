#pragma once
// Ручка окна вывода для seam'а рендера (docs/PLAN_NVRHI.md §2.8, §3.x GAP:
// swapchain). IRenderBackend обязана не содержать HWND — иначе этап 3 не сможет
// поднять Vulkan-бэкенд на не-Windows. Реализация приводит ручку к своему типу:
// D3D9Backend к HWND, SDL-путь этапа 3 к SDL_Window* (HWND тогда берётся из
// свойства окна SDL_PROP_WINDOW_WIN32_HWND_POINTER).
//
// Отдельный заголовок (а не место в renderbackend.h) — потому что Renderer
// хранит окно, а renderbackend.h в renderer.h не подключается намеренно:
// renderer.h включается почти везде, и любое касание seam'а пересобирало бы
// полмира.

namespace Render
{

struct NativeWindow
{
  void* p = 0;
};

// Граница платформы: здесь HWND (или SDL_Window*) превращается в ручку seam'а.
// Вызывающий код вне Render HWND не носит.
inline NativeWindow MakeNativeWindow(void* pHandle)
{
  NativeWindow window;
  window.p = pHandle;
  return window;
}

} // namespace Render
