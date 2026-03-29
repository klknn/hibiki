(ns test-ref
  (:require [hibiki.echo.prelude :as hbk]))
(def r (#'hbk/->ref :track 2 :slot 3))
(println "ref:" r)
(println "track:" (.getTrackIndex r))
(println "slot:" (.getSessionSlot r))
(System/exit 0)
