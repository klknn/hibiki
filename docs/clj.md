# Clojure Frontend Migration

This document tracks the migration of the Hibiki DAW GUI from Java to Clojure.

## Motivation

The Java GUI grew to ~4900 lines across 22 files. Clojure offers:
- **Conciseness**: Equivalent UI code in ~50% fewer lines (4927 → 2424)
- **Interactive development**: REPL-driven workflow for rapid UI iteration (see below)
- **Functional state management**: Atoms and immutable data instead of mutable fields
- **Hot reloading**: Live code reloading during development without restarting the app

## REPL-Driven UI Development

Unlike Java where every change requires recompile → restart → reproduce state, Clojure lets
you modify a **running** GUI interactively. This is incredibly powerful for UI work — you can
tweak colors, resize panels, and test event handlers instantly without losing application state.

### Setup: Socket REPL via Clojure CLI (preferred)

The simplest way to get a REPL is via `deps.edn`:

```bash
# Step 1: Build the GUI fat jar and C++ backend (one-time, or after C++/Java/.proto changes)
# We use the _deploy.jar to ensure Protobuf dependencies are packaged for Clojure.
# The backend is also required as the frontend spawns it automatically.
bazel build -c opt //:hibiki-gui-echo_deploy.jar //:hbk-play

# Step 2: Launch GUI + Socket REPL on port 5555
clj -M:repl

# Step 3: In another terminal, connect to the REPL
rlwrap nc localhost 5555
```

This uses `src/main/clojure/hibiki/repl.clj` which starts a Socket REPL server and
launches the GUI. Clojure source files are loaded directly from disk, so edits are
picked up immediately on `:reload`.

### Headless Mode (no GUI)

Run Clojure scripts against the engine without any GUI — great for batch processing,
automated testing, or generating/bouncing projects from the command line:

```bash
# Run a script
clj -M:headless my-song.clj

# Read from stdin (piping)
echo '(hbk/set-bpm! 140) (hbk/play!) (Thread/sleep 5000)' | clj -M:headless -

# No arguments — drops into an interactive headless REPL
clj -M:headless
```

Example script (`my-song.clj`):

```clojure
;; hbk/* helpers are auto-available
(hbk/set-bpm! 120)
(hbk/load-plugin! 0 "testdata/Dexed.vst3")
(hbk/load-clip! 0 0 "testdata/rickroll.mid")
(hbk/play!)
(Thread/sleep 10000)
(hbk/stop!)
(hbk/save! "/tmp/my-project.hbk")
(System/exit 0)
```

> **Note**: If the script file doesn't exist, the headless runner prints a warning
> and falls back to the interactive REPL.

### Alternative: Socket REPL via Deploy JAR

You can also launch a Socket REPL using the Bazel-built deploy JAR:

```bash
# Build the deploy JAR (contains all dependencies)
bazel build -c opt //:hibiki-gui-clj_deploy.jar

# Launch with a Socket REPL server on port 5555
java -Dclojure.server.repl="{:port 5555 :accept clojure.core.server/repl}" \
     -cp "src/main/clojure:bazel-bin/hibiki-gui-clj_deploy.jar" \
     hibiki.ClojureMain
```

> **Tip**: Adding `src/main/clojure` to the classpath **before** the deploy JAR ensures that
> when you edit a `.clj` file and reload it, the REPL picks up your local changes instead
> of the stale version baked into the JAR.

### Live UI Examples

Once connected to the running GUI via the REPL (either embedded `Ctrl+R` panel in Echo,
or `rlwrap nc localhost 5555` for socket REPL), you can interact with everything live.

#### Setup (run once per session)

This is prelude (i.e. preamble lines) in REPL:

```clojure
;; Echo frontend — dev utilities are auto-available
(require '[hibiki.echo.prelude :as hbk])
(import '[hibiki.ui SessionView TimelineView PluginPane Theme])
```

For starter, try copy-paste these lines and type Ctrl+Enter:

```clojure
(hbk/theme! :solarized-dark)    ; switch entire GUI theme live

(hbk/add-timeline-clip! 0 "testdata/rickroll.mid" 0.0 4.0)
(hbk/load-plugin! 0 "testdata/Dexed.vst3")

(hbk/play!)                     ; start playback
(hbk/show-plugin-gui! 0 0)      ; open plugin GUI
;; (hbk/stop!)                   ; stop
```


#### 🎨 Theme — instant visual feedback

```clojure
(hbk/theme! :solarized-dark)    ; switch entire GUI theme live
(hbk/theme! :win95)             ; retro mode
(hbk/theme! :ableton-dark)      ; back to default

;; Fine-tune: update with custom scaling and font size
(.update (Theme/getInstance) Theme$Preset/ABLETON_DARK 1.5 14)
```

#### 🎹 Transport — play, stop, seek

