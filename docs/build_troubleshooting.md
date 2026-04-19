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

## Test Coverage Measurement

### Running Coverage

Use `bazel coverage` with `--combined_report=lcov` to collect Java line-level coverage:

```bash
bazel coverage -c opt --enable_platform_specific_config \
  :timeline_view_test :piano_roll_test :session_view_test \
  :theme_test :component_initialization_test :backend_manager_test \
  --combined_report=lcov
```

The combined LCOV report is written to:
```
bazel-out/_coverage/_coverage_report.dat
```

### Viewing Per-File Coverage

1. Generate the LCOV official report:

```bash
genhtml --output genhtml "bazel-out/_coverage/_coverage_report.dat" --ignore-errors inconsistent
```

2. Parse the LCOV report with this one-liner:

```bash
python3 -c "
import re
with open('$(bazel info execution_root)/bazel-out/_coverage/_coverage_report.dat') as f:
    content = f.read()
for block in content.split('end_of_record'):
    sf = re.search(r'SF:(.*)', block)
    lh = re.search(r'LH:(\d+)', block)
    lf = re.search(r'LF:(\d+)', block)
    if sf and lh and lf:
        name = sf.group(1).strip()
        hit, total = int(lh.group(1)), int(lf.group(1))
        pct = 100.0 * hit / total if total > 0 else 0
        if 'hibiki' in name:
            print(f'{pct:5.1f}%  {hit:4d}/{total:4d}  {name}')
"
```

> Individual test coverage `.dat` files are also available per target in the `testlogs/` directory under `bazel-out/`.

## Bazel Lockfile Stability

If you work across different machines, you might notice `MODULE.bazel.lock` changes unexpectedly.

### Why it happens
- **Platform differences**: Resolution can vary between Linux/Windows or different architectures.
- **Bazel version**: Ensure all machines use the version specified in `.bazelversion`.
- **Environment variables**: Some module extensions respond to local env vars.

### Investigation command
To find out exactly what is causing the lockfile to be out of date on a specific machine:
```bash
bazel mod deps --lockfile_mode=error
```
This will print a detailed "Delta" explaining which extension or dependency triggered the update.

### Proposed Fix
Standardize the environment or pin the lockfile mode in `.bazelrc`:
```bazel
common --lockfile_mode=error
```
