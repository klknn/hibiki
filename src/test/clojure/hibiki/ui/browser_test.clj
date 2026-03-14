(ns hibiki.ui.browser-test
  "Tests for the browser pane — FileItem records and file extension helpers."
  (:require [clojure.test :refer [deftest is testing]]
            [hibiki.ui.browser :as b])
  (:import [java.io File]))

;; ---------------------------------------------------------------------------
;; FileItem record tests
;; ---------------------------------------------------------------------------

(deftest test-file-item-basic
  (let [f (File. "testdata/test_clip.mid")
        item (b/file-item f "midi" "test_clip.mid")]
    (is (= f (:file item)))
    (is (= "midi" (:type item)))
    (is (= "test_clip.mid" (:display-name item)))
    (is (= "" (:vendor item)))
    (is (= 0 (:plugin-index item)))))

(deftest test-file-item-plugin
  (let [f (File. "/usr/lib/vst3/plugin.vst3")
        item (b/file-item f "vst" "SuperSynth" "Vendor Inc" 2)]
    (is (= "vst" (:type item)))
    (is (= "SuperSynth" (:display-name item)))
    (is (= "Vendor Inc" (:vendor item)))
    (is (= 2 (:plugin-index item)))))

(deftest test-file-item-toString
  (testing "FileItem toString returns display name (for JTree rendering)"
    (let [item (b/file-item (File. "foo.wav") "audio" "foo.wav")]
      (is (= "foo.wav" (str item))))))

(deftest test-file-item-equality
  (let [a (b/file-item (File. "a.mid") "midi" "a.mid")
        b (b/file-item (File. "a.mid") "midi" "a.mid")]
    (is (= a b) "Same content FileItems should be equal")))

(deftest test-file-item-inequality
  (let [a (b/file-item (File. "a.mid") "midi" "a.mid")
        b (b/file-item (File. "b.mid") "midi" "b.mid")]
    (is (not= a b))))

;; ---------------------------------------------------------------------------
;; Extension detection
;; ---------------------------------------------------------------------------

(deftest test-audio-extensions
  (testing "Audio file extensions are recognized"
    (doseq [ext [".wav" ".mp3" ".ogg" ".flac" ".aiff"]]
      (is (contains? @#'b/audio-exts ext) (str ext " should be audio")))))

(deftest test-midi-extensions
  (testing "MIDI file extensions are recognized"
    (doseq [ext [".mid" ".midi"]]
      (is (contains? @#'b/midi-exts ext) (str ext " should be MIDI")))))

(deftest test-non-audio-not-in-exts
  (testing "Non-audio extensions are not recognized"
    (is (not (contains? @#'b/audio-exts ".txt")))
    (is (not (contains? @#'b/audio-exts ".mid")))))
