module gui_d.device_panels;

import core.stdc.math : atanf, cosf, expf, log10f, logf, powf, sinf, tanhf;
import dlangui.core.events : MouseAction, MouseButton, MouseEvent, MouseFlag;
import dlangui.core.types : Rect;
import dlangui.graphics.colors : COLOR_TRANSPARENT;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.graphics.fonts : FontRef, FontWeight;
import gui_d.theme : adjustColorBrightness, contrastTextColor, dp, dpf,
  drawCenteredText, drawFilledCircle, drawOpenArc,
  drawRotaryKnob, drawThemeCard, drawThickLine,
  drawVCenteredText, FlPalette, FlThemeId, getDawFont, getDawMonoFont,
  getFlPalette, ScopedRetroAntialias, truncateLabel;
import gui_d.transport_model : ParamItemState, PluginSlotState, TransportModel;
import std.algorithm : canFind;
import std.format : format;
import std.string : toLower;
import std.utf : toUTF32;

/// Identifies which custom builtin device UI should be rendered inside the Fruity Wrapper.
enum BuiltinDeviceKind {
  GENERIC,
  EQ,
  COMPRESSOR,
  LIMITER,
  HOTT,
  MAXIM,
  DELAY,
  REVERB,
  PHASER,
  CHORUS,
  BITCRUSHER,
  ENVELOPE_SHAPER,
  CONVOLVER,
  VOCODEY,
  STEREO_WIDTH,
  ACID_BASS,
  THREE_OSC,
  SAMPLER,
  EPIANO,
  ORGAN,
  DRUM_MACHINE,
  DR8_MODULE,
  FILM,
  SYLPH,
  AUX,
}

/// Maps a plugin URI/name to its custom builtin device UI variant (`GENERIC` if none).
BuiltinDeviceKind resolveBuiltinDeviceKind(const(PluginSlotState)* plug) pure nothrow @safe {
  if (!plug)
    return BuiltinDeviceKind.GENERIC;
  const string u = plug.uri;
  if (u == "builtin://eq")
    return BuiltinDeviceKind.EQ;
  if (u == "builtin://compressor")
    return BuiltinDeviceKind.COMPRESSOR;
  if (u == "builtin://limiter")
    return BuiltinDeviceKind.LIMITER;
  if (u == "builtin://hott")
    return BuiltinDeviceKind.HOTT;
  if (u == "builtin://maxim")
    return BuiltinDeviceKind.MAXIM;
  if (u == "builtin://delay")
    return BuiltinDeviceKind.DELAY;
  if (u == "builtin://reverb")
    return BuiltinDeviceKind.REVERB;
  if (u == "builtin://phaser")
    return BuiltinDeviceKind.PHASER;
  if (u == "builtin://chorus")
    return BuiltinDeviceKind.CHORUS;
  if (u == "builtin://bitcrusher")
    return BuiltinDeviceKind.BITCRUSHER;
  if (u == "builtin://envelope_shaper")
    return BuiltinDeviceKind.ENVELOPE_SHAPER;
  if (u == "builtin://convolver")
    return BuiltinDeviceKind.CONVOLVER;
  if (u == "builtin://vocodey")
    return BuiltinDeviceKind.VOCODEY;
  if (u == "builtin://stereo_width")
    return BuiltinDeviceKind.STEREO_WIDTH;
  if (u == "builtin://acid_bass")
    return BuiltinDeviceKind.ACID_BASS;
  if (u == "builtin://3xosc")
    return BuiltinDeviceKind.THREE_OSC;
  if (u == "builtin://sampler")
    return BuiltinDeviceKind.SAMPLER;
  if (u == "builtin://epiano")
    return BuiltinDeviceKind.EPIANO;
  if (u == "builtin://organ")
    return BuiltinDeviceKind.ORGAN;
  if (u == "builtin://drum_machine")
    return BuiltinDeviceKind.DRUM_MACHINE;
  if (u.length >= 14 && u[0 .. 14] == "builtin://dr8_")
    return BuiltinDeviceKind.DR8_MODULE;
  if (u == "builtin://film" || (u.length >= 14 && u[0 .. 14] == "builtin://film"))
    return BuiltinDeviceKind.FILM;
  if (u == "builtin://sylph")
    return BuiltinDeviceKind.SYLPH;
  if (u == "builtin://aux")
    return BuiltinDeviceKind.AUX;
  return BuiltinDeviceKind.GENERIC;
}

/// Natural logical-dp body dimensions `(w, h)` matching each Swing device panel's proportions.
struct DevicePreferredSize {
  int width;
  int height;
}

DevicePreferredSize preferredDeviceBodySize(BuiltinDeviceKind kind) pure nothrow @safe {
  final switch (kind) {
  case BuiltinDeviceKind.AUX:
    return DevicePreferredSize(220, 175);
  case BuiltinDeviceKind.LIMITER:
    return DevicePreferredSize(280, 195);
  case BuiltinDeviceKind.CONVOLVER:
    return DevicePreferredSize(340, 185);
  case BuiltinDeviceKind.DR8_MODULE:
    return DevicePreferredSize(320, 215);
  case BuiltinDeviceKind.STEREO_WIDTH:
    return DevicePreferredSize(340, 200);
  case BuiltinDeviceKind.BITCRUSHER:
    return DevicePreferredSize(350, 205);
  case BuiltinDeviceKind.COMPRESSOR:
    return DevicePreferredSize(380, 235);
  case BuiltinDeviceKind.CHORUS:
    return DevicePreferredSize(400, 210);
  case BuiltinDeviceKind.VOCODEY:
    return DevicePreferredSize(430, 225);
  case BuiltinDeviceKind.DELAY:
    return DevicePreferredSize(440, 235);
  case BuiltinDeviceKind.REVERB:
    return DevicePreferredSize(440, 235);
  case BuiltinDeviceKind.ENVELOPE_SHAPER:
    return DevicePreferredSize(440, 250);
  case BuiltinDeviceKind.EQ:
    return DevicePreferredSize(460, 255);
  case BuiltinDeviceKind.MAXIM:
    return DevicePreferredSize(450, 260);
  case BuiltinDeviceKind.EPIANO:
    return DevicePreferredSize(460, 235);
  case BuiltinDeviceKind.ORGAN:
    return DevicePreferredSize(480, 255);
  case BuiltinDeviceKind.PHASER:
    return DevicePreferredSize(500, 250);
  case BuiltinDeviceKind.ACID_BASS:
    return DevicePreferredSize(520, 240);
  case BuiltinDeviceKind.HOTT:
    return DevicePreferredSize(540, 255);
  case BuiltinDeviceKind.SAMPLER:
    return DevicePreferredSize(440, 320);
  case BuiltinDeviceKind.THREE_OSC:
    return DevicePreferredSize(440, 330);
  case BuiltinDeviceKind.DRUM_MACHINE:
    return DevicePreferredSize(580, 320);
  case BuiltinDeviceKind.FILM:
    return DevicePreferredSize(740, 380);
  case BuiltinDeviceKind.SYLPH:
    return DevicePreferredSize(860, 456);
  case BuiltinDeviceKind.GENERIC:
    return DevicePreferredSize(420, 240);
  }
}

/// Clamps oversized `bodyRc` to each device's natural max aspect box so controls never stretch thinly.
Rect fitDeviceBodyRect(Rect bodyRc, BuiltinDeviceKind kind) nothrow @safe {
  const sz = preferredDeviceBodySize(kind);
  const int maxW = dp((sz.width * 5) / 4);
  const int maxH = dp((sz.height * 23) / 20);
  const int w = (bodyRc.width > maxW) ? maxW : bodyRc.width;
  const int h = (bodyRc.height > maxH) ? maxH : bodyRc.height;
  const int x0 = bodyRc.left + (bodyRc.width - w) / 2;
  const int y0 = bodyRc.top + (bodyRc.height - h) / 2;
  return Rect(x0, y0, x0 + w, y0 + h);
}

/// Returns true when the builtin device provides a dedicated Swing-ported custom panel.
bool hasCustomBuiltinDeviceUi(const(PluginSlotState)* plug) pure nothrow @safe {
  return resolveBuiltinDeviceKind(plug) != BuiltinDeviceKind.GENERIC;
}

/// Interactive state for custom builtin device UIs (band selection, sub-tabs, drawbar/graph drag).
struct BuiltinDeviceUiState {
  int eqSelectedBand = 0;
  int eqDragBand = -1;
  int eqDragStartY = 0;
  float eqDragStartQ = 0.35f;

  int maximBandTab = 3; // 0=Low,1=Mid,2=High,3=Master,4=Xover
  int hottTab = 0; // 0=Time,1=Below,2=Above
  int hottDragBand = -1;
  int hottDragMode = 0; // 0=downThresh, 1=upThresh, 2=attack/upRatio, 3=release/downRatio
  int drumSelPad = 0;
  int envDragPoint = -1;
  int acidActiveNote = -1;
  int filmTab = 0; // 0=MAIN, 1..6=OP1..6, 7..9=F1..3
  int filmDragCell = -1; // 0..71 inside FilM 6x12 matrix
  int sylphPresetIdx = 0; // 0..5 curated Sylph Trance/Kick presets
  int sylphFxTab = 0; // 0=DIST, 1=CHORUS, 2=DELAY, 3=REVERB

  int activeDragParamId = -1;
  int dragStartY = 0;
  float dragStartVal = 0.0f;
  float dragDefaultVal = 0.5f;
  Rect activeDragSliderRc;
  bool dragIsVerticalSlider = false;
  bool dragInvertVertical = false;
}

/// Interactive knob descriptor built during device layout for uniform rendering and hit-testing.
struct DeviceKnobSpec {
  uint paramId;
  string label;
  float defaultVal;
  uint accent;
  Rect cellRc;
  int cx;
  int cy;
  int r;
  string formattedVal;
  int knobStyle = 0; // 0=FlPalette dark knob, 1=TB-303 brushed silver+red ring, 2=light text on dark
  bool lightBg = false;
}

/// Interactive pill/toggle button descriptor built during layout.
struct DevicePillSpec {
  Rect rc;
  string label;
  bool active;
  uint activeColor;
  int actionKind; // 0=set param to targetVal, 1=toggle param 0/1, 2=ui tab, 3=virtual note, 4=load wav, 5=step param delta
  uint paramId;
  float targetVal;
  int uiTabVal;
  bool visibleButton = true;
}

private float clamp01(float v) pure nothrow @safe {
  if (v < 0.0f)
    return 0.0f;
  if (v > 1.0f)
    return 1.0f;
  return v;
}

float readPlugParam(const(PluginSlotState)* plug, uint paramId, float fallback = 0.5f) pure nothrow @safe {
  if (!plug)
    return fallback;
  foreach (const ref pm; plug.params) {
    if (pm.id == paramId)
      return clamp01(pm.currentValue);
  }
  if (paramId < plug.params.length && plug.params[paramId].id == paramId) {
    return clamp01(plug.params[paramId].currentValue);
  }
  return fallback;
}

void writePlugParam(PluginSlotState* plug, uint paramId, float value,
    float defaultVal = 0.5f, string label = null) @safe {
  if (!plug)
    return;
  const float v = clamp01(value);
  foreach (ref pm; plug.params) {
    if (pm.id == paramId) {
      pm.currentValue = v;
      return;
    }
  }
  plug.params ~= ParamItemState(paramId, label.length > 0 ? label : format("P%d",
      paramId), clamp01(defaultVal), v);
}

private float normalizeDb(float db, float minDb, float maxDb) pure nothrow @safe {
  if (maxDb <= minDb)
    return 0.0f;
  return clamp01((db - minDb) / (maxDb - minDb));
}

// =========================================================================
// EQ Eight Math (mirrors EqDevicePanel.java & BuiltinEq)
// =========================================================================

private float eqNormToFreq(float norm) nothrow @safe {
  return 20.0f * powf(1000.0f, clamp01(norm));
}

private float eqFreqToNorm(float freq) nothrow @safe {
  if (freq <= 20.0f)
    return 0.0f;
  if (freq >= 20000.0f)
    return 1.0f;
  return log10f(freq / 20.0f) / 3.0f;
}

private float eqNormToQ(float norm) nothrow @safe {
  return 0.1f * powf(100.0f, clamp01(norm));
}

private int eqNormToTypeIdx(float norm) pure nothrow @safe {
  if (norm < 0.1f)
    return 0; // Off
  if (norm < 0.3f)
    return 1; // LP
  if (norm < 0.5f)
    return 2; // HP
  if (norm < 0.7f)
    return 3; // LS
  if (norm < 0.9f)
    return 4; // HS
  return 5; // Bell
}

private float eqTypeIdxToNorm(int idx) pure nothrow @safe {
  static immutable float[6] TYPE_NORMS = [0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f];
  if (idx < 0 || idx >= 6)
    return 0.0f;
  return TYPE_NORMS[idx];
}

private float eqBandGainDbAtFreq(const(PluginSlotState)* plug, int b, float freq) nothrow @safe {
  const int typeIdx = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
  if (typeIdx == 0)
    return 0.0f;
  static immutable float[8] DEF_FREQS = [
    30, 80, 250, 700, 2000, 5000, 10000, 16000
  ];
  const float bandFreq = eqNormToFreq(readPlugParam(plug, cast(uint)(8 + b),
      eqFreqToNorm(DEF_FREQS[b])));
  const float gainDb = (readPlugParam(plug, cast(uint)(16 + b), 0.5f) - 0.5f) * 48.0f;
  const float q = eqNormToQ(readPlugParam(plug, cast(uint)(24 + b), 0.425f));
  const float ratio = freq / (bandFreq > 1.0f ? bandFreq : 1.0f);
  const float logR = log10f(ratio > 1e-4f ? ratio : 1e-4f);

  switch (typeIdx) {
  case 1: // LP
    if (ratio <= 1.0f)
      return 0.0f;
    return -12.0f * (logR / 0.30103f);
  case 2: // HP
    if (ratio >= 1.0f)
      return 0.0f;
    return 12.0f * (logR / 0.30103f);
  case 3: { // Low Shelf
      const float t = 1.0f / (1.0f + powf(ratio, 2.0f * (q > 0.5f ? q : 0.5f)));
      return gainDb * t;
    }
  case 4: { // High Shelf
      const float t = powf(ratio, 2.0f * (q > 0.5f ? q : 0.5f)) / (
          1.0f + powf(ratio, 2.0f * (q > 0.5f ? q : 0.5f)));
      return gainDb * t;
    }
  default: { // Bell
      const float bw = 0.5f / (q > 0.2f ? q : 0.2f);
      return gainDb * expf(-(logR * logR) / (2.0f * bw * bw));
    }
  }
}

private float eqCompositeDbAtFreq(const(PluginSlotState)* plug, float freq) nothrow @safe {
  float total = 0.0f;
  foreach (b; 0 .. 8) {
    total += eqBandGainDbAtFreq(plug, b, freq);
  }
  return total;
}

private string formatDb(float db) @safe {
  return format("%+.1fdB", db);
}

private string formatFreq(float hz) @safe {
  return hz >= 1000.0f ? format("%.1fk", hz / 1000.0f) : format("%.0fHz", hz);
}

private string formatPercent(float n) @safe {
  return format("%d%%", cast(int)(clamp01(n) * 100.0f + 0.5f));
}

private string formatMsFromExp(float n, float minMs, float maxRatio) @safe {
  const float ms = minMs * powf(maxRatio, clamp01(n));
  return ms >= 1000.0f ? format("%.2fs", ms / 1000.0f) : format("%.0fms", ms);
}

private string formatRatio(float n) @safe {
  const float r = 1.0f + clamp01(n) * 19.0f;
  return r >= 19.5f ? "inf:1" : format("%.1f:1", r);
}

/// Curated factory patch names for Sylph (`builtin://sylph`).
public immutable string[6] SYLPH_PRESET_NAMES = [
  "Trance Lead", "Hard Kick", "909 Punch", "Pluck", "Hive Pad", "Psy Bass",
];

/// Applies built-in factory preset `presetIdx` directly into `plug.params` & updates `ui.sylphPresetIdx`.
bool applyBuiltinDevicePreset(PluginSlotState* plug, ref BuiltinDeviceUiState ui, int presetIdx) @safe {
  if (!plug || resolveBuiltinDeviceKind(plug) != BuiltinDeviceKind.SYLPH)
    return false;
  const int idx = ((presetIdx % 6) + 6) % 6;
  ui.sylphPresetIdx = idx;
  foreach (uint pid; 0 .. 71) {
    writePlugParam(plug, pid, SYLPH_PRESETS[idx][pid], SYLPH_PRESETS[0][pid]);
  }
  return true;
}

/// Interactive vertical fader bar descriptor (bottom=0.0 -> top=1.0).
struct DeviceSliderSpec {
  Rect rc;
  uint paramId;
  string label;
  float defaultVal;
  uint accent;
}

private immutable float[71][6] SYLPH_PRESETS = [
  // 0: Trance Anthem Lead
  [
    0.00f, 0.50f, 0.50f, 1.00f, 0.38f, 0.88f, 0.00f, 0.86f, 0.00f, 0.70f,
    0.22f, 0.18f, 0.72f, 0.15f, 0.72f, 0.78f, 0.58f, 0.10f, 0.70f, 0.44f,
    0.58f, 0.00f, 0.75f, 0.50f, 0.67f, 0.46f, 0.92f, 0.00f, 0.58f, 0.35f,
    0.84f, 0.28f, 0.12f, 0.64f, 0.12f, 0.68f, 0.72f, 0.56f, 0.08f, 0.66f,
    0.40f, 0.56f, 0.00f, 0.50f, 0.24f, 0.08f, 0.50f, 0.25f, 0.25f, 0.32f,
    0.05f, 0.12f, 0.00f, 0.06f, 0.75f, 0.00f, 0.20f, 0.60f, 0.25f, 1.00f,
    0.35f, 0.55f, 0.36f, 1.00f, 0.58f, 0.44f, 0.32f, 1.00f, 0.72f, 0.28f, 0.30f,
  ],
  // 1: Hard EDM Kick
  [
    0.85f, 0.50f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.96f, 0.00f, 0.42f,
    0.18f, 0.54f, 0.82f, 0.00f, 0.64f, 0.00f, 0.44f, 0.00f, 0.55f, 0.10f,
    0.42f, 0.55f, 0.50f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.42f, 0.62f,
    0.56f, 0.34f, 0.62f, 0.78f, 0.00f, 0.38f, 0.00f, 0.30f, 0.00f, 0.36f,
    0.00f, 0.28f, 0.00f, 0.85f, 0.56f, 0.58f, 0.94f, 0.36f, 0.16f, 0.20f,
    0.00f, 0.00f, 0.50f, 0.00f, 0.84f, 1.00f, 0.52f, 0.42f, 0.58f, 0.00f,
    0.30f, 0.40f, 0.20f, 0.00f, 0.40f, 0.30f, 0.20f, 0.00f, 0.40f, 0.50f, 0.15f,
  ],
  // 2: Tight 909 Punch
  [
    0.85f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.94f, 0.00f, 0.38f,
    0.24f, 0.64f, 0.84f, 0.00f, 0.68f, 0.00f, 0.48f, 0.00f, 0.58f, 0.08f,
    0.45f, 0.28f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.32f, 0.62f,
    0.52f, 0.30f, 0.68f, 0.76f, 0.00f, 0.42f, 0.00f, 0.32f, 0.00f, 0.40f,
    0.00f, 0.30f, 0.00f, 0.50f, 0.38f, 0.48f, 0.88f, 0.44f, 0.28f, 0.18f,
    0.00f, 0.00f, 0.50f, 0.00f, 0.84f, 1.00f, 0.58f, 0.38f, 0.62f, 0.00f,
    0.30f, 0.40f, 0.20f, 0.00f, 0.40f, 0.30f, 0.20f, 0.00f, 0.40f, 0.50f, 0.15f,
  ],
  // 3: Euphoric Pluck
  [
    0.00f, 0.50f, 0.50f, 0.67f, 0.32f, 0.78f, 0.00f, 0.82f, 0.00f, 0.28f,
    0.26f, 0.16f, 0.90f, 0.00f, 0.68f, 0.08f, 0.56f, 0.00f, 0.62f, 0.10f,
    0.54f, 0.28f, 0.75f, 0.50f, 0.50f, 0.38f, 0.82f, 0.00f, 0.48f, 0.35f,
    0.42f, 0.22f, 0.14f, 0.86f, 0.00f, 0.58f, 0.04f, 0.48f, 0.00f, 0.54f,
    0.06f, 0.46f, 0.00f, 0.50f, 0.18f, 0.05f, 0.54f, 0.18f, 0.10f, 0.38f,
    0.00f, 0.08f, 0.00f, 0.00f, 0.78f, 0.00f, 0.20f, 0.55f, 0.20f, 1.00f,
    0.30f, 0.48f, 0.30f, 1.00f, 0.62f, 0.48f, 0.46f, 1.00f, 0.76f, 0.26f, 0.34f,
  ],
  // 4: Hive Supersaw Pad
  [
    0.00f, 0.50f, 0.50f, 1.00f, 0.54f, 1.00f, 0.00f, 0.76f, 0.00f, 0.54f,
    0.18f, 0.12f, 0.64f, 0.68f, 0.80f, 0.84f, 0.72f, 0.64f, 0.78f, 0.52f,
    0.70f, 0.00f, 0.50f, 0.65f, 1.00f, 0.48f, 1.00f, 0.00f, 0.62f, 0.62f,
    0.66f, 0.28f, 0.10f, 0.58f, 0.74f, 0.82f, 0.80f, 0.74f, 0.68f, 0.80f,
    0.48f, 0.72f, 0.35f, 0.50f, 0.30f, 0.12f, 0.50f, 0.20f, 0.25f, 0.24f,
    0.08f, 0.28f, 0.00f, 0.18f, 0.72f, 0.00f, 0.15f, 0.50f, 0.15f, 1.00f,
    0.28f, 0.68f, 0.52f, 1.00f, 0.55f, 0.40f, 0.26f, 1.00f, 0.86f, 0.22f, 0.52f,
  ],
  // 5: Rolling Psy Bass
  [
    0.00f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.88f, 0.00f, 0.24f,
    0.34f, 0.38f, 0.86f, 0.00f, 0.54f, 0.32f, 0.40f, 0.00f, 0.52f, 0.05f,
    0.40f, 0.28f, 0.25f, 0.50f, 0.00f, 0.00f, 0.00f, 1.00f, 0.28f, 0.62f,
    0.48f, 0.28f, 0.42f, 0.78f, 0.00f, 0.42f, 0.15f, 0.30f, 0.00f, 0.44f,
    0.00f, 0.32f, 0.00f, 0.25f, 0.45f, 0.04f, 0.50f, 0.22f, 0.15f, 0.30f,
    0.00f, 0.00f, 0.80f, 0.12f, 0.80f, 1.00f, 0.38f, 0.46f, 0.42f, 0.00f,
    0.30f, 0.40f, 0.20f, 0.00f, 0.40f, 0.30f, 0.20f, 0.00f, 0.40f, 0.50f, 0.15f,
  ],
];

// =========================================================================
// Layout Builder for Controls per Builtin Device Kind
// =========================================================================

