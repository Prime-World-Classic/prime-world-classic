# Compile the same real interface with and without per-target bootstrap definitions.
foreach(mode plain bootstrap)
	add_library(PrimeWorldLinuxNotifierAbi_${mode} OBJECT endpoint.cpp)
	target_compile_features(PrimeWorldLinuxNotifierAbi_${mode} PRIVATE cxx_std_17)
	target_include_directories(PrimeWorldLinuxNotifierAbi_${mode} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
	set_target_properties(PrimeWorldLinuxNotifierAbi_${mode} PROPERTIES UNITY_BUILD OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
endforeach()
target_compile_definitions(PrimeWorldLinuxNotifierAbi_bootstrap PRIVATE
	PW_NOTIFIER_ABI_BOOTSTRAP=1 PW_LINUX_DB_BOOTSTRAP PW_LINUX_NULL_RENDER)
target_compile_definitions(PrimeWorldLinuxNotifierAbi_plain PRIVATE PW_NOTIFIER_ABI_BOOTSTRAP=0)
# Make the absence check real even if the including directory has bootstrap flags.
target_compile_options(PrimeWorldLinuxNotifierAbi_plain PRIVATE
	-UPW_LINUX_DB_BOOTSTRAP -UPW_LINUX_NULL_RENDER)
add_executable(PrimeWorldLinuxNotifierAbiProbe main.cpp
	$<TARGET_OBJECTS:PrimeWorldLinuxNotifierAbi_plain>
	$<TARGET_OBJECTS:PrimeWorldLinuxNotifierAbi_bootstrap>)
target_compile_features(PrimeWorldLinuxNotifierAbiProbe PRIVATE cxx_std_17)
set_target_properties(PrimeWorldLinuxNotifierAbiProbe PROPERTIES UNITY_BUILD OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
add_test(NAME linux_notifier_abi COMMAND PrimeWorldLinuxNotifierAbiProbe)
set_tests_properties(linux_notifier_abi PROPERTIES TIMEOUT 30)
