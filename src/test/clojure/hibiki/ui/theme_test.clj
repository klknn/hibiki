(ns hibiki.ui.theme-test
  "Tests for the Clojure theme system."
  (:require [clojure.test :refer [deftest is testing]]
            [hibiki.ui.theme :as t])
  (:import [java.awt Color Font]))

;; ---------------------------------------------------------------------------
;; Color tests
;; ---------------------------------------------------------------------------

(deftest test-color-keys
  (testing "All theme color keys return non-nil Color values"
    (doseq [k [:bg-darker :bg-dark :bg-medium :panel-bg :panel-bg-light
               :border :text-bright :text-light :text-dim :text-normal
               :accent-orange :accent-blue :accent-green
               :track-header :clip-midi :clip-audio :clip-playing]]
      (is (instance? Color (t/color k)) (str "Missing color: " k)))))

(deftest test-font-keys
  (testing "Theme fonts are non-nil"
    (is (instance? Font (t/font :font-ui)))
    (is (instance? Font (t/font :font-display)))
    (is (instance? Font (t/font :font-ui-bold)))))

;; ---------------------------------------------------------------------------
;; Scaling tests
;; ---------------------------------------------------------------------------

(deftest test-scale-int
  (let [base 100
        scaled (t/scale base)]
    (is (pos? scaled) "Scaled value should be positive")))

(deftest test-scale-float
  (let [base 10.0
        scaled (t/scale base)]
    (is (pos? scaled) "Scaled float value should be positive")))

(deftest test-scale-with-factor
  (t/update-theme! :scaling 1.5)
  (is (= 150 (t/scale 100)) "scaling 1.5 × 100 = 150")
  ;; Reset
  (t/update-theme! :scaling 1.0))

;; ---------------------------------------------------------------------------
;; Preset tests
;; ---------------------------------------------------------------------------

(deftest test-preset-switch
  (testing "Switching presets updates color values"
    (t/update-theme! :preset :ableton-dark)
    (let [dark-bg (t/color :bg-dark)]
      (is (instance? Color dark-bg))
      (t/update-theme! :preset :solarized-dark)
      (let [solar-bg (t/color :bg-dark)]
        (is (instance? Color solar-bg))
        (is (not= dark-bg solar-bg) "Different presets should give different bg-dark")))
    ;; Reset
    (t/update-theme! :preset :ableton-dark)))

(deftest test-all-presets-have-required-keys
  (testing "Every preset has all required color keys"
    (doseq [[pname pmap] t/presets]
      (doseq [k [:bg-darker :bg-dark :bg-medium :panel-bg :panel-bg-light
                 :border :text-bright :text-dim :accent-orange :accent-blue
                 :accent-green :clip-midi :clip-audio :clip-playing]]
        (is (contains? pmap k) (str "Preset " pname " missing key " k))))))

;; ---------------------------------------------------------------------------
;; Listener tests
;; ---------------------------------------------------------------------------

(deftest test-theme-listener
  (let [called (atom false)]
    (t/add-listener! (fn [] (reset! called true)))
    (t/update-theme! :preset :ableton-dark)
    (is @called "Listener should have been called after update")))

;; ---------------------------------------------------------------------------
;; Grid helpers
;; ---------------------------------------------------------------------------

(deftest test-black-key
  (testing "C is white, C# is black"
    (is (not (t/black-key? 60)) "C4 (60) is white")
    (is (t/black-key? 61) "C#4 (61) is black")
    (is (not (t/black-key? 62)) "D4 (62) is white")
    (is (t/black-key? 63) "D#4 (63) is black")
    (is (not (t/black-key? 64)) "E4 (64) is white")))

(deftest test-black-key-all-octaves
  (testing "Black key pattern repeats across all octaves"
    (doseq [octave (range 0 11)
            note [1 3 6 8 10]]
      (let [pitch (+ (* octave 12) note)]
        (when (< pitch 128)
          (is (t/black-key? pitch) (str "Pitch " pitch " should be black")))))))

(deftest test-grid-modes
  (is (= :auto (first t/grid-modes)))
  (is (>= (count t/grid-modes) 8) "Should have at least 8 grid modes"))

(deftest test-tick-interval
  (testing "Tick intervals with resolution=480"
    (let [res 480]
      (is (= (* res 4) (t/tick-interval :bar res)) "bar = 4 beats")
      (is (= res (t/tick-interval :quarter res)) "quarter = 1 beat")
      (is (= (/ res 2) (t/tick-interval :eighth res)) "eighth = half beat")
      (is (= (/ res 4) (t/tick-interval :sixteenth res)) "sixteenth = quarter beat"))))

(deftest test-seconds-interval
  (testing "Seconds interval at 120 BPM"
    (let [spb 0.5]  ;; 120 BPM
      (is (= (* spb 4) (t/seconds-interval :bar spb)))
      (is (= spb (t/seconds-interval :quarter spb)))
      (is (= (/ spb 2) (t/seconds-interval :eighth spb))))))

(deftest test-auto-tick-interval
  (testing "Auto tick interval picks a visible spacing"
    (let [res 480
          tw 1.0  ;; 1 pixel per tick
          min-px 15
          interval (t/auto-tick-interval res tw min-px)]
      (is (pos? interval) "Should return a positive interval")
      (is (>= (* interval tw) min-px) "Should be >= min pixel spacing"))))

(deftest test-auto-seconds-interval
  (testing "Auto seconds interval picks visible spacing"
    (let [spb 0.5
          pps 160.0
          min-px 15
          interval (t/auto-seconds-interval spb pps min-px)]
      (is (pos? interval) "Should return a positive interval")
      (is (>= (* interval pps) min-px) "Should be >= min pixel spacing"))))
