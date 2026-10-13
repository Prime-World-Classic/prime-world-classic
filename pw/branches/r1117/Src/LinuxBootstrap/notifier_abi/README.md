# Native Notifier ABI Regression

The fixture includes the real [Notifier.h](../../System/Notifier.h) and its narrow
System header prerequisites. It links no engine libraries and instantiates no
notifier containers. The [port plan](../LINUX_PORT_PLAN.md) tracks integration.

`endpoint.cpp` is compiled twice: once with both `PW_LINUX_DB_BOOTSTRAP` and
`PW_LINUX_NULL_RENDER`, and once with both explicitly undefined. Each TU reports
its own compile-time virtual-destructor trait. A missing destructor rejects the
run before any callback allocation, avoiding unsafe baseline deletion.

With the fixed interface, 132 checks exercise the actual `MethodNotifyCallback`
in both producer/consumer directions: invocation, ownership, symmetric RTTI
equality, different receiver/method/ownership rejection, and base-pointer deletion.
Tracked subclasses also verify derived/member destruction and release a member
allocation. Both owned and borrowed callback specializations are covered; the
harness explicitly owns these allocations and does not implement notifier policy.

## Integration

The LinuxBootstrap project registers this directory and builds its probe as a
client dependency. Reproduce from a configured native build:

```sh
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxNotifierAbiProbe
ctest --test-dir /tmp/primeworld-linux-bootstrap -R linux_notifier_abi --output-on-failure
```

The client dependency ensures maintained client builds also produce the runnable
`linux_notifier_abi` CTest executable.
No Windows project or shared compile definitions need modification.

For standalone sanitizer validation, a private CMake project can call
`enable_testing()` and `add_subdirectory()` on this directory. Configure that
project with `-fsanitize=address,undefined -fno-omit-frame-pointer -g` in
`CMAKE_CXX_FLAGS` and `-fsanitize=address,undefined` in `CMAKE_EXE_LINKER_FLAGS`.
Run its executable or CTest with
`ASAN_OPTIONS=detect_leaks=1:new_delete_type_mismatch=1:alloc_dealloc_mismatch=1:halt_on_error=1`
and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. LeakSanitizer requires an
environment without ptrace monitoring; do not suppress leaks to conceal that
environmental failure. This standalone result does not imply engine-wide safety.
