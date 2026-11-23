# Hibiki `gui_d` ライブ・エンドツーエンド統合アーキテクチャ (`d_v2`)

> **目的**: `dlangui` ベースの Y2K/FL スタイル `gui_d` フロントエンドを、Protobuf / HBKI IPC 経由で C++/D リアルタイムオーディオエンジン `hbk-play` に直結した、完全動作するライブ DAW へ統合する設計・実装ドキュメントです。

---

## 1. 初期監査で判明したギャップと解決済み課題

従来の `gui_d` プロトタイプは FL 風ウィンドウ群（Toolbar、Browser、Channel Rack + Graph Editor、Playlist、Piano Roll、Mixer + FX Rack + 3-Band EQ、Plugin Wrapper）を描画しつつ `TransportModel` のローカル状態のみを保持しており、一方の `hbk-play`（`engine/main.d` + `engine/playback_thread.cpp` + `engine/commands/` + `engine/core/`）はリアルタイム 512 サンプルミキシング、内蔵インストゥルメント/エフェクト、VST3/CLAP ホスティング、MIDI/タイムライン/セッション再生、オートメーション、録音、WAV バウンスを既に備えていました。

Phase 1〜11 を通じて解決した主な断絶は以下の通りです：

1. **実行ファイル検出・プロセス寿命管理・IPC 安全性 (`gui_d/main.d`, `gui_d/ipc_client.d`)**
   - `resolveEnginePath` が実行バイナリ隣接パス、`../engine/hbk-play`、および Bazel runfiles（`RUNFILES_DIR` / `TEST_SRCDIR`）から `hbk-play` を自動検出します。
   - `decodeProtoOwned` が通知メッセージごとに独立した GC `Arena` を確保し、静的スクラッチバッファのエイリアシングを根絶しています。
   - `extractNextFrame` は不正/過大長フレーム（`len > MAX_IPC_FRAME_BYTES`）検出時に **1 バイト単位** で前方再同期を行い、アラインされていない正常 `HBKI` ヘッダを読み飛ばしません。
   - `EngineProcessClient` はバックグラウンドの `stderrLoop` スレッドで `hbk-play` の `stderr` を読み取り、`onError` コールバック経由で UI ヒントバーへエラーを通知します。
2. **起動時同期とトラックトポロジ**
   - `gui_d` は起動時に `model.initDefaultTemplate()` を `syncTemplateToEngine()` で `hbk-play` に同期し、`ProjectCmd::ACTION_CLEAR_HISTORY = 9` を送信してテンプレート初期化直後の Undo スタックをクリーンにします。
   - ジェネレータトラックは `100 + channelIdx`（Slot `0` に音源専用）に配置し、`output_track_index` で Mixer Insert の `GROUP` バス `1..12`（Slot `0..9` の FX チェーン専用）へ送り、さらにトポロジカル DAG 順で Master Track `0`（`TrackType::TRACK_MASTER`）へ合流させます。
3. **リアルタイム安全性の強化 (P0)**
   - オーディオコールバックは事前確保ワークスペース（`PlaybackThread::Workspace` および `RenderProjectWorkspace`）を用い、Biquad 3-Band EQ 係数のキャッシュ（`Track::EnsureEqCoeffs`、`Track::IsEqBypassed`）、ロックフリーの `std::atomic` `ProjectState` スカラー値、`tracks_mutex` / `plugins_mutex` 分離と `RenderTimeline` / `BounceProject` におけるスナップショットロックを実現しています。
   - `AudioEditor` の寿命に関する UAF を修正し、Browser の単クリック試聴とダブルクリック/ドラッグ割り当てを明確に分離しました。
4. **ライブ／オフライン統合 DSP (`RenderProjectAudioBlock`) とトポロジカル DAG ルーティング (P1)**
   - ライブ再生（`playback_thread.cpp`）とオフライン書き出し（`BounceProject`）の双方が共通の `RenderProjectAudioBlock(...)` を使用します：
     - **Pass 1**: `NORMAL` およびジェネレータトラック（`100+`）をレンダリング。
     - **Pass 2**: `GROUP` / `AUX` バス（`output_track_index`, `group_parent_index`, `aux_sends`）を Kahn のトポロジカルソート DAG 順で処理（コマンド層 `track_commands.cpp` では `WouldCreateRoutingCycle` BFS により循環ルーティングを拒否、レガシープロジェクト向けフォールバック付き）。
     - **Pass 3**: Master Track `0` の FX チェーン + EQ + Stereo Separation + `master_volume` + `master_pitch` + メトロノームを合成。
   - `BounceTrackClip` は `trim_start_beats`、`plugin_mix`、3-Band EQ・ステレオ分離（`d_apply_track_eq_and_stereo_sep_f32`）をユニティゲインで適用します。
