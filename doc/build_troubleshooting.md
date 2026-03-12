# Build and Development Troubleshooting

Common issues and solutions for the Hibiki DAW build system.

## FlatBuffer Synchronization Issues

### Symptom: Compilation errors in Java/C++ related to new FlatBuffer fields or tables.

When adding new fields to a FlatBuffer schema (e.g., `hibiki_response.fbs` or `hibiki_project.fbs`), the generated code may not immediately update in all sandbox environments, leading to "cannot resolve symbol" or "undefined reference" errors.

**Solution:**
1.  **Update `BUILD` file**: Ensure that any new generated files (like `NewMessage.java` or `NewMessageT.java`) are added to the `outs` list of the corresponding `flatbuffer_library_public` rule.
2.  **Clean Rebuild**: Run `bazel clean` before building to force a complete regeneration of the FlatBuffer headers and JARs.
    ```bash
    bazel clean && bazel build -c opt //:hibiki-gui-java
    ```

### Symptom: Missing Object API classes (`...T.java`).

The project uses `--gen-object-api` for both Java and C++. 

**Solution:**
- Check the `language_flag` in the `BUILD` file for the flatbuffer rule.
- Ensure the `outs` list includes both the standard class and the `T` variant (e.g., `Notification.java` AND `NotificationT.java`).

## Native Library Path Issues

If `hbk-play` cannot be found by the Java GUI:
- Ensure you are running from the project root.
- Verify the path in `BackendManager.java` matches the Bazel output location (`./bazel-bin/hbk-play`).

## VST3 SDK Linker Errors (fastbuild)

### Symptom: Linker errors about `.sframe` relocations in VST3 SDK

When building with the default `fastbuild` mode, you may see errors like:
```
bazel-out/k8-fastbuild/bin/external/+http_archive+vst3sdk/_objs/vst3sdk/commonstringconvert.pic.o(.sframe+0x...): 
error: relocation refers to local symbol "..." which is defined in a discarded section
collect2: error: ld returned 1 exit status
```

**Cause:** The VST3 SDK has linker compatibility issues with the debug/fastbuild mode due to `.sframe` section handling in recent GCC/binutils versions.

**Solution:** Always use `-c opt` (optimized/release mode) when building:
```bash
bazel build -c opt //:hibiki-gui-java
bazel test -c opt //...
```

> [!TIP]
> Add `-c opt` to your shell aliases or use a `.bazelrc` file to set it as default:
> ```
> # .bazelrc
> build --compilation_mode=opt
> ```
