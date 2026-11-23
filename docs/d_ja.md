# Hibiki DAW — D言語移行アーキテクチャおよび移行計画

## 1. エグゼクティブサマリーおよび設計指針

### 1.1 背景と動機
Hibiki DAW は、リアルタイムのモジュラーシンセシス、高精度オーディオ編集、プラグインホスティング（VST3）、および柔軟なシーケンス制御を統合するために開発されたオープンソースのデジタルオーディオワークステーションである。従来のプロジェクト構成は多言語混在システムとして発展してきた。すなわち、オーディオ DSP エンジン、プラグインホスト、および再生サーバーはモダン C++（C++23）で実装され、デスクトップ GUI、タイムラインエディター、およびピアノロールは Java Swing/Java2D と Clojure、ならびに組み込み JavaScript/TypeScript スクリプトエンジンによって構築されていた。

この構成は豊富な機能性を提供する一方で、C++ と JVM 間の言語間ブリッジを維持することに伴う重大な摩擦をもたらしていた。
1. **実行時オーバーヘッドと非決定論的遅延**: Java 仮想マシン（JVM）は大きなメモリオーバーヘッド（ベースラインヒープで 300〜400 MB）を伴い、ガベージコレクションによる停止時間（GC ポーズ）が発生するため、音途切れを回避するには厳密なマルチプロセス分離が必須であった。
2. **ビルドおよび相互運用の複雑性**: C++、Java（`rules_jvm`）、Clojure、およびネイティブ JNI/C シムを横断する Bazel ビルド規則の維持は、CI、パッケージング、およびデバッグを複雑化させていた。
3. **言語的な摩擦**: モダン C++ は決定論的実行とゼロコスト抽象化を提供するものの、Boost や Abseil などの巨大な外部依存なしには、組み込みのモジュール機構、コンパイル時リフレクション、メモリ安全性の制約、および表現力豊かなスライス機構を欠いていた。

**D言語**（とりわけ最新の **LLVM D コンパイラである LDC 1.41.0**）は、低レベルなハードウェア制御と高レベルな生産性を両立する統合された最新システムプログラミングパラダイムを提供する。本仕様書は、Hibiki DAW コードベースをD言語へと移行するための決定的なアーキテクチャ仕様および実装ロードマップを包括的に定めるものである。