struct DeviceLayout {
  BuiltinDeviceKind kind = BuiltinDeviceKind.GENERIC;
  Rect panelRc;
  Rect canvasRc;
  Rect footerRc;
  Rect auxRc;
  Rect filmMatrixRc;
  DeviceKnobSpec[] knobs;
  DevicePillSpec[] pills;
  DeviceSliderSpec[] sliders; // Vertical fader bars (0=bottom -> 1=top)
  Rect[] drawbarRcs; // Organ drawbars 0..8
  Rect[] padRcs; // Drum Machine pads 0..15
  Rect[] bandMeterRcs;
  Rect[3] hottBarRcs;
  Rect[3] hottInspectorRcs;
  Rect[72] filmMatrixCells;
  Rect[10] sylphDeckRcs; // [0..2]=OscA,FiltA,EnvA; [3..5]=OscB,FiltB,EnvB; [6..8]=FxSel,Sub/Mod,LFO; [9]=ChainLCD
  Rect[4] sylphFxBlockRcs; // Serial FX routing blocks inside central LCD
  Rect[4] sylphFxPowerRcs; // Individual ON/OFF LED switches inside each FX block
  Rect[4] sylphEnvCurveRcs; // [0]=AmpA, [1]=FiltA, [2]=AmpB, [3]=FiltB mini ADSR silhouettes
}

private void appendKnobRow(ref DeviceKnobSpec[] dst, Rect area,
    const(PluginSlotState)* plug, const(uint)[] paramIds, const(string)[] labels,
    const(float)[] defaults, const(string)[] formattedVals, const(
      uint)[] accents, const ref FlPalette pal, int knobStyle = 0, bool lightBg = false) @safe {
  const size_t n = paramIds.length;
  if (n == 0 || area.width < dp(24) || area.height < dp(24))
    return;
  const int cw = area.width / cast(int)n;
  const int maxR = (area.height - dp(24)) / 2;
  int r = (cw - dp(12)) / 2;
  if (maxR < r)
    r = maxR;
  if (r > dp(16))
    r = dp(16);
  if (r < dp(8))
    r = dp(8);
  const int cy = area.top + dp(11) + r;
  foreach (i; 0 .. n) {
    const int x0 = area.left + cast(int)i * cw;
    const int cx = x0 + cw / 2;
    const uint col = (i < accents.length) ? accents[i] : ((i % 2 == 0) ?
        pal.accentOrange : pal.accentCyan);
    const float dv = (i < defaults.length) ? defaults[i] : 0.5f;
    const string fv = (i < formattedVals.length && formattedVals[i].length > 0) ? formattedVals[i] : formatPercent(
        readPlugParam(plug, paramIds[i], dv));
    dst ~= DeviceKnobSpec(paramIds[i], labels[i], dv, col, Rect(x0, area.top,
        x0 + cw, area.bottom), cx, cy, r, fv, knobStyle, lightBg);
  }
}