5. **ステップシーケンサ、64 ステップ、ステップパン DSP、クリップ表現の一貫性 (P1/P2)**
   - `16 / 32 / 64` ステップ長切り替え（`SequencerCmd::ACTION_SET_PATTERN_LENGTH = 14`）、Graph Editor の `step_pans` を `MidiNoteEvent::pan` 経由で `BuiltinSampler` / `Builtin3xOsc` / `BuiltinDrumMachine` のボイス毎ステレオパンへ接続、およびシーケンサ全操作の `HistoryManager` Undo/Redo に対応しました。
   - `TimelineClipInfo.is_midi = 15` により純 MIDI / Pattern / Audio クリップを正確に識別し、ミニノート描画やダブルクリック動作（Audio は `AudioEditorLoad` + `PreviewPlay`、MIDI/Pattern は Piano Roll）を分岐します。
   - リモート VST3 エディタの `ColorDrawBuf` 変換では `PluginSlotState.editorRevision` により未更新フレームのピクセル再変換をスキップします。

---

## 2. ターゲット・エンドツーエンド構成

### 2.1 トラック構成と信号ルーティング DAG

```
  [Channel Generator Tracks (100 + channelIdx)]
       │  例: Ch 0 Kick    (track 100) ──> builtin://dr8_kick  ──┐
       │      Ch 1 Snare   (track 101) ──> builtin://dr8_snare ──┼── output_track_index = ch.mixerInsert (1..12 または 0)
       │      Ch 2 Hi-Hat  (track 102) ──> builtin://dr8_hat   ──┤
       │      Ch 3 Acid    (track 103) ──> builtin://acid_bass ──┤
       │      Ch 4 E-Piano (track 104) ──> builtin://epiano    ──┘
       ▼
  [Mixer Insert / Group / Aux Buses (1..12, TrackType = GROUP/AUX)]
       │  トポロジカル DAG パス (Pass 2) + WouldCreateRoutingCycle BFS 循環防止ガード
       │  Insert ごとの FX チェーン Slot 0..9 (plugin_mix ウェット/ドライ + bypass)
       │  係数キャッシュ済み 3-Band EQ (Low/Mid/High) + Stereo Separation + Pan + Fader + Mute/Solo/Record Arm
       │  Route ──> output_track_index = 0 (Master) または他 Insert/Group/Aux
       ▼
  [Master Bus (Track 0, TrackType = TRACK_MASTER, Pass 3)]
       │  Master FX Slots 0..9 + Master 3-Band EQ + Master StereoSep + Master Vol/Pitch + メトロノーム
       ▼
  [d_process_master_output / BounceProject WAV]
```

### 2.2 再生モード: `PAT` と `SONG`

- **`SONG` モード (`PLAYBACK_MODE_SONG`)**: タイムライン上のオーディオ/MIDI クリップ（`TimelineClip`）、パターン参照クリップ（`pattern_index`）、およびオートメーションレーン（動的 `pluginIndex` / `paramId`）を `RenderProjectAudioBlock` でサンプル精度再生します。
- **`PAT` モード (`PLAYBACK_MODE_PAT`)**: 選択中の Pattern（`length_steps` ∈ `{16, 32, 64}`、`activePatternLengthSteps()` / `currentStepIndex()`）をループ再生し、偶数・奇数 16 分音符に対する FL スタイルの **Swing**、Graph Editor の `velocity` / `pitch` / `pan` / `shift`、およびパターン内 Piano Roll ノートを演奏します。
- **録音プリロールとオーバーダブ**: ツールバーの録音ボタンは `countdownEnabled`（33ms UI タイマーによる 1 小節メトロノーム・プリロール後に `ACTION_RECORD` を発行）および `overdubEnabled`（既存テイク保持／クリア切替）を実行し、Mixer ストリップの `[R]` ボタンでレコードアームを切り替えられます。

### 2.3 プロトコルおよび DSP 拡張 (`pb/*.proto`)

