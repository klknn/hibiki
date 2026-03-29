(ns hibiki.echo.prelude-test
  "Tests for the REPL helper functions in hibiki.echo.prelude.
   These tests verify that helpers produce correct protobuf Request objects
   without requiring a running backend — we intercept the send! call."
  (:require [clojure.test :refer [deftest is testing use-fixtures]]
            [hibiki.echo.prelude :as hbk])
  (:import [hibiki.pb.commands Request TransportCmd TrackCmd PluginCmd
                               AutomationCmd MidiCmd ProjectCmd]
           [hibiki.pb.core EntityRef AutomationPoint MidiEvent]))

;; ---------------------------------------------------------------------------
;; Test fixture — intercept send! to capture requests instead of sending IPC
;; ---------------------------------------------------------------------------

(def ^:dynamic *captured-requests* (atom []))

(defn capture-fixture [f]
  (reset! *captured-requests* [])
  (with-redefs [hibiki.echo.prelude/send!
                (fn [^Request req]
                  (swap! *captured-requests* conj req)
                  nil)]
    (f)))

(use-fixtures :each capture-fixture)

(defn- last-request ^Request []
  (last @*captured-requests*))

;; ---------------------------------------------------------------------------
;; Transport tests
;; ---------------------------------------------------------------------------

(deftest test-play!
  (hbk/play!)
  (let [^Request req (last-request)]
    (is (some? req))
    (is (.hasTransport req))
    (is (= TransportCmd$Action/ACTION_PLAY
           (.getAction (.getTransport req))))))

(deftest test-stop!
  (hbk/stop!)
  (let [^Request req (last-request)]
    (is (.hasTransport req))
    (is (= TransportCmd$Action/ACTION_STOP
           (.getAction (.getTransport req))))))

(deftest test-seek!
  (hbk/seek! 4.0)
  (let [^Request req (last-request)
        ^TransportCmd t (.getTransport req)]
    (is (= TransportCmd$Action/ACTION_SEEK (.getAction t)))
    (is (== 4.0 (.getSeekPos t)))))

;; ---------------------------------------------------------------------------
;; Track tests
;; ---------------------------------------------------------------------------

(deftest test-play-clip!
  (hbk/play-clip! 2 3)
  (let [^Request req (last-request)
        ^TrackCmd t (.getTrack req)
        ^EntityRef ref (.getTarget t)]
    (is (= TrackCmd$Action/ACTION_PLAY_SLOT (.getAction t)))
    (is (= 2 (.getTrackIndex ref)))
    (is (= 3 (.getSessionSlot ref)))))

(deftest test-stop-track!
  (hbk/stop-track! 1)
  (let [^Request req (last-request)
        ^TrackCmd t (.getTrack req)]
    (is (= TrackCmd$Action/ACTION_STOP (.getAction t)))
    (is (= 1 (.getTrackIndex (.getTarget t))))))

(deftest test-delete-clip!
  (hbk/delete-clip! 0 2)
  (let [^Request req (last-request)
        ^TrackCmd t (.getTrack req)]
    (is (= TrackCmd$Action/ACTION_DELETE_CLIP (.getAction t)))
    (is (= 0 (.getTrackIndex (.getTarget t))))
    (is (= 2 (.getSessionSlot (.getTarget t))))))

(deftest test-set-clip-loop!
  (hbk/set-clip-loop! 0 0 true)
  (let [^Request req (last-request)
        ^TrackCmd t (.getTrack req)]
    (is (= TrackCmd$Action/ACTION_SET_CLIP_LOOP (.getAction t)))
    (is (.getFlag t))))

(deftest test-remove-timeline-clip!
  (hbk/remove-timeline-clip! 1 3)
  (let [^Request req (last-request)
        ^TrackCmd t (.getTrack req)
        ^EntityRef ref (.getTarget t)]
    (is (= TrackCmd$Action/ACTION_REMOVE_TIMELINE_CLIP (.getAction t)))
    (is (= 1 (.getTrackIndex ref)))
    (is (= 3 (.getTimelineClip ref)))))

;; ---------------------------------------------------------------------------
;; Plugin tests
;; ---------------------------------------------------------------------------

(deftest test-load-plugin!
  (hbk/load-plugin! 0 "/path/to/Dexed.vst3")
  (let [^Request req (last-request)
        ^PluginCmd p (.getPlugin req)]
    (is (= PluginCmd$Action/ACTION_LOAD (.getAction p)))
    (is (= "/path/to/Dexed.vst3" (.getPath p)))
    (is (= 0 (.getTrackIndex (.getTarget p))))
    (is (= 0 (.getPluginIndex (.getTarget p))))))

(deftest test-load-plugin-with-index!
  (hbk/load-plugin! 1 "/path/to/mda.vst3" 2)
  (let [^Request req (last-request)
        ^PluginCmd p (.getPlugin req)]
    (is (= 1 (.getTrackIndex (.getTarget p))))
    (is (= 2 (.getPluginIndex (.getTarget p))))))

(deftest test-remove-plugin!
  (hbk/remove-plugin! 0 1)
  (let [^Request req (last-request)
        ^PluginCmd p (.getPlugin req)]
    (is (= PluginCmd$Action/ACTION_REMOVE (.getAction p)))
    (is (= 1 (.getPluginIndex (.getTarget p))))))

(deftest test-show-plugin-gui!
  (hbk/show-plugin-gui! 0 0)
  (let [^Request req (last-request)
        ^PluginCmd p (.getPlugin req)]
    (is (= PluginCmd$Action/ACTION_SHOW_GUI (.getAction p)))))

