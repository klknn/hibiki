(ns hibiki.ui.theme
  "DAW theme system — colors, fonts, scaling, and presets.
   Ported from Theme.java."
  (:import [java.awt Color Font]))

;; ---------------------------------------------------------------------------
;; Presets — each is a map of keyword -> Color
;; ---------------------------------------------------------------------------

(def presets
  {:ableton-dark
   {:bg-darker      (Color. 25 25 25)
    :bg-dark        (Color. 34 34 34)
    :bg-medium      (Color. 45 45 45)
    :panel-bg       (Color. 45 45 45)
    :panel-bg-light (Color. 55 55 55)
    :border         (Color. 20 20 20)
    :text-normal    (Color. 190 190 190)
    :text-bright    (Color. 240 240 240)
    :text-light     (Color. 220 220 220)
    :text-dim       (Color. 120 120 120)
    :accent-orange  (Color. 255 153 0)
    :accent-blue    (Color. 0 170 255)
    :accent-green   (Color. 0 255 127)
    :track-header   (Color. 60 60 60)
    :clip-midi      (Color. 255 120 120)
    :clip-audio     (Color. 120 200 255)
    :clip-playing   (Color. 120 255 120)}

   :ableton-light
   {:bg-darker      (Color. 220 220 220)
    :bg-dark        (Color. 200 200 200)
    :bg-medium      (Color. 180 180 180)
    :panel-bg       (Color. 210 210 210)
    :panel-bg-light (Color. 230 230 230)
    :border         (Color. 160 160 160)
    :text-normal    (Color. 40 40 40)
    :text-bright    (Color. 0 0 0)
    :text-light     (Color. 60 60 60)
    :text-dim       (Color. 100 100 100)
    :accent-orange  (Color. 255 153 0)
    :accent-blue    (Color. 0 120 255)
    :accent-green   (Color. 0 200 100)
    :track-header   (Color. 170 170 170)
    :clip-midi      (Color. 255 120 120)
    :clip-audio     (Color. 120 200 255)
    :clip-playing   (Color. 120 255 120)}

   :solarized-dark
   {:bg-darker      (Color. 0 25 30)
    :bg-dark        (Color. 7 54 66)
    :bg-medium      (Color. 88 110 117)
    :panel-bg       (Color. 7 54 66)
    :panel-bg-light (Color. 63 85 93)
    :border         (Color. 0 43 54)
    :text-normal    (Color. 147 161 161)
    :text-bright    (Color. 253 246 227)
    :text-light     (Color. 131 148 150)
    :text-dim       (Color. 88 110 117)
    :accent-orange  (Color. 203 75 22)
    :accent-blue    (Color. 38 139 210)
    :accent-green   (Color. 133 153 0)
    :track-header   (Color. 0 43 54)
    :clip-midi      (Color. 211 54 130)
    :clip-audio     (Color. 38 139 210)
    :clip-playing   (Color. 133 153 0)}

   :solarized-light
   {:bg-darker      (Color. 240 235 215)
    :bg-dark        (Color. 253 246 227)
    :bg-medium      (Color. 147 161 161)
    :panel-bg       (Color. 253 246 227)
    :panel-bg-light (Color. 238 232 213)
    :border         (Color. 211 201 171)
    :text-normal    (Color. 101 123 131)
    :text-bright    (Color. 7 54 66)
    :text-light     (Color. 88 110 117)
    :text-dim       (Color. 147 161 161)
    :accent-orange  (Color. 203 75 22)
    :accent-blue    (Color. 38 139 210)
    :accent-green   (Color. 133 153 0)
    :track-header   (Color. 238 232 213)
    :clip-midi      (Color. 211 54 130)
    :clip-audio     (Color. 38 139 210)
    :clip-playing   (Color. 133 153 0)}

   :win95
   {:bg-darker      (Color. 128 128 128)
    :bg-dark        (Color. 192 192 192)
    :bg-medium      (Color. 223 223 223)
    :panel-bg       (Color. 192 192 192)
    :panel-bg-light (Color. 255 255 255)
    :border         (Color. 0 0 0)
    :text-normal    (Color. 0 0 0)
    :text-bright    (Color. 0 0 0)
    :text-light     (Color. 64 64 64)
    :text-dim       (Color. 128 128 128)
    :accent-orange  (Color. 0 0 128)
    :accent-blue    (Color. 0 0 255)
    :accent-green   (Color. 0 128 0)
    :track-header   (Color. 128 128 128)
    :clip-midi      (Color. 255 0 255)
    :clip-audio     (Color. 0 255 255)
    :clip-playing   (Color. 0 255 0)}

   :winxp
   {:bg-darker      (Color. 212 208 200)
    :bg-dark        (Color. 236 233 216)
    :bg-medium      (Color. 255 255 255)
    :panel-bg       (Color. 236 233 216)
    :panel-bg-light (Color. 255 255 255)
    :border         (Color. 172 168 153)
    :text-normal    (Color. 0 0 0)
    :text-bright    (Color. 0 0 0)
    :text-light     (Color. 80 80 80)
    :text-dim       (Color. 150 150 150)
    :accent-orange  (Color. 0 85 225)
    :accent-blue    (Color. 49 106 197)
    :accent-green   (Color. 58 110 165)
    :track-header   (Color. 192 192 192)
    :clip-midi      (Color. 255 100 100)
    :clip-audio     (Color. 100 100 255)
    :clip-playing   (Color. 100 255 100)}})