```clojure
(hbk/play!)                     ; start playback
(hbk/stop!)                     ; stop
(hbk/seek! 0.0)                 ; jump to beginning
(hbk/seek! 4.0)                 ; jump to beat 4
(hbk/set-bpm! 140)              ; change tempo
```

#### 🎚️ Tracks & Clips — load, select, rename

```clojure
;; Select track (1-based for SessionView, 0-based for TimelineView)
(.selectTrackByIdx (SessionView/getInstance) 2)
(.setSelectedTrack (TimelineView/getInstance) 1)

;; Load a clip into Session View (track 0, slot 0)
(hbk/load-clip! 0 0 "/path/to/drums.mid")
(hbk/load-clip! 0 0 "/path/to/loop.wav" :loop true)

;; Play / stop / delete clips
(hbk/play-clip! 0 0)            ; trigger session slot
(hbk/stop-track! 0)
(hbk/delete-clip! 0 0)
(hbk/set-clip-loop! 0 0 true)   ; toggle looping

;; Timeline clips
(hbk/add-timeline-clip! 0 "testdata/rickroll.mid" 0.0 4.0)
(hbk/remove-timeline-clip! 0 1) ; remove clip index 1
```

#### 🔌 Plugins — load, tweak, remove

```clojure
;; Load a VST3 plugin onto track 0
(hbk/load-plugin! 0 "/home/user/.vst3/Dexed.vst3")

;; Load a specific sub-plugin (index 1) from a multi-plugin bundle
(hbk/load-plugin! 0 "/home/user/.vst3/mda.vst3" 1)

;; Open the native plugin GUI window
(hbk/show-plugin-gui! 0 0)       ;; track 0, plugin 0

;; Tweak a parameter (0.0–1.0)
(hbk/set-param! 0 0 42 0.75)     ;; track 0, plugin 0, param 42 = 75%

;; Remove a plugin
(hbk/remove-plugin! 0 0)         ;; track 0, plugin 0

;; Scan a VST3 bundle for available sub-plugins
(hbk/list-plugins! "/home/user/.vst3/Dexed.vst3")

;; Switch plugin pane view to another track
(.setSelectedTrack (PluginPane/getInstance) 1)
(.rebuildDeviceChain (PluginPane/getInstance))
```

#### 🎛️ Automation — parameter curves over time

```clojure
;; Add an automation lane for a plugin parameter
(hbk/add-automation! 0 0 42)         ;; track 0, plugin 0, param 42

;; Draw a curve — points are [time-beats value tension]
;; tension: 0=linear, >0=ease-in, <0=ease-out
(hbk/set-automation! 0 0
  [[0 0.0 0]       ;; beat 0: value 0% (linear)
   [4 1.0 0.5]     ;; beat 4: value 100% (ease-in curve)
   [8 0.0 -0.5]    ;; beat 8: value 0% (ease-out curve)
   [12 0.7 0]])     ;; beat 12: value 70% (linear)

;; Request automation data for a track
(hbk/get-automation! 0)

;; Remove an automation lane
(hbk/remove-automation! 0 0)         ;; track 0, lane 0
```

#### 🔍 GUI Inspection — peek inside the running app

```clojure
;; Inspect session view state
(.getSelectedTrack (SessionView/getInstance))    ;=> 0

;; Check what's loaded in a slot
(aget (.slotPaths (SessionView/getInstance)) 0 0)  ;=> "drums.mid" or nil

;; Get timeline track info
(.getSelectedTrack (TimelineView/getInstance))   ;=> 0

;; Frame info
(let [f (hbk/frame)]
  {:width (.getWidth f) :height (.getHeight f)
   :title (.getTitle f)})
```

#### 🎨 Live Widget Hacking — modify components on the fly

```clojure
;; Turn a panel red (always use invokeLater for Swing mutations!)
(import '[javax.swing SwingUtilities])
(SwingUtilities/invokeLater
  #(doto (SessionView/getInstance)
     (.setBackground (java.awt.Color. 100 0 0))
     (.repaint)))

;; Reload a Clojure namespace after editing the source file
(require '[hibiki.echo.prelude :as hbk] :reload)

;; Hot-swap the entire MainView
(hbk/reload!)
```

#### 🎵 MIDI Composition — create clips programmatically

The `hbk/write-midi!` helper takes note maps of `{:tick :pitch :dur :vel}`.
Resolution is in ticks per quarter note (480 is standard).

