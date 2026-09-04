package hibiki.android;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import hibiki.android.engine.HibikiEngine;
import hibiki.android.model.ArrangerPattern;
import hibiki.android.model.ChannelState;
import hibiki.android.model.DrumPadItem;
import hibiki.android.model.ScaleType;
import hibiki.android.model.SynthMacro;
import hibiki.android.model.TrackerCell;
import hibiki.android.model.ViewMode;
import hibiki.android.ui.components.HeaderBarView;
import hibiki.android.ui.theme.ThemeColors;
import hibiki.android.ui.views.ArrangerView;
import hibiki.android.ui.views.InstrumentView;
import hibiki.android.ui.views.MixerView;
import hibiki.android.ui.views.ProjectView;
import hibiki.android.ui.views.TrackerView;
import java.util.ArrayList;
import java.util.List;

/**
 * Main Android Activity for Hibiki Mobile DAW.
 */
public class MainActivity extends Activity {
    private HeaderBarView headerBar;
    private FrameLayout contentContainer;

    private TrackerView trackerView;
    private ArrangerView arrangerView;
    private InstrumentView instrumentView;
    private MixerView mixerView;
    private ProjectView projectView;

    private ViewMode currentView = ViewMode.TRACKER;
    private boolean isPlaying = false;
    private boolean isRecording = false;
    private boolean isLooping = true;
    private double bpm = 128.0;
    private double playheadSec = 0.0;
    private int currentStepIndex = 0;

    private final List<ChannelState> channels = new ArrayList<>();
    private final List<ArrangerPattern> patterns = new ArrayList<>();
    private final List<DrumPadItem> drumPads = new ArrayList<>();
    private final List<SynthMacro> macros = new ArrayList<>();

    private int rootNoteIdx = 0; // C
    private ScaleType scaleType = ScaleType.PENTATONIC_MINOR;
    private int octave = 4;