(deftest test-set-param!
  (hbk/set-param! 0 0 42 0.75)
  (let [^Request req (last-request)
        ^PluginCmd p (.getPlugin req)]
    (is (= PluginCmd$Action/ACTION_SET_PARAM (.getAction p)))
    (is (= 42 (.getParamId p)))
    (is (== 0.75 (.getParamValue p)))))

(deftest test-list-plugins!
  (hbk/list-plugins! "/path/to/Dexed.vst3")
  (let [^Request req (last-request)
        ^PluginCmd p (.getPlugin req)]
    (is (= PluginCmd$Action/ACTION_LIST (.getAction p)))
    (is (= "/path/to/Dexed.vst3" (.getPath p)))))

;; ---------------------------------------------------------------------------
;; MIDI tests
;; ---------------------------------------------------------------------------

(deftest test-write-midi!
  (hbk/write-midi! 0 0 480 [{:tick 0 :pitch 60 :dur 480 :vel 100}
                              {:tick 480 :pitch 64 :dur 240 :vel 80}])
  (let [^Request req (last-request)
        ^MidiCmd m (.getMidi req)]
    (is (= MidiCmd$Action/ACTION_UPDATE (.getAction m)))
    (is (= 480 (.getResolution m)))
    (is (= 2 (.getEventsCount m)))
    (let [^MidiEvent e0 (.getEvents m 0)
          ^MidiEvent e1 (.getEvents m 1)]
      (is (= 0 (.getTick e0)))
      (is (= 60 (.getPitch e0)))
      (is (= 480 (.getDurationTicks e0)))
      (is (= 100 (.getVelocity e0)))
      (is (= 480 (.getTick e1)))
      (is (= 64 (.getPitch e1))))))

(deftest test-get-midi!
  (hbk/get-midi! 0 0)
  (let [^Request req (last-request)
        ^MidiCmd m (.getMidi req)]
    (is (= MidiCmd$Action/ACTION_GET (.getAction m)))
    (is (= 0 (.getTrackIndex (.getTarget m))))
    (is (= 0 (.getSessionSlot (.getTarget m))))))

;; ---------------------------------------------------------------------------
;; Automation tests
;; ---------------------------------------------------------------------------

(deftest test-add-automation!
  (hbk/add-automation! 0 0 42)
  (let [^Request req (last-request)
        ^AutomationCmd a (.getAutomation req)]
    (is (= AutomationCmd$Action/ACTION_ADD_LANE (.getAction a)))
    (is (= 42 (.getParamId a)))))

(deftest test-remove-automation!
  (hbk/remove-automation! 0 1)
  (let [^Request req (last-request)
        ^AutomationCmd a (.getAutomation req)]
    (is (= AutomationCmd$Action/ACTION_REMOVE_LANE (.getAction a)))
    (is (= 1 (.getLaneIndex (.getTarget a))))))

(deftest test-set-automation!
  (hbk/set-automation! 0 0 [[0 0.0 0] [4 1.0 0.5] [8 0.0 0]])
  (let [^Request req (last-request)
        ^AutomationCmd a (.getAutomation req)]
    (is (= AutomationCmd$Action/ACTION_UPDATE_POINTS (.getAction a)))
    (is (= 3 (.getPointsCount a)))
    (let [^AutomationPoint p1 (.getPoints a 1)]
      (is (== 4.0 (.getTimeBeats p1)))
      (is (== 1.0 (.getValue p1)))
      (is (== 0.5 (.getTension p1))))))

(deftest test-get-automation!
  (hbk/get-automation! 0)
  (let [^Request req (last-request)
        ^AutomationCmd a (.getAutomation req)]
    (is (= AutomationCmd$Action/ACTION_GET_LANES (.getAction a)))))

;; ---------------------------------------------------------------------------
;; Project tests
;; ---------------------------------------------------------------------------

(deftest test-save!
  (hbk/save! "/tmp/test.hbk")
  (let [^Request req (last-request)
        ^ProjectCmd p (.getProject req)]
    (is (= ProjectCmd$Action/ACTION_SAVE (.getAction p)))
    (is (= "/tmp/test.hbk" (.getPath p)))))

(deftest test-load!
  (hbk/load! "/tmp/test.hbk")
  (let [^Request req (last-request)
        ^ProjectCmd p (.getProject req)]
    (is (= ProjectCmd$Action/ACTION_LOAD (.getAction p)))))

(deftest test-set-bpm!
  (hbk/set-bpm! 140)
  (let [^Request req (last-request)
        ^ProjectCmd p (.getProject req)]
    (is (= ProjectCmd$Action/ACTION_SET_BPM (.getAction p)))
    (is (== 140.0 (.getBpm p)))))

(deftest test-undo-redo!
  (hbk/undo!)
  (is (= ProjectCmd$Action/ACTION_UNDO
         (.getAction (.getProject (last-request)))))
  (hbk/redo!)
  (is (= ProjectCmd$Action/ACTION_REDO
         (.getAction (.getProject (last-request))))))

(deftest test-bounce!
  (hbk/bounce! "/tmp/output.wav")
  (let [^Request req (last-request)
        ^ProjectCmd p (.getProject req)]
    (is (= ProjectCmd$Action/ACTION_BOUNCE (.getAction p)))
    (is (= "/tmp/output.wav" (.getPath p)))))
