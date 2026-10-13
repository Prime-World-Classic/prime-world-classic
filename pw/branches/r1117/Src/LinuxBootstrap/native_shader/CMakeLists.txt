# Linux-only bytecode translation; no legacy Windows project depends on this target.
set(MOJO ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/mojoshader)
add_library(PrimeWorldLinuxMojoShader STATIC
	${MOJO}/mojoshader.c ${MOJO}/mojoshader_common.c ${MOJO}/mojoshader_opengl.c
	${MOJO}/profiles/mojoshader_profile_common.c ${MOJO}/profiles/mojoshader_profile_glsl.c)
target_include_directories(PrimeWorldLinuxMojoShader PUBLIC ${MOJO})
target_compile_definitions(PrimeWorldLinuxMojoShader PUBLIC MOJOSHADER_NO_VERSION_INCLUDE
	MOJOSHADER_CHANGESET="ad5dff84830c2863c841f4b1f4e3df78c705b383+pw-oom1"
	MOJOSHADER_DEPTH_CLIPPING SUPPORT_PROFILE_GLSL=1 SUPPORT_PROFILE_GLSL120=1
	SUPPORT_PROFILE_D3D=0 SUPPORT_PROFILE_BYTECODE=0 SUPPORT_PROFILE_HLSL=0
	SUPPORT_PROFILE_GLSLES=0 SUPPORT_PROFILE_GLSLES3=0 SUPPORT_PROFILE_ARB1=0
	SUPPORT_PROFILE_ARB1_NV=0 SUPPORT_PROFILE_METAL=0 SUPPORT_PROFILE_SPIRV=0 SUPPORT_PROFILE_GLSPIRV=0)
target_link_libraries(PrimeWorldLinuxMojoShader PUBLIC m)
add_library(PrimeWorldLinuxShaderBackend STATIC shader_backend.cpp shader_transforms.cpp)
target_compile_features(PrimeWorldLinuxShaderBackend PUBLIC cxx_std_17)
target_link_libraries(PrimeWorldLinuxShaderBackend PRIVATE PrimeWorldLinuxMojoShader PUBLIC OpenGL::GL Threads::Threads)
target_compile_options(PrimeWorldLinuxShaderBackend PRIVATE -Wall -Wextra -Werror)
add_executable(PrimeWorldLinuxShaderTransformProbe shader_transforms_tests.cpp shader_transforms.cpp)
target_compile_features(PrimeWorldLinuxShaderTransformProbe PRIVATE cxx_std_17)
target_compile_options(PrimeWorldLinuxShaderTransformProbe PRIVATE -Wall -Wextra -Werror)
add_test(NAME linux_shader_transforms COMMAND PrimeWorldLinuxShaderTransformProbe)
# Explicit GL gate: invoke under an isolated Xvfb, not concurrent shared GUI CTests.
add_executable(PrimeWorldLinuxShaderPixelProbe pixel_tests.cpp)
target_compile_features(PrimeWorldLinuxShaderPixelProbe PRIVATE cxx_std_17)
target_link_libraries(PrimeWorldLinuxShaderPixelProbe PRIVATE PrimeWorldLinuxShaderBackend
	PrimeWorldLinuxMojoShader OpenGL::GL ${X11_LIBRARIES} Threads::Threads)
# Pure directory/reflection checks compile the implementation without initializing GL.
add_executable(PrimeWorldLinuxShaderFamilyCpuProbe family_cpu_tests.cpp)
target_compile_features(PrimeWorldLinuxShaderFamilyCpuProbe PRIVATE cxx_std_17)
target_compile_options(PrimeWorldLinuxShaderFamilyCpuProbe PRIVATE -Wall -Wextra -Werror)
target_link_libraries(PrimeWorldLinuxShaderFamilyCpuProbe PRIVATE PrimeWorldLinuxMojoShader OpenGL::GL Threads::Threads)
add_test(NAME linux_shader_family_cpu COMMAND PrimeWorldLinuxShaderFamilyCpuProbe
	${CMAKE_CURRENT_SOURCE_DIR}/../../../Data/Shaders/UberShaders/BasicFXMaterial.shd)
add_test(NAME linux_shader_static_cpu COMMAND PrimeWorldLinuxShaderFamilyCpuProbe
	${CMAKE_CURRENT_SOURCE_DIR}/../../../Data/Shaders/UberShaders/BasicFXMaterial.shd
	${CMAKE_CURRENT_SOURCE_DIR}/../../../Data/Shaders/UberShaders/BasicMaterial.shd)
add_test(NAME linux_shader_scene_cpu COMMAND PrimeWorldLinuxShaderFamilyCpuProbe
	${CMAKE_CURRENT_SOURCE_DIR}/../../../Data/Shaders/UberShaders/BasicFXMaterial.shd
	${CMAKE_CURRENT_SOURCE_DIR}/../../../Data/Shaders/UberShaders/BasicMaterial.shd
	${CMAKE_CURRENT_SOURCE_DIR}/../../../Data/Shaders/UberShaders/TerrainElementMaterial.shd)