- `pb/core.proto`:
  - `Track`: `eq_low` (`23`), `eq_mid` (`24`), `eq_high` (`25`), `stereo_sep` (`26`), `repeated float plugin_mix = 27`, `TrackType::TRACK_MASTER = 3`。
  - `Project`: `playback_mode` (`5`), `swing_amount` (`6`), `selected_pattern_index` (`7`), `channels` (`8`), `patterns` (`9`), `metronome_enabled` (`10`), `optional float master_volume = 11`, `master_pitch` (`12`), `loop_enabled` (`13`), `loop_start_sec` (`14`), `loop_end_sec` (`15`)。
  - `ChannelPatternData`: 最大 64 ステップまで拡張（`steps`, `step_velocities`, `step_pitches`, `step_pans`, `step_shifts`, `notes`）。
- `pb/commands.proto`:
  - `ProjectCmd`: `ACTION_CLEAR_HISTORY = 9`。
  - `SequencerCmd`: `ACTION_SET_PLAYBACK_MODE = 1` .. `ACTION_SET_CHANNEL_NOTES = 13`, `ACTION_SET_PATTERN_LENGTH = 14`。
  - `TrackCmd`: `ACTION_ARM_RECORD = 9`, `ACTION_SET_RECORD_MODE = 12`, `ACTION_SET_OUTPUT_ROUTING = 23`, `ACTION_SET_AUX_SEND = 24`, `ACTION_SET_GROUP_PARENT = 25`, `ACTION_SET_EQ = 26`, `ACTION_SET_STEREO_SEP = 27`。
  - `PluginCmd`: `ACTION_SET_BYPASS = 11`, `ACTION_REORDER_PLUGIN = 12`, `ACTION_SET_MIX = 13`。
- `pb/notifications.proto`:
  - `TimelineClipInfo`: `optional int32 pattern_index = 14`, `bool is_midi = 15`。
  - `PlayheadInfo`: `cpu_percent`, `oscilloscope`, `playback_mode`, `transport_state`。
  - `PluginDescription`: バンドル `path` (`4`) および `is_instrument` (`5`)。

---

## 3. フェーズ別実装サマリー (ブランチ `d`)

