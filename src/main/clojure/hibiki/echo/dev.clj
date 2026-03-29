(ns hibiki.echo.dev
  "REPL development utilities for the Echo hybrid frontend.
   Start with:  clj -M:echo:dev
   Connect:     rlwrap nc localhost 5555"
  (:require [hibiki.echo :as echo])
  (:import [hibiki.ui SessionView TimelineView Theme Theme$Preset]
           [hibiki.pb.commands Request PluginCmd AutomationCmd]
           [hibiki.pb.core AutomationPoint EntityRef]))

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
;; Plugin helpers (protobuf)
;; ---------------------------------------------------------------------------

(defn- send-request! [^Request request]
  (.sendRequest (hibiki.BackendManager/getInstance) request))

(defn load-plugin!
  "Load a VST3 plugin onto a track.
   (load-plugin! 0 \"/path/to/Dexed.vst3\")       ;; first sub-plugin
   (load-plugin! 0 \"/path/to/Dexed.vst3\" 1)     ;; second sub-plugin"
  ([track-idx path] (load-plugin! track-idx path 0))
  ([track-idx ^String path plugin-idx]
   (send-request!
     (-> (Request/newBuilder)
         (.setPlugin (-> (PluginCmd/newBuilder)
                         (.setAction hibiki.pb.commands.PluginCmd$Action/ACTION_LOAD)
                         (.setTarget (-> (EntityRef/newBuilder)
                                         (.setTrackIndex (int track-idx))
                                         (.setPluginIndex (int plugin-idx))))
                         (.setPath path)))
         (.build)))))

(defn remove-plugin!
  "Remove a plugin from a track.
   (remove-plugin! 0 0)  ;; remove plugin 0 from track 0"
  [track-idx plugin-idx]
  (send-request!
    (-> (Request/newBuilder)
        (.setPlugin (-> (PluginCmd/newBuilder)
                        (.setAction hibiki.pb.commands.PluginCmd$Action/ACTION_REMOVE)
                        (.setTarget (-> (EntityRef/newBuilder)
                                        (.setTrackIndex (int track-idx))
                                        (.setPluginIndex (int plugin-idx))))))
        (.build))))

(defn show-plugin-gui!
  "Open the native GUI window for a plugin.
   (show-plugin-gui! 0 0)  ;; track 0, plugin 0"
  [track-idx plugin-idx]
  (send-request!
    (-> (Request/newBuilder)
        (.setPlugin (-> (PluginCmd/newBuilder)
                        (.setAction hibiki.pb.commands.PluginCmd$Action/ACTION_SHOW_GUI)
                        (.setTarget (-> (EntityRef/newBuilder)
                                        (.setTrackIndex (int track-idx))
                                        (.setPluginIndex (int plugin-idx))))))
        (.build))))

(defn set-param!
  "Set a plugin parameter value (0.0–1.0).
   (set-param! 0 0 42 0.75)  ;; track 0, plugin 0, param 42 = 75%"
  [track-idx plugin-idx param-id value]
  (send-request!
    (-> (Request/newBuilder)
        (.setPlugin (-> (PluginCmd/newBuilder)
                        (.setAction hibiki.pb.commands.PluginCmd$Action/ACTION_SET_PARAM)
                        (.setTarget (-> (EntityRef/newBuilder)
                                        (.setTrackIndex (int track-idx))
                                        (.setPluginIndex (int plugin-idx))))
                        (.setParamId (int param-id))
                        (.setParamValue (float value))))
        (.build))))

(defn list-plugins!
  "Scan a VST3 bundle and list available sub-plugins.
   (list-plugins! \"/path/to/Dexed.vst3\")"
  [^String path]
  (send-request!
    (-> (Request/newBuilder)
        (.setPlugin (-> (PluginCmd/newBuilder)
                        (.setAction hibiki.pb.commands.PluginCmd$Action/ACTION_LIST)
                        (.setPath path)))
        (.build))))

;; ---------------------------------------------------------------------------
;; Automation helpers (protobuf)
;; ---------------------------------------------------------------------------

(defn add-automation!
  "Add an automation lane for a plugin parameter.
   (add-automation! 0 0 42)  ;; track 0, plugin 0, param 42"
  [track-idx plugin-idx param-id]
  (send-request!
    (-> (Request/newBuilder)
        (.setAutomation (-> (AutomationCmd/newBuilder)
                            (.setAction hibiki.pb.commands.AutomationCmd$Action/ACTION_ADD_LANE)
                            (.setTarget (-> (EntityRef/newBuilder)
                                            (.setTrackIndex (int track-idx))
                                            (.setPluginIndex (int plugin-idx))))
                            (.setParamId (int param-id))))
        (.build))))

(defn remove-automation!
  "Remove an automation lane.
   (remove-automation! 0 0)  ;; track 0, lane 0"
  [track-idx lane-idx]
  (send-request!
    (-> (Request/newBuilder)
        (.setAutomation (-> (AutomationCmd/newBuilder)
                            (.setAction hibiki.pb.commands.AutomationCmd$Action/ACTION_REMOVE_LANE)
                            (.setTarget (-> (EntityRef/newBuilder)
                                            (.setTrackIndex (int track-idx))
                                            (.setLaneIndex (int lane-idx))))))
        (.build))))

(defn set-automation!
  "Update automation points for a lane.
   Points are vectors of [time-beats value tension].
   (set-automation! 0 0 [[0 0.0 0] [4 1.0 0.5] [8 0.0 0]])"
  [track-idx lane-idx points]
  (let [builder (-> (AutomationCmd/newBuilder)
                    (.setAction hibiki.pb.commands.AutomationCmd$Action/ACTION_UPDATE_POINTS)
                    (.setTarget (-> (EntityRef/newBuilder)
                                    (.setTrackIndex (int track-idx))
                                    (.setLaneIndex (int lane-idx)))))]
    (doseq [[t v tension] points]
      (.addPoints builder
        (-> (AutomationPoint/newBuilder)
            (.setTimeBeats (float t))
            (.setValue (float v))
            (.setTension (float tension)))))
    (send-request!
      (-> (Request/newBuilder)
          (.setAutomation builder)
          (.build)))))

(defn get-automation!
  "Request automation lanes data for a track.
   (get-automation! 0)  ;; track 0"
  [track-idx]
  (send-request!
    (-> (Request/newBuilder)
        (.setAutomation (-> (AutomationCmd/newBuilder)
                            (.setAction hibiki.pb.commands.AutomationCmd$Action/ACTION_GET_LANES)
                            (.setTarget (-> (EntityRef/newBuilder)
                                            (.setTrackIndex (int track-idx))))))
        (.build))))

;; ---------------------------------------------------------------------------
;; Entry point — GUI + Socket REPL
;; ---------------------------------------------------------------------------

(defn -main [& _args]
  (clojure.core.server/start-server
   {:port 5555 :name "echo-repl" :accept 'clojure.core.server/repl})
  (println "Echo REPL started on port 5555")
  (echo/-main))
