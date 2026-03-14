(ns hibiki.ui.timeline-test
  "Tests for the Clojure timeline view — track data records, state, and pixel math."
  (:require [clojure.test :refer [deftest is testing]]
            [hibiki.ui.timeline :as tl]))

;; ---------------------------------------------------------------------------
;; TrackClip / TrackTimeline records
;; ---------------------------------------------------------------------------

(deftest test-track-clip-record
  (let [c (tl/->TrackClip 0 "/path/to/clip.wav" 1.0 4.0 "my-clip" nil)]
    (is (= 0 (:clip-index c)))
    (is (= "/path/to/clip.wav" (:path c)))
    (is (= 1.0 (:start-time c)))
    (is (= 4.0 (:duration-beats c)))
    (is (= "my-clip" (:name c)))))

(deftest test-track-timeline-record
  (let [t (tl/->TrackTimeline 0 nil (atom []))]
    (is (= 0 (:track-idx t)))
    (is (nil? (:custom-name t)))
    (is (empty? @(:clips t)))))

(deftest test-track-timeline-custom-name
  (let [t (tl/->TrackTimeline 1 "Bass" (atom []))]
    (is (= "Bass" (:custom-name t)))))

;; ---------------------------------------------------------------------------
;; State tests
;; ---------------------------------------------------------------------------

(deftest test-default-state
  (let [s @(var-get #'tl/tl-state)]
    (is (= 4 (count (:tracks s))) "Should have 4 tracks")
    (is (= 120.0 (:bpm s)))
    (is (= 0.0 (:playhead-sec s)))
    (is (false? (:is-playing s)))
    (is (= :auto (:grid-mode s)))
    (is (= 0 (:selected-track s)))))

(deftest test-set-selected-track
  (tl/set-selected-track 2)
  (is (= 2 (:selected-track @(var-get #'tl/tl-state))))
  ;; Reset
  (tl/set-selected-track 0))

;; ---------------------------------------------------------------------------
;; Pixel math (private helpers via var)
;; ---------------------------------------------------------------------------

(deftest test-pixels-per-second
  (testing "At 120 BPM, 80 px/beat => 160 px/sec"
    ;; pixels-per-beat / (60 / bpm) = 80 / 0.5 = 160
    (let [pps ((var-get #'tl/pixels-per-second))]
      (is (> pps 0) "Should be positive")
      (is (== 160.0 pps) "80 / (60/120) = 160"))))

(deftest test-seconds-to-x
  (is (== 160 ((var-get #'tl/seconds-to-x) 1.0)) "1 second at 160 px/s = 160 px"))

(deftest test-x-to-seconds
  (is (< (Math/abs (- 1.0 ((var-get #'tl/x-to-seconds) 160))) 0.001)
      "160 px at 160 px/s = 1.0 s"))