```clojure
(def PPQ 480)  ;; ticks per quarter note

;; --- 16th note chord arpeggio (Cmaj7: C E G B) ---
(let [chord    [60 64 67 71]
      sixteenth (/ PPQ 4)
      notes    (for [bar (range 4) step (range 16)]
                {:tick (long (+ (* bar 4 PPQ) (* step sixteenth)))
                 :pitch (nth chord (mod step (count chord)))
                 :dur  (long sixteenth)
                 :vel  (if (zero? (mod step 4)) 100 70)})]
  (hbk/write-midi! 0 0 PPQ notes))

;; --- Simple chord progression (Cm → Fm → G → Cm) ---
(let [chords [[60 63 67] [65 68 72] [67 71 74] [60 63 67]]
      notes  (for [[i chord] (map-indexed vector chords)
                   pitch chord]
               {:tick (long (* i 4 PPQ)) :pitch pitch
                :dur  (long (* 4 PPQ))   :vel 80})]
  (hbk/write-midi! 0 1 PPQ notes))

;; --- Euclidean rhythm (5 hits in 8 steps, like Bossa Nova) ---
(let [steps 8  hits 5  sixteenth (/ PPQ 4)
      pattern (for [i (range steps)]
                (< (* i hits) (* steps (inc (quot (* i hits) steps)))))
      notes  (keep-indexed
               (fn [i hit?]
                 (when hit?
                   {:tick (long (* i sixteenth)) :pitch 36
                    :dur (long sixteenth) :vel 90}))
               pattern)]
  (hbk/write-midi! 1 0 PPQ notes))
```

**Read → Transform → Write**: The coolest pattern — read an existing chord clip,
extract pitches, and rewrite it as a 16th note arpeggio:

```clojure
;; Step 1: Request MIDI data from an existing clip (track 0, slot 0)
;;         The response arrives asynchronously via a notification listener.
(import '[hibiki.pb.notifications Notification Notification$ResponseCase]
        '[hibiki.pb.core ClipMidiData])

(def midi-data (promise))
(def bm (hibiki.BackendManager/getInstance))

(def listener
  (reify java.util.function.Consumer
    (accept [_ notif]
      (let [^Notification n notif]
        (when (= (.getResponseCase n) Notification$ResponseCase/CLIP_MIDI_DATA)
          (deliver midi-data (.getClipMidiData n)))))))

(.addNotificationListener bm listener)
(hbk/get-midi! 0 0)              ;; request MIDI for track 0, slot 0

;; Step 2: Wait for response and arpeggiate
(let [^ClipMidiData data (deref midi-data 5000 nil)]
  (when data
    (.removeNotificationListener bm listener)
    (let [pitches (vec (distinct
                         (for [i (range (.getEventsCount data))]
                           (.getPitch (.getEvents data i)))))
          sixteenth (/ PPQ 4)
          notes (for [bar (range 4) step (range 16)]
                  {:tick  (long (+ (* bar 4 PPQ) (* step sixteenth)))
                   :pitch (nth pitches (mod step (count pitches)))
                   :dur   (long sixteenth)
                   :vel   (if (zero? (mod step 4)) 100 70)})]
      ;; Step 3: Write back — instant arp!
      (hbk/write-midi! 0 0 PPQ notes)
      (println "✨ Arpeggiated" (count pitches) "pitches into"
               (count notes) "16th notes!"))))
```

### Workflow Tips

| Tip | Details |
|-----|---------|
| **`:reload` vs `:reload-all`** | `:reload` reloads one namespace. `:reload-all` reloads it and all its dependencies. Use `:reload` for UI tweaks, `:reload-all` when you changed a dependency like `theme.clj`. |
| **`defonce` for state** | `defonce` prevents atoms from being reset on reload — your running state survives. Use `def` only for things you want reset. |
| **`SwingUtilities/invokeLater`** | Always wrap UI mutations in `invokeLater` from the REPL — Swing is single-threaded and will deadlock or corrupt state if you modify components from the REPL thread directly. |
| **`proxy` hot-reload caveat** | `proxy` classes are generated once. If you change a `proxy` body and `:reload`, the already-instantiated proxy won't update. You need to re-create the component (or restart the app). `reify` has the same limitation. |
| **Quick rebuild after Java changes** | If you change Java code (e.g., `BackendManager.java`), you must rebuild the deploy JAR and restart. Clojure REPL reloading only works for `.clj` files. |

### When to Use REPL vs Rebuild

| Scenario | Use REPL | Use Rebuild |
|----------|----------|-------------|
| Adjusting colors, fonts, spacing | ✅ | |
| Testing notification handlers | ✅ | |
| Inspecting atom/state contents | ✅ | |
| Changing a `proxy`/`reify` body | | ✅ (need new instance) |
| Modifying Java backend classes | | ✅ (need new JAR) |
| Adding new `:import` classes | | ✅ (need namespace reload) |

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
| `hibiki.ui.core` | ~188 | `GuiMain.java` | Application entry point, JFrame setup |
| `hibiki.ui.theme` | ~248 | `Theme.java` | Colors, fonts, scaling, grid helpers |
| `hibiki.ui.widgets` | ~274 | (Part of Theme) | Reusable UI widgets (buttons, meters) |
| `hibiki.ui.session` | ~416 | `SessionView.java`, `SessionViewIpc.java` | Session clip grid view |
| `hibiki.ui.timeline` | ~488 | `TimelineView.java`, `TimelineRenderer.java`, etc. | Timeline arrangement view with 3-layer grid, drag-to-create, rounded-rect clips |
| `hibiki.ui.piano-roll` | ~291 | `PianoRoll.java`, `PianoRollRenderer.java`, etc. | MIDI piano roll editor (supports new empty clips) |
| `hibiki.ui.plugin` | ~288 | `PluginPane.java` | Plugin device chain pane |
| `hibiki.ui.browser` | ~231 | `BrowserPane.java` | File/plugin browser |

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