    private final Handler uiHandler = new Handler(Looper.getMainLooper());
    private final Runnable syncRunnable = new Runnable() {
        @Override
        public void run() {
            if (isPlaying) {
                playheadSec = HibikiEngine.getPlaybackPosition();
                double beats = (playheadSec * (bpm / 60.0));
                currentStepIndex = ((int) (beats * 4)) % 16;
                float playheadBar = (float) (beats / 4.0);

                headerBar.updateState(isPlaying, isRecording, isLooping, bpm, playheadSec, currentView);
                trackerView.setCurrentStepIndex(currentStepIndex);
                arrangerView.setPlayheadBar(playheadBar);
            }
            uiHandler.postDelayed(this, 25);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Initialize in-process AAudio engine
        HibikiEngine.initEngine(44100, 50);

        initInitialState();
        buildUI();

        uiHandler.post(syncRunnable);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        uiHandler.removeCallbacks(syncRunnable);
        HibikiEngine.destroyEngine();
    }

    private void initInitialState() {
        // Initial 4-Track Groovebox State
        List<TrackerCell> t1Steps = new ArrayList<>();
        List<TrackerCell> t2Steps = new ArrayList<>();
        List<TrackerCell> t3Steps = new ArrayList<>();
        List<TrackerCell> t4Steps = new ArrayList<>();

        for (int s = 0; s < 16; s++) {
            if (s % 4 == 0) {
                t1Steps.add(new TrackerCell("C-", 3, 120, 0, "00", true));
            } else if (s % 4 == 2) {
                t1Steps.add(new TrackerCell("D-", 3, 90, 1, "00", true));
            } else {
                t1Steps.add(new TrackerCell());
            }

            if (s % 2 == 0) {
                t2Steps.add(new TrackerCell("A-", 2, 100, 2, "00", true));
            } else {
                t2Steps.add(new TrackerCell());
            }

            if (s == 0 || s == 3 || s == 6 || s == 10) {
                t3Steps.add(new TrackerCell("E-", 4, 110, 3, "00", true));
            } else {
                t3Steps.add(new TrackerCell());
            }

            if (s % 4 == 3) {
                t4Steps.add(new TrackerCell("C-", 5, 85, 4, "00", true));
            } else {
                t4Steps.add(new TrackerCell());
            }
        }

        channels.clear();
        channels.add(new ChannelState(0, "DRUMS", ThemeColors.CHANNEL_COLORS[0], 0.85f, 0.0f, false, false, t1Steps));
        channels.add(new ChannelState(1, "BASS", ThemeColors.CHANNEL_COLORS[1], 0.80f, -0.1f, false, false, t2Steps));
        channels.add(new ChannelState(2, "LEAD", ThemeColors.CHANNEL_COLORS[2], 0.75f, 0.2f, false, false, t3Steps));
        channels.add(new ChannelState(3, "PLUCK", ThemeColors.CHANNEL_COLORS[3], 0.70f, -0.2f, false, false, t4Steps));

        // Initial Arranger Patterns
        patterns.clear();
        patterns.add(new ArrangerPattern("p1", 0, 0f, 4f, "Drums A", ThemeColors.CHANNEL_COLORS[0]));
        patterns.add(new ArrangerPattern("p2", 0, 4f, 4f, "Drums B", ThemeColors.CHANNEL_COLORS[0]));
        patterns.add(new ArrangerPattern("p3", 1, 0f, 8f, "Acid Bass", ThemeColors.CHANNEL_COLORS[1]));
        patterns.add(new ArrangerPattern("p4", 2, 4f, 4f, "Lead Hook", ThemeColors.CHANNEL_COLORS[2]));
        patterns.add(new ArrangerPattern("p5", 3, 2f, 6f, "Pluck Arp", ThemeColors.CHANNEL_COLORS[3]));

        // Drum Pads
        drumPads.clear();
        drumPads.add(new DrumPadItem(0, "KICK", 36, ThemeColors.CHANNEL_COLORS[0]));
        drumPads.add(new DrumPadItem(1, "SNARE", 38, ThemeColors.CHANNEL_COLORS[1]));
        drumPads.add(new DrumPadItem(2, "CLP-HAT", 42, ThemeColors.CHANNEL_COLORS[2]));
        drumPads.add(new DrumPadItem(3, "OPN-HAT", 46, ThemeColors.CHANNEL_COLORS[3]));
        drumPads.add(new DrumPadItem(4, "CLAP", 39, ThemeColors.CHANNEL_COLORS[4]));
        drumPads.add(new DrumPadItem(5, "TOM", 45, ThemeColors.CHANNEL_COLORS[5]));
        drumPads.add(new DrumPadItem(6, "PERC", 56, ThemeColors.CHANNEL_COLORS[0]));
        drumPads.add(new DrumPadItem(7, "FX HIT", 49, ThemeColors.CHANNEL_COLORS[1]));

        // Macros
        macros.clear();
        macros.add(new SynthMacro("m1", "CUTOFF", 0.75f));
        macros.add(new SynthMacro("m2", "RESO", 0.40f));
        macros.add(new SynthMacro("m3", "ATTACK", 0.05f));
        macros.add(new SynthMacro("m4", "DECAY", 0.50f));
    }

    private void buildUI() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(ThemeColors.BG_OLED_BLACK);
        root.setLayoutParams(new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // 1. Header Bar
        headerBar = new HeaderBarView(this);
        headerBar.setHeaderActionListener(new HeaderBarView.HeaderActionListener() {
            @Override
            public void onTogglePlay() {
                isPlaying = !isPlaying;
                HibikiEngine.setPlayback(isPlaying);
                headerBar.updateState(isPlaying, isRecording, isLooping, bpm, playheadSec, currentView);
            }

            @Override
            public void onStop() {
                isPlaying = false;
                HibikiEngine.setPlayback(false);
                playheadSec = 0.0;
                currentStepIndex = 0;
                headerBar.updateState(isPlaying, isRecording, isLooping, bpm, playheadSec, currentView);
                trackerView.setCurrentStepIndex(0);
                arrangerView.setPlayheadBar(0.0f);
            }

            @Override
            public void onToggleRecord() {
                isRecording = !isRecording;
                headerBar.updateState(isPlaying, isRecording, isLooping, bpm, playheadSec, currentView);
            }

            @Override
            public void onToggleLoop() {
                isLooping = !isLooping;
                headerBar.updateState(isPlaying, isRecording, isLooping, bpm, playheadSec, currentView);
            }

            @Override
            public void onBpmChange(double newBpm) {
                bpm = newBpm;
                HibikiEngine.setBpm(newBpm);
                headerBar.updateState(isPlaying, isRecording, isLooping, bpm, playheadSec, currentView);
            }

            @Override
            public void onSelectView(ViewMode mode) {
                switchView(mode);
            }
        });
        root.addView(headerBar);

        // 2. View Container
        contentContainer = new FrameLayout(this);
        LinearLayout.LayoutParams containerParams =
                new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f);
        contentContainer.setLayoutParams(containerParams);

        // Create Views
        trackerView = new TrackerView(this);
        trackerView.setChannels(channels, currentStepIndex);
        trackerView.setOnTrackerCellEditedListener((chIdx, stepIdx, newCell) -> {
            if (chIdx < channels.size()) {
                ChannelState updated = channels.get(chIdx).withStep(stepIdx, newCell);
                channels.set(chIdx, updated);
                trackerView.setChannels(channels, currentStepIndex);
            }
        });
        trackerView.setOnChannelToggleListener(new TrackerView.OnChannelToggleListener() {
            @Override
            public void onToggleMute(int channelIdx) {
                if (channelIdx < channels.size()) {
                    ChannelState updated = channels.get(channelIdx).withMuted(!channels.get(channelIdx).isMuted());
                    channels.set(channelIdx, updated);
                    trackerView.setChannels(channels, currentStepIndex);
                    mixerView.setChannels(channels);
                }
            }

            @Override
            public void onToggleSolo(int channelIdx) {
                if (channelIdx < channels.size()) {
                    ChannelState updated = channels.get(channelIdx).withSoloed(!channels.get(channelIdx).isSoloed());
                    channels.set(channelIdx, updated);
                    trackerView.setChannels(channels, currentStepIndex);
                    mixerView.setChannels(channels);
                }
            }
        });
        contentContainer.addView(trackerView);

        arrangerView = new ArrangerView(this);
        arrangerView.setArrangement(channels, patterns, 0.0f);
        arrangerView.setOnArrangerActionListener(new ArrangerView.OnArrangerActionListener() {
            @Override
            public void onAddPattern(int trackIndex, float startBar) {
                String id = "pat_" + System.currentTimeMillis();
                int color = (trackIndex < channels.size()) ? channels.get(trackIndex).getColor() : ThemeColors.ACCENT_CYAN;
                patterns.add(new ArrangerPattern(id, trackIndex, startBar, 4.0f, "Pattern " + (patterns.size() + 1), color));
                arrangerView.setArrangement(channels, patterns, (float) (playheadSec * (bpm / 60.0) / 4.0));
            }

            @Override
            public void onRemovePattern(String patternId) {
                patterns.removeIf(p -> p.getId().equals(patternId));
                arrangerView.setArrangement(channels, patterns, (float) (playheadSec * (bpm / 60.0) / 4.0));
            }

            @Override
            public void onToggleMute(int trackIndex) {
                if (trackIndex < channels.size()) {
                    ChannelState updated = channels.get(trackIndex).withMuted(!channels.get(trackIndex).isMuted());
                    channels.set(trackIndex, updated);
                    arrangerView.setArrangement(channels, patterns, (float) (playheadSec * (bpm / 60.0) / 4.0));
                    trackerView.setChannels(channels, currentStepIndex);
                    mixerView.setChannels(channels);
                }
            }
        });
        contentContainer.addView(arrangerView);

        instrumentView = new InstrumentView(this);
        instrumentView.setInstrumentsData(drumPads, macros, rootNoteIdx, scaleType, octave);
        instrumentView.setOnInstrumentActionListener(new InstrumentView.OnInstrumentActionListener() {
            @Override
            public void onTriggerPad(DrumPadItem pad) {
                // Future: Send drum trigger to JNI
            }

            @Override
            public void onTriggerNote(int midiNote, String noteName) {
                // Future: Send note trigger to JNI
            }

            @Override
            public void onMacroChange(String macroId, float newValue) {
                for (int i = 0; i < macros.size(); i++) {
                    if (macros.get(i).getId().equals(macroId)) {
                        macros.set(i, macros.get(i).withValue(newValue));
                        break;
                    }
                }
            }

            @Override
            public void onScaleConfigChange(int newRootNote, ScaleType newScaleType, int newOctave) {
                rootNoteIdx = newRootNote;
                scaleType = newScaleType;
                octave = newOctave;
            }
        });
        contentContainer.addView(instrumentView);

        mixerView = new MixerView(this);
        mixerView.setChannels(channels);
        mixerView.setOnMixerActionListener(new MixerView.OnMixerActionListener() {
            @Override
            public void onVolumeChange(int channelIdx, float newVolume) {
                if (channelIdx < channels.size()) {
                    channels.set(channelIdx, channels.get(channelIdx).withVolume(newVolume));
                }
            }

            @Override
            public void onPanChange(int channelIdx, float newPan) {
                if (channelIdx < channels.size()) {
                    channels.set(channelIdx, channels.get(channelIdx).withPan(newPan));
                }
            }

            @Override
            public void onToggleMute(int channelIdx) {
                if (channelIdx < channels.size()) {
                    ChannelState updated = channels.get(channelIdx).withMuted(!channels.get(channelIdx).isMuted());
                    channels.set(channelIdx, updated);
                    mixerView.setChannels(channels);
                    trackerView.setChannels(channels, currentStepIndex);
                }
            }

            @Override
            public void onToggleSolo(int channelIdx) {
                if (channelIdx < channels.size()) {
                    ChannelState updated = channels.get(channelIdx).withSoloed(!channels.get(channelIdx).isSoloed());
                    channels.set(channelIdx, updated);
                    mixerView.setChannels(channels);
                    trackerView.setChannels(channels, currentStepIndex);
                }
            }
        });
        contentContainer.addView(mixerView);

        projectView = new ProjectView(this);
        projectView.setProjectInfo("New Beat 01", bpm, 50);
        projectView.setOnProjectActionListener(new ProjectView.OnProjectActionListener() {
            @Override
            public void onLoadDemoSong(String songName) {
                // Reload demo templates
            }

            @Override
            public void onResetEngine() {
                HibikiEngine.destroyEngine();
                HibikiEngine.initEngine(44100, 50);
            }
        });
        contentContainer.addView(projectView);

        root.addView(contentContainer);
        setContentView(root);

        switchView(ViewMode.TRACKER);
    }