### 1.2 コア指針: 簡潔性・安全性・高速性
D言語への移行は、妥協のない以下の 3 つの設計指針に基づいて推進される。

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Hibiki コア設計指針                             │
├────────────────────────────────────────────────────────────────────────┤
│  1. 簡潔性（Conciseness）                                              │
│     - ネイティブな動的・静的スライス（T[]）による定型コードの排除      │
│     - コンパイル時リフレクション（__traits, CTFE）による手作業の排除   │
│     - スコープガード（scope(exit), scope(failure)）による堅牢な RAII   │
│     - 不要なサードパーティ製 D パッケージ（DUB）の完全排除             │
├────────────────────────────────────────────────────────────────────────┤
│  2. 安全性（Safety）                                                   │
│     - コンパイル時メモリ安全性検証（@safe）                            │
│     - 音声スレッドにおけるガベージコレクションの完全禁止（@nogc）      │
│     - リアルタイムパスにおける例外送出の形式的排除（nothrow）          │
│     - 参照透過性を保証する純粋 DSP 関数（pure）                        │
├────────────────────────────────────────────────────────────────────────┤
│  3. 高速性（Speed）                                                    │
│     - LLVM 20.1 バックエンドによるネイティブ機械語の直接生成           │
│     - C++23 と同等以上の DSP 実行性能                                  │
│     - キャッシュ効率に優れた SIMD 自動ベクトル化                       │
│     - スレッド停止を伴わない決定論的 512 サンプルブロック単位処理      │
└────────────────────────────────────────────────────────────────────────┘
```

1. **簡潔性（Conciseness）**:
   D言語の表現力豊かな型システム、統一関数呼出構文（UFCS）、ネイティブスライス、およびコンパイル時関数実行（CTFE）により、C++23 と比較してコード行数を劇的に削減する。Protobuf シリアライズ、プラグインパラメーター登録、Undo 状態の複製といった定型コードは、実行時オーバーヘッドなしにコンパイル時に自動生成される。
2. **安全性（Safety）**:
   オーディオ信号処理は厳格なメモリおよび実行安全性を要求する。コードベースを高レベルの `@safe` 領域と、リアルタイムの `@nogc nothrow` 音声領域に厳密に分離することで、メモリ破損の脆弱性、データ競合、および優先度逆転を数学的に排除する。
3. **高速性（Speed）**:
   LDC は LLVM コンパイラ基盤を活用し、ターゲットハードウェア向けに高度に最適化された SIMD 命令（SSE4.2, AVX2, NEON）を直接出力する。D言語のコードは最適化された C++ と同等の性能を達成しつつ、よりクリーンなメモリレイアウトとポインタ間接参照の削減を実現する。
4. **外部依存性ゼロ（Zero External Dependencies）**:
   長期的な保守性、高速な密閉型ビルド、および完全なプラットフォーム主権を担保するため、外部の DUB パッケージは一切使用しない。ビルドツールチェーン、Protobuf ワイヤシリアライズ、DSP プリミティブ、およびオーディオエンジンは、すべてリポジトリ内で完結して自作・実装される。

### 1.3 4段階移行ロードマップ概要
移行作業は、明確なマイルストーンに分割された以下の 4 段階の手順で段階的に実行される。

```
┌────────────────────────────────────────────────────────────────────────┐
│                        4段階移行ロードマップ概要                       │
├────────────────────────────────────────────────────────────────────────┤
│  ステップ1: LDC 密閉型 Bazel ビルド規則とホスト設定                    │
│  - Starlark（Bzlmod）による LDC 1.41.0 ツールチェーンの密閉型自動取得  │
│  - CcInfo 相互運用を備えた独自ルール（d_library, d_binary, d_test）    │
│  - Darwin ターゲットトリプル解決および -betterC フラグ対応             │
│  - Android ターゲットのスキップ設定（target_compatible_with & manual） │
│  - フォーマッタ統合（tools/format.sh への dfmt 導入）                  │
├────────────────────────────────────────────────────────────────────────┤
│  ステップ2: D言語による外部依存ゼロの Protobuf 実装                    │
│  - dproto/ における自己完結型ワイヤフォーマットコーデック（Type 0,1,2,5│
│  - Proto3 固有仕様: 10バイト符号拡張 int32、パック繰り返し float       │
│  - Oneof タグ付き共用体、proto3 optional 存在判定、デフォルト値省略   │
│  - 音声スレッド向け @nogc 固定バッファ・ゼロヒープシリアライズ         │
│  - C++ および Java との双方向バイナリワイヤ互換性テストスイート        │
├────────────────────────────────────────────────────────────────────────┤
│  ステップ3: コアエンジンの D言語移行（外部 C++ SDK 境界を除く）        │
│  - 3層構造: 第1層（@nogc RT）、第2層（@safe 非RT）、第3層（extern(C++│
│  - 7段階のボトムアップモジュール移行手順                               │
│  - 15種類の内蔵エフェクトおよび 15種類の内蔵音源の完全 D言語実装       │
│  - プロジェクト状態管理、WAV/MIDI パーサー、9系統のコマンドハンドラ    │
│  - 実行バイナリ群: hbk-play, hbk-plugin-worker, hbk-worker-daemon      │
├────────────────────────────────────────────────────────────────────────┤
│  ステップ4: 将来的な GUI の D言語移行計画（Java/Clojure からの脱却）   │
│  - D言語 GUI フレームワーク評価（DlangUI, Raylib, Skia, Dear ImGui）   │
│  - 推奨構成: Dear ImGui（Docking）+ cimgui + GLFW + GPU シェーダー     │
│  - 音声安全性を担保するサブプロセス分離アーキテクチャ                  │
│  - 7段階の GUI 移行ロードマップ（ウィンドウ構築〜描画〜REPL 統合）     │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 全体アーキテクチャと境界の分類体系

### 2.1 3層アーキテクチャモデル
リアルタイム性能の保証、厳格なモジュール分割、および円滑な言語間相互運用を両立するため、Hibiki D エンジンは 3 つの明確な実行層に階層化される。

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Hibiki 3層アーキテクチャ                        │
├────────────────────────────────────────────────────────────────────────┤
│  第2層: 非リアルタイム制御・状態管理・IPC                              │
│  - 言語パラダイム: イディオマティックな @safe D（Phobos 有効）         │
│  - メモリ管理: 動的 GC ヒープ、ネイティブスライス（T[]）、RAII ガード   │
│  - 対象モジュール: ProjectState (.hbk), Track, TimelineClip,           │
│                   HistoryManager (50履歴), AudioFile (WAV), Midi SMF,   │
│                   AudioEditor, レジストリ, IPC 出入力, コマンドハンドラ│
├────────────────────────────────────────────────────────────────────────┤
│  第1層: リアルタイムオーディオ・DSP処理                                │
│  - 言語パラダイム: @nogc nothrow pure, -betterC 互換                   │
│  - メモリ管理: 事前確保済み固定バッファ、スタック配列、動的確保ゼロ    │
│  - 対象モジュール: playback_thread (512サンプル), BiquadFilter, Adsr,  │
│                   MultiPointEnvelope, Modulator LFO, Resampler,        │
│                   15種の内蔵エフェクト, 15種の内蔵音源, DrumMachine,   │
│                   トラックミキサー加算, パンニング, サイドチェイン     │
├────────────────────────────────────────────────────────────────────────┤
│  第3層: 外部 C++ 境界グルー                                            │
│  - 言語パラダイム: extern(C++) クラスおよび extern(C) 関数シム         │
│  - メモリ管理: ネイティブ OS ハンドル、生ポインタ、C++ STL 漏洩ゼロ    │
│  - 対象モジュール: Steinberg VST3 SDK ホスト（engine/vst3/vst3_host.*）│
│                   OS オーディオ・MIDI ハードウェア（engine/audio/）    │
│                   PocketFFT C シム（pocketfft_c_shim.cpp）             │
└────────────────────────────────────────────────────────────────────────┘
```

#### 第1層: リアルタイムオーディオ・DSP処理
- **実行コンテキスト**: オーディオ再生スレッド / 音声処理スレッド（`playback_thread`）。OS の高優先度スレッド（`THREAD_PRIORITY_TIME_CRITICAL` や macOS Audio Workgroup）上で実行される。
- **不変条件**: `@nogc` および `nothrow` の厳格な強制。GC メモリアロケーション（`new`、配列結合 `~=`、連想配列）、動的型キャスト、およびブロッキングを伴う OS システムコール（ファイル I/O、ミューテックスのロック取得、ネットワーク通信）の呼び出しを一切禁止する。
- **処理単位**: 同期的なブロック単位処理（標準 512 サンプル。44.1 kHz 時に約 11.6 ms、48 kHz 時に約 10.7 ms のバッファサイズ）。
- **互換性**: 必要に応じて `-betterC` モードでも動作可能とし、C 標準ライブラリのプリミティブまたは生メモリバッファのみで完結する設計とする。

#### 第2層: 非リアルタイム制御・状態管理・IPC
- **実行コンテキスト**: メインコマンドスレッド、バックグラウンドワーカースレッド、ディスク I/O スレッド、および GUI 同期ループ。
- **不変条件**: Phobos 標準ライブラリ、非リアルタイム状態に対する GC、文字列検索用連想配列、動的スライス、および標準例外（`Exception`）を自由に活用するイディオマティックな `@safe` D言語コード。
- **責務範囲**:
  - プロジェクトツリー構造の管理（`ProjectState`, `TrackState`, `ClipState`）。
  - バイナリプロジェクトファイル（`.hbk`）および設定ファイル（`.hibikirc.textproto`）の保存・読み込み。
  - 50 段階の Undo/Redo 履歴管理（`HistoryManager`）。
  - 非圧縮音声（`AudioFile` WAV リーダー/ライター）および標準 MIDI ファイル（`Midi`）の解析・出力。
  - GUI から標準入力（`stdin`）経由で届くバイナリコマンドの受信解析、および標準出力（`stdout`）への通知フレーム送出。

#### 第3層: 外部 C++ 境界グルー
- **実行コンテキスト**: OS オーディオドライバ、ネイティブウィンドウシステム、および外部サードパーティ製プラグイン SDK。
- **不変条件**: 安定したバイナリインタフェース（`extern(C++)` および `extern(C)`）。シンボルマングリングの不一致やアロケータ競合を防ぐため、C++ の標準テンプレートライブラリ（`std::vector`, `std::string`, `std::function`）を ABI 境界を越えて露出させることを固く禁じる。
- **構成コンポーネント**:
  - Steinberg VST3 SDK ホスト結合層（`engine/vst3/`）: VST3 の COM ライクなインタフェース（`IComponent`, `IAudioProcessor`, `IEditController`）をカプセル化し、ネイティブウィンドウハンドル（macOS の `NSView*`、Windows の `HWND`、Linux X11 の `Window`）を制御する。
  - ネイティブオーディオハードウェア抽象化（`engine/audio/sound.*`）: CoreAudio（macOS/iOS）、ALSA（Linux）、AAudio（Android）、および WASAPI（Windows）を駆動する。
  - ヘッダオンリー C++ ライブラリ（`pocketfft`）: D コンパイラに複雑な C++ テンプレートを展開させないよう、わずか 30 行程度の `extern "C"` シムを介してアクセスする。

### 2.2 メモリ安全性と音声スレッドのGC完全分離
DAW において、ガベージコレクションによる全スレッド一時停止（ストップ・ザ・ワールド現象）は、致命的な音途切れ（バッファアンダーランおよびオーディオグリッチ）を直結して引き起こす。Hibiki は以下の多層防御によりこの問題を完全に排除する。

1. **`@nogc` によるコンパイル時静的解析**:
   D コンパイラは、`@nogc` 属性が付与された関数の構文木（AST）において、D ランタイムのメモリ確保ルーチン（`_d_newitemT`, `_d_newarrayT` 等）を呼び出すコードが存在しないかをコンパイル時に厳密に検証する。開発者が DSP コールバック内で誤って `auto arr = new float[512];` と記述した場合、ビルド時に即座にコンパイルエラーとなる。
2. **決定論的事前割り当て**:
   オーディオ再生に必要なすべてのバッファ（ディレイライン、リバーブのコムフィルタ、シンセの発音ノートプール、畳み込み用パーティション）は、プラグイン初期化時（`init()` / `reset()`）にすべて事前確保される。リアルタイム再生中、DSP 処理系はスタック配列（`float[512]`）またはプロジェクトロード時に確保済みのヒープスライス内でのみ動作する。
3. **ロックフリーなスレッド間通信**:
   第2層から第1層への状態変更要求（パラメーター変更、ノート発音、クリップ再生指示など）は、待機なし・ロックフリーの単一生産者・単一消費者（SPSC: Single-Producer Single-Consumer）リングバッファまたはアトミック変数（`core.atomic`）を通じて伝達される。音声処理スレッドがミューテックスのロックを取得したり、条件変数の待機に入ることは決してない。
4. **音声スレッドの GC 切り離し**:
   OS のスレッド生成 API や D言語の `core.thread` で音声スレッドを起動する際、D ランタイムの GC 追跡レジストリから意図的にスレッドを切り離す（`thread_detachThis()`）。これにより、第2層のバックグラウンド処理で GC 回収サイクルが走った場合でも、音声スレッドが一時停止することは物理的にあり得ない。

### 2.3 C++相互運用性とABI規約
D言語は、言語機能レベルで C++ との高度なバイナリ相互運用性を備えている。Hibiki は中間グルーコードを最小化しつつ既存の C++ コンポーネントと直接通信する。

#### 1. C++ メンバー関数およびクラスの直接呼び出し（`extern(C++)`）
D言語は、macOS および Linux 上で Itanium C++ ABI を、Windows 上で MSVC C++ ABI をそのまま共有する。C++ の純粋仮想クラスは、D言語の `extern(C++) interface` と 1:1 でバイナリ一致する。

```d
// C++ の IPlugin と完全にバイナリ一致する D言語定義
extern(C++) interface IPlugin {
    void reset() @nogc nothrow;
    void process(
        float** inputs,
        float** outputs,
        int numSamples,
        const ref HostProcessContext ctx,
        const(MidiNoteEvent)* events,
        size_t numEvents,
        float** sidechain
    ) @nogc nothrow;
    void setParameter(uint paramId, float value) @nogc nothrow;
    float getParameter(uint paramId) const @nogc nothrow;
}
```

#### 2. 言語間構造体レイアウトの統一（`extern(C)`）
D と C++ の間で受け渡す構造体は、標準 C メモリレイアウト（`extern(C)` または `extern(C++) struct`）に従う。D言語のスライス（`T[]`）は `{ size_t length; T* ptr; }` で構成される 16 バイトの構造体であり、C++ のポインタ規約とは直接合致しない。したがって、ABI 境界を越える関数シグネチャでは、明示的な生ポインタと要素数を受け渡す。
- 音声バッファ: `const(float)* buffer, size_t numSamples`（分離バッファでは `float** channels`）
- 文字列: `const(char)* str, size_t len`（またはヌル終端の `const(char)*`）

#### 3. ランタイムのライフサイクル管理
アプリケーションのエントリポイントが C++ 側（`engine/main.cpp`）に存在し、Phobos を伴う標準 D言語コードを呼び出す場合、C++ はプログラム起動時および終了時に明示的に `druntime` を初期化・終了する。

```cpp
// C++ 側の起動制御
#include <iostream>

extern "C" int rt_init();
extern "C" int rt_term();

int main(int argc, char** argv) {
    if (!rt_init()) {
        std::cerr << "Failed to initialize D runtime" << std::endl;
        return 1;
    }
    // D言語エンジンのロジックを実行...
    rt_term();
    return 0;
}
```
バイナリ全体を `-betterC` でコンパイルする場合、`rt_init()` は一切不要であり省略される。

---

## 3. ステップ1詳細仕様: LDC密閉型Bazelビルド規則とホスト設定

### 3.1 Androidターゲットの分離とビルド遮断の解除
Hibiki リポジトリは Bazel 9 と Bzlmod（`MODULE.bazel`）を採用している。最近リポジトリに導入された実験的な Android ツールチェーン登録および空ディレクトリに対する glob 展開の記述により、Android SDK/NDK がインストールされていない一般的な開発環境や CI において、全体ビルドおよびテスト（`bazel test //... -c opt`）が遮断される問題が発生していた。

1. **根本原因の分析**:
   - `MODULE.bazel` の 33 行目および 41 行目における以下の無条件なツールチェーン登録:
     ```bzl
     register_toolchains("@androidsdk//:sdk-toolchain", "@androidsdk//:all")
     register_toolchains("@androidndk//:all")
     ```
     これにより、Bazel はあらゆるビルド対象のツールチェーン解決プロセスにおいて `@androidndk` リポジトリ規則を評価する。このとき環境変数 `ANDROID_NDK_HOME` が設定されていないと、`@rules_android_ndk//:rules.bzl` 内で `fail("Either the ANDROID_NDK_HOME environment variable or the path attribute of android_ndk_repository must be set.")` が呼び出され、ビルドが停止する。
   - `android/app/BUILD` の 11 行目:
     ```bzl
     srcs = glob(["src/main/jniLibs/**/*.so"]),
     ```
     Bazel 9 のデフォルトオプションである `--incompatible_disallow_empty_glob` のもとでは、該当ディレクトリに `.so` ファイルが存在しない場合に即座にエラーとなる。
2. **是正措置の仕様**:
   - **ツールチェーン登録のスコープ限定**: `MODULE.bazel` から無条件の `register_toolchains` を削除し、`.bazelrc` 内で `--config=android` が明示的に指定された場合のみ評価されるよう変更する。
     ```bzl
     build:android --extra_toolchains=@androidndk//:all
     ```
   - **空 glob の許容**: `android/app/BUILD` において `allow_empty = True` を設定する。
     ```bzl
     cc_library(
         name = "hibiki_jni_so",
         srcs = glob(["src/main/jniLibs/**/*.so"], allow_empty = True),
     )
     ```
   - **プラットフォーム制約と手動タグの付与**:
     `android/app/BUILD` および `engine/android/BUILD` 内のすべての Android 専用ターゲットに対し、以下の属性を付与する。
     ```bzl
     target_compatible_with = ["@platforms//os:android"],
     tags = ["manual"],
     ```
     これにより、macOS や Linux ホスト環境で `bazel build //...` や `bazel test //... -c opt` を実行した際、Bazel は Android ターゲットのツールチェーン評価を行わずに自動的にスキップする。

### 3.2 密閉型LDCツールチェーン構造（`tools/d_rules/`）
ホスト環境にローカルインストールされたコンパイラへの依存を断ち切るため、Bazel は密閉型ツールチェーン（ハーメチックツールチェーン）として、ホスト OS および CPU アーキテクチャに応じた公式 LDC 1.41.0 バイナリを自動的にダウンロードして設定する。

#### プラットフォーム別配布アーカイブおよび整合性ハッシュ（LDC 1.41.0）
公式の LDC 1.41.0（DMD v2.111.0、LLVM 20.1.5 ベース）配布物を取得する。

| 対象ホストプラットフォーム | 配布アーカイブファイル名 | SHA-256 チェックサム |
| :--- | :--- | :--- |
| **macOS ARM64** | `ldc2-1.41.0-osx-arm64.tar.xz` | `157267042f10b047210619314aa719b4f0bf887601e93b1c634aa1ecb3c546e4` |
| **macOS x86_64** | `ldc2-1.41.0-osx-x86_64.tar.xz` | `5bcff48b63c56a45dbaacdb0c5bddc8ea6be86d4a0c7b2c7c8318e047f721181` |
| **Linux x86_64** | `ldc2-1.41.0-linux-x86_64.tar.xz` | `4a439457f0fe59e69d02fd6b57549fc3c87ad0f55ad9fb9e42507b6f8e327c8f` |
| **Linux AArch64** | `ldc2-1.41.0-linux-aarch64.tar.xz` | `1c4b950a13d53379ed4f564366c27ec56d6261e21686880d70c7486b3e8c7ba8` |
| **Windows x64** | `ldc2-1.41.0-windows-x64.7z` | `1db11e5eeed10236729d45cde861fb5197d1cc716b4186600fca7fc6bb48c810` |

オフライン環境においては、環境変数 `LDC_PATH` または `/Users/karita/dlang/ldc-1.41.0` をフォールバック先として認識する。

### 3.3 ターゲットトリプルの解決（macOS Darwin 27および各OS）
最新の macOS（Darwin 27.x.x / macOS 15+ Sequoia）上で `ldc2` を直接実行した場合に深刻な不具合が発生する。
- デフォルトの LDC は Darwin メジャーバージョンから 9 を減算して macOS バージョンを推定する（`27 - 9 = 18`）。その結果、`-target arm64-apple-macos18` が内部の LLVM バックエンドに渡される。
- Apple Clang 21.0.0 および最新リンカーは、未リリースの無効なバージョン番号として `macos18` を拒絶し、以下のエラーを出力して停止する。
  `clang: error: invalid version number in '-target arm64-apple-macos18'`
- **解決策**: D言語 Bazel ルールは macOS 環境を自動検出し、コンパイル引数に以下のフラグを強制注入する。
  `-mtriple=arm64-apple-darwin27.0.0`（または `-mtriple=arm64-apple-macos15.0`）。
  これにより LLVM の自動推定を上書きし、安定したビルドとリンクを保証する。

### 3.4 Starlarkルール仕様: `d_library`, `d_binary`, `d_test`
ビルドロジックの中核は `tools/d_rules/defs.bzl` に集約される。

```bzl
# tools/d_rules/defs.bzl
load("@rules_cc//cc:defs.bzl", "cc_common")

DInfo = provider(
    doc = "D言語のソースファイル、インポートパス、文字列インポートを伝播するプロバイダ。",
    fields = {
        "srcs": "D言語ソースファイル（.d）の depset",
        "import_dirs": "インポート検索ディレクトリ（-I）の depset",
    },
)

def _d_library_impl(ctx):
    out_lib = ctx.actions.declare_file("lib" + ctx.label.name + ".a")
    
    # 依存関係からの推移的 D インポートおよび C++ コンテキストの収集
    transitive_srcs = [ctx.files.srcs]
    transitive_imports = [ctx.label.package]
    cc_dep_contexts = []
    
    for dep in ctx.attr.deps:
        if DInfo in dep:
            transitive_srcs.append(dep[DInfo].srcs)
            transitive_imports.append(dep[DInfo].import_dirs)
        if CcInfo in dep:
            cc_dep_contexts.append(dep[CcInfo])

    # プラットフォーム固有のコンパイルフラグ
    flags = []
    if ctx.target_platform_has_darwin:
        flags.append("-mtriple=arm64-apple-darwin27.0.0")
    if ctx.attr.betterc:
        flags.append("-betterC")
    
    # インポートパスの展開
    for imp in depset(transitive = transitive_imports).to_list():
        flags.append("-I=" + imp)

    args = ctx.actions.args()
    args.add("--lib")
    args.add("-of=" + out_lib.path)
    args.add_all(flags)
    args.add_all(ctx.attr.copts)
    args.add_all(ctx.files.srcs)

    ctx.actions.run(
        inputs = ctx.files.srcs,
        outputs = [out_lib],
        executable = ctx.executable._ldc2,
        arguments = [args],
        mnemonic = "DCompileLib",
        progress_message = "D言語ライブラリをコンパイル中: %{label}",
    )

    # C++ 側から直接リンク可能とするため CcInfo プロバイダを構築
    cc_toolchain = ctx.attr._cc_toolchain[cc_common.CcToolchainInfo]
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    
    linker_input = cc_common.create_linker_input(
        owner = ctx.label,
        libraries = depset([
            cc_common.create_library_to_link(
                actions = ctx.actions,
                feature_configuration = feature_configuration,
                cc_toolchain = cc_toolchain,
                static_library = out_lib,
            )
        ]),
    )
    linking_context = cc_common.create_linking_context(
        linker_inputs = depset([linker_input]),
    )
    
    merged_cc_info = cc_common.merge_cc_infos(
        direct_cc_infos = [CcInfo(linking_context = linking_context)] + cc_dep_contexts,
    )

    return [
        DInfo(
            srcs = depset(ctx.files.srcs, transitive = transitive_srcs),
            import_dirs = depset(transitive_imports),
        ),
        merged_cc_info,
    ]

d_library = rule(
    implementation = _d_library_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = [".d"], mandatory = True),
        "deps": attr.label_list(providers = [[DInfo], [CcInfo]]),
        "copts": attr.string_list(default = ["-O3", "-release"]),
        "betterc": attr.bool(default = False),
        "_ldc2": attr.label(default = "@ldc_toolchain//:ldc2", executable = True, cfg = "exec"),
        "_cc_toolchain": attr.label(default = "@bazel_tools//tools/cpp:current_cc_toolchain"),
    },
    fragments = ["cpp"],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
)
```

#### ルールの機能と役割
- **`d_library`**: D言語ソースをコンパイルして静的ライブラリ（`.a`）を生成する。D言語のターゲット向けに `DInfo` を返し、C++ ターゲット向けに `CcInfo` を返す。これにより、既存の C++ ターゲット（`cc_binary`, `cc_library`, `cc_test`）は追加のグルーコードなしで `d_library` を直接 `deps` に指定できる。
- **`d_binary`**: 実行可能バイナリをリンク・生成する。`betterc = False` の場合は `libphobos2-ldc.a` および `libdruntime-ldc.a` を自動リンクする。`CcInfo` 経由で C++ 依存関係を解決し、標準 C++ ランタイム（macOS では `-L=-lc++`、Linux では `-L=-lstdc++`）を正しくリンクする。
- **`d_test`**: `-unittest -main` を付与してコンパイルし、`bazel test` で直接実行可能な単体テストバイナリを生成する。

### 3.5 Bzlmod統合とMODULE.bazel設定
`MODULE.bazel` において、D言語用モジュール拡張を登録する。

```bzl
# MODULE.bazel
bazel_dep(name = "platforms", version = "0.0.10")
bazel_dep(name = "rules_cc", version = "0.0.17")

ldc_extension = use_extension("//tools/d_rules:extensions.bzl", "ldc_repositories")
use_repo(ldc_extension, "ldc_toolchain")
```

### 3.6 フォーマッタ統合（dfmtおよびtools/format.sh）
D言語コミュニティの標準フォーマッタは `dfmt`（`dlang-community/dfmt`）である。`tools/format.sh` に `google-java-format` や `buildifier` と同様の `ensure_dfmt` 処理を追加する。

```bash
# tools/format.sh への追加内容:
DFMT_VERSION="v0.15.1"
DFMT_BIN="$TOOLS_DIR/dfmt"

ensure_dfmt() {
  if command -v dfmt &>/dev/null; then return 0; fi
  if [[ ! -x "$DFMT_BIN" ]]; then
    local url="https://github.com/dlang-community/dfmt/releases/download/${DFMT_VERSION}/dfmt-${PLATFORM}"
    curl -fsSL -o "$DFMT_BIN" "$url"
    chmod +x "$DFMT_BIN"
  fi
}

format_d() {
  ensure_dfmt
  local files
  files=$(find "$REPO_ROOT" -name '*.d' -not -path '*/.*' -not -path '*/bazel-*')
  if [[ -n "$files" ]]; then
    echo "$files" | xargs "$DFMT_BIN" -i
  fi
}
```

---

## 4. ステップ2詳細仕様: D言語による外部依存ゼロのProtobuf実装

### 4.1 対象スキーマ一覧とスコープ（`pb/*.proto`）
Hibiki は `syntax = "proto3";` で定義された以下の 4 つの標準 Protobuf スキーマを使用している。

| スキーマ定義ファイル | メッセージ数 | 列挙型数 | 主要メッセージ型 | 主な役割 |
| :--- | :--- | :--- | :--- | :--- |
| `pb/core.proto` | 15 | 3 | `Project`, `Track`, `Clip`, `AutomationLane` | プロジェクト状態の永続化（`.hbk`） |
| `pb/commands.proto` | 19 | 10 | `Request`, `ProjectCmd`, `TrackCmd`, `TransportCmd` | GUI からエンジン（stdin）への操作要求 |
| `pb/notifications.proto` | 37 | 1 | `Notification`, `PlaybackState`, `TrackMeter` | エンジン（stdout）から GUI への状態通知 |
| `pb/plugin_worker.proto` | 30 | 1 | `WorkerRequest`, `WorkerResponse`, `ProcessAudio` | 音声処理と外部 VST3 間の IPC 通信 |

**スキーマの構造的特徴**:
全 101 メッセージを網羅した調査結果:
- 使用されるワイヤタイプは **0**（Varint）、**1**（64-bit Fixed）、**2**（Length-delimited）、**5**（32-bit Fixed）の 4 種類のみである。
- Map 型（`map<k, v>`）やジグザグ符号化整数（`sint32`, `sint64`）は一切使用されていない。
- 11 箇所のパック繰り返し float フィールド（波形エンベロープ、スペクトラム解析バー）が存在する。
- 5 箇所の明示的な `oneof` 共用体、および 4 箇所の proto3 `optional` フィールドが存在する。

### 4.2 ワイヤフォーマット・コーデックの設計
パッケージ `dproto/` は、外部ライブラリを一切排除した自己完結型のシリアライザ・デシリアライザを実装する。
- `dproto/wire.d`: 純粋な `@nogc nothrow pure @safe` 低レベルワイヤコーデック。
- `dproto/message.d`: コンパイル時リフレクションを用いたフィールドタグ生成ユーティリティ。
- `pb/*.d`: 各スキーマに対応する強く型付けされた D言語構造体。

```
┌────────────────────────────────────────────────────────────────────────┐
│                        dproto コーデック構造                           │
├────────────────────────────────────────────────────────────────────────┤
│  ワイヤタイプ0: Varint（1〜10バイト）                                  │
│  - 1バイトあたり7ビットのデータ、最上位ビット（bit 7）は継続フラグ     │
│  - 負の int32 仕様: 64ビットに符号拡張して厳密に10バイトを出力する     │
├────────────────────────────────────────────────────────────────────────┤
│  ワイヤタイプ1: 64ビット固定長（8バイト LE）                           │
│  - double, fixed64, sfixed64。IEEE 754 リトルエンディアンで直接配置    │
├────────────────────────────────────────────────────────────────────────┤
│  ワイヤタイプ2: 長さプレフィックス形式（Varint長 + データペイロード）  │
│  - 文字列（文字数ではなく、UTF-8 バイト長を出力）                      │
│  - バイト列（const(ubyte)[]）                                          │
│  - 埋め込みサブメッセージ（再帰的シリアライズ）                        │
│  - パック繰り返し float（N * 4 バイトの連続バイナリ）                  │
├────────────────────────────────────────────────────────────────────────┤
│  ワイヤタイプ5: 32ビット固定長（4バイト LE）                           │
│  - float, fixed32, sfixed32。IEEE 754 リトルエンディアンで直接配置     │
└────────────────────────────────────────────────────────────────────────┘
```

#### 低レベルワイヤコーデックの実装（`dproto/wire.d`）

```d
module dproto.wire;

/// Protobuf 仕様に基づくワイヤタイプ識別子
enum WireType : ubyte {
    varint = 0,
    fixed64 = 1,
    lengthDelimited = 2,
    fixed32 = 5,
}

/// フィールド番号とワイヤタイプからタグバイトを生成する
pragma(inline, true)
pure nothrow @nogc @safe ubyte makeTag(uint fieldNumber, WireType wireType) {
    return cast(ubyte)((fieldNumber << 3) | cast(ubyte)wireType);
}

/// 符号なし64ビット整数を Varint 形式でバッファに書き込む
pure nothrow @nogc @safe size_t writeVarint(ref ubyte[] buf, ulong val) {
    size_t written = 0;
    while (val >= 0x80) {
        if (written >= buf.length) return 0;
        buf[written++] = cast(ubyte)((val & 0x7F) | 0x80);
        val >>= 7;
    }
    if (written >= buf.length) return 0;
    buf[written++] = cast(ubyte)val;
    return written;
}

/// 負の int32 に対する Protobuf 固有の 10 バイト符号拡張 Varint 出力
pure nothrow @nogc @safe size_t writeInt32(ref ubyte[] buf, int val) {
    ulong uval = (val < 0) ? cast(ulong)cast(long)val : cast(ulong)val;
    return writeVarint(buf, uval);
}

/// バッファスライスから Varint をデコードし、スライスを前進させる
pure nothrow @nogc @safe bool readVarint(ref const(ubyte)[] buf, out ulong val) {
    val = 0;
    int shift = 0;
    while (buf.length > 0) {
        ubyte b = buf[0];
        buf = buf[1 .. $];
        val |= cast(ulong)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
        if (shift >= 64) return false; // オーバーフロー防止
    }
    return false;
}

/// 未知のフィールドをワイヤタイプに従ってスキップする
pure nothrow @nogc @safe bool skipField(ref const(ubyte)[] buf, WireType wireType) {
    final switch (wireType) {
        case WireType.varint:
            ulong dummy;
            return readVarint(buf, dummy);
        case WireType.fixed64:
            if (buf.length < 8) return false;
            buf = buf[8 .. $];
            return true;
        case WireType.lengthDelimited:
            ulong len;
            if (!readVarint(buf, len) || buf.length < len) return false;
            buf = buf[cast(size_t)len .. $];
            return true;
        case WireType.fixed32:
            if (buf.length < 4) return false;
            buf = buf[4 .. $];
            return true;
    }
}
```

### 4.3 Proto3仕様準拠: 存在判定・デフォルト値・Oneof
1. **デフォルト値のシリアライズ省略**:
   proto3 仕様に基づき、スカラーフィールドがデフォルト値（`0`, `0.0f`, `false`, `""`）と等しい場合、ワイヤへのバイト出力を完全に省略してデータ量を最小化する。
2. **Proto3 Optional と存在判定（presence）**:
   `optional` 指定されたフィールドは、明示的な存在追跡フラグ（`bool has_fieldName`）を保持する。値が `0` であっても、`has_fieldName` が真であればワイヤ上へシリアライズされる。
3. **Oneof 共用体**:
   D言語側では、アクティブなバリアントを示す enum タグを備えたタグ付き共用体として表現する。出力時は、現在アクティブな単一フィールドのみがシリアライズされる。
4. **パック繰り返しフィールド**:
   proto3 は repeated float フィールドを Length-delimited 形式（`WireType.lengthDelimited`）で 1 つのバッファに連続格納する。デコーダ側は、パック形式とアンパック形式（`WireType.fixed32`）の双方を透過的に受け入れられるよう実装する。

### 4.4 リアルタイムオーディオ伝送用シリアライズ（`@nogc` ゼロヒープ）
プラグインプロセスとの通信（`ProcessAudio`）など、音声再生スレッドが関与する IPC 経路では、シリアライズ処理中にヒープメモリを確保することは許されない。`dproto` は呼び出し側から提供された固定スタックバッファに対して直接シリアライズを行う `@nogc` API を提供する。

```d
module dproto.realtime;

struct RealtimeAudioPacket {
    int sampleRate;
    int numSamples;
    int numChannels;
    float[2][512] audioData;
}

/// ヒープ確保を一切伴わずに固定バッファへ直接シリアライズする
size_t serializeRealtimeAudio(const ref RealtimeAudioPacket packet, ubyte[] dest) @nogc nothrow @safe {
    ubyte[] outBuf = dest;
    size_t totalBytes = 0;

    // フィールド1: sampleRate (varint)
    dest[totalBytes++] = makeTag(1, WireType.varint);
    totalBytes += writeVarint(outBuf[totalBytes .. $], cast(ulong)packet.sampleRate);

    // フィールド2: numSamples (varint)
    dest[totalBytes++] = makeTag(2, WireType.varint);
    totalBytes += writeVarint(outBuf[totalBytes .. $], cast(ulong)packet.numSamples);

    // フィールド3: audioData (packed repeated floats)
    dest[totalBytes++] = makeTag(3, WireType.lengthDelimited);
    const size_t floatBytes = packet.numChannels * packet.numSamples * 4;
    totalBytes += writeVarint(outBuf[totalBytes .. $], floatBytes);

    for (int ch = 0; ch < packet.numChannels; ++ch) {
        for (int s = 0; s < packet.numSamples; ++s) {
            float f = packet.audioData[ch][s];
            const(ubyte)* bytes = cast(const(ubyte)*)&f;
            dest[totalBytes .. totalBytes + 4] = bytes[0 .. 4];
            totalBytes += 4;
        }
    }
    return totalBytes;
}
```

### 4.5 IPCフレームプロトコル（標準入出力およびソケット）
Hibiki では、用途に応じて以下の 3 種類の長さプレフィックス付き IPC フレーム構造を採用している。

```
1. エンジン標準出力から GUI へ（通知）:
   ┌──────────────────────┬──────────────────────┬──────────────────────────────────┐
   │ マジック（4B LE）    │ 長さ（4B LE）        │ シリアライズ済み通知データ       │
   │ 0x48424B49 ("HBKI")  │ uint32 size          │ bytes[size]                      │
   └──────────────────────┴──────────────────────┴──────────────────────────────────┘

2. GUI 標準入力からエンジンへ（コマンド要求）:
   ┌──────────────────────┬────────────────────────────────────────────────────────┐
   │ 長さ（4B LE）        │ シリアライズ済みコマンド要求データ                     │
   │ uint32 size          │ bytes[size]                                            │
   └──────────────────────┴────────────────────────────────────────────────────────┘

3. プラグインワーカーソケット（WorkerRequest / WorkerResponse）:
   ┌──────────────────────┬────────────────────────────────────────────────────────┐
   │ 長さ（4B LE）        │ シリアライズ済みワーカー要求/応答データ                │
   │ uint32 size          │ bytes[size]                                            │
   └──────────────────────┴────────────────────────────────────────────────────────┘
```

- **再同期スキャン機構**: 標準出力パイプラインでバイト列の破損が生じた場合に備え、`IpcClient.java` および `gui.ipc` は最大 10,000 バイトまでストリームを走査してマジックヘッダ `0x48424B49` を再検出する。
- **最大サイズ制約**: 不正なパケットによるメモリ枯渇を防ぐため、受信パケットサイズの上限を 1 MB（`1024 * 1024` バイト）に制限する。

### 4.6 検証手法およびバイナリワイヤ互換性テストスイート
異言語間におけるバイナリレベルの互換性は、以下の自動テストにより厳密に検証される。
1. **ゴールデンベクター生成**: C++ および Java のテストバイナリを実行し、代表的なプロジェクトデータ、コマンド要求、および通知データをシリアライズしたバイナリファイル（`.bin`）を生成する。
2. **D言語によるデコード検証**: D言語の単体テストが各ゴールデンファイルを読み込み、すべてのフィールド値が元のデータ構造と完全に一致して復元されることを検証する。
3. **D言語による再シリアライズ検証**: 復元した構造体を D言語シリアライザで再度バイナリ化し、C++ が出力したゴールデンバイナリと 1 バイトの狂いもなく完全一致することを検証する。

---

## 5. ステップ3詳細仕様: コアエンジンのD言語移行（`engine/`）

### 5.1 7段階ボトムアップ移行戦略
移行の全期間を通じて既存の C++ および Java テストが常に合格し続ける状態を維持するため、エンジンコンポーネントを依存関係の最下層から上位層へ向けて段階的に移行する。

```
[フェーズ3.1: DSPプリミティブおよび算術カーネル]
  core/math, core/biquad_filter, instruments/adsr, instruments/multi_point_envelope,
  core/modulator, core/resample, ipc/tcp
        │
        ▼
[フェーズ3.2: 15種類の内蔵オーディオエフェクト]
  effects/builtin_*（15種類: Compressor, Eq, Delay, Reverb, Limiter, Maxim,
  Hott, Bitcrusher, Chorus, Phaser, StereoWidth, Convolver, EnvelopeShaper, Vocodey, Aux）,
  effects/registry
        │
        ▼
[フェーズ3.3: 15種類の内蔵音源・ドラムシンセ]
  instruments/builtin_*（15種類: 3xOsc, AcidBass, 9種のDR8ドラム,
  EPiano, Film, Organ, Sampler）, instruments/registry
        │
        ▼
[フェーズ3.4: コアオーディオ・プロジェクト状態・履歴・パーサー]
  core/audio_file (WAV), core/midi (SMF), core/clip, core/history (Undo/Redo),
  core/audio_editor (破壊的編集), core/track, core/project (.hbk)
        │
        ▼
[フェーズ3.5: IPC基盤および9系統のコマンドハンドラ]
  instruments/builtin_drum_machine, builtin_registry, ipc/ipc,
  commands/*（handleProjectCmd, handleTrackCmd, handleTransportCmd 等）
        │
        ▼
[フェーズ3.6: 実行バイナリエントリポイント群]
  hbk-play, hbk-plugin-worker, hbk-worker-daemon, plugin/plugin_proxy, plugin/plugin_scanner
        │
        ▼
[フェーズ3.7: C++境界グルー]
  engine/vst3/vst3_host.*, engine/audio/sound.*, engine/audio/midi_input.*,
  third_party/pocketfft/pocketfft_c_shim.cpp
```

#### 各フェーズの詳細仕様

##### フェーズ3.1: DSPプリミティブおよび算術カーネル（`@nogc nothrow pure`）
- `engine/core/math.d`: 高精度数学定数（`pi = 3.14159265358979323846`）、dB・リニア相互変換（`dbToLinear(float db) = 10.0f ^^ (db / 20.0f)`）、および等電力パン則の演算関数。
- `engine/core/biquad_filter.d`: RBJ オーディオ・クックブックに基づくバイパス、ローパス、ハイパス、バンドパス、ノッチ、ピーキング、ローシェルフ、ハイシェルフの各 2 次 IIR フィルタ。
- `engine/instruments/adsr.d` & `multi_point_envelope.d`: 線形・指数曲線に対応した ADSR および任意ブレークポイントのマルチポイントエンベロープ生成器。
- `engine/core/modulator.d`: サンプル精度で位相を累積する LFO（正弦波、三角波、ノコギリ波、矩形波、サンプル＆ホールド乱数）。
- `engine/core/resample.d`: ポリフェーズ Kaiser 窓付き sinc カーネルによる高品質リサンプリングアルゴリズム。

##### フェーズ3.2: 15種類の内蔵オーディオエフェクト
`IPlugin` を実装する純粋な D言語構造体として全 15 種のエフェクトを移行する。
1. `BuiltinCompressor`: ピーク/RMS 検出回路、アタック/リリース弾道特性、可変レシオ、ソフトニー、メイクアップゲイン。
2. `BuiltinEq`: 複素数周波数応答計算を備えた 8 バンドパラメトリック EQ。
3. `BuiltinDelay`: ピンポンモード、クロスフィード、ダンピングフィルタを備えたステレオフィードバックディレイ。
4. `BuiltinReverb`: Schroeder/Moorer 型のコムフィルタおよびオールパス格子構造を統合したアルゴリズミックリバーブ。
5. `BuiltinLimiter`: 透過的な自動リリース制御を備えた先読みピークリミッター。
6. `BuiltinMaxim`: 音圧マキシマイザーおよびソフトニーサチュレーター。
7. `BuiltinHott`: 偶数・奇数倍音をブレンド可能な真空管アンプサチュレーションモデル。
8. `BuiltinBitcrusher`: サンプリング周波数低減およびビット深度量子化。
9. `BuiltinChorus`: 直交 LFO による多相モジュレーション遅延線。
10. `BuiltinPhaser`: レゾナンスフィードバックを備えた 6 段オールパスフィルタラダー。
11. `BuiltinStereoWidth`: Mid/Side 分解およびステレオ音像バランス調整。
12. `BuiltinConvolver`: リアルタイム分割インパルス応答畳み込みエンジン。
13. `BuiltinEnvelopeShaper`: アタック成分とサステイン成分を個別に制御するトランジェントシェーパー。
14. `BuiltinVocodey`: キャリア/モジュレータ用フィルタバンクを備えたチャンネルボコーダー。
15. `BuiltinAux`: AUX バスのセンド/リターンルーティングおよび加算処理。

##### フェーズ3.3: 15種類の内蔵音源・ドラムシンセ
1. `Builtin3xOsc`: デチューン、ミキシング、ハードシンクを備えた 3 オシレータ減算シンセサイザー。
2. `BuiltinAcidBass`: Roland TB-303 のダイオードラダーフィルタ、アクセント、スライドをシミュレートした音源。
3. 9 種の DR8 ドラムシンセ（`dr8_kick`, `dr8_snare`, `dr8_hat`, `dr8_tom`, `dr8_clap`, `dr8_cowbell`, `dr8_crash`, `dr8_rim`, `dr8_conga`）。
4. `BuiltinEPiano`: FM 方式によるエレクトリックピアノ物理モデル音源。
5. `BuiltinFilm`: 映画音楽向けデュアルオシレータ・アンビエントパッドシンセサイザー。
6. `BuiltinOrgan`: 9 本のドローバー、キークリック、パーカッションを備えたトーンホイールオルガン音源。
7. `BuiltinSampler`: ループポイントおよびピッチ補間を備えたマルチゾーンサンプル再生エンジン。

##### フェーズ3.4: コアオーディオ・プロジェクト状態・履歴・パーサー
- `engine/core/audio_file.d`: 16-bit, 24-bit, 32-bit float PCM に対応した WAV ファイル読み書きパーサー。
- `engine/core/midi.d`: 標準 MIDI ファイル（SMF フォーマット 0/1）のチャンク解析、テンポマップ構築、ノート抽出。
- `engine/core/history.d`: プロジェクトスナップショットをリングバッファに格納する 50 段階の Undo/Redo 管理システム。
- `engine/core/audio_editor.d`: 非リアルタイム破壊的波形編集（ノーマライズ、逆再生、フェードイン/アウト、トリミング、ゲイン、畳み込み、ぼかし、スペクトログラム生成）。
- `engine/core/track.d`: トラック階層構造、ボリュームフェーダー、パンカーブ、オートメーションレーン、クリップ配置。
- `engine/core/project.d`: DAW プロジェクト全体のデータモデル、`.hbk` へのシリアライズ、およびオフラインバウンスレンダリング。

##### フェーズ3.5: IPC基盤および9系統のコマンドハンドラ
- `engine/ipc/ipc.d`: 標準出力通知フレーム生成（`0x48424B49` + LE uint32 長さ + Protobuf）。
- `engine/commands/*.d`: 9 系統のコマンドハンドラ（`handleProjectCmd`, `handleTransportCmd`, `handleTrackCmd`, `handlePluginCmd`, `handleAutomationCmd`, `handleMidiCmd`, `handleModulationCmd`, `handleAudioEditorCmd`, `handleDrumPadCmd`）。

##### フェーズ3.6: 実行バイナリエントリポイント群
- `hbk-play`: 再生ループおよび IPC サーバーを含むメインエンジン実行バイナリ。
- `hbk-plugin-worker`: 分離された外部 VST3 プラグインを実行するサブプロセスコンテナ。
- `hbk-worker-daemon`: ワーカープロセスのソケット接続を調停するバックグラウンドデーモン。

##### フェーズ3.7: C++境界グルー
- `engine/vst3/vst3_host.*`: Steinberg COM インタフェースおよび OS ウィンドウハンドルを操作する C++ 層。
- `engine/audio/sound.*`: 各 OS のハードウェアオーディオドライバを抽象化する C++ 層。
- `pocketfft_c_shim.cpp`: 高速フーリエ変換ヘッダを呼び出すための軽量 C シム。

### 5.2 リアルタイムインタフェース規約（`IPlugin` およびオーディオ）
リアルタイムオーディオ処理は、すべて以下の `IPlugin` インタフェース契約に基づいて統一的に実行される。

```d
module engine.plugin.iplugin;

/// リアルタイム DSP プラグインに渡されるホストプロセスコンテキスト
extern(C++) struct HostProcessContext {
    double sampleRate;
    double tempo;
    long playheadSamples;
    bool isPlaying;
}

/// C++ のバイナリレイアウトと一致する MIDI ノートイベント構造体
extern(C++) struct MidiNoteEvent {
    int noteNumber;
    float velocity;
    int sampleOffset;
    bool isNoteOn;
}

/// リアルタイムオーディオプラグインインタフェース
extern(C++) interface IPlugin {
    void reset() @nogc nothrow;
    
    void process(
        float** inputs,
        float** outputs,
        int numSamples,
        const ref HostProcessContext ctx,
        const(MidiNoteEvent)* events,
        size_t numEvents,
        float** sidechain
    ) @nogc nothrow;
    
    void setParameter(uint paramId, float value) @nogc nothrow;
    float getParameter(uint paramId) const @nogc nothrow;
}
```

### 5.3 リグレッションテスト戦略と50件のcc_test等価性検証
Hibiki エンジンには、現在 50 件の `cc_test` ターゲットが存在する。移行プロセスにおいて:
1. 各 C++ テストに対応する D言語テストターゲットを整備する（または `CcInfo` 経由でリンクして既存テストを再利用する）。
2. `bazel test //engine/... -c opt` を実行し、全 50 件のテストが 1 件のエラーもなく合格することを確認する。
3. Java による結合テスト群（`//:backend_manager_test`, `//:plugin_worker_test`, `//:js_repl_test`, `//:acid_house_sdk_e2e_test`）が、D言語でリビルドされた `hbk-play` バイナリに対して無修正で完全に合格することを保証する。

---

## 6. ステップ4詳細仕様: 将来的なGUIのD言語移行計画

### 6.1 現行Java/Clojure GUIアーキテクチャの分析
現行の Hibiki デスクトップ GUI は、Java Swing、FlatLaf、および Java2D（`src/main/java/hibiki/`）をベースとし、Clojure によるスクリプト連携（`src/main/clojure/hibiki/`）および Mozilla Rhino を用いた JavaScript/TypeScript REPL を備えたリッチなグラフィカル環境である。

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                               hibiki-gui-java                               │
│                                                                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ トップバー: 再生・停止・録音・ループ、BPM、拍子記号、画面切り替え     │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│  ┌───────────────┬───────────────────────────────────────────────────────┐  │
│  │ ブラウザー    │ カードレイアウト: SessionView <──> TimelineView       │  │
│  │ （サンプル、  ├───────────────────────────────────────────────────────┤  │
│  │  プリセット、 │ プラグインペイン: デバイスチェーンと ArcKnob パラメー │  │
│  │  プラグイン） ├───────────────────────────────────────────────────────┤  │
│  │               │ 右ペイン: Clojure REPL / JavaScript（Rhino）REPL      │  │
│  └───────────────┴───────────────────────────────────────────────────────┘  │
│                                                                             │
│  IPC 通信層（IpcClient / BackendManager）                                   │
└─────────────────────────────────────────────────────────────────────────────┘
           │                                                       ▲
           │ stdin: [長さ LE (4B)][pb.commands.Request]            │ stdout: [0x48424B49 (4B)]
           ▼                                                       │         [長さ LE (4B)]
┌──────────────────────────────────────────────────────────────────┴─────┐   │         [通知]
│                     hbk-play（D言語エンジンバックエンド）              │   │
│                                                                        │   │
│  ┌──────────────────────────────────────────────────────────────────┐  │   │
│  │ 音声再生スレッド（playback_thread, 512サンプル, @nogc nothrow）  │  │   │
│  └──────────────────────────────────────────────────────────────────┘  │   │
│  ┌──────────────────────────────────────────────────────────────────┐  │   │
│  │ 状態通知スレッド（30Hz: メーター、再生位置、スペクトラム等）     │──┴───┘
│  └──────────────────────────────────────────────────────────────────┘
└────────────────────────────────────────────────────────────────────────┘
```

#### 移行対象となる主要コンポーネント
1. **`TimelineView` & `TimelineRenderer`**（2,556行）:
   - マルチトラック水平タイムライン、時間ルーラー、ループ範囲ブラケット、マーカーレーン。
   - トラックレーン、波形ピークエンベロープ、MIDI ノートプレビュー、3 次ベジェ曲線によるオートメーションカーブ、再生カーソル、ドラッグゴーストの描画。
2. **`PianoRoll` & `PianoRollRenderer`**（1,200行）:
   - 128 鍵の垂直ピアノ鍵盤、スクロールノートキャンバス、ベロシティバー。
   - ノート配置、音高/タイミング移動、長さ伸縮、スケールスナップ（メジャー、マイナー、ペンタトニック、ドリアン等）、グリッドクオンタイズ。
3. **`SessionView` & `SessionViewIpc`**（852行）:
   - 非線形クリップマトリクスランチャー（トラック $\times$ シーンスロット）。
   - トラック別チャンネルストリップ: 音量フェーダー、パンノブ、ミュート/ソロ/録音アームボタン、ステレオピークメーター（`LevelMeter`）。
4. **`PluginPane` およびデバイスパネル群**（`ui/panels/devices/` 内の 30 クラス）:
   - 15 種のエフェクトおよび 15 種の音源に対する専用操作パネル。
   - 円弧状ロータリーノブ（`KnobPanel`, `paintArcKnob`）。
5. **テーマシステム（`Theme.java`）**:
   - 6 種類のカラーパレット（`AbletonDark`, `AbletonLight`, `SolarizedDark`, `SolarizedLight`, `Win95`, `WinXP`）。
   - 全ウィジェットに対する動的 HiDPI スケーリング。

### 6.2 D言語GUIフレームワークの評価とトレードオフ分析
ネイティブ GUI の移行基盤を選定するため、D言語およびネイティブ環境における主要フレームワークを比較評価した。

| 評価軸 | DlangUI | Raylib-d | BindBC-Skia（自作UI） | Dear ImGui（cimgui in D） | 推奨選定方針 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **パラダイム** | リテインドモード（OOP） | イミディエイト（ゲーム用） | 保持/即時描画キャンバス | イミディエイト（GUI専用） | **イミディエイト（`cimgui`）** |
| **描画バックエンド** | CPU 描画 / OpenGL | OpenGL（1.1〜4.3） | GPU（Metal, Vulkan, GL） | GPU（Metal, GL 3.3, DX11/12） | **GPU アクセラレーション** |
| **フレームレート性能** | 30〜60 FPS（CPU 律速） | 60+ FPS | 60〜144 FPS | 60〜144 FPS | **60〜144 FPS（高応答性）** |
| **描画ループの GC 負荷** | 大（Widget ツリーの確保）| 低 | 低 | **ゼロ（完全に @nogc 動作）** | **ゼロ GC（定常時アロケーションなし）** |
| **ドッキング・パネル分割** | 対応 | 非対応 | 自作が必須 | **標準内蔵（Docking ブランチ）**| **ドッキング＋分割内蔵** |
| **Bazel 密閉ビルド** | 複雑（SDL2, FreeType） | 容易（`cc_library`） | 極めて複雑（GN/Ninja 依存）| **極めて平易（C++ 3 ファイル）**| **極めて平易な `cc_library`** |
| **バイナリフットプリント** | 約 10 MB | 約 5 MB | 約 35〜45 MB | **約 6〜8 MB** | **最小フットプリント（<10 MB）**|
| **オーディオ業界での実績** | なし | 限定的 | Avalonia / Flutter | **デファクト標準（Reaper, JUCE）**| **DAW/DSP 業界標準** |

### 6.3 推奨GUIアーキテクチャ選定方針
Hibiki ネイティブ GUI の推奨アーキテクチャとして、**Dear ImGui（Docking ブランチ）を C ラッパー `cimgui` 経由で D言語から駆動する構成**を採用し、ウィンドウ管理には **GLFW** を使用する。
- **macOS**: GLFW + Apple Metal（`imgui_impl_metal.mm`）
- **Linux**: GLFW + OpenGL 3.3 / Vulkan（`imgui_impl_opengl3.cpp`）
- **Windows**: GLFW + DirectX 11 / DirectX 12（`imgui_impl_dx11.cpp`）

#### 選定理由
1. **GC オーバーヘッドの完全排除**:
   イミディエイトモード GUI は、定常的なフレーム描画において D ランタイムの GC ヒープアロケーションを一切行わない。描画ループ全体を `@nogc` で実行可能である。
2. **Java2D 描画命令との 1:1 対応**:
   `TimelineRenderer.java` や `PianoRollRenderer.java` の Java2D 命令は、ImGui の `ImDrawList` 命令に直接対応する。
   - `g2.fillRect(...)` $\rightarrow$ `drawList.AddRectFilled(...)`
   - `g2.draw(GeneralPath)` $\rightarrow$ `drawList.AddPolyline(...)`
   - `g2.drawBezier(...)` $\rightarrow$ `drawList.AddBezierCubic(...)`
   - `g2.setClip(...)` $\rightarrow$ `drawList.PushClipRect(...)`
3. **GPU バッチ処理による描画高速化**:
   Java2D は多数の MIDI ノートや波形スライスを含む複雑なタイムラインの描画に 15〜30 ms を要していたが、ImGui は描画命令を単一の頂点/インデックスバッファに統合（バッチ化）するため、現代の GPU 上で **0.5 ms 未満**で全描画を完了する。
4. **ドッキングおよびマルチウィンドウの標準サポート**:
   ImGui Docking ブランチにより、タブ付きドッキング領域、サブモニターへのピアノロール切り離し表示、リサイズ可能なスプリッターが標準機能として提供される。

#### プロセス分離による音声安全性の確保
GUI（`hbk-gui`）はエンジン（`hbk-play`）の別プロセスとして起動し、ゼロ依存の Protobuf ワイヤ通信（`dproto/`）を標準入出力経由で行う。GUI 側でウィンドウリサイズやダイアログ表示のブロックが発生した場合でも、音声再生スレッドには一切影響を与えない。

### 6.4 ステップ4段階的移行ロードマップ
GUI の移行は以下の 7 つのフェーズに分けて段階的に推進される。

```
[ フェーズ 4.1: ウィンドウ構築とテーマ基盤 ]
                     │
                     ▼
[ フェーズ 4.2: IPC クライアントと状態同期 ]
                     │
                     ▼
[ フェーズ 4.3: タイムラインと波形描画キャンバス ]
                     │
                     ▼
[ フェーズ 4.4: ピアノロールと MIDI エディター ]
                     │
                     ▼
[ フェーズ 4.5: セッションビューとクリップマトリクス ]
                     │
                     ▼
[ フェーズ 4.6: デバイスチェーンと操作ノブ ]
                     │
                     ▼
[ フェーズ 4.7: スクリプト REPL と完全性検証 ]
```

- **フェーズ 4.1: ウィンドウ構築とテーマ基盤**: `cimgui` および GLFW を Bazel で密閉ビルドし、ドッキングレイアウトと 6 種のカラーテーマ（`gui.theme`）を備えたネイティブウィンドウを立ち上げる。
- **フェーズ 4.2: IPC クライアントと状態同期**: `IpcClient.java` を D言語へ移植し、`hbk-play` をサブプロセスとして起動して 60 Hz で状態通知を受信・同期する。
- **フェーズ 4.3: タイムラインと波形描画キャンバス**: `ImDrawList` を用いた `TimelineCanvas` を実装し、トラックヘッダ、波形ピークエンベロープ、ベジェオートメーション曲線を描画・操作可能にする。
- **フェーズ 4.4: ピアノロールと MIDI エディター**: 128 鍵盤、ノート配置/リサイズ、スケールスナップ、ベロシティレーンを実装する。
- **フェーズ 4.5: セッションビューとクリップマトリクス**: 非線形クリップマトリクス、トラック別ストリップ、ステレオピークメーター、および音声/MIDI ファイルのドラッグ＆ドロップインポートを実装する。
- **フェーズ 4.6: デバイスチェーンと操作ノブ**: 共通 `drawArcKnob` ウィジェットを実装し、コンパイル時リフレクションにより全 30 種のデバイスパネルを自動生成する。外部 VST3 の子ウィンドウ埋め込みに対応する。
- **フェーズ 4.7: スクリプト REPL と完全性検証**: 軽量な QuickJS（<1 MB）を組み込み、TypeScript SDK による自動化スクリプトの互換性を確保する。`hibiki-gui-java` との完全な機能等価性を検証した上で JVM 依存を廃止する。

---

## 7. 開発・フォーマットおよびGitワークフロー

### 7.1 Gitブランチ `d` の運用とリモートPush手順
リポジトリ運用規準に従い、以下の規律を遵守する。
1. D言語移行に関わるすべての作業は、専用ブランチ `d` 上で実施する。
2. 作業開始前にカレントブランチを確認する。
   ```bash
   git branch --show-current  # 出力が 'd' であることを確認
   ```
3. リモートリポジトリへの初回 Push 時はアップストリーム追跡を設定する。
   ```bash
   git push -u origin d
   ```

### 7.2 コミット前フォーマット規準（`tools/format.sh`）
コードおよびドキュメントの変更をステージング・コミットする前に:
1. リポジトリ標準フォーマッタを実行する。
   ```bash
   ./tools/format.sh
   ```
2. フォーマット検証を実行し、終了コード 0 を確認する。
   ```bash
   ./tools/format.sh --check
   ```
3. Markdown 文書（`docs/*.md`）においても、D言語コードブロックの 4 スペースインデント、表の整列、および正確な言語タグを維持する。

### 7.3 マイルストーン別の論理的コミット規準
コミットはマイルストーン単位の論理的な粒度で作成し、Conventional Commits 規約に従う。`.agents/teamwork/` 内のオーケストレーション一時ファイルは決してコミットに含めない。

| マイルストーン | 対象スコープ | コミットメッセージ雛形 |
| :--- | :--- | :--- |
| **M1** | 設計ドキュメント | `docs: add comprehensive D language migration plan (English & Japanese)` |
| **M2** | ステップ1（Bazel + LDC） | `build: configure hermetic LDC toolchain rules and skip Android targets` |
| **M3** | ステップ2（D言語 Protobuf） | `feat(dproto): implement zero-dependency Protobuf wire codec and tests in D` |
| **M4** | ステップ3（コアエンジン） | `feat(engine): migrate core audio DSP and control modules to D` |
| **M5** | 最終検証 | `test: verify 100% full test suite pass and format compliance for D migration` |

### 7.4 完了検証チェックリスト
マイルストーン M1 を完了するにあたり、以下の項目を確認する。
- [x] `docs/d.md`（英語仕様書）が存在し、全 4 ステップを網羅して詳細に記述されている。
- [x] `docs/d_ja.md`（日本語仕様書）が存在し、`docs/d.md` と厳密な 1:1 の構造的等価性および用語統一を満たしている。
- [x] `./tools/format.sh --check` によるフォーマット検証が正常終了（コード 0）する。
- [x] ドキュメント成果物のみがステージング対象となり、`.agents/teamwork/` などの一時ファイルが除外されている。
- [x] ブランチ `d` 上でコミットが作成され、リモート `origin d` への Push が正常に完了している。
