(ns hibiki.test-runner
  "CLI test runner – requires all Clojure test namespaces, runs them,
   and exits with code 1 on any failure."
  (:require [clojure.test :as t]
            [hibiki.echo.prelude-test]
            [hibiki.ui.theme-test]
            [hibiki.ui.piano-roll-test]
            [hibiki.ui.session-test]
            [hibiki.ui.timeline-test]
            [hibiki.ui.browser-test]
            [hibiki.echo.prelude-test]))

(defn -main [& _args]
  (let [res (apply t/run-tests
                   '[hibiki.echo.prelude-test
                     hibiki.ui.theme-test
                     hibiki.ui.piano-roll-test
                     hibiki.ui.session-test
                     hibiki.ui.timeline-test
                     hibiki.ui.browser-test
                     hibiki.echo.prelude-test])]
    (when (pos? (+ (:fail res) (:error res)))
      (System/exit 1))))