/// Computes widget sub-rectangles, knob specs, and interactive pills inside `bodyRc`.
DeviceLayout buildBuiltinDeviceLayout(Rect bodyRc, const(PluginSlotState)* plug,
    const ref BuiltinDeviceUiState ui, const FlPalette pal) @safe {
  DeviceLayout dl;
  dl.kind = resolveBuiltinDeviceKind(plug);
  if (bodyRc.width < dp(160) || bodyRc.height < dp(120))
    return dl;

  Rect fitted = fitDeviceBodyRect(bodyRc, dl.kind);
  dl.panelRc = fitted;
  const int pad = dp(8);
  Rect inner = Rect(fitted.left + pad, fitted.top + dp(5), fitted.right - pad,
      fitted.bottom - dp(5));

  switch (dl.kind) {
  case BuiltinDeviceKind.EQ: {
      static immutable uint[8] BAND_COLS = [
        0x00E04040, 0x00E07020, 0x00D0C020, 0x0040B040, 0x0030A0A0, 0x004080E0,
        0x008060D0, 0x00C050A0,
      ];
      static immutable string[6] TYPE_NAMES = [
        "Off", "LP", "HP", "LS", "HS", "Bell"
      ];
      static immutable float[8] DEF_FREQS = [
        30, 80, 250, 700, 2000, 5000, 10000, 16000
      ];

      const int botH = dp(40);
      const int inspW = (inner.width > dp(340)) ? dp(96) : dp(82);
      Rect topRc = Rect(inner.left, inner.top, inner.right, inner.bottom - botH - dp(
          4));
      dl.canvasRc = Rect(topRc.left, topRc.top, topRc.right - inspW - dp(5), topRc
          .bottom);
      dl.auxRc = Rect(dl.canvasRc.right + dp(5), topRc.top, topRc.right, topRc
          .bottom);
      dl.footerRc = Rect(inner.left, topRc.bottom + dp(4), inner.right, inner
          .bottom);

      // Bottom 8 rainbow band strip columns (rendered cleanly like EqDevicePanel.java)
      const int bw = dl.footerRc.width / 8;
      foreach (b; 0 .. 8) {
        const int bx = dl.footerRc.left + b * bw;
        const int ti = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
        const bool sel = (ui.eqSelectedBand == b);
        dl.pills ~= DevicePillSpec(Rect(bx + dp(1), dl.footerRc.top,
            bx + bw - dp(1), dl.footerRc.bottom), TYPE_NAMES[ti], sel || ti > 0,
            sel ? BAND_COLS[b] : adjustColorBrightness(BAND_COLS[b], -24), 2,
            cast(uint)b, 0.0f, b, false);
      }

      // Right selected-band inspector: Type cycler pill + 3 knobs (Freq, Gain, Q)
      const int sb = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui
        .eqSelectedBand : 0;
      const int curType = eqNormToTypeIdx(readPlugParam(plug, cast(uint)sb, 0.0f));
      dl.pills ~= DevicePillSpec(Rect(dl.auxRc.left + dp(3),
          dl.auxRc.top + dp(18), dl.auxRc.right - dp(3), dl.auxRc.top + dp(36)), "Type: " ~ TYPE_NAMES[curType],
          curType > 0, BAND_COLS[sb], 0, cast(uint)sb, eqTypeIdxToNorm((curType + 1) % 6),
          sb, true);

      const float fNorm = readPlugParam(plug, cast(uint)(8 + sb), eqFreqToNorm(DEF_FREQS[sb]));
      const float gNorm = readPlugParam(plug, cast(uint)(16 + sb), 0.5f);
      const float qNorm = readPlugParam(plug, cast(uint)(24 + sb), 0.425f);
      Rect kZone = Rect(dl.auxRc.left + dp(2), dl.auxRc.top + dp(40),
          dl.auxRc.right - dp(2), dl.auxRc.bottom);
      const uint[3] pids = [
        cast(uint)(8 + sb), cast(uint)(16 + sb), cast(uint)(24 + sb)
      ];
      const string[3] klbls = ["Freq", "Gain", "Q"];
      const float[3] kdefs = [eqFreqToNorm(DEF_FREQS[sb]), 0.5f, 0.425f];
      const string[3] kvals = [
        formatFreq(eqNormToFreq(fNorm)), formatDb((gNorm - 0.5f) * 48.0f),
        format("%.2f", eqNormToQ(qNorm))
      ];
      if (kZone.height >= dp(148)) {
        const int kh = kZone.height / 3;
        foreach (k; 0 .. 3) {
          appendKnobRow(dl.knobs, Rect(kZone.left, kZone.top + k * kh,
              kZone.right, kZone.top + (k + 1) * kh), plug, [pids[k]],
              [klbls[k]], [kdefs[k]], [kvals[k]], [BAND_COLS[sb]], pal);
        }
      } else {
        const int row1H = kZone.height / 2;
        appendKnobRow(dl.knobs, Rect(kZone.left, kZone.top, kZone.right,
            kZone.top + row1H), plug, [pids[0], pids[1]], [klbls[0], klbls[1]],
            [kdefs[0], kdefs[1]], [kvals[0], kvals[1]], [
              BAND_COLS[sb], BAND_COLS[sb]
        ], pal);
        appendKnobRow(dl.knobs, Rect(kZone.left, kZone.top + row1H, kZone.right,
            kZone.bottom), plug, [pids[2]], [klbls[2]], [kdefs[2]], [kvals[2]],
            [BAND_COLS[sb]], pal);
      }
      break;
    }
  case BuiltinDeviceKind.COMPRESSOR: {
      const int knobH = dp(68);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      const float th = readPlugParam(plug, 0, 1.0f);
      const float rt = readPlugParam(plug, 1, 0.0f);
      const float at = readPlugParam(plug, 2, 0.3f);
      const float rl = readPlugParam(plug, 3, 0.3f);
      const float kn = readPlugParam(plug, 4, 0.0f);
      const float mk = readPlugParam(plug, 5, 0.0f);
      const float ut = readPlugParam(plug, 7, 0.0f);
      const float ur = readPlugParam(plug, 8, 0.0f);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u],
          [
            "Thresh", "Ratio", "Attack", "Release", "Knee", "Makeup", "UpThr",
            "UpRat"
      ], [1.0f, 0.0f, 0.3f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f],
          [
            formatDb((th - 1.0f) * 60.0f), formatRatio(rt),
            formatMsFromExp(at, 0.1f, 1000.0f), formatMsFromExp(rl, 10.0f,
              100.0f), format("%.0fdB", kn * 24.0f), format("+%.1fdB",
              mk * 24.0f), formatDb((ut - 1.0f) * 60.0f), formatRatio(ur)
      ], [
            pal.accentAmber, pal.accentOrange, pal.accentCyan, pal.accentCyan,
            pal.fgMuted, pal.accentGreen, pal.accentPurple, pal.accentPurple
      ], pal);
      break;
    }
  case BuiltinDeviceKind.LIMITER: {
      const int knobH = dp(68);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      const float ce = readPlugParam(plug, 0, 1.0f);
      const float rl = readPlugParam(plug, 1, 0.3f);
      const float la = readPlugParam(plug, 2, 0.5f);
      const float gn = readPlugParam(plug, 3, 0.0f);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u], [
        "Ceiling", "Release", "Lookahead", "Gain"
      ], [1.0f, 0.3f, 0.5f, 0.0f], [
        formatDb(ce * 12.0f - 12.0f), formatMsFromExp(rl, 10.0f, 100.0f),
        format("%.1fms", la * 10.0f), format("+%.1fdB", gn * 24.0f)
      ], [pal.accentRed, pal.accentCyan, pal.accentAmber, pal.accentOrange], pal);
      break;
    }
  case BuiltinDeviceKind.HOTT: {
      // Authentic 3-column Xfer OTT / Swing HottDevicePanel layout:
      // Top banner + [Left: Split Freqs + SoftKnee/RMS] [Center: 3-Band Up/Down Comp Meters + T/B/A] [Right: 2x3 Out/Depth]
      const int hdrH = dp(18);
      dl.footerRc = Rect(inner.left, inner.top, inner.right, inner.top + hdrH);
      Rect body = Rect(inner.left, dl.footerRc.bottom + dp(4), inner.right, inner
          .bottom);

      const int leftW = (body.width >= dp(440)) ? dp(78) : dp(64);
      const int rightW = (body.width >= dp(440)) ? dp(164) : dp(136);
      Rect leftCol = Rect(body.left, body.top, body.left + leftW, body.bottom);
      Rect rightCol = Rect(body.right - rightW, body.top, body.right, body.bottom);
      dl.auxRc = leftCol;
      dl.canvasRc = Rect(leftCol.right + dp(4), body.top, rightCol.left - dp(4), body
          .bottom);

      // Left column: HiFreq, LoFreq knobs + Soft Knee, RMS toggles
      const float hiF = readPlugParam(plug, 1, 0.436f);
      const float loF = readPlugParam(plug, 0, 0.461f);
      const float hiHz = 500.0f * powf(40.0f, hiF);
      const float loHz = 20.0f * powf(25.0f, loF);
      const int btnAreaH = dp(38);
      const int splH = (leftCol.height - dp(14) - btnAreaH) / 2;
      appendKnobRow(dl.knobs, Rect(leftCol.left, leftCol.top + dp(12),
          leftCol.right, leftCol.top + dp(12) + splH), plug, [1u], ["HiFreq"],
          [0.436f], [format("%.2fk", hiHz / 1000.0f)], [0x0000D4FF], pal);
      appendKnobRow(dl.knobs, Rect(leftCol.left, leftCol.top + dp(12) + splH,
          leftCol.right, leftCol.bottom - btnAreaH - dp(2)), plug, [0u],
          ["LoFreq"], [0.461f], [format("%.0fHz", loHz)], [0x0000D4FF], pal);

      const bool softKnee = readPlugParam(plug, 15, 1.0f) >= 0.5f;
      const bool rmsMode = readPlugParam(plug, 16, 1.0f) >= 0.5f;
      dl.pills ~= DevicePillSpec(Rect(leftCol.left + dp(2),
          leftCol.bottom - dp(36), leftCol.right - dp(2), leftCol.bottom - dp(19)),
          "Soft Knee", softKnee, 0x0000D4FF, 1, 15, 1.0f, 0);
      dl.pills ~= DevicePillSpec(Rect(leftCol.left + dp(2),
          leftCol.bottom - dp(17), leftCol.right - dp(2), leftCol.bottom),
          "RMS", rmsMode, 0x0010B981, 1, 16, 1.0f, 0);

      // Center meter panel rows & [T] [B] [A] tabs
      Rect c = dl.canvasRc;
      const int cPad = dp(6);
      const int tabH = dp(18);
      const int topRulerH = dp(12);
      const int inspW = (c.width >= dp(230)) ? dp(68) : dp(54);
      const int meterW = c.width - cPad * 2 - inspW - dp(4);
      const int bandsTop = c.top + topRulerH + dp(2);
      const int bandsBot = c.bottom - tabH - dp(2);
      const int bandGap = dp(4);
      const int bandH = ((bandsBot - bandsTop) - bandGap * 2) / 3;
      foreach (bi; 0 .. 3) {
        const int ry0 = bandsTop + bi * (bandH + bandGap);
        const int ry1 = ry0 + bandH;
        dl.hottBarRcs[bi] = Rect(c.left + cPad, ry0, c.left + cPad + meterW, ry1);
        dl.hottInspectorRcs[bi] = Rect(c.left + cPad + meterW + dp(4), ry0, c
            .right - cPad, ry1);
      }

      static immutable string[3] TABS = ["T", "B", "A"];
      const int tbW = dp(20);
      const int tbGap = dp(2);
      const int tabX0 = c.right - cPad - 3 * tbW - 2 * tbGap;
      const int tabY0 = c.bottom - tabH;
      foreach (ti; 0 .. 3) {
        const int x0 = tabX0 + ti * (tbW + tbGap);
        dl.pills ~= DevicePillSpec(Rect(x0, tabY0, x0 + tbW, c.bottom - dp(2)),
            TABS[ti], ui.hottTab == ti, 0x0000D4FF, 2, 0, 0.0f, ti);
      }

      // Right column: 2x3 OTT cyan knobs (HiOut/MidOut/LoOut + Output/Time/Depth)
      const int rHalf = rightCol.height / 2;
      const float lo = readPlugParam(plug, 5, 0.715f);
      const float md = readPlugParam(plug, 6, 0.619f);
      const float hi = readPlugParam(plug, 7, 0.715f);
      const float outV = readPlugParam(plug, 4, 0.5f);
      const float tm = readPlugParam(plug, 3, 1.0f);
      const float amt = readPlugParam(plug, 2, 1.0f);
      appendKnobRow(dl.knobs, Rect(rightCol.left, rightCol.top, rightCol.right,
          rightCol.top + rHalf), plug, [7u, 6u, 5u], [
        "Hi Out", "Mid Out", "Lo Out"
      ], [0.715f, 0.619f, 0.715f], [
        formatDb(hi * 48.0f - 24.0f), formatDb(md * 48.0f - 24.0f),
        formatDb(lo * 48.0f - 24.0f)
      ], [0x0000D4FF, 0x0000D4FF, 0x0000D4FF], pal);
      appendKnobRow(dl.knobs, Rect(rightCol.left, rightCol.top + rHalf,
          rightCol.right, rightCol.bottom), plug, [4u, 3u, 2u], [
        "Output", "Time", "Amount"
      ], [0.5f, 1.0f, 1.0f], [
        formatDb(outV * 48.0f - 24.0f), formatPercent(tm), formatPercent(amt)
      ], [0x0000D4FF, 0x0000D4FF, 0x0000D4FF], pal);
      break;
    }
  case BuiltinDeviceKind.MAXIM: {
      const int tabH = dp(20);
      const int knobH = dp(66);
      Rect tabRc = Rect(inner.left, inner.top, inner.right, inner.top + tabH);
      static immutable string[5] BTABS = [
        "LOW", "MID", "HIGH", "MASTER", "XOVER"
      ];
      static immutable uint[5] BCOLS = [
        0x00E25822, 0x0010B981, 0x003B82F6, 0x00F59E0B, 0x008B5CF6
      ];
      const int bw = tabRc.width / 5;
      foreach (ti; 0 .. 5) {
        dl.pills ~= DevicePillSpec(Rect(tabRc.left + ti * bw, tabRc.top,
            tabRc.left + (ti + 1) * bw - dp(2), tabRc.bottom), BTABS[ti],
            ui.maximBandTab == ti, BCOLS[ti], 2, 0, 0.0f, ti);
      }
      dl.canvasRc = Rect(inner.left, tabRc.bottom + dp(4), inner.right,
          inner.bottom - knobH - dp(4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      if (ui.maximBandTab == 4) {
        appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u], [
          "Low Xover", "High Xover", "Lookahead"
        ], [0.461f, 0.436f, 0.2f], null, [BCOLS[0], BCOLS[2], BCOLS[4]], pal);
      } else {
        const uint base = cast(uint)(4 + ui.maximBandTab * 10);
        const uint col = BCOLS[ui.maximBandTab];
        appendKnobRow(dl.knobs, kArea, plug, [
          base + 0, base + 1, base + 4, base + 5, base + 7, base + 8, base + 9
        ], ["Pre", "Post", "Thresh", "Ratio", "Attack", "Release", "Ceiling"],
          [
            0.5f, 0.5f, 1.0f, 0.0f, 0.3f, 0.3f, ui.maximBandTab == 3 ? 0.975f : 1.0f
        ], null, [col, col, col, col, col, col, pal.accentRed], pal);
      }
      break;
    }
  case BuiltinDeviceKind.DELAY: {
      const int knobH = dp(66);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      const bool synced = readPlugParam(plug, 8, 1.0f) >= 0.5f;
      dl.pills ~= DevicePillSpec(Rect(dl.canvasRc.right - dp(56),
          dl.canvasRc.top + dp(4), dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(
          20)), synced ? "SYNC" : "FREE", synced, pal.accentCyan, 1, 8, 1.0f, 0);
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u, 4u, 5u, 6u],
          ["Time L", "Time R", "Feedbk", "Mix", "HPF", "LPF", "X-Talk"], [
            0.6f, 0.6f, 0.4f, 0.3f, 0.15f, 0.75f, 1.0f
      ], null, [
            pal.accentCyan, pal.accentCyan, pal.accentOrange, pal.accentGreen,
            pal.fgMuted, pal.fgMuted, pal.accentAmber
      ], pal);
      break;
    }
  case BuiltinDeviceKind.REVERB: {
      const int knobH = dp(66);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u, 4u, 5u, 6u],
          ["Size", "Damp", "Mix", "PreDly", "HPF", "LPF", "Width"], [
            0.5f, 0.5f, 0.3f, 0.1f, 0.15f, 0.85f, 1.0f
      ], null, [
            pal.accentPurple, pal.accentPurple, pal.accentGreen,
            pal.accentCyan, pal.fgMuted, pal.fgMuted, pal.accentOrange
      ], pal);
      break;
    }
  case BuiltinDeviceKind.PHASER: {
      const int knobH = dp(66);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      const bool synced = readPlugParam(plug, 10, 0.0f) >= 0.5f;
      dl.pills ~= DevicePillSpec(Rect(dl.canvasRc.right - dp(56),
          dl.canvasRc.top + dp(4), dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(
          20)), synced ? "SYNC" : "FREE", synced, pal.accentCyan, 1, 10, 1.0f, 0);
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u, 4u, 5u, 8u, 9u],
          [
            "Rate", "Depth", "Feedbk", "Stages", "Mix", "Stereo", "Center",
            "Spread"
      ], [0.3f, 0.7f, 0.4f, 0.33f, 0.5f, 0.5f, 0.5f, 0.3f], null,
          [
            pal.accentCyan, pal.accentOrange, pal.accentAmber,
            pal.accentPurple,
            pal.accentGreen, pal.accentCyan, pal.fgMuted, pal.fgMuted
      ], pal);
      break;
    }
  case BuiltinDeviceKind.CHORUS: {
      const int knobH = dp(68);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u, 4u], [
        "Rate", "Depth", "Delay", "Feedbk", "Wet/Dry"
      ], [0.25f, 0.5f, 0.4f, 0.25f, 0.5f], null, [
        pal.accentCyan, pal.accentOrange, pal.accentAmber, pal.accentPurple,
        pal.accentGreen
      ], pal);
      break;
    }
  case BuiltinDeviceKind.BITCRUSHER: {
      const int knobH = dp(68);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      const float bits = 1.0f + readPlugParam(plug, 0, 0.5f) * 23.0f;
      const float sr = 20.0f * powf(2205.0f, readPlugParam(plug, 1, 0.8f));
      const float dr = readPlugParam(plug, 2, 0.2f);
      const float mx = readPlugParam(plug, 3, 1.0f);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u], [
        "Bits", "SampleRt", "Drive", "Mix"
      ], [0.5f, 0.8f, 0.2f, 1.0f], [
        format("%.1fb", bits), formatFreq(sr), format("+%.1fdB", dr * 24.0f),
        formatPercent(mx)
      ], [pal.accentOrange, pal.accentCyan, pal.accentRed, pal.accentGreen], pal);
      break;
    }
  case BuiltinDeviceKind.ENVELOPE_SHAPER: {
      const int knobH = dp(66);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      const float mVal = readPlugParam(plug, 3, 0.0f);
      const int modeIdx = (mVal < 0.2f) ? 0 : (mVal < 0.45f ? 1 : (mVal < 0.75f ? 2 : 3));
      static immutable string[4] MODES = ["GAIN", "HPF", "BPF", "LPF"];
      static immutable float[4] M_NORMS = [0.0f, 0.33f, 0.5f, 1.0f];
      dl.pills ~= DevicePillSpec(Rect(dl.canvasRc.right - dp(62),
          dl.canvasRc.top + dp(4), dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(
          20)), MODES[modeIdx], true, pal.accentOrange, 0, 3, M_NORMS[(modeIdx + 1) % 4],
          0);
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u], [
        "Mix", "Rate", "Smooth"
      ], [1.0f, 0.5f, 0.5f], null, [
        pal.accentGreen, pal.accentCyan, pal.accentAmber
      ], pal);
      break;
    }
  case BuiltinDeviceKind.CONVOLVER: {
      const int knobH = dp(68);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u], [
        "Dry", "Wet", "PreDelay"
      ], [0.0f, 1.0f, 0.0f], null, [
        pal.fgMuted, pal.accentCyan, pal.accentOrange
      ], pal);
      break;
    }
  case BuiltinDeviceKind.VOCODEY: {
      const int knobH = dp(66);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u],
          ["Atk", "Dec", "BW", "Bleed", "Detune", "Dry", "Wet", "Vol"], [
            0.15f, 0.25f, 0.35f, 0.3f, 0.15f, 0.0f, 1.0f, 0.7f
      ], null, null, pal);
      break;
    }
  case BuiltinDeviceKind.STEREO_WIDTH: {
      const int knobH = dp(68);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      const bool rightCh = readPlugParam(plug, 1, 0.0f) >= 0.5f;
      dl.pills ~= DevicePillSpec(Rect(dl.canvasRc.right - dp(68),
          dl.canvasRc.top + dp(4), dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(
          20)), rightCh ? "Ch: R" : "Ch: L", rightCh, pal.accentCyan, 1, 1, 1.0f, 0);
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 2u, 3u], [
        "Delay", "MonoHz", "Width"
      ], [0.2f, 0.25f, 0.5f], null, [
        pal.accentCyan, pal.accentAmber, pal.accentOrange
      ], pal);
      break;
    }
  case BuiltinDeviceKind.ACID_BASS: {
      // Authentic Roland TB-303 brushed silver face:
      // Top charcoal badge | 8 silver vertical bays (Waveform + 7 dials) | Lower keyboard/step bay
      const int hdrH = dp(16);
      dl.auxRc = Rect(inner.left, inner.top, inner.right, inner.top + hdrH);
      const int remH = inner.bottom - dl.auxRc.bottom;
      const int upperH = (remH * 55) / 100;
      Rect mainRc = Rect(inner.left, dl.auxRc.bottom, inner.right, dl.auxRc
          .bottom + upperH);
      Rect kbdRc = Rect(inner.left, mainRc.bottom, inner.right, inner.bottom);
      dl.canvasRc = mainRc;
      dl.footerRc = kbdRc;

      // Col 0: WAVEFORM SAW/SQR switch; Cols 1..7: 7 TB-303 silver dials
      const int colW = mainRc.width / 8;
      const bool isSq = readPlugParam(plug, 0, 0.0f) >= 0.5f;
      dl.pills ~= DevicePillSpec(Rect(mainRc.left + dp(4), mainRc.top + dp(18),
          mainRc.left + colW - dp(4), mainRc.bottom - dp(16)), isSq ?
          "SQR" : "SAW", isSq, 0x00FF3333, 1, 0, 1.0f, 0, false);

      Rect kArea = Rect(mainRc.left + colW, mainRc.top + dp(4),
          mainRc.left + colW * 8, mainRc.bottom - dp(2));
      const float cut = readPlugParam(plug, 1, 0.3f);
      const float dec = readPlugParam(plug, 4, 0.2f);
      appendKnobRow(dl.knobs, kArea, plug, [1u, 2u, 3u, 4u, 5u, 6u, 7u],
          [
            "CUTOFF", "RESONANCE", "ENV MOD", "DECAY", "ACCENT", "OVERDRIVE",
            "VOLUME"
      ], [0.3f, 0.6f, 0.5f, 0.2f, 0.0f, 0.1f, 0.7f],
          [
            formatFreq(100.0f * powf(30.0f, cut)),
            formatPercent(readPlugParam(plug, 2, 0.6f)),
            formatPercent(readPlugParam(plug, 3, 0.5f)),
            formatMsFromExp(dec, 50.0f, 60.0f),
            formatPercent(readPlugParam(plug, 5, 0.0f)),
            formatPercent(readPlugParam(plug, 6, 0.1f)),
            formatPercent(readPlugParam(plug, 7, 0.7f))
      ], [
            0x00FF3333, 0x00FF3333, 0x00FF3333, 0x00FF3333, 0x00FF3333, 0x00FF3333,
            0x00FF3333
      ], pal, 1, true);

      // Lower keyboard bay: Left Octave ◀/▶ | Center 12 staggered silver keys | Right SLIDE/ACCENT
      const int octVal = 1 + cast(int)(readPlugParam(plug, 8, 0.5f) * 4.0f + 0.5f);
      const uint baseMidi = cast(uint)(24 + octVal * 12);
      const int octW = dp(82);
      const int modW = dp(78);
      const int keySpanW = kbdRc.width - octW - modW;
      const int keyStart = kbdRc.left + octW + dp(4);
      const int wStep = (keySpanW - dp(8)) / 7;
      const int kw = (wStep > dp(18)) ? dp(14) : (wStep - dp(3));
      const int kh = (kbdRc.height >= dp(88)) ? dp(26) : dp(20);

      // Octave Down / Up circular buttons
      const int octBtnY = kbdRc.top + dp(34);
      dl.pills ~= DevicePillSpec(Rect(kbdRc.left + dp(10), octBtnY - dp(9),
          kbdRc.left + dp(28), octBtnY + dp(9)), "<", false, 0x00CCCCCC, 5, 8, -0.25f,
          0, false);
      dl.pills ~= DevicePillSpec(Rect(kbdRc.left + dp(52), octBtnY - dp(9),
          kbdRc.left + dp(70), octBtnY + dp(9)), ">", false, 0x00CCCCCC, 5, 8, 0.25f,
          0, false);

      // 5 Black keys (upper row: C#, D#, F#, G#, A#)
      static immutable int[5] BLK_OFF = [1, 3, 6, 8, 10];
      static immutable float[5] BLK_MULT = [0.5f, 1.5f, 3.5f, 4.5f, 5.5f];
      static immutable string[5] BLK_LBL = ["C#", "D#", "F#", "G#", "A#"];
      const int blkCenterY = kbdRc.top + (kbdRc.height * 32) / 100;
      foreach (bi; 0 .. 5) {
        const int kx = keyStart + wStep / 2 + cast(int)(BLK_MULT[bi] * wStep);
        dl.pills ~= DevicePillSpec(Rect(kx - kw / 2, blkCenterY - kh / 2, kx + kw / 2,
            blkCenterY + kh / 2), BLK_LBL[bi], true, 0x00111111, 3,
            baseMidi + BLK_OFF[bi], 1.0f, BLK_OFF[bi], false);
      }

      // 7 White keys (lower row: C, D, E, F, G, A, B)
      static immutable int[7] WHT_OFF = [0, 2, 4, 5, 7, 9, 11];
      static immutable string[7] WHT_LBL = ["C", "D", "E", "F", "G", "A", "B"];
      const int whtCenterY = kbdRc.top + (kbdRc.height * 68) / 100;
      foreach (wi; 0 .. 7) {
        const int kx = keyStart + wStep / 2 + wi * wStep;
        dl.pills ~= DevicePillSpec(Rect(kx - kw / 2, whtCenterY - kh / 2, kx + kw / 2,
            whtCenterY + kh / 2), WHT_LBL[wi], false, 0x00FAFAFA, 3,
            baseMidi + WHT_OFF[wi], 1.0f, WHT_OFF[wi], false);
      }

      // Right SLIDE & ACCENT brushed buttons with glowing Red LEDs
      const bool slideOn = readPlugParam(plug, 9, 0.0f) >= 0.5f;
      const bool accOn = readPlugParam(plug, 10, 0.0f) >= 0.5f;
      const int rModY = kbdRc.middley;
      const int slideCx = kbdRc.right - dp(54);
      const int accCx = kbdRc.right - dp(20);
      dl.pills ~= DevicePillSpec(Rect(slideCx - dp(10), rModY - dp(10),
          slideCx + dp(10), rModY + dp(10)), "SLIDE", slideOn, 0x00FF3333, 1, 9, 1.0f,
          0, false);
      dl.pills ~= DevicePillSpec(Rect(accCx - dp(10), rModY - dp(10), accCx + dp(
          10), rModY + dp(10)), "ACCENT", accOn, 0x00FF3333, 1, 10, 1.0f, 0, false);
      break;
    }
  case BuiltinDeviceKind.THREE_OSC: {
      // 3 stacked Oscillator rows + compact bottom Envelope/Filter row
      const int rowH = (inner.height - dp(72)) / 3;
      static immutable string[4] WAVES = ["Sin", "Saw", "Sq", "Tri"];
      static immutable uint[3] OCOLS = [0x004080E0, 0x0040B040, 0x00E07020];
      foreach (osc; 0 .. 3) {
        const int ry0 = inner.top + osc * rowH;
        Rect rRc = Rect(inner.left, ry0, inner.right, ry0 + rowH - dp(3));
        const uint base = cast(uint)(osc * 5);
        const float wVal = readPlugParam(plug, base, 0.0f);
        const int wi = (wVal < 0.2f) ? 0 : (wVal < 0.5f ? 1 : (wVal < 0.85f ? 2 : 3));
        static immutable float[4] WNORMS = [0.0f, 0.33f, 0.67f, 1.0f];
        dl.pills ~= DevicePillSpec(Rect(rRc.left + dp(4), rRc.top + dp(10),
            rRc.left + dp(68), rRc.bottom - dp(10)), format("O%d:%s", osc + 1,
            WAVES[wi]), true, OCOLS[osc], 0, base, WNORMS[(wi + 1) % 4], osc);
        appendKnobRow(dl.knobs, Rect(rRc.left + dp(72), rRc.top, rRc.right,
            rRc.bottom), plug, [base + 1, base + 2, base + 3, base + 4], [
          "Coarse", "Fine", "Vol", "Pan"
        ], [0.5f, 0.5f, osc == 0 ? 1.0f : 0.0f, 0.5f], null, [
          OCOLS[osc], OCOLS[osc], OCOLS[osc], pal.accentCyan
        ], pal);
      }
      Rect botRc = Rect(inner.left, inner.top + 3 * rowH + dp(2), inner.right, inner
          .bottom);
      dl.canvasRc = Rect(botRc.left, botRc.top, botRc.left + dp(96), botRc.bottom);
      appendKnobRow(dl.knobs, Rect(botRc.left + dp(100), botRc.top, botRc.right,
          botRc.bottom), plug, [15u, 17u, 18u, 20u, 21u, 27u], [
        "Atk", "Sus", "Rel", "Cutoff", "Res", "Vol"
      ], [0.0f, 0.7f, 0.3f, 1.0f, 0.0f, 0.8f], null, [
        pal.accentGreen, pal.accentGreen, pal.accentGreen, pal.accentOrange,
        pal.accentOrange, pal.accentCyan
      ], pal);
      break;
    }
  case BuiltinDeviceKind.SAMPLER: {
      const int knobH = dp(66);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      dl.pills ~= DevicePillSpec(Rect(dl.canvasRc.right - dp(78),
          dl.canvasRc.top + dp(4), dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(
          22)), "LOAD WAV", true, pal.accentOrange, 4, 0, 1.0f, 0);
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 3u, 4u, 6u, 8u, 9u, 15u],
          ["Start", "End", "Atk", "Dec", "Rel", "Cutoff", "Res", "Vol"], [
            0.0f, 1.0f, 0.0f, 0.0f, 0.3f, 1.0f, 0.0f, 0.8f
      ], null, null, pal);
      break;
    }
  case BuiltinDeviceKind.EPIANO: {
      const int knobH = dp(66);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u, 2u, 3u, 4u, 7u, 11u, 12u],
          ["Decay", "Rel", "Hard", "Treble", "Trem", "Width", "Drive", "Vol"],
          [0.5f, 0.4f, 0.5f, 0.5f, 0.3f, 0.5f, 0.1f, 0.8f], null, null, pal);
      break;
    }
  case BuiltinDeviceKind.ORGAN: {
      // 9 Hammond Drawbars + Rotary/Perc/Vol knobs at right
      const int rightW = dp(110);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right - rightW - dp(6), inner
          .bottom);
      const int dw = dl.canvasRc.width / 9;
      foreach (d; 0 .. 9) {
        const int dx = dl.canvasRc.left + d * dw;
        dl.drawbarRcs ~= Rect(dx + dp(3), dl.canvasRc.top + dp(18),
            dx + dw - dp(3), dl.canvasRc.bottom - dp(18));
      }
      const bool percOn = readPlugParam(plug, 9, 1.0f) >= 0.5f;
      const bool fastOn = readPlugParam(plug, 11, 0.0f) >= 0.5f;
      Rect rCol = Rect(dl.canvasRc.right + dp(6), inner.top, inner.right, inner
          .bottom);
      dl.pills ~= DevicePillSpec(Rect(rCol.left, rCol.top + dp(4), rCol.right,
          rCol.top + dp(26)), "PERC", percOn, pal.accentOrange, 1, 9, 1.0f, 0);
      dl.pills ~= DevicePillSpec(Rect(rCol.left, rCol.top + dp(30), rCol.right,
          rCol.top + dp(52)), fastOn ? "LESLIE: FAST" : "LESLIE: SLOW", fastOn,
          pal.accentCyan, 1, 11, 1.0f, 0);
      appendKnobRow(dl.knobs, Rect(rCol.left, rCol.top + dp(58), rCol.right,
          rCol.bottom), plug, [10u, 12u], ["PercDec", "Vol"], [0.4f, 0.8f],
          null, [pal.accentAmber, pal.accentGreen], pal);
      break;
    }
  case BuiltinDeviceKind.DRUM_MACHINE: {
      // 4x2 (8 pad) grid on top + Pad preview / right-click Load WAV hint
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.top + dp(34));
      Rect gridRc = Rect(inner.left, dl.canvasRc.bottom + dp(6), inner.right, inner
          .bottom);
      const int cols = 4;
      const int rows = 2;
      const int gap = dp(5);
      const int pw = (gridRc.width - (cols - 1) * gap) / cols;
      const int ph = (gridRc.height - (rows - 1) * gap) / rows;
      foreach (pi; 0 .. 8) {
        const int col = pi % cols;
        const int row = pi / cols;
        const int x0 = gridRc.left + col * (pw + gap);
        const int y0 = gridRc.top + row * (ph + gap);
        dl.padRcs ~= Rect(x0, y0, x0 + pw, y0 + ph);
      }
      break;
    }
  case BuiltinDeviceKind.DR8_MODULE: {
      const int knobH = dp(68);
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.bottom - knobH - dp(
          4));
      dl.pills ~= DevicePillSpec(Rect(dl.canvasRc.right - dp(62),
          dl.canvasRc.top + dp(4), dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(
          22)), "HIT", true, pal.accentOrange, 3, 60, 1.0f, 0);
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(4), inner.right, inner
          .bottom);
      const size_t cnt = plug ? (plug.params.length > 6 ? 6 : plug.params.length) : 0;
      uint[] ids;
      string[] lbls;
      float[] defs;
      foreach (i; 0 .. cnt) {
        ids ~= plug.params[i].id;
        lbls ~= plug.params[i].name;
        defs ~= plug.params[i].defaultValue;
      }
      appendKnobRow(dl.knobs, kArea, plug, ids, lbls, defs, null, null, pal);
      break;
    }
  case BuiltinDeviceKind.FILM: {
      // Sytrus-inspired FilM 6-Op FM Synth: Left tabbed MAIN/OP1..6/F1..3 | Right 6x12 Mod Matrix
      const int hdrH = dp(18);
      dl.auxRc = Rect(inner.left, inner.top, inner.right, inner.top + hdrH);
      Rect body = Rect(inner.left, dl.auxRc.bottom + dp(4), inner.right, inner
          .bottom);
      const int leftW = (body.width * 53) / 100;
      Rect leftRc = Rect(body.left, body.top, body.left + leftW, body.bottom);
      Rect rightRc = Rect(leftRc.right + dp(6), body.top, body.right, body.bottom);

      // Left Top Tab Row (MAIN, OP1..6, F1..3)
      const int tabH = dp(19);
      const int tabW = leftRc.width / 10;
      static immutable string[10] FTABS = [
        "MAIN", "OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "F1", "F2", "F3"
      ];
      foreach (ti; 0 .. 10) {
        const uint tc = (ti == 0 || ti <= 6) ? 0x00D4B84A : 0x009B7EC8;
        dl.pills ~= DevicePillSpec(Rect(leftRc.left + ti * tabW, leftRc.top,
            leftRc.left + (ti + 1) * tabW - dp(1), leftRc.top + tabH),
            FTABS[ti], ui.filmTab == ti, tc, 2, 0, 0.0f, ti);
      }
      Rect leftBody = Rect(leftRc.left, leftRc.top + tabH + dp(4), leftRc.right, leftRc
          .bottom);
      if (ui.filmTab == 0) {
        // MAIN tab: Global knobs (Vol, Algo, Porta, Voices, Detune, Spread) + OP1 Gain Env
        const int kH = (leftBody.height * 46) / 100;
        appendKnobRow(dl.knobs, Rect(leftBody.left, leftBody.top,
            leftBody.right, leftBody.top + kH), plug, [
          265u, 264u, 270u, 267u, 268u, 269u
        ], ["Vol", "Algo", "Porta", "Voices", "Detune", "Spread"], [
          0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f
        ], null, [
          0x00D4B84A, 0x00D4B84A, 0x00D4B84A, 0x009B7EC8, 0x009B7EC8, 0x009B7EC8
        ], pal);
        dl.canvasRc = Rect(leftBody.left, leftBody.top + kH + dp(4),
            leftBody.right, leftBody.bottom);
      } else if (ui.filmTab >= 1 && ui.filmTab <= 6) {
        // OP1..6 tab: Waveform buttons + Level/Ratio/Fine/FB/Pan + ADSR knobs & Env curve
        const int op = ui.filmTab - 1;
        const uint opBase = cast(uint)(op * 38);
        static immutable string[5] WAVES = ["Sin", "Saw", "Sq", "Tri", "Nse"];
        static immutable float[5] WNORMS = [0.0f, 0.3f, 0.5f, 0.7f, 0.9f];
        const float wVal = readPlugParam(plug, opBase + 0, 0.0f);
        const int curW = (wVal < 0.15f) ? 0 : (wVal < 0.4f ? 1 : (wVal < 0.6f ? 2 : (
            wVal < 0.8f ? 3 : 4)));
        const int ww = leftBody.width / 5;
        foreach (wi; 0 .. 5) {
          dl.pills ~= DevicePillSpec(Rect(leftBody.left + wi * ww, leftBody.top,
              leftBody.left + (wi + 1) * ww - dp(2), leftBody.top + dp(18)),
              WAVES[wi], curW == wi, 0x00D4B84A, 0, opBase + 0, WNORMS[wi], wi);
        }
        const int kTop = leftBody.top + dp(21);
        const int kH = (leftBody.bottom - kTop) / 2;
        appendKnobRow(dl.knobs, Rect(leftBody.left, kTop, leftBody.right,
            kTop + kH), plug, [
          opBase + 1, opBase + 2, opBase + 3, opBase + 8, opBase + 9
        ], ["Level", "Ratio", "Fine", "FB", "Pan"], [
          op == 0 ? 1.0f : 0.0f, 0.5f, 0.5f, 0.0f, 0.5f
        ], null, [0x00D4B84A, 0x00D4B84A, 0x00D4B84A, 0x009B7EC8, 0x009B7EC8], pal);
        const int envTop = kTop + kH + dp(2);
        const int envW = (leftBody.width * 38) / 100;
        dl.canvasRc = Rect(leftBody.left, envTop, leftBody.left + envW, leftBody
            .bottom);
        appendKnobRow(dl.knobs, Rect(dl.canvasRc.right + dp(4), envTop,
            leftBody.right, leftBody.bottom), plug, [
          opBase + 4, opBase + 5, opBase + 6, opBase + 7
        ], ["Atk", "Dec", "Sus", "Rel"], [0.0f, 0.2f, 0.7f, 0.3f], null,
            [0x00D4B84A, 0x00D4B84A, 0x00D4B84A, 0x00D4B84A], pal);
      } else {
        // F1..F3 tab: Filter Type + Cutoff/Res/EnvDpt/Mix + Filter Env curve
        const int f = ui.filmTab - 7;
        const uint fBase = cast(uint)(228 + f * 12);
        static immutable string[6] FNAMES = [
          "LP", "HP", "BP", "LS", "HS", "Bell"
        ];
        static immutable float[6] FNORMS = [0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f];
        const float ft = readPlugParam(plug, fBase + 0, 0.0f);
        const int curFt = (ft < 0.1f) ? 0 : (ft < 0.3f ? 1 : (ft < 0.5f ?
            2 : (ft < 0.7f ? 3 : (ft < 0.9f ? 4 : 5))));
        const int fw = leftBody.width / 6;
        foreach (fi; 0 .. 6) {
          dl.pills ~= DevicePillSpec(Rect(leftBody.left + fi * fw, leftBody.top,
              leftBody.left + (fi + 1) * fw - dp(2), leftBody.top + dp(18)),
              FNAMES[fi], curFt == fi, 0x009B7EC8, 0, fBase + 0, FNORMS[fi], fi);
        }
        const int kTop = leftBody.top + dp(22);
        const int kH = (leftBody.bottom - kTop) / 2;
        appendKnobRow(dl.knobs, Rect(leftBody.left, kTop, leftBody.right,
            kTop + kH), plug, [fBase + 1, fBase + 2, fBase + 7, fBase + 8],
            ["Cutoff", "Reso", "EnvDpt", "Mix"], [1.0f, 0.0f, 0.5f, 1.0f],
            null, [0x009B7EC8, 0x009B7EC8, 0x00D4B84A, 0x009B7EC8], pal);
        dl.canvasRc = Rect(leftBody.left, kTop + kH + dp(4), leftBody.right, leftBody
            .bottom);
      }

      // Right half: Sytrus 6x12 Modulation Matrix + FM/RM switch
      dl.filmMatrixRc = rightRc;
      const bool isRm = readPlugParam(plug, 271, 0.0f) >= 0.5f;
      dl.pills ~= DevicePillSpec(Rect(rightRc.right - dp(46),
          rightRc.top + dp(2), rightRc.right - dp(4), rightRc.top + dp(18)),
          isRm ? "RM" : "FM", isRm, 0x00FF6644, 1, 271, 1.0f, 0);
      const int lblW = dp(16);
      const int hdrRowH = dp(16);
      const int gridX0 = rightRc.left + lblW + dp(2);
      const int gridY0 = rightRc.top + dp(20) + hdrRowH;
      const int gridW = rightRc.width - lblW - dp(6);
      const int gridH = rightRc.bottom - dp(6) - gridY0;
      const int cw = gridW / 12;
      const int ch = gridH / 6;
      foreach (r; 0 .. 6) {
        foreach (col; 0 .. 12) {
          const int x0 = gridX0 + col * cw;
          const int y0 = gridY0 + r * ch;
          dl.filmMatrixCells[r * 12 + col] = Rect(x0, y0, x0 + cw - dp(1), y0 + ch - dp(
              1));
        }
      }
      break;
    }
  case BuiltinDeviceKind.SYLPH: {
      // Hive / Sylenth1 dual-layer chassis (zero redundant preset bar; preset lives in wrapper dropdown):
      // [Left: PART A (Osc A + Filter A + AmpA/FiltA ADSR Bars)]
      // [Center: Serial FX Chain LCD + Selected FX Knobs + Sub/Punch/PlayMode/LFO/Master]
      // [Right: PART B (Osc B + Filter B + AmpB/FiltB ADSR Bars)]
      const uint CYAN = 0x0032B8FF;
      const uint AMBER = 0x00F59E0B;
      const uint GREEN = 0x0034D399;
      const uint PURPLE = 0x00A78BFA;

      // 3 Main Columns directly from inner.top: Part A (33%) | Center FX+Punch (34%) | Part B (33%)
      Rect colsRc = inner;
      const int sideW = (colsRc.width * 33) / 100;
      Rect colARc = Rect(colsRc.left, colsRc.top, colsRc.left + sideW, colsRc
          .bottom);
      Rect colBRc = Rect(colsRc.right - sideW, colsRc.top, colsRc.right, colsRc
          .bottom);
      Rect colCRc = Rect(colARc.right + dp(6), colsRc.top, colBRc.left - dp(6), colsRc
          .bottom);

      // Build Part A (colARc, base Osc=0, Filt=8, AmpEnv=13, FiltEnv=17) & Part B (colBRc, 21, 29, 34, 38)
      static immutable string[4] WAVES = ["SAW", "SQR", "TRI", "SIN"];
      static immutable float[4] WNORMS = [0.0f, 0.35f, 0.62f, 0.90f];
      static immutable string[4] FTYPES = ["LP24", "LP12", "BP12", "HP12"];
      static immutable float[4] FTNORMS = [0.0f, 0.35f, 0.62f, 0.88f];
      static immutable string[4] ADSR_LBLS = ["A", "D", "S", "R"];

      foreach (pi; 0 .. 2) {
        Rect pRc = (pi == 0) ? colARc : colBRc;
        const uint col = (pi == 0) ? CYAN : AMBER;
        const uint envCol = (pi == 0) ? GREEN : PURPLE;
        const uint oscB = (pi == 0) ? 0u : 21u;
        const uint fltB = (pi == 0) ? 8u : 29u;
        const uint ampB = (pi == 0) ? 13u : 34u;
        const uint fenB = (pi == 0) ? 17u : 38u;

        const int oscH = (pRc.height * 34) / 100;
        const int fltH = (pRc.height * 21) / 100;
        Rect oscCard = Rect(pRc.left, pRc.top, pRc.right, pRc.top + oscH);
        Rect fltCard = Rect(pRc.left, oscCard.bottom + dp(4), pRc.right,
            oscCard.bottom + dp(4) + fltH);
        Rect envCard = Rect(pRc.left, fltCard.bottom + dp(4), pRc.right, pRc
            .bottom);
        dl.sylphDeckRcs[pi * 3 + 0] = oscCard;
        dl.sylphDeckRcs[pi * 3 + 1] = fltCard;
        dl.sylphDeckRcs[pi * 3 + 2] = envCard;

        // OSC A / B pills + 2x3 knobs
        const float wv = readPlugParam(plug, oscB + 0, 0.0f);
        const int wi = (wv < 0.25f) ? 0 : (wv < 0.50f ? 1 : (wv < 0.75f ? 2 : 3));
        const bool retOn = readPlugParam(plug, oscB + 6, 0.0f) >= 0.5f;
        const int barTop = oscCard.top + dp(18);
        const int halfW = (oscCard.width - dp(12)) / 2;
        dl.pills ~= DevicePillSpec(Rect(oscCard.left + dp(4), barTop,
            oscCard.left + dp(4) + halfW, barTop + dp(17)), WAVES[wi], true,
            col, 0, oscB + 0, WNORMS[(wi + 1) % 4], wi);
        dl.pills ~= DevicePillSpec(Rect(oscCard.right - dp(4) - halfW, barTop,
            oscCard.right - dp(4), barTop + dp(17)), retOn ? "RETRIG" : "FREE",
            retOn, retOn ? AMBER : col, 1, oscB + 6, 1.0f, 0);

        const float octN = readPlugParam(plug, oscB + 1, pi == 0 ? 0.5f : 0.75f);
        const int octShow = cast(int)((octN - 0.5f) * 4.0f + (octN >= 0.5f ? 0.5f : -0.5f));
        const float semiN = readPlugParam(plug, oscB + 2, 0.5f);
        const int semiShow = cast(int)((semiN - 0.5f) * 24.0f + (semiN >= 0.5f ? 0.5f : -0.5f));
        const int uniShow = cast(int)(readPlugParam(plug, oscB + 3, pi == 0 ? 1.0f : 0.67f) * 6.0f + 0.5f) + 1;
        const int kTop = barTop + dp(19);
        const int kRowH = (oscCard.bottom - dp(3) - kTop) / 2;
        appendKnobRow(dl.knobs, Rect(oscCard.left + dp(2), kTop,
            oscCard.right - dp(2), kTop + kRowH), plug, [
          oscB + 1, oscB + 2, oscB + 3
        ], ["Oct", "Semi", "Unison"], [
          pi == 0 ? 0.5f : 0.75f, 0.5f, pi == 0 ? 1.0f : 0.67f
        ], [
          format("%+d", octShow), format("%+d", semiShow), format("%dv", uniShow)
        ], [col, col, col], pal);
        appendKnobRow(dl.knobs, Rect(oscCard.left + dp(2), kTop + kRowH,
            oscCard.right - dp(2), oscCard.bottom - dp(2)), plug, [
          oscB + 4, oscB + 5, oscB + 7
        ], ["Detune", "Stereo", "Vol"], [
          pi == 0 ? 0.38f : 0.46f, pi == 0 ? 0.88f : 0.92f, pi == 0 ? 0.86f : 0.58f
        ], null, [col, col, col], pal);

        // Independent FILTER A / FILTER B pill + 4 knobs
        const float ftv = readPlugParam(plug, fltB + 0, pi == 0 ? 0.0f : 0.35f);
        const int fti = (ftv < 0.25f) ? 0 : (ftv < 0.50f ? 1 : (ftv < 0.75f ? 2 : 3));
        const int fPillW = dp(40);
        Rect fBody = Rect(fltCard.left + dp(3), fltCard.top + dp(17),
            fltCard.right - dp(3), fltCard.bottom - dp(2));
        dl.pills ~= DevicePillSpec(Rect(fBody.left, fBody.middley - dp(10),
            fBody.left + fPillW, fBody.middley + dp(10)), FTYPES[fti], true,
            col, 0, fltB + 0, FTNORMS[(fti + 1) % 4], fti);
        const float fCutDef = pi == 0 ? 0.70f : 0.84f;
        const float fCutHz = 20.0f * powf(900.0f, readPlugParam(plug, fltB + 1, fCutDef));
        const float fEnvPct = (readPlugParam(plug, fltB + 4, pi == 0 ? 0.72f : 0.64f) - 0.5f) * 200.0f;
        appendKnobRow(dl.knobs, Rect(fBody.left + fPillW + dp(2), fBody.top,
            fBody.right, fBody.bottom), plug, [
          fltB + 1, fltB + 2, fltB + 3, fltB + 4
        ], ["Cutoff", "Reso", "Drive", "Env"], [
          fCutDef, pi == 0 ? 0.22f : 0.28f, pi == 0 ? 0.18f : 0.12f,
          pi == 0 ? 0.72f : 0.64f
        ], [formatFreq(fCutHz), null, null, format("%+.0f%%", fEnvPct)], [
          col, col, AMBER, col
        ], pal);

        // Visual ADSR Fader Bars (AMP Env & FILT Env side-by-side)
        const int envGap = dp(5);
        const int halfEnvW = (envCard.width - dp(8) - envGap) / 2;
        foreach (ei; 0 .. 2) {
          const uint eBase = (ei == 0) ? ampB : fenB;
          const uint barCol = (ei == 0) ? envCol : col;
          const int ex0 = envCard.left + dp(4) + ei * (halfEnvW + envGap);
          Rect eBox = Rect(ex0, envCard.top + dp(17), ex0 + halfEnvW, envCard
              .bottom - dp(4));
          const int crvH = dp(32);
          dl.sylphEnvCurveRcs[pi * 2 + ei] = Rect(eBox.left + dp(2), eBox.top,
              eBox.right - dp(2), eBox.top + crvH);
          Rect slArea = Rect(eBox.left + dp(2), eBox.top + crvH + dp(4),
              eBox.right - dp(2), eBox.bottom - dp(13));
          const int sw = slArea.width / 4;
          static immutable float[4] DEF_AMP = [0.15f, 0.72f, 0.78f, 0.58f];
          static immutable float[4] DEF_FLT = [0.10f, 0.70f, 0.44f, 0.58f];
          foreach (si; 0 .. 4) {
            const int sx0 = slArea.left + si * sw + dp(3);
            const int sx1 = slArea.left + (si + 1) * sw - dp(3);
            dl.sliders ~= DeviceSliderSpec(Rect(sx0, slArea.top, sx1,
                slArea.bottom), eBase + cast(uint)si, ADSR_LBLS[si], (ei == 0) ?
                DEF_AMP[si] : DEF_FLT[si], barCol);
          }
        }
      }

      // --- Center Column: Serial FX Chain LCD + Selected FX Controls + Sub/Punch/LFO/Master ---
      const int fxLcdH = (colCRc.height * 34) / 100;
      const int fxCtrlH = (colCRc.height * 21) / 100;
      Rect fxLcdCard = Rect(colCRc.left, colCRc.top, colCRc.right, colCRc.top + fxLcdH);
      Rect fxCtrlCard = Rect(colCRc.left, fxLcdCard.bottom + dp(4),
          colCRc.right, fxLcdCard.bottom + dp(4) + fxCtrlH);
      Rect punchCard = Rect(colCRc.left, fxCtrlCard.bottom + dp(4), colCRc.right, colCRc
          .bottom);
      dl.sylphDeckRcs[6] = fxLcdCard;
      dl.sylphDeckRcs[7] = fxCtrlCard;
      dl.sylphDeckRcs[8] = punchCard;

      // FX Chain LCD interior (`dl.canvasRc`) & 4 clickable routing blocks + Power LEDs
      dl.canvasRc = Rect(fxLcdCard.left + dp(5), fxLcdCard.top + dp(17),
          fxLcdCard.right - dp(5), fxLcdCard.bottom - dp(4));
      const int laneH = dp(42);
      Rect laneRc = Rect(dl.canvasRc.left + dp(4), dl.canvasRc.top + dp(4),
          dl.canvasRc.right - dp(4), dl.canvasRc.top + dp(4) + laneH);
      const int arrowW = dp(10);
      const int blkW = (laneRc.width - arrowW * 3) / 4;
      foreach (fi; 0 .. 4) {
        const int bx0 = laneRc.left + fi * (blkW + arrowW);
        Rect bRc = Rect(bx0, laneRc.top, bx0 + blkW, laneRc.bottom);
        dl.sylphFxBlockRcs[fi] = bRc;
        dl.sylphFxPowerRcs[fi] = Rect(bRc.right - dp(16), bRc.top + dp(2),
            bRc.right - dp(2), bRc.top + dp(15));
      }

      // Selected FX Controls Card (1 toggle pill + 3 dedicated knobs for ui.sylphFxTab)
      const int fxSel = (ui.sylphFxTab >= 0 && ui.sylphFxTab < 4) ? ui.sylphFxTab : 0;
      const uint fxBase = cast(uint)(55 + fxSel * 4);
      const bool fxOn = readPlugParam(plug, fxBase, fxSel > 0 ? 1.0f : 0.0f) >= 0.5f;
      static immutable uint[4] FX_COLS = [
        0x00F59E0B, 0x0032B8FF, 0x0034D399, 0x00A78BFA
      ];
      static immutable string[3][4] FX_KNOB_NAMES = [
        ["Drive", "Tone", "Mix"], ["Rate", "Depth", "Mix"], [
          "Time", "Fdbk", "Mix"
        ], ["Size", "Damp", "Mix"],
      ];
      static immutable float[3][4] FX_KNOB_DEFS = [
        [0.20f, 0.60f, 0.25f], [0.35f, 0.55f, 0.36f], [0.58f, 0.44f, 0.32f],
        [0.72f, 0.28f, 0.30f],
      ];
      Rect fxBody = Rect(fxCtrlCard.left + dp(4), fxCtrlCard.top + dp(17),
          fxCtrlCard.right - dp(4), fxCtrlCard.bottom - dp(2));
      const int fxPillW = dp(48);
      dl.pills ~= DevicePillSpec(Rect(fxBody.left, fxBody.middley - dp(10),
          fxBody.left + fxPillW, fxBody.middley + dp(10)), fxOn ?
          "ON" : "BYPASS", fxOn, FX_COLS[fxSel], 1, fxBase, 1.0f, 0);
      appendKnobRow(dl.knobs, Rect(fxBody.left + fxPillW + dp(4), fxBody.top,
          fxBody.right, fxBody.bottom), plug, [fxBase + 1, fxBase + 2,
        fxBase + 3],
          FX_KNOB_NAMES[fxSel], FX_KNOB_DEFS[fxSel], null, [
            FX_COLS[fxSel], FX_COLS[fxSel], FX_COLS[fxSel]
      ], pal);

      // Bottom Center Card: Sub / Click / Kick Pitch Snap + LFO / Glide / Vol + PlayMode pill in header
      const float pmVal = readPlugParam(plug, 52, 0.0f);
      const int pmIdx = (pmVal < 0.34f) ? 0 : (pmVal < 0.67f ? 1 : 2);
      static immutable string[3] PMODES = ["POLY", "MONO", "LEGA"];
      static immutable float[3] PMODE_NORMS = [0.0f, 0.50f, 0.85f];
      dl.pills ~= DevicePillSpec(Rect(punchCard.right - dp(48),
          punchCard.top + dp(3), punchCard.right - dp(4), punchCard.top + dp(17)),
          PMODES[pmIdx], pmIdx > 0, AMBER, 0, 52, PMODE_NORMS[(pmIdx + 1) % 3], pmIdx);
      Rect pBody = Rect(punchCard.left + dp(4), punchCard.top + dp(18),
          punchCard.right - dp(4), punchCard.bottom - dp(2));
      const int pRowH = pBody.height / 2;
      Rect rPunch = Rect(pBody.left, pBody.top, pBody.right, pBody.top + pRowH);
      Rect rMod = Rect(pBody.left, rPunch.bottom, pBody.right, pBody.bottom);

      const float swv = readPlugParam(plug, 42, 0.0f);
      const int swi = (swv < 0.34f) ? 0 : (swv < 0.67f ? 1 : 2);
      static immutable string[3] SUBWAVES = ["SIN", "TRI", "SQR"];
      static immutable float[3] SUBNORMS = [0.0f, 0.50f, 0.85f];
      const int subPW = dp(38);
      dl.pills ~= DevicePillSpec(Rect(rPunch.left, rPunch.middley - dp(10),
          rPunch.left + subPW, rPunch.middley + dp(10)), SUBWAVES[swi], true,
          AMBER, 0, 42, SUBNORMS[(swi + 1) % 3], swi);
      const int pSt = cast(int)((readPlugParam(plug, 46, 0.5f) - 0.5f) * 96.0f + 0.5f);
      const float pDecMs = 4.0f * powf(125.0f, readPlugParam(plug, 47, 0.25f));
      appendKnobRow(dl.knobs, Rect(rPunch.left + subPW + dp(2), rPunch.top,
          rPunch.right, rPunch.bottom), plug, [44u, 45u, 46u, 47u, 48u],
          ["Sub", "Click", "P.Amt", "P.Dec", "P.Crv"], [
            0.24f, 0.08f, 0.50f, 0.25f, 0.25f
      ], [null, null, format("%+dst", pSt), format("%.0fms", pDecMs), null],
          [AMBER, AMBER, AMBER, AMBER, AMBER], pal);

      appendKnobRow(dl.knobs, rMod, plug, [49u, 50u, 51u, 53u, 54u], [
        "LFO", "L>Pit", "L>Cut", "Glide", "Vol"
      ], [0.32f, 0.05f, 0.12f, 0.06f, 0.75f], null, [
        PURPLE, PURPLE, PURPLE, CYAN, AMBER
      ], pal);
      break;
    }
  case BuiltinDeviceKind.AUX: {
      dl.canvasRc = Rect(inner.left, inner.top, inner.right, inner.top + dp(56));
      Rect kArea = Rect(inner.left, dl.canvasRc.bottom + dp(6), inner.right, inner
          .bottom);
      appendKnobRow(dl.knobs, kArea, plug, [0u, 1u], ["Gain", "Pan"], [
        0.8f, 0.5f
      ], null, [pal.accentCyan, pal.accentOrange], pal);
      break;
    }
  default:
    break;
  }
  return dl;
}