;; ---------------------------------------------------------------------------
;; Mutable theme state (atom)
;; ---------------------------------------------------------------------------

(defonce ^:private state
  (atom {:preset     :ableton-dark
         :scaling    1.0
         :font-size  11
         :font-family "SansSerif"
         :listeners  []}))

(defn- make-fonts [{:keys [font-family font-size scaling]}]
  (let [sz (int (* font-size scaling))]
    {:font-ui       (Font. font-family Font/PLAIN sz)
     :font-ui-bold  (Font. font-family Font/BOLD sz)
     :font-display  (Font. "Monospaced" Font/BOLD (int (* 14 scaling)))}))

(defn theme
  "Returns current theme map: colors merged with fonts and config."
  []
  (let [s @state]
    (merge (get presets (:preset s))
           (make-fonts s)
           (select-keys s [:scaling :font-size :font-family :preset]))))

(defn color
  "Shorthand: (color :bg-dark) => current Color."
  [k]
  (get (theme) k))

(defn font [k] (get (theme) k))

(defn scale
  "Scale an int or float by the current scaling factor."
  [v]
  (let [s (:scaling @state)]
    (if (float? v) (* v s) (int (* v s)))))

(defn update-theme!
  "Update preset, scaling, font-size, and/or font-family. Notifies listeners."
  [& {:keys [preset scaling font-size font-family]}]
  (swap! state (fn [s]
                 (cond-> s
                   preset      (assoc :preset preset)
                   scaling     (assoc :scaling scaling)
                   font-size   (assoc :font-size font-size)
                   font-family (assoc :font-family font-family))))
  (doseq [f (:listeners @state)] (f)))

(defn add-listener! [f]
  (swap! state update :listeners conj f))

;; ---------------------------------------------------------------------------
;; Grid mode — an enum-like keyword set with interval helpers
;; Ported from GridMode.java
;; ---------------------------------------------------------------------------

(def grid-modes
  [:auto :seconds :bar :half :quarter :eighth :sixteenth :thirty-second
   :triplet-quarter :triplet-eighth :triplet-16th :triplet-32nd])

(defn tick-interval
  "Get tick interval for a fixed grid mode given resolution (ticks/beat)."
  [mode resolution]
  (let [bar (* resolution 4)]
    (case mode
      :seconds  resolution
      :bar      bar
      :half     (/ bar 2)
      :quarter  resolution
      :eighth   (/ resolution 2)
      :sixteenth (/ resolution 4)
      :thirty-second (/ resolution 8)
      :triplet-quarter (/ bar 3)
      :triplet-eighth  (/ bar 6)
      :triplet-16th    (/ bar 12)
      :triplet-32nd    (/ bar 24)
      resolution)))

(defn auto-tick-interval
  "Auto-select finest tick interval that keeps >= min-px pixel spacing."
  [resolution tick-width min-px]
  (let [bar (* resolution 4)
        divisions [(/ resolution 8) (/ resolution 4) (/ resolution 2)
                   resolution (/ bar 2) bar]]
    (or (first (filter #(>= (* % tick-width) min-px) divisions))
        bar)))

(defn seconds-interval
  "Get seconds interval for a fixed grid mode given seconds-per-beat."
  [mode seconds-per-beat]
  (let [spb seconds-per-beat
        bar (* spb 4)]
    (case mode
      :seconds  1.0
      :bar      bar
      :half     (/ bar 2)
      :quarter  spb
      :eighth   (/ spb 2)
      :sixteenth (/ spb 4)
      :thirty-second (/ spb 8)
      :triplet-quarter (/ bar 3)
      :triplet-eighth  (/ bar 6)
      :triplet-16th    (/ bar 12)
      :triplet-32nd    (/ bar 24)
      spb)))

(defn auto-seconds-interval
  "Auto-select finest seconds interval that keeps >= min-px pixel spacing."
  [seconds-per-beat px-per-sec min-px]
  (let [bar (* seconds-per-beat 4)
        divisions [(/ seconds-per-beat 8) (/ seconds-per-beat 4)
                   (/ seconds-per-beat 2) seconds-per-beat
                   (/ bar 2) bar]]
    (or (first (filter #(>= (* % px-per-sec) min-px) divisions))
        bar)))

(defn black-key?
  "True if MIDI pitch is a black key."
  [pitch]
  (let [n (mod pitch 12)]
    (contains? #{1 3 6 8 10} n)))
