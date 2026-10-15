# nvrhi_probe — вне графа клиента (план docs/PLAN_NVRHI.md, этап 0).
#
# Цель: снять три неизвестности до того, как NVRHI попадает в CMake-граф клиента:
#   1. собирается ли NVRHI (C++17, статика, /MT) нашим тулчейном (cl 14.44 под Wine);
#   2. поднимается ли Vulkan-бэкенд на HWND клиента (Wine winevulkan / tiny10);
#   3. сколько весят артефакты.
#
# NVRHI НЕ создаёт ни устройство, ни swapchain — см. README.md этого каталога.
# Поэтому probe.cpp делает Vulkan-бутстрап руками (VkInstance -> VkDevice ->
# VkSurfaceKHR -> VkSwapchainKHR) и оборачивает бэк-буфер через
# IDevice::createHandleForNativeTexture.

cmake_minimum_required(VERSION 3.16)
project(nvrhi_probe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NVRHI_ROOT  "${CMAKE_CURRENT_LIST_DIR}/vendor/nvrhi")
set(VK_HEADERS  "${CMAKE_CURRENT_LIST_DIR}/vendor/vulkan-headers")
set(DX_HEADERS  "${CMAKE_CURRENT_LIST_DIR}/vendor/directx-headers")

# Заголовки подставляем свои, иначе NVRHI уйдёт в FetchContent (сети в этом
# окружении нет).
add_subdirectory("${VK_HEADERS}"  "${CMAKE_BINARY_DIR}/vk-headers"  EXCLUDE_FROM_ALL)
add_subdirectory("${DX_HEADERS}"  "${CMAKE_BINARY_DIR}/dx-headers"  EXCLUDE_FROM_ALL)

set(NVRHI_BUILD_SHARED     OFF CACHE BOOL "")
set(NVRHI_INSTALL          OFF CACHE BOOL "")
set(NVRHI_WITH_VALIDATION  ON  CACHE BOOL "")
set(NVRHI_WITH_VULKAN      ON  CACHE BOOL "")
# DX11/DX12 — только Windows (в NVRHI это cmake_dependent_option c WIN32; на
# Linux их нельзя включать и в кэш).
if(WIN32)
  set(NVRHI_WITH_DX11      ON  CACHE BOOL "")
  set(NVRHI_WITH_DX12      ON  CACHE BOOL "")
else()
  set(NVRHI_WITH_DX11      OFF CACHE BOOL "")
  set(NVRHI_WITH_DX12      OFF CACHE BOOL "")
endif()
set(NVRHI_FETCH_VULKAN_HEADERS   OFF CACHE BOOL "")
set(NVRHI_FETCH_DIRECTX_HEADERS  OFF CACHE BOOL "")
add_subdirectory("${NVRHI_ROOT}" "${CMAKE_BINARY_DIR}/nvrhi" EXCLUDE_FROM_ALL)

# ВАЖНО (найдено этим прогоном): при СТАТИЧЕСКОЙ сборке NVRHI даёт НЕ один
# nvrhi.lib, а четыре: nvrhi (common+validation), nvrhi_d3d11, nvrhi_d3d12,
# nvrhi_vk. Ссылаться надо на все нужные, иначе createDevice не слинкуется.
# ПОРЯДОК ВАЖЕН: бэкенды зависят от core (nvrhi), поэтому core идёт ПОСЛЕДНИМ
# (статическая линковка слева направо). На MSVC порядок не важен, на g++ — важен.
set(NVRHI_LIBS nvrhi_vk nvrhi)
if(WIN32)
  list(INSERT NVRHI_LIBS 0 nvrhi_d3d11 nvrhi_d3d12)
endif()

# SDL3 (шаг 12 очереди): окно обязан отдавать SDL, а не WndProc клиента, поэтому
# проверяем это здесь, до появления NvrhiBackend. Вендор — тот же, что у клиента
# (pw/branches/r1117/Vendor/SDL3), опции те же, что ставит gen_cmake.py.
set(SDL3_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../pw/branches/r1117/Vendor/SDL3")
if(WIN32 AND EXISTS "${SDL3_ROOT}/CMakeLists.txt")
  set(SDL_SHARED OFF CACHE BOOL "")
  set(SDL_STATIC ON  CACHE BOOL "")
  set(SDL_TESTS OFF  CACHE BOOL "")
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "")
  set(SDL_JOYSTICK OFF CACHE BOOL "")
  set(SDL_HAPTIC OFF CACHE BOOL "")
  set(SDL_HIDAPI_JOYSTICK OFF CACHE BOOL "")
  set(SDL_VIRTUAL_JOYSTICK OFF CACHE BOOL "")
  set(SDL_AUDIO OFF CACHE BOOL "")
  set(SDL_WASAPI OFF CACHE BOOL "")
  set(SDL_CAMERA OFF CACHE BOOL "")
  set(SDL_RENDER OFF CACHE BOOL "")
  set(SDL_GPU OFF CACHE BOOL "")
  # roapi.h лежит в Include/<ver>/winrt, которого в include-пути нет (ucrt/um/
  # shared), а PreseedMSVCCache.cmake:176 при MSVC прописывает HAVE_ROAPI_H=1 в
  # кэш как CACHE INTERNAL. Обычная (не кэшевая) переменная перекрывает кэш для
  # if() в поддиректории — тот же приём, что в Tools/ClientCMake/gen_cmake.py:806.
  # Без HAVE_ROAPI_H SDL сам объявляет RO_INIT_TYPE и грузит RoInitialize.
  set(HAVE_ROAPI_H OFF)
  # itoa: PreseedMSVCCache.cmake:63 ставит LIBC_HAS_ITOA=1 (проверка идёт на /MD,
  # где itoa есть в импорте MSVCRT.dll), а мы собираем на /MT — там этого символа
  # нет, и legacy_stdio_definitions.lib в этом тулчейне тоже нет. Обычная
  # переменная перекрывает кэш: SDL берёт собственный SDL_ltoa.
  set(LIBC_HAS_ITOA "")
  add_subdirectory("${SDL3_ROOT}" "${CMAKE_BINARY_DIR}/sdl3" EXCLUDE_FROM_ALL)
endif()

add_executable(nvrhi_probe probe.cpp)
target_link_libraries(nvrhi_probe PRIVATE ${NVRHI_LIBS} Vulkan::Headers)
if(TARGET SDL3::SDL3-static)
  target_link_libraries(nvrhi_probe PRIVATE SDL3::SDL3-static)
  target_compile_definitions(nvrhi_probe PRIVATE PW_WITH_SDL3=1)
endif()
# /MT вместо /MD: клиент статически слинкован, CRT-DLL в поставке клиента нет.
if(MSVC)
  target_compile_options(nvrhi_probe PRIVATE /W3 /permissive-)
else()
  target_compile_options(nvrhi_probe PRIVATE -Wall)
  target_link_libraries(nvrhi_probe PRIVATE dl)
endif()
set_property(TARGET nvrhi_probe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
foreach(_l ${NVRHI_LIBS})
  set_property(TARGET ${_l} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endforeach()
# SDL3 по умолчанию собирается на /MD, а probe — на /MT: линковка даёт
# LNK2005 (libucrt против MSVCRT) и LNK2019 (__imp_round и т.п.). Тот же приём,
# что в Tools/ClientCMake/gen_cmake.py:827 для вендорских целей клиента.
# SDL3::SDL3-static — ALIAS, set_property на нём запрещён; реальная цель — SDL3-static.
if(MSVC AND TARGET SDL3-static)
  set_property(TARGET SDL3-static PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