// =========================================================================
// Rendering of Java Swing-Ported Device UIs in FL Studio 2026 Theme Flavor
// =========================================================================

private void drawPanelGrid(DrawBuf buf, Rect rc, const ref FlPalette pal) {
  buf.fillRect(Rect(rc.left, rc.middley, rc.right, rc.middley + dp(1)), pal
      .borderSubtle);
  buf.fillRect(Rect(rc.left + rc.width / 4, rc.top, rc.left + rc.width / 4 + dp(1),
      rc.bottom), pal.borderSubtle);
  buf.fillRect(Rect(rc.middlex, rc.top, rc.middlex + dp(1), rc.bottom), pal
      .borderSubtle);
  buf.fillRect(Rect(rc.left + (rc.width * 3) / 4, rc.top,
      rc.left + (rc.width * 3) / 4 + dp(1), rc.bottom), pal.borderSubtle);
}

private void drawDbMeterBar(DrawBuf buf, Rect rc, float db, float minDb,
    float maxDb, uint col, string label, FontRef fnt, const ref FlPalette pal) {
  drawThemeCard(buf, rc, pal.bgWindow, pal.borderSubtle, pal, 1.5f, true);
  const float n = normalizeDb(db, minDb, maxDb);
  const int fh = cast(int)(n * (rc.height - dp(2)));
  if (fh > 0) {
    buf.fillRect(Rect(rc.left + dp(1), rc.bottom - dp(1) - fh, rc.right - dp(1), rc
        .bottom - dp(1)), col);
  }
  if (label.length > 0 && rc.width >= dp(14)) {
    drawCenteredText(buf, fnt, Rect(rc.left, rc.bottom - dp(12), rc.right,
        rc.bottom), toUTF32(label), pal.fgMuted);
  }
}

private uint lerpColorRgb(uint a, uint b, float t) pure nothrow @safe {
  const float u = clamp01(t);
  const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  const uint r = cast(uint)(ar + (br - ar) * u + 0.5f) & 0xFF;
  const uint g = cast(uint)(ag + (bg - ag) * u + 0.5f) & 0xFF;
  const uint bl = cast(uint)(ab + (bb - ab) * u + 0.5f) & 0xFF;
  return (a & 0xFF000000u) | (r << 16) | (g << 8) | bl;
}

private void drawVGradientRect(DrawBuf buf, Rect rc, uint topCol, uint botCol) {
  if (rc.width <= 0 || rc.height <= 0)
    return;
  const int h = rc.height;
  const int step = (h > 48) ? 2 : 1;
  for (int dy = 0; dy < h; dy += step) {
    const float t = (h > 1) ? cast(float)dy / cast(float)(h - 1) : 0.0f;
    const uint col = lerpColorRgb(topCol, botCol, t);
    const int y1 = (dy + step < h) ? (rc.top + dy + step) : rc.bottom;
    buf.fillRect(Rect(rc.left, rc.top + dy, rc.right, y1), col);
  }
}

private void drawAcidLed(DrawBuf buf, float cx, float cy, float r, bool lit) {
  if (lit) {
    drawFilledCircle(buf, cx, cy, r * 1.9f, 0xA8FF3333, COLOR_TRANSPARENT);
    drawFilledCircle(buf, cx, cy, r, 0x00FF3333, 0x00555555);
    drawFilledCircle(buf, cx - r * 0.25f, cy - r * 0.25f, r * 0.35f, 0x40FFFFFF, COLOR_TRANSPARENT);
  } else {
    drawFilledCircle(buf, cx, cy, r, 0x00480808, 0x00666666);
    drawFilledCircle(buf, cx - r * 0.25f, cy - r * 0.25f, r * 0.28f, 0xC8FFFFFF, COLOR_TRANSPARENT);
  }
}

private void drawTb303Knob(DrawBuf buf, int cx, int cy, int r, float normVal) {
  if (r < 4)
    return;
  const float v = clamp01(normVal);
  // 11 radial tick marks around knob perimeter (135 deg -> 405 deg)
  foreach (i; 0 .. 11) {
    const float deg = 135.0f + cast(float)i * 27.0f;
    const float rad = deg * 0.0174532925f;
    const float cs = cosf(rad);
    const float sn = sinf(rad);
    drawThickLine(buf, cx + cs * (r + dpf(1.2f)), cy + sn * (r + dpf(1.2f)),
        cx + cs * (r + dpf(4.0f)), cy + sn * (r + dpf(4.0f)), dpf(1.1f), 0x00333333);
  }
  // Outer brushed silver aluminum skirt + dark inner cap + glowing red value arc + white pointer
  drawFilledCircle(buf, cx + dpf(1.0f), cy + dpf(1.5f), r, 0xB8000000, COLOR_TRANSPARENT);
  drawFilledCircle(buf, cx, cy, r, 0x00D0D4DA, 0x0044484E);
  const float innerR = r * 0.72f;
  drawFilledCircle(buf, cx, cy, innerR, 0x0022262C, COLOR_TRANSPARENT);
  if (v > 0.01f) {
    drawOpenArc(buf, cx, cy, innerR - dpf(1.6f), 135.0f, 135.0f + 270.0f * v, dpf(
        1.6f), 0x00FF3333);
  }
  const float ptrRad = (135.0f + 270.0f * v) * 0.0174532925f;
  drawThickLine(buf, cx, cy, cx + cosf(ptrRad) * (r - dpf(1.5f)),
      cy + sinf(ptrRad) * (r - dpf(1.5f)), dpf(1.8f), 0x00FFFFFF);
}