| Component | Java Files | Java Lines | Clj Lines | Reduction |
|-----------|-----------|-----------|---------------|-----------|
| Theme + Widgets | `Theme.java`, `GridMode.java`, `LevelMeter.java`, `ZoomControlPanel.java` | 523 | 522 | 0% |
| Session View | `SessionView.java`, `SessionViewIpc.java` | 571 | 416 | 27% |
| Timeline View | `TimelineView.java`, `TimelineRenderer.java`, `TimelineMouseHandler.java`, `TimelineNotificationHandler.java` | 1190 | 488 | 59% |
| Piano Roll | `PianoRoll.java`, `PianoRollRenderer.java`, `PianoRollMouseHandler.java`, `MidiDataModel.java` | 1047 | 291 | 72% |
| Plugin Pane | `PluginPane.java` | 339 | 288 | 15% |
| Browser | `BrowserPane.java` | 378 | 231 | 39% |
| Main/Core | `GuiMain.java`, `MainView.java`, `TopBar.java`, `SettingsDialog.java`, `WaveformPanel.java` | 762 | 188 | 75% |
| **Total** | **22 files** | **4927** | **2424** | **51%** |

### Key Patterns

**Self-referencing `doto` caveat**: `doto` always threads the **original** object, not
intermediate return values. Two pitfalls:

```clojure
;; PITFALL 1: Self-reference — panel not yet bound during doto body
(doto (JPanel.)
  (.setLayout (BoxLayout. panel BoxLayout/Y_AXIS)))  ; ❌ panel undefined
;; Fix: bind first
(let [p (JPanel.)] (.setLayout p (BoxLayout. p BoxLayout/Y_AXIS)) p)

;; PITFALL 2: Method chaining — doto ignores intermediate return values
(doto (JScrollPane. param-list)
  (.getVerticalScrollBar)        ; returns JScrollBar, but doto ignores it
  (.setUnitIncrement 10))        ; ❌ called on JScrollPane, not JScrollBar!
;; Fix: break out of doto for intermediate return values
(let [scroll (doto (JScrollPane. param-list) (.setBorder nil))]
  (.setUnitIncrement (.getVerticalScrollBar scroll) 10))
```

**IPC class names**: Protobuf-generated classes exactly mirror the schema message names:
- `PluginCmd` (not `PluginCommand` or `Plugin`)
- `TrackCmd` (not `TrackCommand` or `Track`)
- `MidiCmd` (not `MidiCommand`)

## Type Safety

Clojure is dynamically typed — Java interop calls go through **reflection** at runtime unless
you provide type hints (`^Type`). Without hints, calling a wrong method (like `.setUnitIncrement`
on `JScrollPane`) only fails when executed, not at compile time.

### `*warn-on-reflection*`

All namespaces have `(set! *warn-on-reflection* true)` after the `ns` form. This makes the
compiler emit **compile-time warnings** for any method call it can't resolve statically:

```
Reflection warning, plugin.clj:99:9 - call to method setUnitIncrement
  on javax.swing.JScrollPane can't be resolved (no such method).
```

This is Clojure's closest equivalent to Java's compile-time type checking for interop calls.

### Type Hint Rules

| Rule | Example | Notes |
|------|---------|-------|
| Hint locals in `let` | `(let [^JPanel p ...]` | Resolves method calls on `p` |
| Hint fn params | `[^BackendManager backend]` | Direct dispatch, no reflection |
| Hint fn return | `^JPanel [backend]` | Caller knows the return type |
| `reify` params — **NO** hints | `(accept [_ notif] ...)` | Use `let` cast inside body |
| Only `^long`/`^double` primitives | `[^long idx]` | `^int`, `^boolean`, `^float` are **not** supported |
| Max 4 primitive args | — | Functions with 5+ primitive-hinted args fail |
| Arrays need `^objects`/`^ints` etc. | `^objects arr` | For `aget`/`aset` without reflection |

### `reify` Interface Method Gotcha

When implementing a generic interface like `Consumer<Notification>`, the erased method
signature is `accept(Object)`. Adding `^Notification` on the parameter causes
"Can't find matching method" at compile time:

```clojure
;; ❌ WRONG — erased generic takes Object
(reify Consumer (accept [_ ^Notification n] ...))

;; ✅ CORRECT — unhinted param, cast inside body
(reify Consumer
  (accept [_ notif]
    (let [^Notification notif notif]
      (.responseType notif)  ;; now resolved statically
      ...)))
```