    private void switchView(ViewMode mode) {
        currentView = mode;
        headerBar.updateState(isPlaying, isRecording, isLooping, bpm, playheadSec, currentView);

        trackerView.setVisibility(mode == ViewMode.TRACKER ? View.VISIBLE : View.GONE);
        arrangerView.setVisibility(mode == ViewMode.ARRANGER ? View.VISIBLE : View.GONE);
        instrumentView.setVisibility(mode == ViewMode.INSTRUMENT ? View.VISIBLE : View.GONE);
        mixerView.setVisibility(mode == ViewMode.MIXER ? View.VISIBLE : View.GONE);
        projectView.setVisibility(mode == ViewMode.PROJECT ? View.VISIBLE : View.GONE);

        if (mode == ViewMode.TRACKER) {
            trackerView.setChannels(channels, currentStepIndex);
        } else if (mode == ViewMode.ARRANGER) {
            arrangerView.setArrangement(channels, patterns, (float) (playheadSec * (bpm / 60.0) / 4.0));
        } else if (mode == ViewMode.MIXER) {
            mixerView.setChannels(channels);
        } else if (mode == ViewMode.PROJECT) {
            projectView.setProjectInfo("New Beat 01", bpm, 50);
        }
    }

    public HeaderBarView getHeaderBar() { return headerBar; }
    public FrameLayout getContentContainer() { return contentContainer; }
    public TrackerView getTrackerView() { return trackerView; }
    public ArrangerView getArrangerView() { return arrangerView; }
    public InstrumentView getInstrumentView() { return instrumentView; }
    public MixerView getMixerView() { return mixerView; }
    public ProjectView getProjectView() { return projectView; }
    public ViewMode getCurrentView() { return currentView; }
    public boolean isPlaying() { return isPlaying; }
    public boolean isRecording() { return isRecording; }
    public boolean isLooping() { return isLooping; }
    public double getBpm() { return bpm; }
    public double getPlayheadSec() { return playheadSec; }
    public int getCurrentStepIndex() { return currentStepIndex; }
    public List<ChannelState> getChannels() { return channels; }
    public List<ArrangerPattern> getPatterns() { return patterns; }
    public List<DrumPadItem> getDrumPads() { return drumPads; }
    public List<SynthMacro> getMacros() { return macros; }
    public int getRootNoteIdx() { return rootNoteIdx; }
    public ScaleType getScaleType() { return scaleType; }
    public int getOctave() { return octave; }
    public void performSwitchView(ViewMode mode) { switchView(mode); }
}