void drawBuiltinDevicePanel(DrawBuf buf, Rect bodyRc,
    const(TransportModel)* model,
    const(PluginSlotState)* plug, const ref BuiltinDeviceUiState ui, FontRef fnt) {
  if (!plug || bodyRc.width < dp(160) || bodyRc.height < dp(120))
    return;
  const pal = model ? model.palette() : getFlPalette();
  auto aaGuard = ScopedRetroAntialias(pal.retroBevel);

  const dl = buildBuiltinDeviceLayout(bodyRc, plug, ui, pal);
  if (dl.kind == BuiltinDeviceKind.GENERIC)
    return;

  FontRef uiFnt = getDawFont(10, fnt, FontWeight.Normal);
  FontRef valFnt = getDawMonoFont(9, fnt, FontWeight.Normal);

  // Tight chassis frame sized cleanly to `dl.panelRc` (never stretched thinly inside large windows)
  drawThemeCard(buf, Rect(dl.panelRc.left + dp(4), dl.panelRc.top + dp(2),
      dl.panelRc.right - dp(4), dl.panelRc.bottom - dp(3)), pal.bgPanel,
      pal.borderStrong, pal, 3.0f, false);

  // 1. Device-specific visualizers
  switch (dl.kind) {
  case BuiltinDeviceKind.EQ: {
      static immutable uint[8] BAND_COLS = [
        0x00E04040, 0x00E07020, 0x00D0C020, 0x0040B040, 0x0030A0A0, 0x004080E0,
        0x008060D0, 0x00C050A0,
      ];
      static immutable string[6] TYPE_NAMES = [
        "Off", "LP", "HP", "LS", "HS", "Bell"
      ];
      static immutable float[8] DEF_FREQS = [
        30, 80, 250, 700, 2000, 5000, 10000, 16000
      ];

      // Left Main Fruity EQ 2 / EQ Eight Curve + FFT Heatmap Canvas
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, 0x000F131A, pal.borderSubtle, pal, 2.0f, true);
      const int pad = dp(8);
      const int sw = c.width - pad * 2;
      const int sh = c.height - pad * 2;
      if (sw > dp(20) && sh > dp(16)) {
        // Log frequency gridlines (30, 50, 100, 200, 500, 1k, 2k, 5k, 10k) & dB gridlines (-12, 0, +12)
        static immutable float[7] GRID_HZ = [
          50, 100, 200, 500, 1000, 5000, 10000
        ];
        static immutable string[7] GRID_LBL = [
          "50", "100", "200", "500", "1k", "5k", "10k"
        ];
        foreach (gi, hz; GRID_HZ) {
          const int gx = c.left + pad + cast(int)(eqFreqToNorm(hz) * sw);
          buf.fillRect(Rect(gx, c.top + dp(2), gx + dp(1), c.bottom - dp(2)), 0xE8FFFFFF);
          if (gx + dp(20) < c.right) {
            drawVCenteredText(buf, valFnt, gx + dp(2), Rect(gx,
                c.bottom - dp(13), gx + dp(26), c.bottom - dp(1)),
                toUTF32(GRID_LBL[gi]), 0x80FFFFFF);
          }
        }
        const int yZero = c.top + pad + sh / 2;
        const int yP12 = c.top + pad + sh / 4;
        const int yM12 = c.top + pad + (sh * 3) / 4;
        buf.fillRect(Rect(c.left + dp(2), yP12, c.right - dp(2), yP12 + dp(1)), 0xECFFFFFF);
        buf.fillRect(Rect(c.left + dp(2), yZero, c.right - dp(2), yZero + dp(1)), 0xD0FFFFFF);
        buf.fillRect(Rect(c.left + dp(2), yM12, c.right - dp(2), yM12 + dp(1)), 0xECFFFFFF);

        // Fruity EQ 2 style FFT spectrum overlay (soft heat bars + smooth trace)
        void plotFftFill(const(float)[] mags, uint barCol, uint lineCol) {
          if (mags.length < 2)
            return;
          float px = c.left + pad;
          float py = c.bottom - pad - normalizeDb(mags[0], -72.0f, 6.0f) * sh;
          foreach (i; 1 .. mags.length) {
            const float t = cast(float)i / cast(float)(mags.length - 1);
            const float x = c.left + pad + t * sw;
            const float y = c.bottom - pad - normalizeDb(mags[i], -72.0f, 6.0f) * sh;
            if (cast(int)y < c.bottom - pad) {
              const int bx0 = cast(int)px;
              const int bx1 = (cast(int)x > bx0) ? cast(int)x : (bx0 + dp(1));
              buf.fillRect(Rect(bx0, cast(int)y, bx1, c.bottom - pad), barCol);
            }
            drawThickLine(buf, px, py, x, y, dpf(1.3f), lineCol);
            px = x;
            py = y;
          }
        }

        plotFftFill(plug.specInDb, 0xDF4080E0, 0x904080E0);
        plotFftFill(plug.specOutDb, 0xD4F59E0B, 0x48FFC040);

        // Per-band translucent response curves & fills toward 0dB
        enum int CURVE_N = 56;
        foreach (b; 0 .. 8) {
          if (eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f)) == 0)
            continue;
          const uint bc = BAND_COLS[b];
          const uint bcFill = (bc & 0x00FFFFFF) | 0xDE000000;
          const uint bcLine = (bc & 0x00FFFFFF) | 0x70000000;
          float bpx = c.left + pad;
          float bpy = yZero;
          foreach (i; 0 .. CURVE_N + 1) {
            const float t = cast(float)i / cast(float)CURVE_N;
            const float hz = 20.0f * powf(1000.0f, t);
            float bdb = eqBandGainDbAtFreq(plug, b, hz);
            if (bdb < -25.0f)
              bdb = -25.0f;
            else if (bdb > 25.0f)
              bdb = 25.0f;
            const float x = c.left + pad + t * sw;
            const float y = c.top + pad + (1.0f - (bdb + 25.0f) / 50.0f) * sh;
            if (i > 0) {
              const int ry0 = (y < yZero) ? cast(int)y : yZero;
              const int ry1 = (y > yZero) ? cast(int)y : yZero;
              if (ry1 > ry0) {
                const int rx1 = (cast(int)x > cast(int)bpx) ? cast(int)x : (cast(
                    int)bpx + 1);
                buf.fillRect(Rect(cast(int)bpx, ry0, rx1, ry1), bcFill);
              }
              drawThickLine(buf, bpx, bpy, x, y, dpf(1.2f), bcLine);
            }
            bpx = x;
            bpy = y;
          }
        }

        // Crisp white/cream composite EQ response curve
        float prevX = c.left + pad;
        float prevDb = eqCompositeDbAtFreq(plug, 20.0f);
        if (prevDb < -25.0f)
          prevDb = -25.0f;
        else if (prevDb > 25.0f)
          prevDb = 25.0f;
        float prevY = c.top + pad + (1.0f - (prevDb + 25.0f) / 50.0f) * sh;
        foreach (i; 1 .. CURVE_N + 1) {
          const float t = cast(float)i / cast(float)CURVE_N;
          const float hz = 20.0f * powf(1000.0f, t);
          float db = eqCompositeDbAtFreq(plug, hz);
          if (db < -25.0f)
            db = -25.0f;
          else if (db > 25.0f)
            db = 25.0f;
          const float x = c.left + pad + t * sw;
          const float y = c.top + pad + (1.0f - (db + 25.0f) / 50.0f) * sh;
          drawThickLine(buf, prevX, prevY, x, y, dpf(2.2f), 0x00F4F7FC);
          prevX = x;
          prevY = y;
        }

        // 8 colored numbered Fruity EQ2 / EQ Eight band tokens ('1'..'8')
        foreach (b; 0 .. 8) {
          const int ti = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
          if (ti == 0 && b != ui.eqSelectedBand)
            continue;
          const float fn = readPlugParam(plug, cast(uint)(8 + b), eqFreqToNorm(DEF_FREQS[b]));
          const float gn = readPlugParam(plug, cast(uint)(16 + b), 0.5f);
          float db = (gn - 0.5f) * 48.0f;
          if (db < -25.0f)
            db = -25.0f;
          else if (db > 25.0f)
            db = 25.0f;
          const float bx = c.left + pad + fn * sw;
          const float by = c.top + pad + (1.0f - (db + 25.0f) / 50.0f) * sh;
          const bool isSel = (b == ui.eqSelectedBand);
          if (isSel) {
            buf.fillRect(Rect(cast(int)bx, c.top + dp(2), cast(int)bx + dp(1), c
                .bottom - dp(2)), (BAND_COLS[b] & 0x00FFFFFF) | 0xA8000000);
            drawFilledCircle(buf, bx, by, dpf(9.0f),
                (BAND_COLS[b] & 0x00FFFFFF) | 0x90000000, 0x00FFFFFF);
          }
          const uint tokFill = (ti == 0) ? adjustColorBrightness(BAND_COLS[b], -28) : BAND_COLS[b];
          drawFilledCircle(buf, bx, by, dpf(isSel ? 7.2f : 6.0f), tokFill, 0x00FFFFFF);
          drawCenteredText(buf, valFnt, Rect(cast(int)bx - dp(7),
              cast(int)by - dp(7), cast(int)bx + dp(7), cast(int)by + dp(7)),
              toUTF32(format("%d", b + 1)), 0x00FFFFFF);
        }

        // Selected band HUD badge at top-left
        const int sb = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui
          .eqSelectedBand : 0;
        const int sbType = eqNormToTypeIdx(readPlugParam(plug, cast(uint)sb, 0.0f));
        const float sbFreq = eqNormToFreq(readPlugParam(plug,
            cast(uint)(8 + sb), eqFreqToNorm(DEF_FREQS[sb])));
        const float sbGain = (readPlugParam(plug, cast(uint)(16 + sb), 0.5f) - 0.5f) * 48.0f;
        const float sbQ = eqNormToQ(readPlugParam(plug, cast(uint)(24 + sb), 0.425f));
        const string hud = format("B%d %s · %s · %+.1fdB · Q%.2f", sb + 1,
            TYPE_NAMES[sbType], formatFreq(sbFreq), sbGain, sbQ);
        drawVCenteredText(buf, valFnt, c.left + dp(8), Rect(c.left + dp(4),
            c.top + dp(2), c.right - dp(4), c.top + dp(16)), toUTF32(hud), BAND_COLS[sb]);
      }

      // Right Inspector Card
      drawThemeCard(buf, dl.auxRc, pal.bgPanelAlt, pal.borderSubtle, pal, 2.0f, false);
      const int sbIdx = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui
        .eqSelectedBand : 0;
      drawCenteredText(buf, uiFnt, Rect(dl.auxRc.left, dl.auxRc.top + dp(2),
          dl.auxRc.right, dl.auxRc.top + dp(17)), toUTF32(format("BAND %d",
          sbIdx + 1)), BAND_COLS[sbIdx]);

      // Bottom 8 rainbow band strip cells (matching EqDevicePanel.java)
      foreach (b; 0 .. 8) {
        if (b >= dl.pills.length)
          break;
        Rect br = dl.pills[b].rc;
        const int ti = eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f));
        const bool sel = (b == ui.eqSelectedBand);
        const uint cellBg = sel ? adjustColorBrightness(pal.bgControlHover, 6) : pal
          .bgWindow;
        drawThemeCard(buf, br, cellBg, sel ? BAND_COLS[b] : pal.borderStrong, pal,
            1.5f, false);
        buf.fillRect(Rect(br.left + dp(2), br.top + dp(2), br.right - dp(2), br
            .top + dp(5)), ti > 0 ? BAND_COLS[b] : adjustColorBrightness(BAND_COLS[b],
            -26));
        drawFilledCircle(buf, br.left + dp(9), br.top + dp(14), dpf(3.2f),
            BAND_COLS[b], COLOR_TRANSPARENT);
        drawVCenteredText(buf, valFnt, br.left + dp(15), Rect(br.left + dp(14),
            br.top + dp(6), br.right, br.top + dp(22)), toUTF32(format("%d %s",
            b + 1, TYPE_NAMES[ti])), ti > 0 || sel ? pal.fgText : pal.fgMuted);
        const float bf = eqNormToFreq(readPlugParam(plug, cast(uint)(8 + b),
            eqFreqToNorm(DEF_FREQS[b])));
        drawCenteredText(buf, valFnt, Rect(br.left, br.top + dp(21), br.right,
            br.bottom - dp(2)), toUTF32(formatFreq(bf)), pal.fgMuted);
      }
      break;
    }
  case BuiltinDeviceKind.COMPRESSOR:
  case BuiltinDeviceKind.LIMITER: {
      Rect c = dl.canvasRc;
      const int grW = dp(24);
      Rect curveRc = Rect(c.left, c.top, c.right - grW - dp(4), c.bottom);
      Rect grRc = Rect(curveRc.right + dp(4), c.top, c.right, c.bottom);
      drawThemeCard(buf, curveRc, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      drawPanelGrid(buf, curveRc, pal);

      const int pad = dp(6);
      const int cw = curveRc.width - pad * 2;
      const int ch = curveRc.height - pad * 2;
      if (cw > dp(12) && ch > dp(12)) {
        // Unity diagonal reference
        drawThickLine(buf, curveRc.left + pad, curveRc.bottom - pad,
            curveRc.right - pad, curveRc.top + pad, dpf(1.0f), pal.borderStrong);
        const float threshDb = (dl.kind == BuiltinDeviceKind.LIMITER) ? (
            readPlugParam(plug, 0, 1.0f) * 12.0f - 12.0f) : (
            (readPlugParam(plug, 0, 1.0f) - 1.0f) * 60.0f);
        const float ratio = (dl.kind == BuiltinDeviceKind.LIMITER) ? 100.0f : (
            1.0f + readPlugParam(plug, 1, 0.0f) * 19.0f);
        const float makeupDb = (dl.kind == BuiltinDeviceKind.LIMITER) ? (
            readPlugParam(plug, 3, 0.0f) * 24.0f) : (readPlugParam(plug, 5, 0.0f) * 24.0f);

        float prevX = curveRc.left + pad;
        float prevY = curveRc.bottom - pad;
        enum int STEPS = 32;
        foreach (i; 0 .. STEPS + 1) {
          const float inDb = -60.0f + (cast(float)i / STEPS) * 60.0f;
          float outDb = inDb;
          if (inDb > threshDb) {
            outDb = threshDb + (inDb - threshDb) / ratio;
          }
          outDb += makeupDb;
          if (outDb < -60.0f)
            outDb = -60.0f;
          else if (outDb > 0.0f)
            outDb = 0.0f;
          const float x = curveRc.left + pad + ((inDb + 60.0f) / 60.0f) * cw;
          const float y = curveRc.bottom - pad - ((outDb + 60.0f) / 60.0f) * ch;
          if (i > 0) {
            drawThickLine(buf, prevX, prevY, x, y, dpf(2.0f),
                dl.kind == BuiltinDeviceKind.LIMITER ?
                pal.accentRed : pal.accentAmber);
          }
          prevX = x;
          prevY = y;
        }
        if (plug.hasMetering && plug.meterInDb > -60.0f) {
          const float inN = normalizeDb(plug.meterInDb, -60.0f, 0.0f);
          const float outN = normalizeDb(plug.meterOutDb, -60.0f, 0.0f);
          drawFilledCircle(buf, curveRc.left + pad + inN * cw,
              curveRc.bottom - pad - outN * ch, dpf(4.0f), pal.accentCyan, pal
                .bgLcd);
        }
      }
      drawDbMeterBar(buf, grRc, -plug.meterGrDb, 0.0f, 24.0f,
          pal.accentOrange, "GR", valFnt, pal);
      break;
    }
  case BuiltinDeviceKind.HOTT: {
      // Top Xfer OTT / Hott header banner
      drawThemeCard(buf, dl.footerRc, 0x001A6B8A, 0x0000A8CC, pal, 2.0f, false);
      drawVCenteredText(buf, uiFnt, dl.footerRc.left + dp(8), dl.footerRc,
          "⚡ HOTT  ·  OTT MULTIBAND COMPRESSOR"d, 0x0000D4FF);

      // Left Split Freq panel card
      drawThemeCard(buf, dl.auxRc, pal.bgPanelAlt, pal.borderSubtle, pal, 2.0f, false);
      drawCenteredText(buf, valFnt, Rect(dl.auxRc.left, dl.auxRc.top + dp(1),
          dl.auxRc.right, dl.auxRc.top + dp(13)), "SPLIT FREQ"d, pal.fgMuted);

      // Center Xfer OTT Band Meter Panel
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, 0x00151C24, pal.borderSubtle, pal, 2.0f, true);

      // dB gridlines across bands (-80 .. 0 dB) & top inspector header
      Rect b0 = dl.hottBarRcs[0];
      Rect b2 = dl.hottBarRcs[2];
      foreach (db; [-80, -70, -60, -50, -40, -30, -20, -10, 0]) {
        const float n = normalizeDb(cast(float)db, -80.0f, 0.0f);
        const int gx = b0.left + cast(int)(n * b0.width);
        buf.fillRect(Rect(gx, b0.top, gx + dp(1), b2.bottom), 0xDCFFFFFF);
      }
      foreach (db; [-80, -60, -40, -20, 0]) {
        const float n = normalizeDb(cast(float)db, -80.0f, 0.0f);
        const int gx = b0.left + cast(int)(n * b0.width);
        drawVCenteredText(buf, valFnt, gx - dp(4), Rect(gx - dp(6),
            c.bottom - dp(16), gx + dp(18), c.bottom - dp(2)),
            toUTF32(format("%d", db < 0 ? -db : 0)), 0x90FFFFFF);
      }
      static immutable string[3] HDR_TXT = ["Att/Rel", "Below", "Above"];
      drawVCenteredText(buf, valFnt, dl.hottInspectorRcs[0].left,
          Rect(dl.hottInspectorRcs[0].left, c.top + dp(2),
            dl.hottInspectorRcs[0].right, c.top + dp(14)), toUTF32(HDR_TXT[ui
            .hottTab]), 0x00A8B8C8);

      // 3 Band Horizontal Threshold/Compression Bars (High, Mid, Low)
      static immutable string[3] BNAMES = ["High", "Mid", "Low"];
      static immutable uint[3] DOWN_PIDS = [11u, 10u, 9u];
      static immutable uint[3] UP_PIDS = [14u, 13u, 12u];
      static immutable uint[3] ATK_PIDS = [19u, 18u, 17u];
      static immutable uint[3] REL_PIDS = [22u, 21u, 20u];
      static immutable uint[3] DRAT_PIDS = [25u, 24u, 23u];
      static immutable uint[3] URAT_PIDS = [28u, 27u, 26u];
      static immutable float[3] DEF_DN = [
        (-35.5f + 60.0f) / 60.0f, (-30.2f + 60.0f) / 60.0f, (-33.8f + 60.0f) / 60.0f
      ];
      static immutable float[3] DEF_UP = [
        (-40.8f + 60.0f) / 72.0f, (-41.8f + 60.0f) / 72.0f, (-40.8f + 60.0f) / 72.0f
      ];
      static immutable float[3] DEF_ATK = [0.710f, 0.783f, 0.893f];
      static immutable float[3] DEF_REL = [0.560f, 0.725f, 0.725f];
      static immutable float[3] DEF_DR = [0.999f, 0.985f, 0.985f];
      float[3] grs = [plug.meterHighGrDb, plug.meterMidGrDb, plug.meterLowGrDb];
      float[3] ins = [plug.meterHighInDb, plug.meterMidInDb, plug.meterLowInDb];

      foreach (bi; 0 .. 3) {
        Rect barRc = dl.hottBarRcs[bi];
        Rect inspRc = dl.hottInspectorRcs[bi];
        buf.fillRect(barRc, 0x0026323D);

        const float downDb = readPlugParam(plug, DOWN_PIDS[bi], DEF_DN[bi]) * 60.0f - 60.0f;
        const float upDb = readPlugParam(plug, UP_PIDS[bi], DEF_UP[bi]) * 72.0f - 60.0f;
        const int upX = barRc.left + cast(int)(normalizeDb(upDb, -80.0f, 0.0f) * barRc
            .width);
        const int downX = barRc.left + cast(int)(normalizeDb(downDb, -80.0f, 0.0f) * barRc
            .width);

        // Cyan upward zone & dark downward zone
        if (upX > barRc.left) {
          buf.fillRect(Rect(barRc.left, barRc.top, upX, barRc.bottom), 0xB000D4FF);
        }
        if (downX < barRc.right) {
          buf.fillRect(Rect(downX > barRc.left ? downX : barRc.left, barRc.top,
              barRc.right, barRc.bottom), 0x70121C26);
        }
        // Output bar & orange input tick
        const float bandInDb = (ins[bi] > -80.0f) ? ins[bi] : plug.meterOutDb;
        if (bandInDb > -80.0f) {
          const float outN = normalizeDb(plug.meterOutDb > -80.0f ?
              plug.meterOutDb : bandInDb, -80.0f, 0.0f);
          const int barH = dp(4);
          const int by = barRc.middley - barH / 2;
          buf.fillRect(Rect(barRc.left, by,
              barRc.left + cast(int)(outN * barRc.width), by + barH), 0x5500D4FF);
          const int inX = barRc.left + cast(int)(normalizeDb(bandInDb, -80.0f, 0.0f) * barRc
              .width);
          buf.fillRect(Rect(inX - dp(1), barRc.middley - dp(5), inX + dp(2), barRc
              .middley + dp(5)), 0x00FF8C00);
        }
        // Draggable thick cyan threshold lines (upX, downX)
        buf.fillRect(Rect(upX - dp(1), barRc.top, upX + dp(1), barRc.bottom), 0x0000D4FF);
        buf.fillRect(Rect(downX - dp(1), barRc.top, downX + dp(1), barRc.bottom), 0x0000D4FF);

        // Band label & threshold readout
        drawVCenteredText(buf, valFnt, barRc.right - dp(28),
            Rect(barRc.right - dp(30), barRc.top + dp(1), barRc.right - dp(2), barRc
              .top + dp(12)), toUTF32(BNAMES[bi]), 0x70FFFFFF);
        drawVCenteredText(buf, valFnt, (downX > barRc.left + dp(28)) ?
            (downX - dp(28)) : (barRc.left + dp(2)), Rect(barRc.left + dp(2),
              barRc.bottom - dp(13), barRc.right, barRc.bottom),
            toUTF32(format("%.1f", downDb)), 0x0000D4FF);

        // Right inspector cell per band (Att/Rel, Below Thresh/Ratio, or Above Thresh/Ratio)
        drawThemeCard(buf, inspRc, 0x001A242E, 0x002B3A48, pal, 1.0f, false);
        string topStr, botStr;
        if (ui.hottTab == 0) {
          const float atkMs = 0.1f * powf(1000.0f, readPlugParam(plug, ATK_PIDS[bi],
              DEF_ATK[bi]));
          const float relMs = 10.0f * powf(100.0f, readPlugParam(plug, REL_PIDS[bi],
              DEF_REL[bi]));
          topStr = format("%.1f ms", atkMs);
          botStr = format("%.0f ms", relMs);
        } else if (ui.hottTab == 1) {
          const float urN = readPlugParam(plug, URAT_PIDS[bi], 0.76f);
          const float uRat = (urN >= 0.999f) ? 1000.0f : (1.0f / (1.0f - urN));
          topStr = format("%.1fdB", upDb);
          botStr = format("1:%.2f", uRat);
        } else {
          const float drN = readPlugParam(plug, DRAT_PIDS[bi], DEF_DR[bi]);
          const float dRat = (drN >= 0.999f) ? 1000.0f : (1.0f / (1.0f - drN));
          topStr = format("%.1fdB", downDb);
          botStr = (dRat >= 999.0f) ? "inf:1" : format("%.1f:1", dRat);
        }
        drawVCenteredText(buf, valFnt, inspRc.left + dp(4), Rect(inspRc.left,
            inspRc.top, inspRc.right, inspRc.middley), toUTF32(topStr), 0x0000D4FF);
        drawVCenteredText(buf, valFnt, inspRc.left + dp(4), Rect(inspRc.left,
            inspRc.middley, inspRc.right, inspRc.bottom), toUTF32(botStr), 0x006CE5FF);
      }
      break;
    }
  case BuiltinDeviceKind.MAXIM: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      drawPanelGrid(buf, c, pal);
      static immutable uint[4] MCOLS = [
        0x00E25822, 0x0010B981, 0x003B82F6, 0x00F59E0B
      ];
      const uint col = (ui.maximBandTab >= 0 && ui.maximBandTab < 4) ? MCOLS[ui
          .maximBandTab] : pal.accentPurple;
      if (ui.maximBandTab == 4) {
        const float lx = c.left + dp(8) + readPlugParam(plug, 0, 0.461f) * (c
            .width - dp(16));
        const float hx = c.left + dp(8) + readPlugParam(plug, 1, 0.436f) * (c
            .width - dp(16));
        drawThickLine(buf, lx, c.top + dp(6), lx, c.bottom - dp(6), dpf(2.0f), MCOLS[0]);
        drawThickLine(buf, hx, c.top + dp(6), hx, c.bottom - dp(6), dpf(2.0f), MCOLS[2]);
        drawCenteredText(buf, uiFnt, c, "3-Band Linkwitz-Riley Crossover"d, pal
            .fgMuted);
      } else {
        const uint base = cast(uint)(4 + ui.maximBandTab * 10);
        const float ceilN = readPlugParam(plug, base + 9, 0.975f);
        const float cy = c.bottom - dp(6) - ceilN * (c.height - dp(12));
        drawThickLine(buf, c.left + dp(6), c.bottom - dp(6), c.right - dp(6), cy, dpf(
            2.0f), col);
        drawThickLine(buf, c.left + dp(6), cy, c.right - dp(6), cy, dpf(1.2f), pal
            .accentRed);
      }
      break;
    }
  case BuiltinDeviceKind.DELAY: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const float tl = readPlugParam(plug, 0, 0.6f);
      const float tr = readPlugParam(plug, 1, 0.6f);
      const float fb = readPlugParam(plug, 2, 0.4f);
      const int baseY = c.bottom - dp(8);
      const int spanH = c.height - dp(16);
      const int spanW = c.width - dp(20);
      float amp = 1.0f;
      foreach (tap; 0 .. 8) {
        const float tFrac = (tap + 1) * (0.04f + 0.08f * ((tap % 2 == 0) ? tl : tr));
        if (tFrac > 1.0f)
          break;
        const int tx = c.left + dp(10) + cast(int)(tFrac * spanW);
        const int th = cast(int)(amp * spanH);
        const uint col = (tap % 2 == 0) ? pal.accentCyan : pal.accentOrange;
        drawThickLine(buf, tx, baseY, tx, baseY - th, dpf(3.0f), col);
        amp *= fb;
      }
      break;
    }
  case BuiltinDeviceKind.REVERB:
  case BuiltinDeviceKind.CONVOLVER: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const float room = (dl.kind == BuiltinDeviceKind.REVERB) ? readPlugParam(plug,
          0, 0.5f) : readPlugParam(plug, 1, 1.0f);
      const float damp = (dl.kind == BuiltinDeviceKind.REVERB) ? readPlugParam(plug,
          1, 0.5f) : 0.3f;
      const float pre = (dl.kind == BuiltinDeviceKind.REVERB) ? readPlugParam(plug,
          3, 0.1f) : readPlugParam(plug, 2, 0.0f);
      const int pad = dp(8);
      const int x0 = c.left + pad + cast(int)(pre * 0.25f * (c.width - pad * 2));
      const int rw = c.right - pad - x0;
      const int rh = c.height - pad * 2;
      if (rw > dp(12) && rh > dp(10)) {
        float px = x0;
        float py = c.top + pad;
        const float rate = 1.5f + (1.0f - room) * 4.5f + damp * 1.5f;
        foreach (i; 1 .. 33) {
          const float t = cast(float)i / 32.0f;
          const float x = x0 + t * rw;
          const float y = (c.bottom - pad) - expf(-t * rate) * rh;
          drawThickLine(buf, px, py, x, y, dpf(2.0f),
              dl.kind == BuiltinDeviceKind.REVERB ? pal.accentPurple : pal
                .accentCyan);
          px = x;
          py = y;
        }
      }
      break;
    }
  case BuiltinDeviceKind.PHASER: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const int pad = dp(8);
      const int sw = c.width - pad * 2;
      const int sh = c.height - pad * 2;
      const float midY = c.top + pad + sh * 0.5f;
      if (plug.scopeL.length > 1 && plug.scopeR.length > 1) {
        const size_t n = plug.scopeL.length < plug.scopeR.length ?
          plug.scopeL.length : plug.scopeR.length;
        foreach (i; 1 .. n) {
          const float x0 = c.left + pad + cast(float)(i - 1) / (n - 1) * sw;
          const float x1 = c.left + pad + cast(float)i / (n - 1) * sw;
          drawThickLine(buf, x0, midY - plug.scopeL[i - 1] * sh * 0.44f, x1,
              midY - plug.scopeL[i] * sh * 0.44f, dpf(1.5f), pal.accentCyan);
          drawThickLine(buf, x0, midY - plug.scopeR[i - 1] * sh * 0.44f, x1,
              midY - plug.scopeR[i] * sh * 0.44f, dpf(1.2f), pal.accentOrange);
        }
      } else {
        const float rate = 1.0f + readPlugParam(plug, 0, 0.3f) * 6.0f;
        const float depth = readPlugParam(plug, 1, 0.7f);
        float px = c.left + pad;
        float py = midY;
        foreach (i; 1 .. 41) {
          const float t = cast(float)i / 40.0f;
          const float x = c.left + pad + t * sw;
          const float y = midY - sinf(t * 6.28318f * rate) * depth * sh * 0.42f;
          drawThickLine(buf, px, py, x, y, dpf(1.8f), pal.accentCyan);
          px = x;
          py = y;
        }
      }
      break;
    }
  case BuiltinDeviceKind.BITCRUSHER: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      drawPanelGrid(buf, c, pal);
      const int steps = cast(int)(2 + readPlugParam(plug, 0, 0.5f) * 14.0f);
      const int pad = dp(8);
      const int sw = c.width - pad * 2;
      const int sh = c.height - pad * 2;
      float px = c.left + pad;
      float py = c.bottom - pad;
      foreach (i; 1 .. 49) {
        const float t = cast(float)i / 48.0f;
        const float q = cast(float)(cast(int)(t * steps)) / cast(float)steps;
        const float x = c.left + pad + t * sw;
        const float y = c.bottom - pad - q * sh;
        drawThickLine(buf, px, py, x, y, dpf(2.0f), pal.accentOrange);
        px = x;
        py = y;
      }
      break;
    }
  case BuiltinDeviceKind.CHORUS:
  case BuiltinDeviceKind.VOCODEY:
  case BuiltinDeviceKind.STEREO_WIDTH:
  case BuiltinDeviceKind.EPIANO: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      drawPanelGrid(buf, c, pal);
      const float p0 = readPlugParam(plug, 0, 0.4f);
      const float p1 = readPlugParam(plug, 1, 0.5f);
      const int pad = dp(8);
      const float midY = c.middley;
      const int sw = c.width - pad * 2;
      const int sh = c.height - pad * 2;
      float px = c.left + pad;
      float py = midY;
      foreach (i; 1 .. 41) {
        const float t = cast(float)i / 40.0f;
        const float x = c.left + pad + t * sw;
        const float y = midY - sinf(t * 6.28318f * (1.5f + p0 * 4.0f)) * (0.25f + p1 * 0.2f) * sh;
        drawThickLine(buf, px, py, x, y, dpf(1.8f), pal.accentCyan);
        px = x;
        py = y;
      }
      break;
    }
  case BuiltinDeviceKind.ENVELOPE_SHAPER: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      drawPanelGrid(buf, c, pal);
      const int pad = dp(8);
      const int sw = c.width - pad * 2;
      const int sh = c.height - pad * 2;
      int nPts = 2 + cast(int)(readPlugParam(plug, 21, 0.14f) * 14.0f + 0.5f);
      if (nPts < 2)
        nPts = 2;
      if (nPts > 16)
        nPts = 16;
      float px = c.left + pad;
      float py = c.bottom - pad - readPlugParam(plug, 5, 0.5f) * sh;
      foreach (pi; 0 .. nPts) {
        const float x = c.left + pad + (cast(float)pi / cast(float)(nPts - 1)) * sw;
        const float y = c.bottom - pad - readPlugParam(plug, cast(uint)(5 + pi), 0.5f) * sh;
        if (pi > 0)
          drawThickLine(buf, px, py, x, y, dpf(2.0f), pal.accentAmber);
        drawFilledCircle(buf, x, y, dpf(4.0f), pal.accentOrange, pal.bgLcd);
        px = x;
        py = y;
      }
      break;
    }
  case BuiltinDeviceKind.ACID_BASS: {
      // Iconic Roland TB-303 charcoal header + brushed silver metallic chassis + vertical dividers + red LEDs
      drawThemeCard(buf, dl.auxRc, 0x0033363B, 0x001D2024, pal, 0.0f, false);
      drawAcidLed(buf, dl.auxRc.left + dp(10), dl.auxRc.middley, dpf(3.5f), true);
      drawVCenteredText(buf, uiFnt, dl.auxRc.left + dp(20), dl.auxRc,
          "ACID BASS  ·  TB-303 BASS LINE"d, 0x00EEEEEE);

      Rect c = dl.canvasRc;
      Rect kbd = dl.footerRc;
      drawVGradientRect(buf, c, 0x00EAEAEA, 0x00B5B5B5);
      drawVGradientRect(buf, kbd, 0x00E4E4E4, 0x00B0B0B0);
      buf.fillRect(Rect(c.left, c.top, c.right, c.top + dp(1)), 0x00222222);
      buf.fillRect(Rect(c.left, kbd.top, c.right, kbd.top + dp(2)), 0x00141414);

      // 8 vertical black partition lines across upper faceplate
      const int colW = c.width / 8;
      foreach (ci; 1 .. 8) {
        const int lx = c.left + ci * colW;
        buf.fillRect(Rect(lx, c.top, lx + dp(1), c.bottom), 0x002C2C2C);
      }

      // Col 0: WAVEFORM SAW / SQR switch + glowing Red LEDs
      drawCenteredText(buf, valFnt, Rect(c.left, c.top + dp(2), c.left + colW, c
          .top + dp(15)), "WAVEFORM"d, 0x00222222);
      if (dl.pills.length > 0) {
        const auto wfPill = dl.pills[0];
        const bool isSq = (readPlugParam(plug, 0, 0.0f) >= 0.5f);
        drawAcidLed(buf, wfPill.rc.left + dp(8), wfPill.rc.middley - dp(8), dpf(2.8f),
            !isSq);
        drawCenteredText(buf, valFnt, Rect(wfPill.rc.left, wfPill.rc.middley,
            wfPill.rc.left + dp(18), wfPill.rc.bottom), "SAW"d, 0x00222222);
        Rect swBtn = Rect(wfPill.rc.middlex - dp(9), wfPill.rc.middley - dp(9),
            wfPill.rc.middlex + dp(9), wfPill.rc.middley + dp(9));
        drawVGradientRect(buf, swBtn, isSq ? 0x00CCCCCC : 0x00F6F6F6, isSq ?
            0x00999999 : 0x00C8C8C8);
        buf.drawFrame(swBtn, 0x00333333, Rect(1, 1, 1, 1), 0);
        drawCenteredText(buf, uiFnt, swBtn, "~"d, 0x00111111);
        drawAcidLed(buf, wfPill.rc.right - dp(8), wfPill.rc.middley - dp(8), dpf(
            2.8f), isSq);
        drawCenteredText(buf, valFnt, Rect(wfPill.rc.right - dp(18),
            wfPill.rc.middley, wfPill.rc.right, wfPill.rc.bottom), "SQR"d, 0x00222222);
      }

      // Lower bay Left: OCTAVE ◀ ▶ buttons + digital Red LED C1..C5 box
      const int octVal = 1 + cast(int)(readPlugParam(plug, 8, 0.5f) * 4.0f + 0.5f);
      drawCenteredText(buf, valFnt, Rect(kbd.left + dp(4), kbd.top + dp(6),
          kbd.left + dp(76), kbd.top + dp(18)), "OCTAVE"d, 0x00222222);
      if (dl.pills.length >= 3) {
        foreach (oi; 1 .. 3) {
          Rect r = dl.pills[oi].rc;
          drawFilledCircle(buf, r.middlex, r.middley, r.width * 0.5f, 0x00DEDEDE, 0x00333333);
          drawCenteredText(buf, valFnt, r, oi == 1 ? "◀"d : "▶"d, 0x00111111);
        }
      }
      Rect dispRc = Rect(kbd.left + dp(20), kbd.top + dp(52), kbd.left + dp(60), kbd
          .top + dp(68));
      drawThemeCard(buf, dispRc, 0x001D1F22, 0x00444444, pal, 2.0f, true);
      drawCenteredText(buf, valFnt, dispRc, toUTF32(format("C%d", octVal)), 0x00FF3333);

      // Lower bay Center: 12 staggered TB-303 keys (dl.pills[3..15]) with glowing Red LEDs above each
      foreach (ki; 3 .. 15) {
        if (ki >= dl.pills.length)
          break;
        const auto kp = dl.pills[ki];
        const bool isBlk = kp.active;
        const bool lit = (ui.acidActiveNote == kp.uiTabVal);
        drawAcidLed(buf, kp.rc.middlex, kp.rc.top - dp(6), dpf(2.6f), lit);
        if (isBlk) {
          Rect boxRc = Rect(kp.rc.left - dp(3), kp.rc.top - dp(2),
              kp.rc.right + dp(3), kp.rc.bottom + dp(2));
          buf.fillRect(boxRc, 0x00101215);
          buf.drawFrame(boxRc, 0x00444444, Rect(1, 1, 1, 1), 0);
        }
        drawVGradientRect(buf, kp.rc, lit ? 0x00D8D8D8 : 0x00FAFAFA, lit ? 0x00909090 : 0x00C6C6C6);
        buf.drawFrame(kp.rc, 0x00444444, Rect(1, 1, 1, 1), 0);
        drawCenteredText(buf, valFnt, Rect(kp.rc.left - dp(4),
            kp.rc.bottom + dp(1), kp.rc.right + dp(4), kp.rc.bottom + dp(12)),
            toUTF32(kp.label), 0x00222222);
      }

      // Lower bay Right: circular SLIDE & ACCENT brushed buttons + glowing Red LEDs
      foreach (mi; 15 .. 17) {
        if (mi >= dl.pills.length)
          break;
        const auto mp = dl.pills[mi];
        drawAcidLed(buf, mp.rc.middlex, mp.rc.top - dp(8), dpf(2.6f), mp.active);
        drawFilledCircle(buf, mp.rc.middlex, mp.rc.middley, mp.rc.width * 0.5f,
            mp.active ? 0x00D6D6D6 : 0x00EBEBEB, 0x00333333);
        drawCenteredText(buf, valFnt, Rect(mp.rc.left - dp(8),
            mp.rc.bottom + dp(2), mp.rc.right + dp(8), mp.rc.bottom + dp(13)),
            toUTF32(mp.label), 0x00222222);
      }
      break;
    }
  case BuiltinDeviceKind.THREE_OSC: {
      drawThemeCard(buf, dl.canvasRc, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const int pad = dp(5);
      const int w = dl.canvasRc.width - pad * 2;
      const int h = dl.canvasRc.height - pad * 2;
      const float a = readPlugParam(plug, 15, 0.0f) * 0.25f;
      const float d = readPlugParam(plug, 16, 0.2f) * 0.25f;
      const float s = readPlugParam(plug, 17, 0.7f);
      const float r = readPlugParam(plug, 18, 0.3f) * 0.3f;
      const float x0 = dl.canvasRc.left + pad;
      const float yBase = dl.canvasRc.bottom - pad;
      const float x1 = x0 + a * w;
      const float x2 = x1 + d * w;
      const float x3 = dl.canvasRc.right - pad - r * w;
      const float x4 = dl.canvasRc.right - pad;
      drawThickLine(buf, x0, yBase, x1, dl.canvasRc.top + pad, dpf(1.6f), pal
          .accentGreen);
      drawThickLine(buf, x1, dl.canvasRc.top + pad, x2, yBase - s * h, dpf(1.6f), pal
          .accentGreen);
      drawThickLine(buf, x2, yBase - s * h, x3, yBase - s * h, dpf(1.6f), pal
          .accentGreen);
      drawThickLine(buf, x3, yBase - s * h, x4, yBase, dpf(1.6f), pal.accentGreen);
      break;
    }
  case BuiltinDeviceKind.SAMPLER: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const int pad = dp(6);
      const int sw = c.width - pad * 2;
      const int sh = c.height - pad * 2;
      if (plug.sampleWaveform.length > 0 && sw > dp(16)) {
        const int midY = c.middley;
        const int halfH = sh / 2;
        foreach (i, amp; plug.sampleWaveform) {
          const int px = c.left + pad + cast(int)(cast(double)i / plug
              .sampleWaveform.length * sw);
          float a = amp < 0.0f ? -amp : amp;
          if (a > 1.0f)
            a = 1.0f;
          int bh = cast(int)(a * halfH);
          if (bh < 1)
            bh = 1;
          buf.fillRect(Rect(px, midY - bh, px + dp(2), midY + bh), pal.accentCyan);
        }
      }
      const int sx = c.left + pad + cast(int)(readPlugParam(plug, 0, 0.0f) * sw);
      const int ex = c.left + pad + cast(int)(readPlugParam(plug, 1, 1.0f) * sw);
      buf.fillRect(Rect(sx, c.top + dp(3), sx + dp(2), c.bottom - dp(3)), pal
          .accentGreen);
      buf.fillRect(Rect(ex - dp(2), c.top + dp(3), ex, c.bottom - dp(3)), pal
          .accentRed);
      const string sLbl = plug.sampleName.length > 0 ? format("%s (%.2fs)",
          plug.sampleName, plug.sampleDurationSec) : "Sampler (No Sample Loaded)";
      drawVCenteredText(buf, uiFnt, c.left + dp(8), Rect(c.left, c.top + dp(2),
          c.right - dp(84), c.top + dp(20)), toUTF32(truncateLabel(sLbl, 28)), pal
          .fgText);
      break;
    }
  case BuiltinDeviceKind.ORGAN: {
      drawThemeCard(buf, dl.canvasRc, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      static immutable string[9] DB_NAMES = [
        "16'", "5⅓", "8'", "4'", "2⅔", "2'", "1⅗", "1⅓", "1'"
      ];
      foreach (d, rc; dl.drawbarRcs) {
        const float v = readPlugParam(plug, cast(uint)d, d == 2 ? 1.0f : 0.0f);
        drawThemeCard(buf, rc, pal.bgWindow, pal.borderSubtle, pal, 1.5f, true);
        const int fh = cast(int)(v * (rc.height - dp(4)));
        const uint barCol = (d == 0 || d == 2 || d == 3 || d == 5 || d == 8) ? pal
          .fgText : pal.accentAmber;
        if (fh > 0) {
          buf.fillRect(Rect(rc.left + dp(2), rc.top + dp(2), rc.right - dp(2), rc
              .top + dp(2) + fh), barCol);
        }
        drawCenteredText(buf, valFnt, Rect(rc.left, rc.bottom + dp(2), rc.right,
            rc.bottom + dp(16)), toUTF32(DB_NAMES[d]), pal.fgMuted);
      }
      break;
    }
  case BuiltinDeviceKind.DRUM_MACHINE: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const int sp = (ui.drumSelPad >= 0 && ui.drumSelPad < cast(int)plug
          .drumPads.length) ? ui.drumSelPad : 0;
      const string sName = (sp < cast(int)plug.drumPads.length &&
          plug.drumPads[sp].sampleName.length > 0) ? plug.drumPads[sp]
        .sampleName
        : format("Pad %d — Left-click trigger, Right-click load .wav",
            sp + 1);
      drawVCenteredText(buf, uiFnt, c.left + dp(8), c,
          toUTF32(truncateLabel(sName, 44)), pal.accentCyan);
      foreach (pi, prc; dl.padRcs) {
        bool active = false;
        string lbl = format("Pad %d", pi + 1);
        if (pi < plug.drumPads.length) {
          active = plug.drumPads[pi].activeFlash;
          if (plug.drumPads[pi].sampleName.length > 0)
            lbl = truncateLabel(plug.drumPads[pi].sampleName, 8);
        }
        const bool sel = (cast(int)pi == ui.drumSelPad);
        drawThemeCard(buf, prc, active ? pal.accentOrange : (sel ?
            pal.bgControlHover
            : pal.bgControl), sel ? pal.accentOrange : pal.borderStrong, pal,
            2.0f, pal.retroBevel);
        drawCenteredText(buf, uiFnt, prc, toUTF32(lbl), active ? pal.bgWindow : pal
            .fgText);
      }
      break;
    }
  case BuiltinDeviceKind.DR8_MODULE: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const float p0 = readPlugParam(plug, 0, 0.5f);
      const float p1 = readPlugParam(plug, 1, 0.4f);
      const int pad = dp(8);
      const int sw = c.width - pad * 2;
      const int sh = c.height - pad * 2;
      const float midY = c.middley;
      float px = c.left + pad;
      float py = midY;
      foreach (i; 1 .. 49) {
        const float t = cast(float)i / 48.0f;
        const float env = expf(-t * (2.5f + (1.0f - p1) * 6.0f));
        const float y = midY - env * sinf(t * 28.0f * (0.4f + p0)) * sh * 0.44f;
        const float x = c.left + pad + t * sw;
        drawThickLine(buf, px, py, x, y, dpf(1.8f), pal.accentOrange);
        px = x;
        py = y;
      }
      break;
    }
  case BuiltinDeviceKind.FILM: {
      // Sytrus-inspired FilM purple header + envelope curve + right 6x12 purple/gold FM Matrix
      drawThemeCard(buf, dl.auxRc, 0x004A3570, 0x007B5EA7, pal, 2.0f, false);
      drawVCenteredText(buf, uiFnt, dl.auxRc.left + dp(8), dl.auxRc,
          "FilM  ·  6-OP FM SYNTHESIZER"d, 0x00D4B84A);

      // Envelope visual box for selected tab (OP1 Gain / OP1..6 / F1..3)
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const int pad = dp(6);
      const int ew = c.width - pad * 2;
      const int eh = c.height - pad * 2;
      if (ew > dp(12) && eh > dp(10)) {
        uint envBase = 4;
        uint envCol = 0x00D4B84A;
        if (ui.filmTab >= 1 && ui.filmTab <= 6) {
          envBase = cast(uint)((ui.filmTab - 1) * 38 + 4);
        } else if (ui.filmTab >= 7) {
          envBase = cast(uint)(228 + (ui.filmTab - 7) * 12 + 3);
          envCol = 0x009B7EC8;
        }
        const float a = readPlugParam(plug, envBase + 0, 0.0f) * 0.25f;
        const float d = readPlugParam(plug, envBase + 1, 0.2f) * 0.25f;
        const float s = readPlugParam(plug, envBase + 2, 0.7f);
        const float r = readPlugParam(plug, envBase + 3, 0.3f) * 0.3f;
        const float x0 = c.left + pad;
        const float yBase = c.bottom - pad;
        const float x1 = x0 + a * ew;
        const float x2 = x1 + d * ew;
        const float x3 = c.right - pad - r * ew;
        const float x4 = c.right - pad;
        drawThickLine(buf, x0, yBase, x1, c.top + pad, dpf(1.8f), envCol);
        drawThickLine(buf, x1, c.top + pad, x2, yBase - s * eh, dpf(1.8f), envCol);
        drawThickLine(buf, x2, yBase - s * eh, x3, yBase - s * eh, dpf(1.8f), envCol);
        drawThickLine(buf, x3, yBase - s * eh, x4, yBase, dpf(1.8f), envCol);
      }

      // Right 6x12 Modulation Matrix card
      Rect mRc = dl.filmMatrixRc;
      drawThemeCard(buf, mRc, 0x001E1E24, 0x00333340, pal, 2.0f, true);
      drawVCenteredText(buf, uiFnt, mRc.left + dp(8), Rect(mRc.left,
          mRc.top + dp(2), mRc.right - dp(50), mRc.top + dp(18)), "MATRIX (FM / RM)"d,
          0x00D4B84A);

      static immutable string[12] MCOLS = [
        "1", "2", "3", "4", "5", "6", "F1", "F2", "F3", "P", "FX", "O"
      ];
      static immutable uint[6] OP_COLS = [
        0x00D4B84A, 0x00C9A83E, 0x00BE9832, 0x009B7EC8, 0x008B6EB8, 0x007B5EA7
      ];
      foreach (col; 0 .. 12) {
        Rect topCell = dl.filmMatrixCells[col];
        const uint cc = (col < 6) ? 0x00D4B84A : (col < 9 ? 0x009B7EC8 : 0x009096A4);
        drawCenteredText(buf, valFnt, Rect(topCell.left, topCell.top - dp(15),
            topCell.right, topCell.top - dp(1)), toUTF32(MCOLS[col]), cc);
      }
      foreach (r; 0 .. 6) {
        Rect firstCell = dl.filmMatrixCells[r * 12];
        drawCenteredText(buf, valFnt, Rect(mRc.left + dp(2), firstCell.top,
            firstCell.left - dp(2), firstCell.bottom), toUTF32(format("%d", r + 1)),
            OP_COLS[r]);
        foreach (col; 0 .. 12) {
          const int idx = r * 12 + col;
          Rect cr = dl.filmMatrixCells[idx];
          buf.fillRect(cr, 0x0024242C);
          const uint pid = cast(uint)(272 + idx);
          const float defV = (col == 11 && r == 0) ? 0.75f : 0.5f;
          const float val = readPlugParam(plug, pid, defV);
          const float diff = val - 0.5f;
          const bool inactive = (diff > -0.015f && diff < 0.015f);
          const float rDial = (cr.width < cr.height ? cr.width : cr.height) * 0.36f;
          if (rDial >= 3.0f) {
            const uint arcCol = (r == col) ? OP_COLS[r] : (col < 6 ? 0x00D4B84A : 0x009B7EC8);
            if (inactive) {
              drawOpenArc(buf, cr.middlex, cr.middley, rDial, 135.0f, 405.0f,
                  dpf(1.3f), 0x00444450);
            } else {
              drawOpenArc(buf, cr.middlex, cr.middley, rDial, 135.0f, 405.0f,
                  dpf(1.3f), 0x002E2E38);
              drawOpenArc(buf, cr.middlex, cr.middley, rDial, 135.0f,
                  135.0f + 270.0f * val, dpf(1.6f), arcCol);
            }
          }
        }
      }
      break;
    }
  case BuiltinDeviceKind.SYLPH: {
      const uint CYAN = 0x0032B8FF;
      const uint AMBER = 0x00F59E0B;
      const uint GREEN = 0x0034D399;
      const uint PURPLE = 0x00A78BFA;

      // Deck cards across Part A, Center FX/Punch, Part B
      const int fxSel = (ui.sylphFxTab >= 0 && ui.sylphFxTab < 4) ? ui.sylphFxTab : 0;
      static immutable string[4] FX_TITLES = [
        "FX: DISTORTION", "FX: STEREO CHORUS", "FX: PING-PONG DELAY",
        "FX: HALL REVERB",
      ];
      static immutable uint[4] FX_COLS = [AMBER, CYAN, GREEN, PURPLE];
      static immutable string[9] DECK_TITLES = [
        "OSC A · SUPERSAW", "FILTER A · INDEPENDENT",
        "ENVELOPES A (AMP | FILT)",
        "OSC B · HARMONIC", "FILTER B · INDEPENDENT",
        "ENVELOPES B (AMP | FILT)",
        "SERIAL MASTER FX CHAIN", "", "SUB · KICK PUNCH · MOD · OUT",
      ];
      static immutable uint[9] DECK_COLS = [
        CYAN, CYAN, GREEN, AMBER, AMBER, PURPLE, CYAN, CYAN, AMBER,
      ];
      foreach (di; 0 .. 9) {
        Rect r = dl.sylphDeckRcs[di];
        const uint dCol = (di == 7) ? FX_COLS[fxSel] : DECK_COLS[di];
        const string dLbl = (di == 7) ? FX_TITLES[fxSel] : DECK_TITLES[di];
        drawThemeCard(buf, r, 0x001D222C, 0x00323948, pal, 2.0f, false);
        buf.fillRect(Rect(r.left + dp(2), r.top + dp(1), r.right - dp(2), r.top + dp(
            3)), dCol);
        drawVCenteredText(buf, valFnt, r.left + dp(6), Rect(r.left,
            r.top + dp(3), r.right, r.top + dp(16)), toUTF32(dLbl), dCol);
      }

      // 4 live ADSR silhouette graphs right above the visual vertical fader bars
      static immutable uint[4] ENV_PIDS = [13u, 17u, 34u, 38u];
      static immutable uint[4] ENV_COLS = [GREEN, CYAN, PURPLE, AMBER];
      foreach (ei; 0 .. 4) {
        Rect cr = dl.sylphEnvCurveRcs[ei];
        drawThemeCard(buf, cr, 0x000D1118, 0x002B364A, pal, 1.5f, true);
        const int pad = dp(3);
        const int cw = cr.width - pad * 2;
        const int ch = cr.height - pad * 2;
        if (cw > dp(8) && ch > dp(6)) {
          const uint eb = ENV_PIDS[ei];
          const float a = readPlugParam(plug, eb + 0, 0.15f) * 0.26f;
          const float d = readPlugParam(plug, eb + 1, 0.70f) * 0.28f;
          const float s = readPlugParam(plug, eb + 2, 0.72f);
          const float r = readPlugParam(plug, eb + 3, 0.56f) * 0.28f;
          const float x0 = cr.left + pad;
          const float yB = cr.bottom - pad;
          const float x1 = x0 + a * cw;
          const float x2 = x1 + d * cw;
          const float x3 = cr.right - pad - r * cw;
          const float x4 = cr.right - pad;
          const float yS = yB - s * ch;
          drawThickLine(buf, x0, yB, x1, cr.top + pad, dpf(1.5f), ENV_COLS[ei]);
          drawThickLine(buf, x1, cr.top + pad, x2, yS, dpf(1.5f), ENV_COLS[ei]);
          drawThickLine(buf, x2, yS, x3, yS, dpf(1.5f), ENV_COLS[ei]);
          drawThickLine(buf, x3, yS, x4, yB, dpf(1.5f), ENV_COLS[ei]);
        }
      }

      // Central Serial FX Chain LCD (`dl.canvasRc`)
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, 0x000D1118, 0x002B364A, pal, 2.0f, true);
      static immutable string[4] FX_SHORT = ["DIST", "CHOR", "DLY", "REV"];
      foreach (fi; 0 .. 4) {
        Rect blk = dl.sylphFxBlockRcs[fi];
        Rect pwr = dl.sylphFxPowerRcs[fi];
        const bool on = readPlugParam(plug, cast(uint)(55 + fi * 4), fi > 0 ? 1.0f : 0.0f) >= 0.5f;
        const bool sel = (fi == fxSel);
        const uint fCol = FX_COLS[fi];
        const uint blkBg = sel ? 0x00222A3A : (on ? 0x00171C26 : 0x0012151C);
        drawThemeCard(buf, blk, blkBg, sel ? fCol : (on ? 0x003B475C : 0x00242A36),
            pal, 2.0f, false);
        if (sel) {
          buf.fillRect(Rect(blk.left + dp(2), blk.bottom - dp(3),
              blk.right - dp(2), blk.bottom - dp(1)), fCol);
        }
        drawCenteredText(buf, valFnt, Rect(blk.left, blk.top + dp(12),
            blk.right, blk.bottom - dp(4)), toUTF32(FX_SHORT[fi]), on ? (sel ?
  fCol : 0x00E2E8F0) : 0x0064748B);
        // Clickable LED Power dot on top-right corner of each block
        drawFilledCircle(buf, pwr.middlex, pwr.middley, dpf(4.0f), on ? fCol
            : 0x00262D3B, on ? 0x00FFFFFF : 0x00475569);
        drawVCenteredText(buf, valFnt, blk.left + dp(4), Rect(blk.left,
            blk.top + dp(1), pwr.left, blk.top + dp(15)), on ? "ON"d : "OFF"d, on ? fCol : 0x00475569);

        // Glowing series signal arrow `->` to next FX block
        if (fi < 3) {
          Rect nxt = dl.sylphFxBlockRcs[fi + 1];
          const float ay = blk.middley;
          drawThickLine(buf, blk.right + dp(1), ay, nxt.left - dp(2), ay,
              dpf(1.5f), on ? fCol : 0x00334155);
          drawThickLine(buf, nxt.left - dp(5), ay - dp(3), nxt.left - dp(2), ay,
              dpf(1.4f), on ? fCol : 0x00334155);
          drawThickLine(buf, nxt.left - dp(5), ay + dp(3), nxt.left - dp(2), ay,
              dpf(1.4f), on ? fCol : 0x00334155);
        }
      }

      // Bottom area inside FX Chain LCD: Live visualizer for currently selected FX
      Rect viz = Rect(c.left + dp(6), dl.sylphFxBlockRcs[0].bottom + dp(6),
          c.right - dp(6), c.bottom - dp(5));
      if (viz.width > dp(24) && viz.height > dp(16)) {
        drawPanelGrid(buf, viz, pal);
        const uint vCol = FX_COLS[fxSel];
        const float midY = viz.middley;
        if (fxSel == 0) {
          // Waveshaper transfer curve (Distortion)
          const float drv = 1.0f + readPlugParam(plug, 56, 0.2f) * 6.5f;
          const float inv = 1.0f / tanhf(drv);
          float px = viz.left + dp(4);
          float py = viz.bottom - dp(4);
          const float vw = viz.width - dp(8);
          const float vh = viz.height - dp(8);
          foreach (i; 1 .. 33) {
            const float t = cast(float)i / 32.0f;
            const float xIn = t * 2.0f - 1.0f;
            const float yOut = tanhf(xIn * drv) * inv;
            const float nx = viz.left + dp(4) + t * vw;
            const float ny = midY - yOut * vh * 0.45f;
            drawThickLine(buf, px, py, nx, ny, dpf(1.8f), vCol);
            px = nx;
            py = ny;
          }
        } else if (fxSel == 1) {
          // Dual LFO modulated stereo phase waves (Chorus)
          const float dep = 0.2f + readPlugParam(plug, 61, 0.55f) * 0.7f;
          const float vw = viz.width - dp(8);
          const float vh = viz.height - dp(8);
          float px = viz.left + dp(4);
          float pyL = midY;
          float pyR = midY;
          foreach (i; 1 .. 41) {
            const float t = cast(float)i / 40.0f;
            const float nx = viz.left + dp(4) + t * vw;
            const float yL = midY - sinf(t * 14.0f) * dep * vh * 0.42f;
            const float yR = midY - sinf(t * 14.0f + 1.3f) * dep * vh * 0.42f;
            drawThickLine(buf, px, pyL, nx, yL, dpf(1.5f), CYAN);
            drawThickLine(buf, px, pyR, nx, yR, dpf(1.5f), AMBER);
            px = nx;
            pyL = yL;
            pyR = yR;
          }
        } else if (fxSel == 2) {
          // Ping-pong echo tap bars (Delay)
          const float fb = readPlugParam(plug, 65, 0.44f);
          const float tNorm = readPlugParam(plug, 64, 0.58f);
          const int stepPx = dp(10) + cast(int)(tNorm * dp(18));
          float amp = 0.90f;
          int tx = viz.left + dp(8);
          int tap = 0;
          while (tx + dp(4) < viz.right - dp(6) && tap < 10) {
            const int bh = cast(int)(amp * (viz.height - dp(12)) * 0.48f);
            const uint tc = (tap % 2 == 0) ? GREEN : CYAN;
            buf.fillRect(Rect(tx, cast(int)midY - bh, tx + dp(4), cast(int)midY + bh),
                tc);
            amp *= (0.35f + fb * 0.52f);
            tx += stepPx;
            tap++;
          }
        } else {
          // Exponential reverberant decay silhouette (Reverb)
          const float sz = readPlugParam(plug, 68, 0.72f);
          const float dmp = readPlugParam(plug, 69, 0.28f);
          const float vw = viz.width - dp(8);
          const float vh = viz.height - dp(8);
          float px = viz.left + dp(4);
          float py = viz.top + dp(6);
          foreach (i; 1 .. 33) {
            const float t = cast(float)i / 32.0f;
            const float env = expf(-t * (5.2f - sz * 3.4f + dmp * 1.2f));
            const float nx = viz.left + dp(4) + t * vw;
            const float ny = viz.bottom - dp(4) - env * vh * 0.85f;
            drawThickLine(buf, px, py, nx, ny, dpf(1.8f), vCol);
            px = nx;
            py = ny;
          }
        }
      }
      break;
    }
  case BuiltinDeviceKind.AUX: {
      Rect c = dl.canvasRc;
      drawThemeCard(buf, c, pal.bgLcd, pal.borderSubtle, pal, 2.0f, true);
      const float g = readPlugParam(plug, 0, 0.8f);
      drawCenteredText(buf, uiFnt, c, toUTF32(format("AUX SEND  ·  %.0f%%",
          g * 100.0f)), pal.accentCyan);
      break;
    }
  default:
    break;
  }

  // 2. Render interactive pill buttons cleanly
  foreach (const ref pl; dl.pills) {
    if (!pl.visibleButton)
      continue;
    const uint fill = pl.active ? pl.activeColor : pal.bgControl;
    drawThemeCard(buf, pl.rc, fill, pal.borderStrong, pal, 2.0f, pal.retroBevel);
    drawCenteredText(buf, uiFnt, pl.rc, toUTF32(pl.label), pl.active ?
        contrastTextColor(fill) : pal.fgText);
  }

  // 3. Render rotary knobs cleanly (TB-303 silver brushed dials vs standard FlPalette knobs)
  foreach (const ref kn; dl.knobs) {
    const float val = readPlugParam(plug, kn.paramId, kn.defaultVal);
    if (kn.knobStyle == 1) {
      drawTb303Knob(buf, kn.cx, kn.cy, kn.r, val);
    } else {
      drawRotaryKnob(buf, kn.cx, kn.cy, kn.r, val, pal.bgLcd, kn.accent,
          pal.bgControl, pal.borderSubtle, false);
    }
    const uint lblCol = kn.lightBg ? 0x001E2126 : pal.fgText;
    const uint valCol = kn.lightBg ? 0x00333840 : pal.fgMuted;
    drawCenteredText(buf, valFnt, Rect(kn.cellRc.left, kn.cy + kn.r + dp(2),
        kn.cellRc.right, kn.cy + kn.r + dp(15)), toUTF32(truncateLabel(kn
        .formattedVal, 9)), valCol);
    drawCenteredText(buf, valFnt, Rect(kn.cellRc.left, kn.cy - kn.r - dp(13),
        kn.cellRc.right, kn.cy - kn.r - dp(1)), toUTF32(truncateLabel(kn.label, 9)),
        lblCol);
  }

  // 4. Render vertical visual fader bars (e.g. Sylph ADSR Amp/Filter bars)
  foreach (const ref sl; dl.sliders) {
    const float v = readPlugParam(plug, sl.paramId, sl.defaultVal);
    const int midX = sl.rc.middlex;
    const int trkW = dp(6);
    Rect trk = Rect(midX - trkW / 2, sl.rc.top + dp(2), midX + trkW / 2, sl.rc
        .bottom - dp(2));
    drawThemeCard(buf, trk, pal.bgLcd, pal.borderSubtle, pal, 1.5f, true);
    const int th = trk.height;
    const int fh = cast(int)(v * th);
    if (fh > 0) {
      buf.fillRect(Rect(trk.left + dp(1), trk.bottom - fh, trk.right - dp(1), trk
          .bottom), sl.accent);
    }
    const int thumbY = trk.bottom - fh;
    const int tw = (sl.rc.width > dp(14)) ? dp(14) : sl.rc.width;
    Rect thumbRc = Rect(midX - tw / 2, thumbY - dp(3), midX + tw / 2, thumbY + dp(
        3));
    drawThemeCard(buf, thumbRc, 0x00CBD5E1, 0x001E293B, pal, 1.0f, false);
    buf.fillRect(Rect(thumbRc.left + dp(2), thumbY, thumbRc.right - dp(2), thumbY + dp(
        1)), 0x000F172A);
    drawCenteredText(buf, valFnt, Rect(sl.rc.left - dp(2), sl.rc.bottom + dp(1),
        sl.rc.right + dp(2), sl.rc.bottom + dp(13)), toUTF32(sl.label), pal
        .fgText);
  }
}

