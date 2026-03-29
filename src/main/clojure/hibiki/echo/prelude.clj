(ns hibiki.echo.prelude
  "REPL development utilities for the Echo hybrid frontend.
   Start with:  clj -M:echo:dev
   Connect:     rlwrap nc localhost 5555

   All IPC helpers use short, REPL-friendly names:
     (play!) (stop!) (seek! 4.0)
     (load-plugin! 0 \"/path/to/Dexed.vst3\")
     (load-clip! 0 0 \"/path/to/beat.mid\")
     (set-param! 0 0 42 0.75)
     (set-bpm! 140)
     (write-midi! 0 0 480 [{:tick 0 :pitch 60 :dur 480 :vel 100}])
     (save! \"/tmp/my-project.hbk\")"
  (:require [hibiki.echo :as echo])
  (:import [hibiki.ui SessionView TimelineView Theme Theme$Preset]
           [hibiki.pb.commands Request TransportCmd TrackCmd PluginCmd
                              AutomationCmd MidiCmd ProjectCmd]
           [hibiki.pb.core EntityRef Clip AutomationPoint MidiEvent]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; Component accessors
;; ---------------------------------------------------------------------------

(defn session  "Returns the SessionView singleton."  ^SessionView  [] (SessionView/getInstance))
(defn timeline "Returns the TimelineView singleton." ^TimelineView [] (TimelineView/getInstance))
(defn theme    "Returns the Theme singleton."        ^Theme        [] (Theme/getInstance))

;; ---------------------------------------------------------------------------
;; Live manipulation
;; ---------------------------------------------------------------------------

(defn theme!
  "Switch theme preset live. Available presets:
   :ableton-dark, :ableton-light, :solarized-dark, :solarized-light, :win95, :winxp"
  [preset-kw]
  (let [^Theme t (theme)
        preset (case preset-kw
                 :ableton-dark    Theme$Preset/ABLETON_DARK
                 :ableton-light   Theme$Preset/ABLETON_LIGHT
                 :solarized-dark  Theme$Preset/SOLARIZED_DARK
                 :solarized-light Theme$Preset/SOLARIZED_LIGHT
                 :win95           Theme$Preset/WIN95
                 :winxp           Theme$Preset/WINXP
                 (throw (IllegalArgumentException.
                          (str "Unknown preset: " preset-kw))))]
    (.update t preset (.getScaling t) (.getBaseFontSize t))))

(defn reload!
  "Re-create the MainView in the running frame (useful after Java changes)."
  []
  (when-let [^javax.swing.JFrame f (echo/frame)]
    (javax.swing.SwingUtilities/invokeLater
     (fn []
       (.removeAll (.getContentPane f))
       (.add f (hibiki.ui.MainView.))
       (.revalidate f)
       (.repaint f)))))

;; ---------------------------------------------------------------------------
;; Internal — request dispatch
;; ---------------------------------------------------------------------------

(defn- send! [^Request request]
  (.sendRequest (hibiki.BackendManager/getInstance) request))

(defn- ->ref
  "Build an EntityRef from keyword args."
  ^EntityRef [& {:keys [track plugin slot clip lane]
                 :or {track 0 plugin 0 slot 0 clip -1 lane 0}}]
  (-> (EntityRef/newBuilder)
      (.setTrackIndex (int track))
      (.setPluginIndex (int plugin))
      (.setSessionSlot (int slot))
      (.setTimelineClip (int clip))
      (.setLaneIndex (int lane))
      (.build)))

;; ---------------------------------------------------------------------------
;; Transport — play / stop / seek
;; ---------------------------------------------------------------------------

(defn play!
  "Start playback.  (play!)"
  []
  (send! (-> (Request/newBuilder)
             (.setTransport (-> (TransportCmd/newBuilder)
                                (.setAction TransportCmd$Action/ACTION_PLAY)))
             (.build))))

(defn stop!
  "Stop playback.  (stop!)"
  []
  (send! (-> (Request/newBuilder)
             (.setTransport (-> (TransportCmd/newBuilder)
                                (.setAction TransportCmd$Action/ACTION_STOP)))
             (.build))))

(defn seek!
  "Seek to position in beats.  (seek! 4.0)"
  [pos]
  (send! (-> (Request/newBuilder)
             (.setTransport (-> (TransportCmd/newBuilder)
                                (.setAction TransportCmd$Action/ACTION_SEEK)
                                (.setSeekPos (float pos))))
             (.build))))

;; ---------------------------------------------------------------------------
;; Track — clips, slots, scenes
;; ---------------------------------------------------------------------------

(defn load-clip!
  "Load an audio/MIDI clip into a session slot.
   (load-clip! 0 0 \"/path/to/beat.mid\")
   (load-clip! 0 0 \"/path/to/loop.wav\" :loop true)"
  [track-idx slot-idx ^String path & {:keys [loop] :or {loop false}}]
  (send! (-> (Request/newBuilder)
             (.setTrack (-> (TrackCmd/newBuilder)
                            (.setAction TrackCmd$Action/ACTION_LOAD_CLIP)
                            (.setTarget (->ref :track track-idx :slot slot-idx))
                            (.setClipData (-> (Clip/newBuilder)
                                              (.setPath path)
                                              (.setIsLoop (boolean loop))))))
             (.build))))

(defn play-clip!
  "Trigger a session slot.  (play-clip! 0 0)"
  [track-idx slot-idx]
  (send! (-> (Request/newBuilder)
             (.setTrack (-> (TrackCmd/newBuilder)
                            (.setAction TrackCmd$Action/ACTION_PLAY_SLOT)
                            (.setTarget (->ref :track track-idx :slot slot-idx))))
             (.build))))

(defn stop-track!
  "Stop a track.  (stop-track! 0)"
  [track-idx]
  (send! (-> (Request/newBuilder)
             (.setTrack (-> (TrackCmd/newBuilder)
                            (.setAction TrackCmd$Action/ACTION_STOP)
                            (.setTarget (->ref :track track-idx))))
             (.build))))

(defn delete-clip!
  "Delete a clip from a session slot.  (delete-clip! 0 0)"
  [track-idx slot-idx]
  (send! (-> (Request/newBuilder)
             (.setTrack (-> (TrackCmd/newBuilder)
                            (.setAction TrackCmd$Action/ACTION_DELETE_CLIP)
                            (.setTarget (->ref :track track-idx :slot slot-idx))))
             (.build))))

(defn set-clip-loop!
  "Toggle looping on a clip.  (set-clip-loop! 0 0 true)"
  [track-idx slot-idx loop?]
  (send! (-> (Request/newBuilder)
             (.setTrack (-> (TrackCmd/newBuilder)
                            (.setAction TrackCmd$Action/ACTION_SET_CLIP_LOOP)
                            (.setTarget (->ref :track track-idx :slot slot-idx))
                            (.setFlag (boolean loop?))))
             (.build))))

(defn add-timeline-clip!
  "Add a clip to the timeline.
   (add-timeline-clip! 0 \"/path/to/beat.mid\" 0.0 4.0)"
  [track-idx ^String path start-beats duration-beats]
  (send! (-> (Request/newBuilder)
             (.setTrack (-> (TrackCmd/newBuilder)
                            (.setAction TrackCmd$Action/ACTION_ADD_TIMELINE_CLIP)
                            (.setTarget (->ref :track track-idx))
                            (.setClipData (-> (Clip/newBuilder)
                                              (.setPath path)
                                              (.setDurationBeats (float duration-beats))))
                            (.setValue (float start-beats))))
             (.build))))

(defn remove-timeline-clip!
  "Remove a clip from the timeline.  (remove-timeline-clip! 0 0)"
  [track-idx clip-idx]
  (send! (-> (Request/newBuilder)
             (.setTrack (-> (TrackCmd/newBuilder)
                            (.setAction TrackCmd$Action/ACTION_REMOVE_TIMELINE_CLIP)
                            (.setTarget (->ref :track track-idx :clip clip-idx))))
             (.build))))

;; ---------------------------------------------------------------------------
;; Plugin — load / remove / GUI / params
;; ---------------------------------------------------------------------------

(defn load-plugin!
  "Load a VST3 plugin onto a track.
   (load-plugin! 0 \"/path/to/Dexed.vst3\")        ;; first sub-plugin
   (load-plugin! 0 \"/path/to/Dexed.vst3\" 1)      ;; second sub-plugin"
  ([track-idx path] (load-plugin! track-idx path 0))
  ([track-idx ^String path plugin-idx]
   (send! (-> (Request/newBuilder)
              (.setPlugin (-> (PluginCmd/newBuilder)
                              (.setAction PluginCmd$Action/ACTION_LOAD)
                              (.setTarget (->ref :track track-idx :plugin plugin-idx))
                              (.setPath path)))
              (.build)))))

(defn remove-plugin!
  "Remove a plugin from a track.  (remove-plugin! 0 0)"
  [track-idx plugin-idx]
  (send! (-> (Request/newBuilder)
             (.setPlugin (-> (PluginCmd/newBuilder)
                             (.setAction PluginCmd$Action/ACTION_REMOVE)
                             (.setTarget (->ref :track track-idx :plugin plugin-idx))))
             (.build))))

(defn show-plugin-gui!
  "Open the native GUI window for a plugin.  (show-plugin-gui! 0 0)"
  [track-idx plugin-idx]
  (send! (-> (Request/newBuilder)
             (.setPlugin (-> (PluginCmd/newBuilder)
                             (.setAction PluginCmd$Action/ACTION_SHOW_GUI)
                             (.setTarget (->ref :track track-idx :plugin plugin-idx))))
             (.build))))

(defn set-param!
  "Set a plugin parameter value (0.0–1.0).
   (set-param! 0 0 42 0.75)  ;; track 0, plugin 0, param 42 = 75%"
  [track-idx plugin-idx param-id value]
  (send! (-> (Request/newBuilder)
             (.setPlugin (-> (PluginCmd/newBuilder)
                             (.setAction PluginCmd$Action/ACTION_SET_PARAM)
                             (.setTarget (->ref :track track-idx :plugin plugin-idx))
                             (.setParamId (int param-id))
                             (.setParamValue (float value))))
             (.build))))

(defn list-plugins!
  "Scan a VST3 bundle and list available sub-plugins.
   (list-plugins! \"/path/to/Dexed.vst3\")"
  [^String path]
  (send! (-> (Request/newBuilder)
             (.setPlugin (-> (PluginCmd/newBuilder)
                             (.setAction PluginCmd$Action/ACTION_LIST)
                             (.setPath path)))
             (.build))))

;; ---------------------------------------------------------------------------
;; MIDI — read / write clip events
;; ---------------------------------------------------------------------------

(defn write-midi!
  "Write MIDI events to a clip. Notes are maps of {:tick :pitch :dur :vel}.
   (write-midi! 0 0 480 [{:tick 0 :pitch 60 :dur 480 :vel 100}])
   Track 0, slot 0, resolution 480 PPQ, session clip (clip-idx -1)."
  ([track-idx slot-idx resolution notes]
   (write-midi! track-idx slot-idx -1 resolution notes))
  ([track-idx slot-idx clip-idx resolution notes]
   (let [builder (-> (MidiCmd/newBuilder)
                     (.setAction MidiCmd$Action/ACTION_UPDATE)
                     (.setTarget (->ref :track track-idx :slot slot-idx :clip clip-idx))
                     (.setResolution (int resolution)))]
     (doseq [{:keys [tick pitch dur vel]} notes]
       (.addEvents builder
         (-> (MidiEvent/newBuilder)
             (.setTick (long tick))
             (.setPitch (int pitch))
             (.setDurationTicks (long dur))
             (.setVelocity (int vel)))))
     (send! (-> (Request/newBuilder)
                (.setMidi builder)
                (.build))))))

(defn get-midi!
  "Request MIDI data for a clip (response arrives as notification).
   (get-midi! 0 0)  ;; track 0, slot 0, session clip"
  ([track-idx slot-idx] (get-midi! track-idx slot-idx -1))
  ([track-idx slot-idx clip-idx]
   (send! (-> (Request/newBuilder)
              (.setMidi (-> (MidiCmd/newBuilder)
                            (.setAction MidiCmd$Action/ACTION_GET)
                            (.setTarget (->ref :track track-idx :slot slot-idx :clip clip-idx))))
              (.build)))))

;; ---------------------------------------------------------------------------
;; Automation — lanes and points
;; ---------------------------------------------------------------------------

(defn add-automation!
  "Add an automation lane for a plugin parameter.
   (add-automation! 0 0 42)  ;; track 0, plugin 0, param 42"
  [track-idx plugin-idx param-id]
  (send! (-> (Request/newBuilder)
             (.setAutomation (-> (AutomationCmd/newBuilder)
                                 (.setAction AutomationCmd$Action/ACTION_ADD_LANE)
                                 (.setTarget (->ref :track track-idx :plugin plugin-idx))
                                 (.setParamId (int param-id))))
             (.build))))

(defn remove-automation!
  "Remove an automation lane.  (remove-automation! 0 0)"
  [track-idx lane-idx]
  (send! (-> (Request/newBuilder)
             (.setAutomation (-> (AutomationCmd/newBuilder)
                                 (.setAction AutomationCmd$Action/ACTION_REMOVE_LANE)
                                 (.setTarget (->ref :track track-idx :lane lane-idx))))
             (.build))))

(defn set-automation!
  "Update automation points for a lane.
   Points are vectors of [time-beats value tension].
   (set-automation! 0 0 [[0 0.0 0] [4 1.0 0.5] [8 0.0 0]])"
  [track-idx lane-idx points]
  (let [builder (-> (AutomationCmd/newBuilder)
                    (.setAction AutomationCmd$Action/ACTION_UPDATE_POINTS)
                    (.setTarget (->ref :track track-idx :lane lane-idx)))]
    (doseq [[t v tension] points]
      (.addPoints builder
        (-> (AutomationPoint/newBuilder)
            (.setTimeBeats (float t))
            (.setValue (float v))
            (.setTension (float tension)))))
    (send! (-> (Request/newBuilder)
               (.setAutomation builder)
               (.build)))))

(defn get-automation!
  "Request automation lanes data for a track.  (get-automation! 0)"
  [track-idx]
  (send! (-> (Request/newBuilder)
             (.setAutomation (-> (AutomationCmd/newBuilder)
                                 (.setAction AutomationCmd$Action/ACTION_GET_LANES)
                                 (.setTarget (->ref :track track-idx))))
             (.build))))

;; ---------------------------------------------------------------------------
;; Project — save / load / bpm / undo
;; ---------------------------------------------------------------------------

(defn save!
  "Save the project.  (save! \"/tmp/my-project.hbk\")"
  [^String path]
  (send! (-> (Request/newBuilder)
             (.setProject (-> (ProjectCmd/newBuilder)
                              (.setAction ProjectCmd$Action/ACTION_SAVE)
                              (.setPath path)))
             (.build))))

(defn load!
  "Load a project.  (load! \"/tmp/my-project.hbk\")"
  [^String path]
  (send! (-> (Request/newBuilder)
             (.setProject (-> (ProjectCmd/newBuilder)
                              (.setAction ProjectCmd$Action/ACTION_LOAD)
                              (.setPath path)))
             (.build))))

(defn set-bpm!
  "Set the project BPM.  (set-bpm! 140)"
  [bpm]
  (send! (-> (Request/newBuilder)
             (.setProject (-> (ProjectCmd/newBuilder)
                              (.setAction ProjectCmd$Action/ACTION_SET_BPM)
                              (.setBpm (float bpm))))
             (.build))))

(defn undo! "Undo last action.  (undo!)" []
  (send! (-> (Request/newBuilder)
             (.setProject (-> (ProjectCmd/newBuilder)
                              (.setAction ProjectCmd$Action/ACTION_UNDO)))
             (.build))))

(defn redo! "Redo last undone action.  (redo!)" []
  (send! (-> (Request/newBuilder)
             (.setProject (-> (ProjectCmd/newBuilder)
                              (.setAction ProjectCmd$Action/ACTION_REDO)))
             (.build))))

(defn bounce!
  "Bounce to WAV.  (bounce! \"/tmp/output.wav\")"
  [^String path]
  (send! (-> (Request/newBuilder)
             (.setProject (-> (ProjectCmd/newBuilder)
                              (.setAction ProjectCmd$Action/ACTION_BOUNCE)
                              (.setPath path)))
             (.build))))

;; ---------------------------------------------------------------------------
;; Entry point — GUI + Socket REPL
;; ---------------------------------------------------------------------------

(defn -main [& _args]
  (clojure.core.server/start-server
   {:port 5555 :name "echo-repl" :accept 'clojure.core.server/repl})
  (println "Echo REPL started on port 5555")
  (echo/-main))