- **Phase 0–5**: 初期設計、`resolveEnginePath`、Master Bus・ライブテレメトリ、Channel Rack / Step Sequencer / `PAT` vs `SONG`、3-Band EQ / Stereo Sep / Wet-Dry FX Mix、Plugin Wrapper リモート VST3 GUI と計測/スペクトラム/スコープ、実ファイルスキャン・試聴、Save / Load / Bounce / Collect / Settings モーダル。
- **Phase 6 (`ff3d078`)**: 全リポジトリテスト検証とドキュメント反映。
- **Phase 7 (`d8a9fcb`)**: `decodeProtoOwned` の通知単位 GC `Arena` 化、macOS メインループ正常終了、クリップ/スロット/パターン同期修正、実 `hbk-play` 子プロセスを用いる `//gui_d:gui_e2e_test` 新設。
- **Phase 8 (`3374d31`, P0)**: ゼロアロケーション `PlaybackThread::Workspace`、EQ 係数キャッシュ、アトミック `ProjectState` スカラー値、`tracks_mutex` / `plugins_mutex` ロック分割、`AudioEditor` UAF 修正、Browser クリック/ドラッグ判別。
- **Phase 9 (`6259950`, P1)**: 統合 `RenderProjectAudioBlock`、Pass 2 トポロジカル DAG + `WouldCreateRoutingCycle` BFS 循環防止、ユニティゲイン `BounceTrackClip`、`Project` スカラー `10..15` 永続化、64 ステップ化（`ACTION_SET_PATTERN_LENGTH`）、ステップパン DSP（`BuiltinSampler`, `Builtin3xOsc`, `BuiltinDrumMachine`）、Sequencer Undo/Redo、`ACTION_CLEAR_HISTORY`、動的オートメーションターゲット、`TimelineClipInfo.is_midi`、および Audio vs MIDI/Pattern ダブルクリック導線統一。
- **Phase 10 (`b8ac57c`, P2 & 拡張 E2E)**: `stderr` スレッド & `onError` UI 表示、1 バイト再同期、`PluginWrapperWidget` の `editorRevision` フレームバッファキャッシュ、Mixer レコードアーム `[R]`、1 小節カウントダウン・プリロール & オーバーダブ制御、タイマー駆動 `PendingNoteOff[]` + `FocusOut` ノート解放、Timeline `SLIP`/`SELECT` + `DEL`、Piano Roll `snapBeats()` & `SELECT` + `DEL`、および `//gui_d:gui_e2e_test` 拡充（バウンス WAV 非無音波形ピーク検証、`PAT`/`SONG`、32 ステップ Undo/Redo、`ACTION_CLEAR_HISTORY`、`.hbk` 往復復元、`onError` 検証）。
- **Phase 11 (`bd343b9`)**: `docs/d_v2.md` の最新化、日本語ドキュメント `docs/d_v2_ja.md` の作成、最終フォーマットおよび全リポジトリ検証。
- **Phase 12 (`c7b86d8`)**: `GROUP`/`AUX` Pass 2 のタイムラインクリップ再生境界（`allow_timeline_clips`）、デフォルト `SONG` モードの `loop140.wav` 2 小節目 1 小節再生（`//testdata:testdata` 同梱）、空クリップに仮ノートを描画しない実パターン/MIDI プレビュー描画、および終了時 UI スレッド競合ガード（`uiClosing`, 冪等な `EngineProcessClient.stop()`）を修正。
- **Phase 13 (`e10f19e`)**: 終了時 `FT_Done_Face` GC クラッシュを `third_party/dlangui_ft_shutdown.patch`（`APP_IS_SHUTTING_DOWN` ガード）で解消し、`gui_d/browser_panel.d` の D `foreach` ループクロージャ捕捉バグ（任意デバイスクリックで末尾項目 `loop140.wav` が発火する問題）を修正。
- **Phase 14 (`04024e7`)**: クリップダブルクリック時の Piano Roll 表示同期（`selectPatternForPianoRoll`, `frameActiveNotes`, ステップノート描画）、ラッパー表示時の外部エディタ自動オープン、64 ステップ完全同期、実サンプルパス描画、および Channel Rack `+` / Mixer FX 全内蔵プラグイン導線を統一。
- **Phase 15**: `gui_d/device_panels.d` を新設して 21 種の Java Swing 内蔵デバイス UI（`EQ`, `Compressor`, `Limiter`, `Hott`, `Maxim`, `Delay`, `Reverb`, `Phaser`, `Chorus`, `Bitcrusher`, `EnvelopeShaper`, `Convolver`, `Vocodey`, `StereoWidth`, `AcidBass`, `3xOsc`, `Sampler`, `EPiano`, `Organ`, `DrumMachine`, `DR8_*`）を `FlPalette`/`retroBevel` 調和デザインで移植。マルチバンド計測値（`low/mid/high` `in_db`/`gr_db`）を接続し、`PluginWrapperWidget` でカスタム UI を `EDITOR` 二度押し不要で即時表示（UI レスプラグイン向けの汎用ノブ `PARAMS` フォールバック維持）。

---

## 4. 検証ステータス (`d` ブランチ)

- **`./tools/format.sh`**: C++（206）、Java（114）、`BUILD`、`.proto`（4）、D（76）ソースファイルの整形・検査を実行。
- **`bazel test //... -c opt --test_output=errors`**: **107 件中 105 件のテストターゲットが合格**（失敗 0 件、Android 実機/エミュレータ依存の 2 件 `//android/app:hibiki_engine_playback_test` および `//android/app:melody_sequencer_integration_test` はスキップ）。
  - `//engine/core:project_test`、`//engine/commands:commands_test`、`//engine:playback_d_test`、`//gui_d:gui_d_test`、および実プロセス `//gui_d:gui_e2e_test`（IPC ハンドシェイク、BPM・テンプレート同期、非無音 WAV バウンス、`PAT`/`SONG`、32 ステップ + Undo/Redo + `ACTION_CLEAR_HISTORY`、`.hbk` 保存・読込往復、`onError` 検出）を含みます。
- **`bazel build //gui_d:hibiki_gui //engine:hbk-play //engine:hbk-plugin-worker //engine:hbk-worker-daemon -c opt`**: 4 つのリリースバイナリすべてが終了コード `0` でビルド完了。
- **検証範囲の補足**: Bazel 上の自動検証はヘッドレス単体・DSP・ウィジェット（オフスクリーン `ColorDrawBuf`）・実 `hbk-play` IPC E2E スイートを対象としており、実 CoreAudio デバイスや外部 `.vst3` のデスクトップウィンドウ描画を用いたインタラクティブ操作は `bazel run //gui_d:hibiki_gui` による手動起動確認が対象となります。
