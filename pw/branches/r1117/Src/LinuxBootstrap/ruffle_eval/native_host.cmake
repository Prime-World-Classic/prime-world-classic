# Optional native inspection support. No Rust build/download or global backend switch.
if(NOT TARGET PwRuffleNativeHost)
	find_package(X11 REQUIRED)
	find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
	find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
	add_library(PwRuffleNativeHost STATIC
		"${CMAKE_CURRENT_LIST_DIR}/native_host.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gl_compositor.cpp")
	target_compile_features(PwRuffleNativeHost PRIVATE cxx_std_17)
	target_include_directories(PwRuffleNativeHost PRIVATE "${NLOHMANN_JSON_INCLUDE_DIR}" ${X11_INCLUDE_DIR})
	target_link_libraries(PwRuffleNativeHost PUBLIC OpenGL::GL OpenGL::EGL ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(PwRuffleNativeHost PRIVATE -Wall -Wextra -Werror)
	endif()
endif()