## Testing

### Clojure Test Files

| Test File | Tests | Coverage |
|-----------|-------|----------|
| `theme_test.clj` | 15 | Colors, fonts, scaling, presets, grid helpers |
| `piano_roll_test.clj` | 12 | Note records, MIDI parsing, file loading (incl. empty sequence for new clips) |
| `session_test.clj` | 3 | Track selection, default state |
| `timeline_test.clj` | 8 | Track data records, state, pixel math |
| `browser_test.clj` | 8 | FileItem records, toString, extension sets |

### Running Tests

```bash
# Run all Clojure tests via Bazel
bazel test -c opt :clojure_tests --test_output=all

# Run alongside Java tests
bazel test -c opt :all --test_output=errors

# Run via Clojure CLI (cognitect test-runner, auto-discovers test namespaces)
clj -X:test

# Run via Clojure CLI (custom runner, hardcoded namespace list)
clj -M:test-runner
```

### Test Runners

| Runner | Command | Discovery |
|--------|---------|-----------|
| **Bazel** | `bazel test :clojure_tests` | `ClojureTestRunner.java` with hardcoded list |
| **cognitect test-runner** | `clj -X:test` | Auto-discovers `*-test` namespaces in `src/test/clojure` |
| **Custom runner** | `clj -M:test-runner` | `hibiki.test-runner` namespace with hardcoded list |

## Building & Running

### Bazel (primary)

```bash
# Build Clojure frontend
bazel build -c opt --enable_platform_specific_config :hibiki-gui-clj

# Run Clojure frontend
bazel run -c opt --enable_platform_specific_config :hibiki-gui-clj

# Run Java frontend (still available)
bazel run -c opt --enable_platform_specific_config :hibiki-gui-java
```

### Clojure CLI (alternative)

Clojure CLI (`deps.edn`) provides lightweight dependency resolution and integrates with
standard Clojure tooling (CIDER, Calva, Cursive). Unlike Leiningen, it doesn't try to be
a full build tool — it just resolves deps and sets up the classpath, which is ideal since
Bazel already handles builds.

