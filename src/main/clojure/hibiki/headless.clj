(ns hibiki.headless
  "Headless script runner — starts the backend engine without any GUI,
   loads hibiki.echo.prelude helpers (aliased as `hbk`), then evaluates
   a user-provided .clj script file.

   Usage:
     clj -M:headless script.clj       ;; run a script
     clj -M:headless -                 ;; read from stdin
     clj -M:headless                   ;; start a headless REPL

   Inside the script, all hbk/* helpers are available:
     (hbk/load-plugin! 0 \"testdata/Dexed.vst3\")
     (hbk/load-clip! 0 0 \"testdata/rickroll.mid\")
     (hbk/play!)
     (Thread/sleep 5000)
     (hbk/stop!)
     (hbk/save! \"/tmp/my-project.hbk\")
     (System/exit 0)"
  (:require [hibiki.echo.prelude :as hbk])
  (:import [hibiki BackendManager]))

(set! *warn-on-reflection* true)

(defn- start-repl! []
  (println "Starting headless REPL... (hbk/* helpers are available, Ctrl-D to exit)")
  (clojure.main/repl :init #(in-ns 'hibiki.headless)))

(defn -main
  "Starts the backend engine (headless) and runs a .clj script.
   Falls back to REPL if no file given or file doesn't exist."
  [& args]
  (let [^BackendManager bm (BackendManager/getInstance)]
    (.start bm)
    (Thread/sleep 500)
    (println "Hibiki engine started (headless).")

    (if-let [script (first args)]
      (cond
        ;; Read from stdin
        (= script "-")
        (do (println "Reading from stdin...")
            (binding [*ns* (the-ns 'hibiki.headless)]
              (clojure.core/load-reader (java.io.InputStreamReader. System/in)))
            (println "Done."))

        ;; File exists — run it
        (.exists (java.io.File. ^String script))
        (do (println (str "Running: " script))
            (binding [*ns* (the-ns 'hibiki.headless)]
              (clojure.core/load-file script))
            (println "Script finished."))

        ;; File not found — warn and fall back to REPL
        :else
        (do (println (str "File not found: " script))
            (start-repl!)))

      ;; No args — REPL
      (start-repl!))))
