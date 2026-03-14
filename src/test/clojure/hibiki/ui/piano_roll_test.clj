(ns hibiki.ui.piano-roll-test
  "Tests for the Piano Roll MIDI parsing and note record logic."
  (:require [clojure.test :refer [deftest is testing]]
            [hibiki.ui.piano-roll :as pr]
            [hibiki.ui.theme :as t])
  (:import [javax.sound.midi MidiSystem Sequence Track MidiEvent ShortMessage]
           [java.io File]))

;; ---------------------------------------------------------------------------
;; Note record tests
;; ---------------------------------------------------------------------------

(deftest test-note-constructor
  (let [n (pr/->Note 60 0 480 100)]
    (is (= 60 (:pitch n)))
    (is (= 0 (:start-tick n)))
    (is (= 480 (:duration-ticks n)))
    (is (= 100 (:velocity n)))))

(deftest test-note-default-fields
  (let [n (pr/->Note 64 100 240 80)]
    (is (= 64 (:pitch n)))
    (is (= 100 (:start-tick n)))
    (is (= 240 (:duration-ticks n)))
    (is (= 80 (:velocity n)))))

(deftest test-note-high-pitch
  (let [n (pr/->Note 127 0 100 127)]
    (is (= 127 (:pitch n)))))

(deftest test-note-low-pitch
  (let [n (pr/->Note 0 0 100 1)]
    (is (= 0 (:pitch n)))))

(deftest test-note-multiple-instances
  (let [n1 (pr/->Note 60 0 480 100)
        n2 (pr/->Note 64 480 480 80)]
    (is (not= n1 n2))
    (is (= 60 (:pitch n1)))
    (is (= 64 (:pitch n2)))))

(deftest test-num-keys-constant
  (is (= 128 @#'pr/NUM_KEYS) "Should have 128 MIDI keys"))

;; ---------------------------------------------------------------------------
;; MIDI file loading tests
;; ---------------------------------------------------------------------------

(deftest test-load-midi-test-file
  (testing "Loading the test MIDI file"
    (let [f (File. "src/test/resources/test_clip.mid")]
      (when (.exists f)
        ;; Use internal parse function via calling load-midi-file
        ;; The function is private, so we invoke it via var
        (let [result (#'pr/load-midi-file f)]
          (is (some? (:sequence result)))
          (is (some? (:track result)))
          (is (some? (:notes result)))
          (is (pos? (:resolution result))))))))

(deftest test-load-midi-nonexistent
  (testing "Loading a non-existent MIDI file throws"
    (is (thrown? Exception (#'pr/load-midi-file (File. "/tmp/nonexistent.mid"))))))

;; ---------------------------------------------------------------------------
;; MIDI parsing tests
;; ---------------------------------------------------------------------------

(defn- make-test-track
  "Create a MIDI track with the given notes as [pitch velocity start-tick end-tick]."
  [notes]
  (let [seq (Sequence. Sequence/PPQ 480)
        track (.createTrack seq)]
    (doseq [[pitch velocity start-tick end-tick] notes]
      (let [on (ShortMessage.)]
        (.setMessage on ShortMessage/NOTE_ON 0 pitch velocity)
        (.add track (MidiEvent. on start-tick)))
      (let [off (ShortMessage.)]
        (.setMessage off ShortMessage/NOTE_OFF 0 pitch 0)
        (.add track (MidiEvent. off end-tick))))
    track))

(deftest test-parse-midi-track-note-on-off
  (testing "Basic NOTE_ON/NOTE_OFF parsing"
    (let [track (make-test-track [[60 100 0 480]])
          notes (#'pr/parse-midi-track track)]
      (is (= 1 (count notes)))
      (let [n (first notes)]
        (is (= 60 (:pitch n)))
        (is (= 0 (:start-tick n)))
        (is (= 480 (:duration-ticks n)))
        (is (= 100 (:velocity n)))))))

(deftest test-parse-midi-track-velocity-zero-as-note-off
  (testing "NOTE_ON with velocity 0 treated as NOTE_OFF"
    (let [seq (Sequence. Sequence/PPQ 480)
          track (.createTrack seq)
          on (ShortMessage.)
          off (ShortMessage.)]
      (.setMessage on ShortMessage/NOTE_ON 0 64 100)
      (.add track (MidiEvent. on 0))
      ;; velocity=0 NOTE_ON = NOTE_OFF
      (.setMessage off ShortMessage/NOTE_ON 0 64 0)
      (.add track (MidiEvent. off 240))
      (let [notes (#'pr/parse-midi-track track)]
        (is (= 1 (count notes)))
        (is (= 240 (:duration-ticks (first notes))))))))

(deftest test-parse-midi-track-multiple-notes
  (testing "Multiple notes parse correctly"
    (let [track (make-test-track [[60 100 0 480] [64 80 480 960]])
          notes (#'pr/parse-midi-track track)]
      (is (= 2 (count notes)))
      (is (= #{60 64} (set (map :pitch notes)))))))

(deftest test-parse-midi-track-note-off-without-on
  (testing "NOTE_OFF without prior NOTE_ON is ignored"
    (let [seq (Sequence. Sequence/PPQ 480)
          track (.createTrack seq)
          off (ShortMessage.)]
      (.setMessage off ShortMessage/NOTE_OFF 0 60 0)
      (.add track (MidiEvent. off 480))
      (let [notes (#'pr/parse-midi-track track)]
        (is (empty? notes))))))
