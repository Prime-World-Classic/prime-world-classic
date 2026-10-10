# Optional native inspection support. No Rust build/download or global backend switch.
if(NOT TARGET PwRuffleNativeHost)
	find_package(X11 REQUIRED)
	find_package(OpenGL REQUIRED)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(PW_NATIVE_EGL REQUIRED egl)
	pkg_get_variable(PW_NATIVE_EGL_LIBDIR egl libdir)
	# Avoid unrelated bundled EGL implementations discovered under application prefixes.
	find_library(PW_NATIVE_EGL_LIBRARY NAMES EGL PATHS "${PW_NATIVE_EGL_LIBDIR}" NO_DEFAULT_PATH REQUIRED)
	find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
	add_library(PwRuffleNativeHost STATIC
		"${CMAKE_CURRENT_LIST_DIR}/hud_calls.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/action_calls.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/client_inspection.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/native_host.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gl_compositor.cpp")
	target_compile_features(PwRuffleNativeHost PRIVATE cxx_std_17)
	target_include_directories(PwRuffleNativeHost PRIVATE "${NLOHMANN_JSON_INCLUDE_DIR}" ${X11_INCLUDE_DIR} ${PW_NATIVE_EGL_INCLUDE_DIRS})
	target_link_libraries(PwRuffleNativeHost PUBLIC OpenGL::GL "${PW_NATIVE_EGL_LIBRARY}" ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(PwRuffleNativeHost PRIVATE -Wall -Wextra -Werror)
	endif()
endif()
