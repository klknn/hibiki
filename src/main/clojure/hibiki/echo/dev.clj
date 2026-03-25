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
;; Entry point — GUI + Socket REPL
;; ---------------------------------------------------------------------------

(defn -main [& _args]
  (clojure.core.server/start-server
   {:port 5555 :name "echo-repl" :accept 'clojure.core.server/repl})
  (println "Echo REPL started on port 5555")
  (echo/-main))
