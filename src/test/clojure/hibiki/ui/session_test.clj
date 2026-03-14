(ns hibiki.ui.session-test
  "Tests for the Clojure session view state and track selection."
  (:require [clojure.test :refer [deftest is testing]]
            [hibiki.ui.session :as s]))

(deftest test-default-selected-track
  (is (= 0 (s/get-selected-track))
      "Default selected track should be 0"))

(deftest test-select-track
  (s/select-track! 2)
  (is (= 2 (s/get-selected-track)))
  ;; Reset
  (s/select-track! 0))

(deftest test-select-track-sequential
  (s/select-track! 0)
  (is (= 0 (s/get-selected-track)))
  (s/select-track! 3)
  (is (= 3 (s/get-selected-track)))
  ;; Reset
  (s/select-track! 0))
