# Clojure Frontend Migration

This document tracks the migration of the Hibiki DAW GUI from Java to Clojure.

## Motivation

The Java GUI grew to ~2500 lines across multiple files. Clojure offers:
- **Conciseness**: Equivalent UI code in ~40% fewer lines
- **Interactive development**: REPL-driven workflow for rapid UI iteration
- **Functional state management**: Atoms and immutable data instead of mutable fields
- **Hot reloading**: Potential for live code reloading during development

## Architecture

### Bootstrap
The Clojure frontend is bootstrapped via `ClojureMain.java`, which:
1. Calls `clojure.java.api.Clojure` to initialize the runtime
2. Requires the `hibiki.ui.core` namespace
3. Invokes the `-main` entry point

This approach avoids AOT compilation and leverages Clojure's dynamic loading.

### Namespace Structure

| Namespace | Lines | Java Equivalent | Description |
|-----------|-------|-----------------|-------------|
| `hibiki.ui.core` | ~100 | `GuiMain.java` | Application entry point, JFrame setup |
| `hibiki.ui.theme` | ~250 | `Theme.java` | Colors, fonts, scaling, grid helpers |
| `hibiki.ui.widgets` | ~90 | (Part of Theme) | Reusable UI widgets (buttons, meters) |
| `hibiki.ui.session` | ~400 | `SessionView.java`, `SessionViewIpc.java` | Session clip grid view |
| `hibiki.ui.timeline` | ~265 | `TimelineView.java`, `TimelineRenderer.java`, etc. | Timeline arrangement view |
| `hibiki.ui.piano-roll` | ~280 | `PianoRoll.java`, `PianoRollRenderer.java`, etc. | MIDI piano roll editor |
| `hibiki.ui.plugin` | ~190 | `PluginPane.java` | Plugin device chain pane |
| `hibiki.ui.browser` | ~170 | `BrowserPane.java` | File/plugin browser |

### Build System

Clojure integration uses `http_jar` rules (not Maven) to fetch:
- `org.clojure:clojure:1.12.0`
- `org.clojure:spec.alpha:0.5.238`
- `org.clojure:core.specs.alpha:0.4.74`

This avoids a circular dependency in Clojure's Maven POM (clojure → spec.alpha → clojure).

Clojure `.clj` files are packaged as Java resources with `resource_strip_prefix = "src/main/clojure"` 
so the Clojure runtime can locate them on the classpath.

## Migration Results

### Line Count Comparison

| Component | Java Lines | Clojure Lines | Reduction |
|-----------|-----------|---------------|-----------|
| Theme + Widgets | 380 | 340 | 11% |
| Session View | 560 | 400 | 29% |
| Timeline View | 700 | 265 | 62% |
| Piano Roll | 750 | 280 | 63% |
| Plugin Pane | 290 | 190 | 34% |
| Browser | 250 | 170 | 32% |
| Main/Core | 80 | 100 | (25% increase) |
| **Total** | **3010** | **1745** | **42%** |

### Key Patterns

**Self-referencing `doto` caveat**: Clojure's `doto` macro evaluates all forms in a body,
but the binding is not yet available during the `doto` body evaluation. For patterns like
`BoxLayout(panel, BoxLayout.Y_AXIS)` where the component itself is needed, use a `let` binding
followed by explicit method calls:

```clojure
;; WRONG — panel not yet bound during doto body
(doto (JPanel.)
  (.setLayout (BoxLayout. panel BoxLayout/Y_AXIS)))

;; CORRECT — bind first, then configure
(let [p (JPanel.)]
  (.setLayout p (BoxLayout. p BoxLayout/Y_AXIS))
  p)
```

**IPC class names**: FlatBuffer-generated classes exactly mirror the schema names:
- `SetParamValue` (not `SetParam`)
- `GetClipMidi` (not `RequestClipMidi`)
- `ListPlugins` (exists in request schema)

## Testing

### Clojure Test Files

| Test File | Tests | Coverage |
|-----------|-------|----------|
| `theme_test.clj` | 17 | Colors, fonts, scaling, presets, listeners, grid helpers |
| `piano_roll_test.clj` | 11 | Note records, MIDI parsing, file loading |
| `session_test.clj` | 3 | Track selection, default state |
| `timeline_test.clj` | 8 | Track data records, state, pixel math |

### Running Tests

```bash
# Run all Clojure tests
bazel test -c opt :clojure_tests --test_output=all

# Run alongside Java tests
bazel test -c opt :all --test_output=errors
```

### Test Runner

Tests use `clojure.test` and are executed via `ClojureTestRunner.java`,
a Java main class that:
1. Requires all Clojure test namespaces
2. Invokes `clojure.test/run-tests` on each
3. Exits with code 1 if any test fails

## Building & Running

```bash
# Build Clojure frontend
bazel build -c opt --enable_platform_specific_config :hibiki-gui-clj

# Run Clojure frontend
bazel run -c opt --enable_platform_specific_config :hibiki-gui-clj

# Run Java frontend (still available)
bazel run -c opt --enable_platform_specific_config :hibiki-gui-java
```

## Known Issues

- Java GUI and Clojure GUI use the same backend; both builds coexist
- Clojure test startup is slower (~3s) due to runtime initialization
- Some IDE lint errors for `ClojureMain.java` imports (expected — Clojure jar not in IDE classpath)