**Prerequisites**: Java 17+, [Clojure CLI](https://clojure.org/guides/install), one initial Bazel build.

#### First-Time Setup

```bash
# 1. Build the GUI jar with Bazel (required before any clj command)
bazel build -c opt //:hibiki-gui-lib
```

This produces `bazel-bin/libhibiki-gui-lib.jar` which `deps.edn` references as a
local dependency. It contains all compiled Java classes and Protobuf-generated IPC code.

> **Note**: Re-run this command when Java sources or `.proto` schemas change.
> For day-to-day Clojure-only changes, `clj -M:run` works directly.

#### Common Commands

```bash
clj -M:run          # Run the Clojure GUI (full port)
clj -M:echo         # Run Echo hybrid GUI (Java components + Clojure glue)
clj -M:echo:dev     # Echo + Socket REPL on port 5555
clj -X:test         # Run Clojure tests (cognitect test-runner)
clj -M:test-runner  # Run Clojure tests (custom runner, backup)
clj -M:repl         # Clojure GUI + Socket REPL on port 5555
```

Connect to socket REPL: `rlwrap nc localhost 5555`

> **Tip**: The Echo frontend (`clj -M:echo`) includes an embedded REPL panel.
> Press **Ctrl+R** or click the **λ REPL** button to toggle it. Use **Ctrl+Enter**
> to evaluate, select text for partial eval, and **Ctrl+↑↓** for history.

#### Bazel vs Clojure CLI

| Aspect | Bazel | Clojure CLI |
|--------|-------|-------------|
| **Java/Protobuf build** | Automatic (build rule) | One-time: `bazel build -c opt //:hibiki-gui-lib` |
| **C++ backend** | Built together | Not supported (uses pre-built `hbk-play`) |
| **REPL** | Deploy JAR + `java -D...` | `clj -M:repl` |
| **IDE integration** | Limited | CIDER, Calva, Cursive |
| **CI/CD** | ✅ Primary | Optional |

## When Clojure Excels (with real examples)

### 1. Data-driven configuration — Theme (265 → 123 lines, 54% reduction)

Java requires mutable fields, a `switch` statement per preset, and explicit getters:

```java
// Theme.java — 17 public Color fields + switch per preset + 6 presets
public Color BG_DARKER, BG_DARK, BG_MEDIUM, PANEL_BG, ...;
private void applyPreset(Preset preset) {
    switch (preset) {
        case ABLETON_DARK:
            BG_DARKER = new Color(25, 25, 25);
            BG_DARK = new Color(34, 34, 34);
            // ... 15 more fields × 6 presets = 90+ lines of assignments
            break;
        case ABLETON_LIGHT: ...
    }
}
```

Clojure replaces the entire class with a map literal — each preset is just a `{:keyword (Color.)}` map, and lookup is `(get (theme) :bg-dark)`:

```clojure
;; theme.clj — presets are plain data, zero boilerplate
(def presets
  {:ableton-dark
   {:bg-darker  (Color. 25 25 25)
    :bg-dark    (Color. 34 34 34)
    ;; ... all 17 keys, same for each preset
    }})

(defn color [k] (get (theme) k))   ; that's the entire getter
```

**Why it wins**: Data-as-code eliminates the `switch`/`enum`/field boilerplate entirely.  Adding a new theme preset is +17 lines of data, not +17 assignments in a switch case.

### 2. Custom painting with closeable state — LevelMeter (41 → 25 lines, 39% reduction)

Java needs a subclass with mutable fields:

```java
// LevelMeter.java — class with mutable state
class LevelMeter extends JPanel {
    private float levelL = 0, levelR = 0;

    void setLevels(float l, float r) {
        this.levelL = l; this.levelR = r; repaint();
    }

    @Override protected void paintComponent(Graphics g) {
        super.paintComponent(g);
        int h = getHeight(), w = getWidth();
        g.setColor(Theme.getInstance().ACCENT_GREEN);
        int hL = (int)(levelL * h);
        g.fillRect(1, h - hL, w/2 - 2, hL);
        // ... 20 more lines of drawing
    }
}
```

Clojure uses `proxy` + `atom` — no class needed, the state is a closed-over atom:

```clojure
;; widgets.clj — proxy closes over an atom, returns panel + updater fn
(defn make-level-meter []
  (let [levels (atom [0.0 0.0])
        panel  (proxy [JPanel] []
                 (paintComponent [^Graphics g]
                   (proxy-super paintComponent g)
                   (let [[l r] @levels, w (.getWidth this), h (.getHeight this)]
                     (.setColor g (t/color :accent-green))
                     (.fillRect g 0 (- h (int (* h l))) (/ w 2) (int (* h l)))
                     ;; ... rest of drawing
                     )))]
    {:panel panel
     :set-levels! (fn [l r] (reset! levels [l r]) (.repaint panel))}))
```

**Why it wins**: No class definition needed. The `atom` replaces mutable fields and the
returned map replaces public setter methods. Caller gets `(:set-levels! meter)`.

### 3. State management — Session View (mutable arrays → atom)

Java uses parallel mutable arrays and tracks selection state imperatively:

```java
// SessionView.java — 6 mutable fields, explicit singleton
private JButton[][] slotButtons = new JButton[5][5];
String[][] slotPaths = new String[5][5];
private LevelMeter[] trackMeters = new LevelMeter[4];
private JPanel[] trackStrips = new JPanel[4];
JLabel[] trackHeaders = new JLabel[4];
private int selectedTrack = 0;
private static SessionView instance;
```

Clojure uses a single atom with a map — all state is in one place, thread-safe by default:

```clojure
;; session.clj — one atom holds everything
(defonce ^:private session-state
  (atom {:slot-buttons    {}    ;; {[track slot] -> JButton}
         :slot-paths      {}    ;; {[track slot] -> path}
         :track-meters    {}    ;; {track-idx -> {:panel :set-levels!}}
         :track-strips    {}
         :track-headers   {}
         :selected-track  0
         :instance        nil}))

(defn get-selected-track ^long [] (:selected-track @session-state))
```

**Why it wins**: No parallel arrays, no null initialization, no getter boilerplate.
State shape is self-documenting through map keys.

### 4. Notification handlers — lambda vs reify (comparable, slight Clojure advantage)

Java lambda:
```java
BackendManager.getInstance().addNotificationListener(notification -> {
    if (notification.responseType() == Response.ClipInfo) {
        ClipInfo info = (ClipInfo) notification.response(new ClipInfo());
        updateSlotLabel(info.trackIndex(), info.slotIndex(), info.name());
    }
});
```

Clojure reify:
```clojure
(.addNotificationListener backend
  (reify java.util.function.Consumer
    (accept [_ notif]
      (let [^Notification notif notif]
        (when (= (.responseType notif) Response/ClipInfo)
          (let [^ClipInfo info (.response notif (ClipInfo.))]
            (update-slot-label (.trackIndex info) (.slotIndex info) (.name info))))))))
```

**Roughly equal**: Lambda syntax is slightly more concise in Java, but Clojure's destructuring
and `when`/`case` patterns make complex notification handlers shorter.

## Where Java and Clojure Are Similar

### IPC helpers — Protobuf Builder boilerplate (comparable)

Both need the same Protobuf builder ceremony — Clojure doesn't save much here:

```java
// PluginPane.java
private void sendShowGui(int trackIndex, int pluginIndex) {
    Request req = Request.newBuilder()
        .setPlugin(PluginCmd.newBuilder()
            .setAction(PluginCmd.Action.ACTION_SHOW_GUI)
            .setTarget(EntityRef.newBuilder()
                .setTrackIndex(trackIndex)
                .setPluginIndex(pluginIndex)))
        .build();
    BackendManager.getInstance().sendRequest(req);
}
```

```clojure
;; plugin.clj (type hints add verbosity but enable direct dispatch)
(defn- send-show-gui
  [^BackendManager backend ^long track-idx ^long plugin-idx]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setPlugin (-> (PluginCmd/newBuilder)
                        (.setAction PluginCmd$Action/ACTION_SHOW_GUI)
                        (.setTarget (-> (EntityRef/newBuilder)
                                        (.setTrackIndex (int track-idx))
                                        (.setPluginIndex (int plugin-idx))))))
        (.build))))
```

**Similar**: Protobuf Builder code is inherently imperative. Method chaining (`->`)
makes Clojure look similar to Java's pattern, though type casing `(int ...)` can be slightly
more verbose than Java's implicit primitive widening/narrowing.

## Java Design Patterns → Clojure

This section maps common Java design patterns to their Clojure equivalents using real
examples from this codebase.

### 1. Singleton → `defonce` atom

Java's Singleton pattern requires `private` constructor, `static` instance, and `synchronized`:

```java
// BackendManager.java — classic double-checked locking Singleton
public class BackendManager {
    private static BackendManager instance;
    private BackendManager() {}                       // private ctor
    public static synchronized BackendManager getInstance() {
        if (instance == null) instance = new BackendManager();
        return instance;
    }
}
// Usage: BackendManager.getInstance().sendRequest(...)
```

Clojure replaces this with `defonce` — the value is created once per JVM, survives
namespace reloads, and is thread-safe by default:

```clojure
;; session.clj — defonce = singleton, atom = thread-safe mutable state
(defonce ^:private session-state
  (atom {:selected-track 0, :instance nil}))

(defn get-instance ^JPanel [] (:instance @session-state))
;; Usage: (session/get-instance)
```

**Pattern eliminated**: No `private` constructor, no `synchronized`, no `null` check.
`defonce` guarantees single initialization; the `atom` provides thread-safe access.

### 2. Observer / Listener → atom watches or function lists

Java's Observer pattern requires an interface, a list, and synchronized add/remove:

```java
// Theme.java — custom listener interface + explicit list management
public interface ThemeListener { void onThemeChanged(); }
private final List<ThemeListener> listeners = new ArrayList<>();
public void addListener(ThemeListener l) { listeners.add(l); }
public void update(...) {
    applyPreset(preset);
    for (ThemeListener l : listeners) l.onThemeChanged();
}

// SessionView.java — Consumer<Notification> with synchronized list
public void addNotificationListener(Consumer<Notification> listener) {
    synchronized (listeners) { listeners.add(listener); }
}
```

Clojure stores listeners as plain functions in the atom — no interface needed:

```clojure
;; theme.clj — listeners are just functions stored in the atom
(defonce ^:private state
  (atom {:preset :ableton-dark, :listeners []}))

(defn add-listener! [f]
  (swap! state update :listeners conj f))   ;; f is any (fn [] ...)

(defn update-theme! [& {:keys [preset]}]
  (swap! state assoc :preset preset)
  (doseq [f (:listeners @state)] (f)))      ;; call each fn
```

**Pattern simplified**: No interface definition. Listeners are first-class functions — any
`(fn [] ...)` works. `add-watch` on atoms is another option for reactive state changes.

### 3. Strategy / Enum with behavior → `case` expressions or maps

Java uses enums with methods to encapsulate varying behavior:

```java
// GridMode.java — 151 lines: enum + switch + toString override
enum GridMode {
    AUTO("Auto"), BAR("1/1"), HALF("1/2"), QUARTER("1/4"),
    EIGHTH("1/8"), SIXTEENTH("1/16"), ...;
    private final String label;
    GridMode(String label) { this.label = label; }
    @Override public String toString() { return label; }

    int getTickInterval(int resolution) {
        int bar = resolution * 4;
        switch (this) {
            case BAR:      return bar;
            case HALF:     return bar / 2;
            case QUARTER:  return resolution;
            case EIGHTH:   return resolution / 2;
            // ...12 more cases
        }
    }
}
```

Clojure uses a vector of keywords + a `case` expression — no class needed:

```clojure
;; theme.clj — 30 lines total for the same functionality
(def grid-modes
  [:auto :seconds :bar :half :quarter :eighth :sixteenth :thirty-second
   :triplet-quarter :triplet-eighth :triplet-16th :triplet-32nd])

(defn tick-interval [mode resolution]
  (let [bar (* resolution 4)]
    (case mode
      :bar      bar
      :half     (/ bar 2)
      :quarter  resolution
      :eighth   (/ resolution 2)
      ;; ...same logic, 1/5th the code
      resolution)))
```

**Pattern eliminated**: No class, no constructor, no `toString`, no field storage.
Keywords are their own representation. `case` replaces `switch` with less ceremony.

### 4. Builder → Thread-first macro (`->`)

Java's Builder pattern is used for constructing Protobuf messages:

```java
// Java — Builder pattern with method chaining
Request req = Request.newBuilder()
    .setPlugin(PluginCmd.newBuilder()
        .setAction(PluginCmd.Action.ACTION_SHOW_GUI)
        .setTarget(EntityRef.newBuilder()
            .setTrackIndex(trackIndex)
            .setPluginIndex(pluginIndex)))
    .build();
```

Clojure uses the thread-first macro (`->`) to chain builder method calls:

```clojure
;; Clojure — thread-first macro (->) mirrors method chaining
(let [req (-> (Request/newBuilder)
              (.setPlugin (-> (PluginCmd/newBuilder)
                              (.setAction PluginCmd$Action/ACTION_SHOW_GUI)
                              (.setTarget (-> (EntityRef/newBuilder)
                                              (.setTrackIndex (int track-idx))
                                              (.setPluginIndex (int plugin-idx))))))
              (.build))]
  (.sendRequest backend req))
```

**Clean and readable**: Clojure's thread-first macro elegantly maps to Java's method-chaining builder pattern without nested parenthetical clutter.

### 5. Template Method → `proxy` with closures

Java uses inheritance and `@Override` for the Template Method pattern:

```java
// LevelMeter.java — subclass overrides paintComponent
class LevelMeter extends JPanel {
    private float levelL, levelR;            // mutable fields
    @Override
    protected void paintComponent(Graphics g) {
        super.paintComponent(g);             // template hook
        // ...custom drawing using this.levelL, this.levelR
    }
}
```

Clojure uses `proxy` which closes over external state instead of using `this` fields:

```clojure
;; widgets.clj — proxy closes over atom, no inheritance
(let [levels (atom [0.0 0.0])
      panel  (proxy [JPanel] []
               (paintComponent [^Graphics g]
                 (proxy-super paintComponent g)    ;; template hook
                 (let [[l r] @levels]
                   ;; ...custom drawing using closed-over atom
                   )))]
  {:panel panel :set-levels! (fn [l r] (reset! levels [l r]))})
```

**Pattern simplified**: Closures over atoms replace subclass fields. No need to define a
separate class — the proxy is anonymous and its state is captured from the enclosing scope.

### 6. Value Object → `defrecord`

Java uses inner classes with mutable fields for data objects:

```java
// MidiDataModel.java — mutable inner class with 6 fields
static class Note {
    int pitch;
    long startTick, durationTicks;
    int velocity;
    MidiEvent onEvent, offEvent;
    Note(int pitch, long startTick, long durationTicks, int velocity) {
        this.pitch = pitch;
        this.startTick = startTick;
        this.durationTicks = durationTicks;
        this.velocity = velocity;
    }
}
```

Clojure uses `defrecord` for named tuples with automatic `equals`, `hashCode`, and keyword access:

```clojure
;; piano_roll.clj — one line, immutable, with :keyword access
(defrecord Note [pitch start-tick duration-ticks velocity])
;; Usage:
(->Note 60 0 480 100)           ;; positional constructor
(:pitch note)                    ;; keyword access
(assoc note :velocity 80)       ;; "update" returns new Note
```

**Pattern simplified**: `defrecord` gives you a constructor, field access, equality, hashing,
and immutability in one line. In Java you'd add `equals`/`hashCode`/`toString` manually
(or use `record` in Java 16+).

### Summary Table

| Java Pattern | Java Mechanism | Clojure Equivalent | Reduction |
|-------------|---------------|-------------------|-----------|
| **Singleton** | `private` ctor + `static getInstance()` | `defonce` + atom | ~80% |
| **Observer** | Interface + `List<Listener>` + `synchronized` | Functions in atom + `doseq` | ~70% |
| **Strategy/Enum** | `enum` + `switch` + fields + ctor | Keywords + `case` | ~80% |
| **Builder** | Builder class + method chaining | `let` + `do` | ~0% (similar) |
| **Template Method** | Subclass + `@Override` | `proxy` + closures | ~50% |
| **Value Object** | Inner class + fields + ctor | `defrecord` (1 line) | ~90% |

## Known Issues

- Java GUI and Clojure GUI use the same backend; both builds coexist
- Clojure test startup is slower (~3s) due to runtime initialization
- Some IDE lint errors for `ClojureMain.java` imports (expected — Clojure jar not in IDE classpath)
- 6 unfixable `paintComponent` reflection warnings remain — `proxy-super` cannot resolve protected parent methods at compile time (Clojure limitation). All other reflection warnings have been resolved with explicit type hints.
- `Nonexistent button 4/5` warnings from `X11.XToolkit` appear on scroll wheel events — this is a JDK/X11 compatibility issue unrelated to application code