/// Handles mouse interactions inside custom builtin device UIs; returns true if handled.
bool handleBuiltinDeviceMouse(MouseEvent event, Rect bodyRc,
    TransportModel* model, PluginSlotState* plug,
    ref BuiltinDeviceUiState ui,
    int trkIdx, void delegate(int trackIdx, int pluginIdx, uint paramId,
      float value) onSetParam,
    void delegate(int trackIdx, uint notePitch) onTriggerPad,
    void delegate(int trackIdx,
      int pluginIdx, int padIdx) onPromptPadSample, void delegate(
      int trackIdx, int pluginIdx) onLoadSample, void delegate(string msg) onHint) {
  if (!plug)
    return false;
  const pal = model ? model.palette() : getFlPalette();
  const dl = buildBuiltinDeviceLayout(bodyRc, plug, ui, pal);
  if (dl.kind == BuiltinDeviceKind.GENERIC)
    return false;

  void emitParam(uint pid, float val, float defVal, string lbl) {
    const float nv = clamp01(val);
    writePlugParam(plug, pid, nv, defVal, lbl);
    if (onSetParam !is null)
      onSetParam(trkIdx, plug.pluginIndex, pid, nv);
    if (onHint !is null)
      onHint(format("%s — %s: %d%%", plug.name, lbl, cast(int)(nv * 100.0f + 0.5f)));
  }

  if (event.action == MouseAction.Wheel) {
    if (dl.kind == BuiltinDeviceKind.EQ && (dl.canvasRc.isPointInside(event.x,
        event.y) || dl.footerRc.isPointInside(event.x, event.y))) {
      const int b = (ui.eqSelectedBand >= 0 && ui.eqSelectedBand < 8) ? ui
        .eqSelectedBand : 0;
      const float curQ = readPlugParam(plug, cast(uint)(24 + b), 0.425f);
      const float dq = (event.wheelDelta > 0) ? 0.04f : -0.04f;
      emitParam(cast(uint)(24 + b), curQ + dq, 0.425f, format("Band %d Q", b + 1));
      return true;
    }
    return false;
  }

  if (event.action == MouseAction.ButtonDown) {
    // 1. Check pills first
    foreach (const ref pl; dl.pills) {
      if (!pl.rc.isPointInside(event.x, event.y))
        continue;
      if (pl.actionKind == 0) {
        emitParam(pl.paramId, pl.targetVal, 0.5f, pl.label);
      } else if (pl.actionKind == 1) {
        emitParam(pl.paramId, pl.active ? 0.0f : 1.0f, pl.active ? 1.0f : 0.0f, pl
            .label);
      } else if (pl.actionKind == 2) {
        if (dl.kind == BuiltinDeviceKind.EQ) {
          if (ui.eqSelectedBand == pl.uiTabVal && event.doubleClick) {
            const int curT = eqNormToTypeIdx(readPlugParam(plug, cast(uint)pl
                .uiTabVal, 0.0f));
            emitParam(cast(uint)pl.uiTabVal, eqTypeIdxToNorm((curT + 1) % 6), 0.0f,
                "Band Type");
          }
          ui.eqSelectedBand = pl.uiTabVal;
        } else if (dl.kind == BuiltinDeviceKind.HOTT) {
          ui.hottTab = pl.uiTabVal;
        } else if (dl.kind == BuiltinDeviceKind.MAXIM) {
          ui.maximBandTab = pl.uiTabVal;
        } else if (dl.kind == BuiltinDeviceKind.FILM) {
          ui.filmTab = pl.uiTabVal;
        }
      } else if (pl.actionKind == 3) {
        if (dl.kind == BuiltinDeviceKind.ACID_BASS) {
          ui.acidActiveNote = pl.uiTabVal;
        }
        if (onTriggerPad !is null)
          onTriggerPad(trkIdx, pl.paramId);
      } else if (pl.actionKind == 4) {
        if (onLoadSample !is null)
          onLoadSample(trkIdx, plug.pluginIndex);
      } else if (pl.actionKind == 5) {
        const float cur = readPlugParam(plug, pl.paramId, 0.5f);
        emitParam(pl.paramId, cur + pl.targetVal, 0.5f, pl.label);
      } else if (pl.actionKind == 6 && dl.kind == BuiltinDeviceKind.SYLPH) {
        const int pidx = pl.uiTabVal;
        if (pidx >= 0 && pidx < 6) {
          ui.sylphPresetIdx = pidx;
          foreach (uint pid; 0 .. 71) {
            const float pv = SYLPH_PRESETS[pidx][pid];
            writePlugParam(plug, pid, pv, SYLPH_PRESETS[0][pid]);
            if (onSetParam !is null)
              onSetParam(trkIdx, plug.pluginIndex, pid, pv);
          }
          if (onHint !is null)
            onHint(format("Sylph — Loaded Preset: %s", SYLPH_PRESET_NAMES[pidx]));
        }
      }
      return true;
    }

    // 2. Rotary knobs
    foreach (const ref kn; dl.knobs) {
      if (!kn.cellRc.isPointInside(event.x, event.y))
        continue;
      if (event.button == MouseButton.Right) {
        emitParam(kn.paramId, kn.defaultVal, kn.defaultVal, kn.label);
      } else {
        ui.activeDragParamId = cast(int)kn.paramId;
        ui.dragStartY = event.y;
        ui.dragStartVal = readPlugParam(plug, kn.paramId, kn.defaultVal);
        ui.dragDefaultVal = kn.defaultVal;
        ui.dragIsVerticalSlider = false;
        ui.dragInvertVertical = false;
      }
      return true;
    }

    // 2b. Vertical visual fader bars (Sylph ADSR sliders: bottom=0 -> top=1)
    foreach (const ref sl; dl.sliders) {
      Rect hitRc = Rect(sl.rc.left - dp(2), sl.rc.top - dp(4),
          sl.rc.right + dp(2), sl.rc.bottom + dp(4));
      if (!hitRc.isPointInside(event.x, event.y))
        continue;
      if (event.button == MouseButton.Right) {
        emitParam(sl.paramId, sl.defaultVal, sl.defaultVal, sl.label);
      } else {
        const float raw = clamp01(cast(float)(event.y - sl.rc.top) / cast(float)(
            sl.rc.height > 1 ? sl.rc.height : 1));
        const float v = 1.0f - raw;
        emitParam(sl.paramId, v, sl.defaultVal, sl.label);
        ui.activeDragParamId = cast(int)sl.paramId;
        ui.activeDragSliderRc = sl.rc;
        ui.dragDefaultVal = sl.defaultVal;
        ui.dragIsVerticalSlider = true;
        ui.dragInvertVertical = true;
      }
      return true;
    }

    // 2c. Sylph Serial FX Chain LCD block select + individual power LED toggle
    if (dl.kind == BuiltinDeviceKind.SYLPH && dl.canvasRc.isPointInside(event.x, event
        .y)) {
      foreach (fi; 0 .. 4) {
        if (dl.sylphFxPowerRcs[fi].isPointInside(event.x, event.y)) {
          const uint pPid = cast(uint)(55 + fi * 4);
          const bool curOn = readPlugParam(plug, pPid, fi > 0 ? 1.0f : 0.0f) >= 0.5f;
          emitParam(pPid, curOn ? 0.0f : 1.0f, fi > 0 ? 1.0f : 0.0f, "FX Power");
          return true;
        }
        if (dl.sylphFxBlockRcs[fi].isPointInside(event.x, event.y)) {
          ui.sylphFxTab = fi;
          return true;
        }
      }
    }

    // 3. Organ drawbars
    if (dl.kind == BuiltinDeviceKind.ORGAN) {
      foreach (d, rc; dl.drawbarRcs) {
        if (!rc.isPointInside(event.x, event.y))
          continue;
        const float v = clamp01(
            cast(float)(event.y - rc.top) / cast(float)(rc.height > 1 ? rc.height : 1));
        emitParam(cast(uint)d, v, d == 2 ? 1.0f : 0.0f, format("Drawbar %d", cast(
            int)d + 1));
        ui.activeDragParamId = cast(int)d;
        ui.activeDragSliderRc = rc;
        ui.dragIsVerticalSlider = true;
        return true;
      }
    }

    // 4. Drum Machine pads
    if (dl.kind == BuiltinDeviceKind.DRUM_MACHINE) {
      foreach (pi, prc; dl.padRcs) {
        if (!prc.isPointInside(event.x, event.y))
          continue;
        ui.drumSelPad = cast(int)pi;
        if (event.button == MouseButton.Right) {
          if (onPromptPadSample !is null)
            onPromptPadSample(trkIdx, plug.pluginIndex, cast(int)pi);
        } else {
          uint notePitch = cast(uint)(36 + pi);
          if (pi < plug.drumPads.length) {
            plug.drumPads[pi].activeFlash = true;
            if (plug.drumPads[pi].triggerNote > 0)
              notePitch = plug.drumPads[pi].triggerNote;
          }
          if (onTriggerPad !is null)
            onTriggerPad(trkIdx, notePitch);
        }
        return true;
      }
    }

    // 4b. HOTT interactive 3-band OTT thresholds & Att/Rel drag
    if (dl.kind == BuiltinDeviceKind.HOTT) {
      static immutable uint[3] DOWN_PIDS = [11u, 10u, 9u];
      static immutable uint[3] UP_PIDS = [14u, 13u, 12u];
      static immutable uint[3] ATK_PIDS = [19u, 18u, 17u];
      static immutable uint[3] REL_PIDS = [22u, 21u, 20u];
      static immutable uint[3] DRAT_PIDS = [25u, 24u, 23u];
      static immutable uint[3] URAT_PIDS = [28u, 27u, 26u];
      static immutable float[3] DEF_DN = [
        (-35.5f + 60.0f) / 60.0f, (-30.2f + 60.0f) / 60.0f, (-33.8f + 60.0f) / 60.0f
      ];
      static immutable float[3] DEF_UP = [
        (-40.8f + 60.0f) / 72.0f, (-41.8f + 60.0f) / 72.0f, (-40.8f + 60.0f) / 72.0f
      ];
      foreach (bi; 0 .. 3) {
        Rect barRc = dl.hottBarRcs[bi];
        Rect inspRc = dl.hottInspectorRcs[bi];
        if (barRc.isPointInside(event.x, event.y)) {
          ui.hottDragBand = bi;
          const float downDb = readPlugParam(plug, DOWN_PIDS[bi], DEF_DN[bi]) * 60.0f - 60.0f;
          const float upDb = readPlugParam(plug, UP_PIDS[bi], DEF_UP[bi]) * 72.0f - 60.0f;
          if (ui.hottTab == 1) {
            ui.hottDragMode = 1;
          } else if (ui.hottTab == 2) {
            ui.hottDragMode = 0;
          } else {
            const int upX = barRc.left + cast(int)(normalizeDb(upDb, -80.0f, 0.0f) * barRc
                .width);
            const int downX = barRc.left + cast(int)(normalizeDb(downDb,
                -80.0f, 0.0f) * barRc.width);
            const int dUp = event.x > upX ? (event.x - upX) : (upX - event.x);
            const int dDn = event.x > downX ? (event.x - downX) : (downX - event
                .x);
            ui.hottDragMode = (dUp < dDn) ? 1 : 0;
          }
          const float db = -80.0f + clamp01(cast(float)(event.x - barRc.left) / (
              barRc.width > 1 ? barRc.width : 1)) * 80.0f;
          if (ui.hottDragMode == 1) {
            emitParam(UP_PIDS[bi], (db + 60.0f) / 72.0f, DEF_UP[bi], "Up Thresh");
          } else {
            emitParam(DOWN_PIDS[bi], (db + 60.0f) / 60.0f, DEF_DN[bi], "Down Thresh");
          }
          return true;
        }
        if (inspRc.isPointInside(event.x, event.y)) {
          ui.hottDragBand = bi;
          const bool topHalf = event.y < inspRc.middley;
          ui.hottDragMode = topHalf ? 2 : 3;
          const float frac = clamp01(cast(float)(event.x - inspRc.left) / (
              inspRc.width > 1 ? inspRc.width : 1));
          if (ui.hottTab == 0) {
            emitParam(topHalf ? ATK_PIDS[bi] : REL_PIDS[bi], frac, 0.7f,
                topHalf ? "Attack" : "Release");
          } else if (ui.hottTab == 1) {
            emitParam(URAT_PIDS[bi], frac * 0.999f, 0.76f, "Up Ratio");
          } else {
            emitParam(DRAT_PIDS[bi], frac * 0.999f, 0.985f, "Down Ratio");
          }
          return true;
        }
      }
    }

    // 5. EQ interactive band handle drag
    if (dl.kind == BuiltinDeviceKind.EQ && dl.canvasRc.isPointInside(event.x, event
        .y)) {
      const int pad = dp(8);
      const int sw = dl.canvasRc.width - pad * 2;
      const int sh = dl.canvasRc.height - pad * 2;
      if (sw > dp(8) && sh > dp(8)) {
        static immutable float[8] DEF_FREQS = [
          30, 80, 250, 700, 2000, 5000, 10000, 16000
        ];
        int bestB = -1;
        double bestD = dp(16);
        foreach (b; 0 .. 8) {
          if (eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f)) == 0)
            continue;
          const float fn = readPlugParam(plug, cast(uint)(8 + b), eqFreqToNorm(DEF_FREQS[b]));
          const float gn = readPlugParam(plug, cast(uint)(16 + b), 0.5f);
          const double bx = dl.canvasRc.left + pad + fn * sw;
          const double by = dl.canvasRc.top + pad + (1.0 - gn) * sh;
          const double dx = event.x - bx;
          const double dy = event.y - by;
          const double dist = dx * dx + dy * dy;
          if (dist < bestD * bestD) {
            bestD = dist;
            bestB = b;
          }
        }
        if (bestB < 0 && event.doubleClick) {
          foreach (b; 0 .. 8) {
            if (eqNormToTypeIdx(readPlugParam(plug, cast(uint)b, 0.0f)) == 0) {
              bestB = b;
              emitParam(cast(uint)b, 1.0f, 0.0f, format("Band %d Enable", b + 1));
              break;
            }
          }
        }
        if (bestB >= 0) {
          ui.eqSelectedBand = bestB;
          if (event.button == MouseButton.Right) {
            emitParam(cast(uint)bestB, 0.0f, 0.0f, format("Band %d Off", bestB + 1));
            return true;
          }
          ui.eqDragBand = bestB;
          ui.eqDragStartY = event.y;
          ui.eqDragStartQ = readPlugParam(plug, cast(uint)(24 + bestB), 0.425f);
          if ((event.flags & MouseFlag.Shift) == 0) {
            const float fn = clamp01(cast(float)(event.x - (dl.canvasRc.left + pad)) / sw);
            const float gn = clamp01(1.0f - cast(float)(event.y - (dl.canvasRc
                .top + pad)) / sh);
            emitParam(cast(uint)(8 + bestB), fn, eqFreqToNorm(DEF_FREQS[bestB]), "Freq");
            emitParam(cast(uint)(16 + bestB), gn, 0.5f, "Gain");
          }
          return true;
        }
      }
    }

    // 6. Envelope Shaper draggable point handles
    if (dl.kind == BuiltinDeviceKind.ENVELOPE_SHAPER && dl.canvasRc.isPointInside(
        event.x, event.y)) {
      const int pad = dp(8);
      const int sw = dl.canvasRc.width - pad * 2;
      const int sh = dl.canvasRc.height - pad * 2;
      int nPts = 2 + cast(int)(readPlugParam(plug, 21, 0.14f) * 14.0f + 0.5f);
      if (nPts < 2)
        nPts = 2;
      if (nPts > 16)
        nPts = 16;
      const float normX = clamp01(cast(float)(event.x - (dl.canvasRc.left + pad)) / (
          sw > 1 ? sw : 1));
      const int nearIdx = cast(int)(normX * (nPts - 1) + 0.5f);
      if (nearIdx >= 0 && nearIdx < nPts) {
        ui.envDragPoint = nearIdx;
        const float yVal = clamp01(
            1.0f - cast(float)(event.y - (dl.canvasRc.top + pad)) / (sh > 1 ? sh : 1));
        emitParam(cast(uint)(5 + nearIdx), yVal, 0.5f, format("Pt %d", nearIdx + 1));
        return true;
      }
    }

    // 6b. FilM 6x12 interactive modulation matrix cell vertical drag / right-click reset
    if (dl.kind == BuiltinDeviceKind.FILM && dl.filmMatrixRc.isPointInside(event
        .x, event.y)) {
      foreach (ci; 0 .. 72) {
        if (!dl.filmMatrixCells[ci].isPointInside(event.x, event.y))
          continue;
        const uint pid = cast(uint)(272 + ci);
        const int col = ci % 12;
        const int row = ci / 12;
        const float defV = (col == 11 && row == 0) ? 0.75f : 0.5f;
        if (event.button == MouseButton.Right) {
          emitParam(pid, defV, defV, format("M[%d,%d]", row + 1, col + 1));
        } else {
          ui.filmDragCell = ci;
          ui.dragStartY = event.y;
          ui.dragStartVal = readPlugParam(plug, pid, defV);
          ui.dragDefaultVal = defV;
        }
        return true;
      }
    }

    return true;
  }

  if (event.action == MouseAction.Move) {
    if (ui.filmDragCell >= 0 && ui.filmDragCell < 72 && dl.kind == BuiltinDeviceKind
        .FILM) {
      const float v = clamp01(ui.dragStartVal + cast(float)(ui.dragStartY - event
          .y) / dpf(90.0f));
      const uint pid = cast(uint)(272 + ui.filmDragCell);
      emitParam(pid, v, ui.dragDefaultVal, "Matrix");
      return true;
    }
    if (ui.hottDragBand >= 0 && ui.hottDragBand < 3 && dl.kind == BuiltinDeviceKind
        .HOTT) {
      static immutable uint[3] DOWN_PIDS = [11u, 10u, 9u];
      static immutable uint[3] UP_PIDS = [14u, 13u, 12u];
      static immutable uint[3] ATK_PIDS = [19u, 18u, 17u];
      static immutable uint[3] REL_PIDS = [22u, 21u, 20u];
      static immutable uint[3] DRAT_PIDS = [25u, 24u, 23u];
      static immutable uint[3] URAT_PIDS = [28u, 27u, 26u];
      const int bi = ui.hottDragBand;
      if (ui.hottDragMode <= 1) {
        Rect barRc = dl.hottBarRcs[bi];
        const float db = -80.0f + clamp01(
            cast(float)(event.x - barRc.left) / (barRc.width > 1 ? barRc.width : 1)) * 80.0f;
        if (ui.hottDragMode == 1) {
          emitParam(UP_PIDS[bi], (db + 60.0f) / 72.0f, 0.25f, "Up Thresh");
        } else {
          emitParam(DOWN_PIDS[bi], (db + 60.0f) / 60.0f, 0.45f, "Down Thresh");
        }
      } else {
        Rect inspRc = dl.hottInspectorRcs[bi];
        const float frac = clamp01(
            cast(float)(event.x - inspRc.left) / (inspRc.width > 1 ? inspRc.width : 1));
        const bool topHalf = (ui.hottDragMode == 2);
        if (ui.hottTab == 0) {
          emitParam(topHalf ? ATK_PIDS[bi] : REL_PIDS[bi], frac, 0.7f,
              topHalf ? "Attack" : "Release");
        } else if (ui.hottTab == 1) {
          emitParam(URAT_PIDS[bi], frac * 0.999f, 0.76f, "Up Ratio");
        } else {
          emitParam(DRAT_PIDS[bi], frac * 0.999f, 0.985f, "Down Ratio");
        }
      }
      return true;
    }
    if (ui.eqDragBand >= 0 && dl.kind == BuiltinDeviceKind.EQ) {
      const int pad = dp(8);
      const int sw = dl.canvasRc.width - pad * 2;
      const int sh = dl.canvasRc.height - pad * 2;
      if (sw > dp(8) && sh > dp(8)) {
        const int b = ui.eqDragBand;
        if ((event.flags & MouseFlag.Shift) != 0) {
          const float dq = cast(float)(ui.eqDragStartY - event.y) / dpf(110.0f);
          emitParam(cast(uint)(24 + b), ui.eqDragStartQ + dq, 0.425f, "Q");
        } else {
          const float fn = clamp01(cast(float)(event.x - (dl.canvasRc.left + pad)) / sw);
          const float gn = clamp01(1.0f - cast(float)(event.y - (dl.canvasRc.top + pad)) / sh);
          emitParam(cast(uint)(8 + b), fn, 0.5f, "Freq");
          emitParam(cast(uint)(16 + b), gn, 0.5f, "Gain");
        }
      }
      return true;
    }
    if (ui.envDragPoint >= 0 && dl.kind == BuiltinDeviceKind.ENVELOPE_SHAPER) {
      const int pad = dp(8);
      const int sh = dl.canvasRc.height - pad * 2;
      const float yVal = clamp01(1.0f - cast(float)(event.y - (dl.canvasRc.top + pad)) / (
          sh > 1 ? sh : 1));
      emitParam(cast(uint)(5 + ui.envDragPoint), yVal, 0.5f, format("Pt %d", ui
          .envDragPoint + 1));
      return true;
    }
    if (ui.activeDragParamId >= 0) {
      float v;
      if (ui.dragIsVerticalSlider && ui.activeDragSliderRc.height > 1) {
        const float raw = clamp01(
            cast(float)(event.y - ui.activeDragSliderRc.top) / cast(
            float)ui.activeDragSliderRc.height);
        v = ui.dragInvertVertical ? (1.0f - raw) : raw;
      } else {
        v = clamp01(ui.dragStartVal + cast(float)(ui.dragStartY - event.y) / dpf(
            110.0f));
      }
      emitParam(cast(uint)ui.activeDragParamId, v, ui.dragDefaultVal, "Param");
      return true;
    }
  }

  if (event.action == MouseAction.ButtonUp || event.action == MouseAction.Cancel) {
    ui.eqDragBand = -1;
    ui.hottDragBand = -1;
    ui.filmDragCell = -1;
    ui.envDragPoint = -1;
    ui.acidActiveNote = -1;
    ui.activeDragParamId = -1;
    ui.dragIsVerticalSlider = false;
    ui.dragInvertVertical = false;
    return true;
  }

  return false;
}

