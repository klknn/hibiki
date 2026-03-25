(ns hibiki.echo.dev
  "REPL development utilities for the Echo hybrid frontend.
   Start with:  clj -M:echo:dev
   Connect:     rlwrap nc localhost 5555"
  (:require [hibiki.echo :as echo])
  (:import [hibiki.ui SessionView TimelineView Theme Theme$Preset]))

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
;; Plugin helpers
;; ---------------------------------------------------------------------------

(defn- send-request! [^com.google.flatbuffers.FlatBufferBuilder builder]
  (.sendRequest (hibiki.BackendManager/getInstance) builder))

(defn load-plugin!
  "Load a VST3 plugin onto a track.
   (load-plugin! 0 \"/path/to/Dexed.vst3\")       ;; first sub-plugin
   (load-plugin! 0 \"/path/to/Dexed.vst3\" 1)     ;; second sub-plugin"
  ([track-idx path] (load-plugin! track-idx path 0))
  ([track-idx ^String path plugin-idx]
   (let [builder (com.google.flatbuffers.FlatBufferBuilder. 512)
         path-off (.createString builder path)
         cmd-off  (hibiki.ipc.LoadPlugin/createLoadPlugin builder (int track-idx) path-off (int plugin-idx))
         req-off  (hibiki.ipc.Request/createRequest builder hibiki.ipc.Command/LoadPlugin cmd-off)]
     (.finish builder req-off)
     (send-request! builder))))

(defn remove-plugin!
  "Remove a plugin from a track.
   (remove-plugin! 0 0)  ;; remove plugin 0 from track 0"
  [track-idx plugin-idx]
  (let [builder (com.google.flatbuffers.FlatBufferBuilder. 64)
        cmd-off (hibiki.ipc.RemovePlugin/createRemovePlugin builder (int track-idx) (int plugin-idx))
        req-off (hibiki.ipc.Request/createRequest builder hibiki.ipc.Command/RemovePlugin cmd-off)]
    (.finish builder req-off)
    (send-request! builder)))

(defn show-plugin-gui!
  "Open the native GUI window for a plugin.
   (show-plugin-gui! 0 0)  ;; track 0, plugin 0"
  [track-idx plugin-idx]
  (let [builder (com.google.flatbuffers.FlatBufferBuilder. 64)
        cmd-off (hibiki.ipc.ShowPluginGui/createShowPluginGui builder (int track-idx) (int plugin-idx))
        req-off (hibiki.ipc.Request/createRequest builder hibiki.ipc.Command/ShowPluginGui cmd-off)]
    (.finish builder req-off)
    (send-request! builder)))

(defn set-param!
  "Set a plugin parameter value (0.0–1.0).
   (set-param! 0 0 42 0.75)  ;; track 0, plugin 0, param 42 = 75%"
  [track-idx plugin-idx param-id value]
  (let [builder (com.google.flatbuffers.FlatBufferBuilder. 64)
        cmd-off (hibiki.ipc.SetParamValue/createSetParamValue
                  builder (int track-idx) (int plugin-idx) (int param-id) (float value))
        req-off (hibiki.ipc.Request/createRequest builder hibiki.ipc.Command/SetParamValue cmd-off)]
    (.finish builder req-off)
    (send-request! builder)))

(defn list-plugins!
  "Scan a VST3 bundle and list available sub-plugins.
   (list-plugins! \"/path/to/Dexed.vst3\")"
  [^String path]
  (let [builder (com.google.flatbuffers.FlatBufferBuilder. 256)
        path-off (.createString builder path)
        cmd-off  (hibiki.ipc.ListPlugins/createListPlugins builder path-off)
        req-off  (hibiki.ipc.Request/createRequest builder hibiki.ipc.Command/ListPlugins cmd-off)]
    (.finish builder req-off)
    (send-request! builder)))

;; ---------------------------------------------------------------------------
;; Entry point — GUI + Socket REPL
;; ---------------------------------------------------------------------------

(defn -main [& _args]
  (clojure.core.server/start-server
   {:port 5555 :name "echo-repl" :accept 'clojure.core.server/repl})
  (println "Echo REPL started on port 5555")
  (echo/-main))
