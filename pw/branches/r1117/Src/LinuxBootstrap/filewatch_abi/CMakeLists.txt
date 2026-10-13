# Native Linux ABI regression; the parent engine probe covers processor ownership paths.
if(NOT DEFINED PW_FILEWATCH_ABI_SRC)
	set(PW_FILEWATCH_ABI_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()
foreach(mode plain bootstrap)
	add_library(PrimeWorldLinuxFileWatchAbi_${mode} OBJECT endpoint.cpp)
	target_compile_definitions(PrimeWorldLinuxFileWatchAbi_${mode} PRIVATE PW_FILEWATCH_ENDPOINT=${mode})
	target_compile_features(PrimeWorldLinuxFileWatchAbi_${mode} PRIVATE cxx_std_17)
	target_include_directories(PrimeWorldLinuxFileWatchAbi_${mode} PRIVATE
		"${PW_FILEWATCH_ABI_SRC}" "${PW_FILEWATCH_ABI_SRC}/System/FileSystem")
	set_target_properties(PrimeWorldLinuxFileWatchAbi_${mode} PROPERTIES UNITY_BUILD OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
endforeach()
target_compile_definitions(PrimeWorldLinuxFileWatchAbi_bootstrap PRIVATE PW_LINUX_DB_BOOTSTRAP PW_LINUX_NULL_RENDER)
target_compile_options(PrimeWorldLinuxFileWatchAbi_plain PRIVATE -UPW_LINUX_DB_BOOTSTRAP -UPW_LINUX_NULL_RENDER)
add_executable(PrimeWorldLinuxFileWatchAbiProbe main.cpp
	$<TARGET_OBJECTS:PrimeWorldLinuxFileWatchAbi_plain>
	$<TARGET_OBJECTS:PrimeWorldLinuxFileWatchAbi_bootstrap>)
target_compile_features(PrimeWorldLinuxFileWatchAbiProbe PRIVATE cxx_std_17)
target_include_directories(PrimeWorldLinuxFileWatchAbiProbe PRIVATE
	"${PW_FILEWATCH_ABI_SRC}" "${PW_FILEWATCH_ABI_SRC}/System/FileSystem")
set_target_properties(PrimeWorldLinuxFileWatchAbiProbe PROPERTIES UNITY_BUILD OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
add_test(NAME linux_filewatch_abi COMMAND PrimeWorldLinuxFileWatchAbiProbe)
set_tests_properties(linux_filewatch_abi PROPERTIES TIMEOUT 30)