unittest {
  import dlangui.graphics.drawbuf : ColorDrawBuf;
  import gui_d.transport_model : DrumPadSlotState;

  TransportModel model;
  model.initDefaultTemplate();
  auto buf = new ColorDrawBuf(440, 260);
  Rect bodyRc = Rect(0, 32, 440, 260);

  // 1. Test every builtin URI maps to non-GENERIC device UI & renders cleanly across modern & WIN95 themes
  static immutable string[23] URIS = [
    "builtin://eq", "builtin://compressor", "builtin://limiter", "builtin://hott",
    "builtin://maxim", "builtin://delay", "builtin://reverb", "builtin://phaser",
    "builtin://chorus", "builtin://bitcrusher", "builtin://envelope_shaper",
    "builtin://convolver", "builtin://vocodey", "builtin://stereo_width",
    "builtin://acid_bass", "builtin://3xosc", "builtin://sampler",
    "builtin://epiano", "builtin://organ", "builtin://dr8_kick",
    "builtin://film", "builtin://sylph", "builtin://aux"
  ];
  foreach (u; URIS) {
    PluginSlotState slot;
    slot.uri = u;
    slot.name = u;
    assert(hasCustomBuiltinDeviceUi(&slot), "Expected custom Swing-ported UI for " ~ u);
    BuiltinDeviceUiState ui;
    drawBuiltinDevicePanel(buf, bodyRc, &model, &slot, ui, FontRef.init);
  }

  // Verify per-device preferred dimensions differ meaningfully (no uniform-box distortion)
  const limSz = preferredDeviceBodySize(BuiltinDeviceKind.LIMITER);
  const acidSz = preferredDeviceBodySize(BuiltinDeviceKind.ACID_BASS);
  const sampSz = preferredDeviceBodySize(BuiltinDeviceKind.SAMPLER);
  const filmSz = preferredDeviceBodySize(BuiltinDeviceKind.FILM);
  assert(limSz.width < acidSz.width && acidSz.height < sampSz.height &&
      filmSz.width > acidSz.width,
      "Expected distinct device proportions per Swing device panel");
  const fittedLim = fitDeviceBodyRect(Rect(0, 0, 800, 600), BuiltinDeviceKind
      .LIMITER);
  assert(fittedLim.width < 500 && fittedLim.height < 350,
      "Expected oversized window not to stretch compact Limiter device");

  // Also test DrumMachine + WIN95 retro theme
  model.themeId = FlThemeId.WIN95;
  PluginSlotState dm;
  dm.uri = "builtin://drum_machine";
  dm.name = "Drum Machine";
  dm.drumPads = [
    DrumPadSlotState(0, "Kick", "bd.wav"), DrumPadSlotState(1, "Snare", "sd.wav")
  ];
  assert(hasCustomBuiltinDeviceUi(&dm));
  BuiltinDeviceUiState dmUi;
  drawBuiltinDevicePanel(buf, bodyRc, &model, &dm, dmUi, FontRef.init);

  // 2. Test interactive EQ band knob / pill & Acid Bass virtual key click
  model.themeId = FlThemeId.DEFAULT_DARK;
  PluginSlotState eqSlot;
  eqSlot.uri = "builtin://eq";
  eqSlot.name = "Parametric EQ";
  BuiltinDeviceUiState eqUi;
  int lastPid = -1;
  float lastVal = -1.0f;
  const dlEq = buildBuiltinDeviceLayout(bodyRc, &eqSlot, eqUi, model.palette());
  assert(dlEq.knobs.length == 3 && dlEq.pills.length >= 9);
  auto k0 = dlEq.knobs[0];
  handleBuiltinDeviceMouse(new MouseEvent(MouseAction.ButtonDown,
      MouseButton.Left, MouseFlag.LButton, cast(short)k0.cx, cast(short)k0.cy),
      bodyRc, &model, &eqSlot, eqUi, 1, (int t, int p, uint pid, float v) {
    lastPid = cast(int)pid;
    lastVal = v;
  }, null, null, null, null);
  handleBuiltinDeviceMouse(new MouseEvent(MouseAction.Move, MouseButton.None,
      MouseFlag.LButton, cast(short)k0.cx, cast(short)(k0.cy - 22)), bodyRc,
      &model, &eqSlot, eqUi, 1, (int t, int p, uint pid, float v) {
    lastPid = cast(int)pid;
    lastVal = v;
  }, null, null, null, null);
  assert(lastPid == 8 && lastVal > 0.0f, "Expected EQ Freq knob drag to emit param 8");

  PluginSlotState acidSlot;
  acidSlot.uri = "builtin://acid_bass";
  acidSlot.name = "Acid Bass";
  BuiltinDeviceUiState acidUi;
  const dlAcid = buildBuiltinDeviceLayout(bodyRc, &acidSlot, acidUi, model
      .palette());
  assert(dlAcid.knobs.length == 7 && dlAcid.pills.length == 17);
  uint triggeredNote = 0;
  // Pill index 8 in TB-303 Acid Bass is lower key 'C' (offset 0, C3 at default octave 3 = MIDI note 60)
  const auto c3Pill = dlAcid.pills[8];
  handleBuiltinDeviceMouse(new MouseEvent(MouseAction.ButtonDown,
      MouseButton.Left,
      MouseFlag.LButton, cast(short)c3Pill.rc.middlex, cast(short)c3Pill.rc
      .middley), bodyRc, &model, &acidSlot, acidUi, 103, null, (int t, uint note) {
    triggeredNote = note;
  }, null, null, null);
  assert(triggeredNote == 60 && acidUi.acidActiveNote == 0, "Expected TB-303 Acid Bass C key click to trigger MIDI note 60 and light its LED");

  // 3. Test HOTT (OTT) interactive band threshold bar drag in center BandMeterPanel
  PluginSlotState hottSlot;
  hottSlot.uri = "builtin://hott";
  hottSlot.name = "Hott";
  BuiltinDeviceUiState hottUi;
  const dlHott = buildBuiltinDeviceLayout(Rect(0, 32, 520, 290), &hottSlot,
      hottUi, model.palette());
  assert(dlHott.knobs.length == 8);
  int hottPid = -1;
  handleBuiltinDeviceMouse(new MouseEvent(MouseAction.ButtonDown,
      MouseButton.Left,
      MouseFlag.LButton, cast(short)dlHott.hottBarRcs[0].middlex,
      cast(short)dlHott.hottBarRcs[0].middley), Rect(0, 32, 520, 290), &model,
      &hottSlot, hottUi, 4, (int t, int p, uint pid, float v) {
    hottPid = cast(int)pid;
  }, null, null, null, null);
  assert(hottPid == 11 || hottPid == 14,
      "Expected OTT High band bar click to adjust High Down/Up threshold param");

  // 4. Test FilM (FM Synth) tabs & 6x12 modulation matrix vertical drag
  PluginSlotState filmSlot;
  filmSlot.uri = "builtin://film";
  filmSlot.name = "FilM";
  BuiltinDeviceUiState filmUi;
  Rect filmRc = Rect(0, 32, 740, 412);
  auto filmBuf = new ColorDrawBuf(740, 420);
  drawBuiltinDevicePanel(filmBuf, filmRc, &model, &filmSlot, filmUi, FontRef.init);
  const dlFilm = buildBuiltinDeviceLayout(filmRc, &filmSlot, filmUi, model
      .palette());
  int filmPid = -1;
  const auto cell0 = dlFilm.filmMatrixCells[0];
  handleBuiltinDeviceMouse(new MouseEvent(MouseAction.ButtonDown,
      MouseButton.Left,
      MouseFlag.LButton, cast(short)cell0.middlex, cast(short)cell0.middley),
      filmRc, &model, &filmSlot, filmUi, 100, (int t, int p, uint pid, float v) {
    filmPid = cast(int)pid;
  }, null, null, null, null);
  handleBuiltinDeviceMouse(new MouseEvent(MouseAction.Move, MouseButton.None,
      MouseFlag.LButton, cast(short)cell0.middlex, cast(short)(cell0.middley - 20)),
      filmRc, &model, &filmSlot, filmUi, 100, (int t, int p, uint pid, float v) {
    filmPid = cast(int)pid;
  }, null, null, null, null);
  assert(filmPid == 272, "Expected FilM matrix cell[0,0] drag to emit param 272");

  // 5. Test Sylph v2 UI: dual Filter A/B, 16 ADSR fader bars, Serial FX Chain LCD selection & ON/OFF, applyBuiltinDevicePreset
  PluginSlotState sylphSlot;
  sylphSlot.uri = "builtin://sylph";
  sylphSlot.name = "Sylph";
  BuiltinDeviceUiState sylphUi;
  Rect sylphRc = Rect(0, 32, 860, 488);
  auto sylphBuf = new ColorDrawBuf(860, 500);
  drawBuiltinDevicePanel(sylphBuf, sylphRc, &model, &sylphSlot, sylphUi, FontRef
      .init);
  const dlSylph = buildBuiltinDeviceLayout(sylphRc, &sylphSlot, sylphUi, model
      .palette());
  assert(dlSylph.knobs.length == 33 && dlSylph.sliders.length == 16, "Expected 33 rotary controls (incl Filter A/B + selected FX) and 16 visual ADSR fader bars");
  assert(applyBuiltinDevicePreset(&sylphSlot, sylphUi, 1) && sylphUi
      .sylphPresetIdx == 1, "Expected HARD KICK preset to apply cleanly without duplicate inline preset strip");
  assert(readPlugParam(&sylphSlot, 6, 0.0f) >= 0.5f && readPlugParam(
      &sylphSlot, 46, 0.5f) > 0.85f && readPlugParam(&sylphSlot, 55, 0.0f) >= 0.5f,
      "Expected HARD KICK preset to enable OscA Retrig, high Pitch Snap, and Dist FX");

  // Click DELAY block in serial FX Chain LCD -> switches selected FX controls to DELAY
  const auto delayBlk = dlSylph.sylphFxBlockRcs[2];
  handleBuiltinDeviceMouse(new MouseEvent(MouseAction.ButtonDown,
      MouseButton.Left,
      MouseFlag.LButton, cast(short)(delayBlk.left + 4), cast(short)(delayBlk
      .bottom - 6)), sylphRc, &model, &sylphSlot, sylphUi, 100, null, null, null, null,
      null);
  assert(sylphUi.sylphFxTab == 2, "Expected FX Chain LCD click to select DELAY block");

  // Drag first visual ADSR fader bar (Amp A Attack, paramId 13)
  int sliderPid = -1;
  const auto envBar0 = dlSylph.sliders[0];
  handleBuiltinDeviceMouse(new MouseEvent(MouseAction.ButtonDown,
      MouseButton.Left,
      MouseFlag.LButton, cast(short)envBar0.rc.middlex, cast(short)envBar0.rc
      .middley), sylphRc, &model, &sylphSlot, sylphUi, 100, (int t, int p, uint pid,
      float v) { sliderPid = cast(int)pid; }, null, null, null, null);
  assert(sliderPid == 13,
      "Expected visual ADSR fader bar click/drag to emit AmpA Attack (param 13)");
}
