(ns hibiki.test-runner
  "CLI test runner – requires all Clojure test namespaces, runs them,
   and exits with code 1 on any failure."
  (:require [clojure.test :as t]
            [hibiki.ui.theme-test]
            [hibiki.ui.piano-roll-test]
            [hibiki.ui.session-test]
            [hibiki.ui.timeline-test]
            [hibiki.ui.browser-test]))

(defn -main [& _args]
  (let [res (apply t/run-tests
                   '[hibiki.ui.theme-test
                     hibiki.ui.piano-roll-test
                     hibiki.ui.session-test
                     hibiki.ui.timeline-test
                     hibiki.ui.browser-test])]
    (when (pos? (+ (:fail res) (:error res)))
      (System/exit 1))))
