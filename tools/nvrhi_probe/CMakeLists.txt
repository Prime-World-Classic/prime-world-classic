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

add_executable(nvrhi_probe probe.cpp)
target_link_libraries(nvrhi_probe PRIVATE ${NVRHI_LIBS} Vulkan::Headers)
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
