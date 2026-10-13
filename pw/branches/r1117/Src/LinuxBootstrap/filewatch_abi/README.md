# File-Watch Callback ABI Regression

This native Linux target includes the actual `System/FileSystem/FileSystemChangesProcessor.h`.
It compiles one allocator/deleter TU with `PW_LINUX_DB_BOOTSTRAP` and
`PW_LINUX_NULL_RENDER`, and another without either definition. Both directions
exercise `IFileWatchFunctor` deletion, the actual `FileWatchMethodFunctor`, a
derived wrapper with an owned heap payload, and the actual `FileWatchFunctor`.
The receiver is borrowed. Deleting a wrapper must not delete or call it.

The interface trait rejects an unfixed header before unsafe deletion. After the
fix, 26 checks cover virtual destruction, payload release, `Call`, and `Equals`.
Processor registration and ownership transitions belong to the parent engine
lifetime fixture, not this ABI target. No replacement processor is implemented.

The normal client build depends on this regression. Run it with:

```sh
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxFileWatchAbiProbe
ctest --test-dir /tmp/primeworld-linux-bootstrap -R linux_filewatch_abi --output-on-failure
```

A private embedding can set `PW_FILEWATCH_ABI_SRC` to the engine's absolute
`Src` path. All Linux consumers must rebuild together after changing the interface;
the OS guard deliberately preserves the original Windows interface.

For standalone instrumentation, compile and link with
`-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`.
Run with `ASAN_OPTIONS=detect_leaks=1:new_delete_type_mismatch=1:halt_on_error=1`
and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. No exclusions are needed.
LeakSanitizer needs an execution environment that does not trace the process.

References: [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html),
[LeakSanitizer](https://clang.llvm.org/docs/LeakSanitizer.html),
[UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html).
