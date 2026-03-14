(ns hibiki.ui.piano-roll
  "Piano Roll MIDI editor — note grid with velocity editing.
   Ported from PianoRoll.java, PianoRollRenderer.java,
   PianoRollMouseHandler.java, MidiDataModel.java."
  (:require [hibiki.ui.theme :as t])
  (:import [javax.swing JDialog JPanel JScrollPane JLabel JComboBox Timer
                         BorderFactory SwingConstants SwingUtilities BoxLayout Box]
           [java.awt BorderLayout Dimension Color Font Graphics Graphics2D
                     RenderingHints BasicStroke Frame]
           [java.awt.event MouseAdapter MouseEvent MouseMotionAdapter
                           ActionListener WindowAdapter WindowEvent KeyEvent]
           [javax.sound.midi MidiSystem Sequence Track MidiEvent ShortMessage]
           [java.io File]
           [com.google.flatbuffers FlatBufferBuilder]
           [hibiki.ipc Request Command UpdateClipMidi GetClipMidi
                       ClipMidiData MidiEventData
                       Response Notification PlayheadInfo]))

;; ---------------------------------------------------------------------------
;; Note record and MIDI model helpers
;; ---------------------------------------------------------------------------

(defrecord Note [pitch start-tick duration-ticks velocity])

(defn- parse-midi-track
  "Parse a javax.sound.midi.Track into a seq of Note records."
  [^Track track]
  (let [note-ons (atom {})  ;; pitch -> [tick, velocity]
        notes    (atom [])]
    (dotimes [i (.size track)]
      (let [^MidiEvent evt (.get track i)
            msg (.getMessage evt)]
        (when (instance? ShortMessage msg)
          (let [^ShortMessage sm msg
                cmd  (.getCommand sm)
                pitch (.getData1 sm)
                vel   (.getData2 sm)
                tick  (.getTick evt)]
            (cond
              (and (= cmd ShortMessage/NOTE_ON) (> vel 0))
              (swap! note-ons assoc pitch [tick vel])

              (or (= cmd ShortMessage/NOTE_OFF)
                  (and (= cmd ShortMessage/NOTE_ON) (= vel 0)))
              (when-let [[start-tick velocity] (get @note-ons pitch)]
                (swap! notes conj (->Note pitch start-tick (- tick start-tick) velocity))
                (swap! note-ons dissoc pitch)))))))
    @notes))

(defn- load-midi-file
  "Load a MIDI file, returns {:sequence :track :notes :resolution}."
  [^File file]
  (let [seq  (MidiSystem/getSequence file)
        tracks (.getTracks seq)
        ;; Find first track with notes
        track-idx (or (first (for [i (range (alength tracks))
                                   :when (> (.size ^Track (aget tracks i)) 1)]
                               i))
                      0)
        track (aget tracks track-idx)]
    {:sequence   seq
     :track      track
     :notes      (parse-midi-track track)
     :resolution (.getResolution seq)}))

;; ---------------------------------------------------------------------------
;; Rendering
;; ---------------------------------------------------------------------------

(def ^:private NUM_KEYS 128)

(defn- paint-keys [^Graphics2D g key-height w]
  (dotimes [k NUM_KEYS]
    (let [pitch (- 127 k)
          y (* k key-height)]
      (if (t/black-key? pitch)
        (do (.setColor g (Color. 40 40 40))
            (.fillRect g 0 y w key-height))
        (do (.setColor g Color/WHITE)
            (.fillRect g 0 y w key-height)))
      (.setColor g (t/color :border))
      (.drawLine g 0 (+ y key-height) w (+ y key-height))
      ;; C labels
      (when (zero? (mod pitch 12))
        (.setColor g (t/color :text-dim))
        (.setFont g (Font. "SansSerif" Font/PLAIN 9))
        (.drawString g (str "C" (- (/ pitch 12) 2)) 2 (+ y key-height -2))))))

(defn- paint-grid [^Graphics2D g key-height tick-width total-ticks grid-interval]
  (let [h (* NUM_KEYS key-height)]
    ;; Horizontal lines (keys)
    (.setColor g (Color. 50 50 50))
    (dotimes [k NUM_KEYS]
      (let [y (* k key-height)]
        (.drawLine g 0 y (int (* total-ticks tick-width)) y)))
    ;; Vertical lines (grid)
    (loop [tick 0]
      (when (<= tick total-ticks)
        (let [x (int (* tick tick-width))]
          (.setColor g (if (zero? (mod tick (* grid-interval 4)))
                         (Color. 80 80 80)
                         (Color. 50 50 50)))
          (.drawLine g x 0 x h))
        (recur (+ tick grid-interval))))))

(defn- paint-notes [^Graphics2D g notes key-height tick-width]
  (doseq [{:keys [pitch start-tick duration-ticks velocity]} notes]
    (let [y (* (- 127 pitch) key-height)
          x (int (* start-tick tick-width))
          w (max 1 (int (* duration-ticks tick-width)))
          alpha (+ 100 (int (* (/ velocity 127.0) 155)))]
      (.setColor g (Color. 255 90 90 alpha))
      (.fillRect g x y w (dec key-height))
      (.setColor g (Color. 255 60 60))
      (.drawRect g x y w (dec key-height)))))

(defn- paint-playhead [^Graphics2D g playhead-tick tick-width h]
  (when (>= playhead-tick 0)
    (let [x (int (* playhead-tick tick-width))]
      (.setColor g Color/RED)
      (.setStroke g (BasicStroke. 2.0))
      (.drawLine g x 0 x h)
      (.setStroke g (BasicStroke. 1.0)))))

;; ---------------------------------------------------------------------------
;; Piano Roll dialog
;; ---------------------------------------------------------------------------

(defn open-piano-roll
  "Opens a modal-less piano roll editor dialog for the given MIDI file."
  [^Frame owner ^File midi-file track-idx slot-idx & {:keys [clip-idx clip-start-time]
                                                       :or {clip-idx -1 clip-start-time 0.0}}]
  (let [backend   (hibiki.BackendManager/getInstance)
        midi-data (load-midi-file midi-file)
        notes-a   (atom (vec (:notes midi-data)))
        key-height 12
        tick-scale (atom 1.0)
        base-tw    1.0
        tick-width #(* base-tw @tick-scale)
        grid-mode  (atom :auto)
        playhead   (atom -1)
        bpm-a      (atom 120.0)
        resolution (:resolution midi-data)
        total-ticks (.getTickLength ^Sequence (:sequence midi-data))

        grid-interval #(let [mode @grid-mode
                              tw (tick-width)]
                         (if (= mode :auto)
                           (t/auto-tick-interval resolution tw 15)
                           (t/tick-interval mode resolution)))

        ;; Grid panel
        grid-panel (proxy [JPanel] []
                     (paintComponent [^Graphics g]
                       (proxy-super paintComponent g)
                       (let [^Graphics2D g2 (cast Graphics2D g)
                             tw (tick-width)]
                         (.setRenderingHint g2 RenderingHints/KEY_ANTIALIASING
                                           RenderingHints/VALUE_ANTIALIAS_ON)
                         (paint-grid g2 key-height tw total-ticks (grid-interval))
                         (paint-notes g2 @notes-a key-height tw)
                         (paint-playhead g2 @playhead tw (* NUM_KEYS key-height)))))

        ;; Keys panel
        keys-panel (proxy [JPanel] []
                     (paintComponent [^Graphics g]
                       (proxy-super paintComponent g)
                       (paint-keys (cast Graphics2D g) key-height (.getWidth this))))

        dialog (JDialog. owner (str "Piano Roll — " (.getName midi-file)) false)]

    (.setBackground grid-panel (t/color :bg-dark))
    (.setPreferredSize grid-panel (Dimension. (int (* total-ticks (tick-width)))
                                              (* NUM_KEYS key-height)))
    (.setBackground keys-panel (t/color :bg-dark))
    (.setPreferredSize keys-panel (Dimension. 60 (* NUM_KEYS key-height)))

    ;; Mouse handler — click to add/remove notes
    (.addMouseListener grid-panel
      (proxy [MouseAdapter] []
        (mousePressed [^MouseEvent e]
          (let [tw (tick-width)
                tick (long (/ (.getX e) tw))
                pitch (- 127 (int (/ (.getY e) key-height)))
                snap (grid-interval)
                snapped (* (long (/ tick snap)) snap)]
            (if (SwingUtilities/isRightMouseButton e)
              ;; Remove note at position
              (swap! notes-a (fn [ns]
                               (vec (remove #(and (= (:pitch %) pitch)
                                                  (<= (:start-tick %) tick)
                                                  (> (+ (:start-tick %) (:duration-ticks %)) tick))
                                            ns))))
              ;; Add note
              (swap! notes-a conj (->Note pitch snapped snap 100)))
            (.repaint grid-panel)))))

    ;; Grid mode combo
    (let [modes (into-array String ["Auto" "Bar" "1/2" "1/4" "1/8" "1/16" "1/32"])
          combo (JComboBox. modes)]
      (.setFont combo (t/font :font-ui))
      (.addActionListener combo
        (reify ActionListener
          (actionPerformed [_ _]
            (reset! grid-mode (nth [:auto :bar :half :quarter :eighth :sixteenth :thirty-second]
                                   (.getSelectedIndex combo)))
            (.repaint grid-panel))))
      (let [toolbar (doto (JPanel. (java.awt.FlowLayout. java.awt.FlowLayout/LEFT 5 2))
                      (.setBackground (t/color :bg-darker))
                      (.add (doto (JLabel. "Grid:") (.setForeground (t/color :text-dim))))
                      (.add combo))
            scroll (doto (JScrollPane. grid-panel)
                     (.setRowHeaderView keys-panel)
                     (.setBorder nil)
                     (.setBackground (t/color :bg-dark)))]

        (.setLayout (.getContentPane dialog) (BorderLayout.))
        (.add (.getContentPane dialog) toolbar BorderLayout/NORTH)
        (.add (.getContentPane dialog) scroll BorderLayout/CENTER)))

    ;; Sync to backend on close
    (.addWindowListener dialog
      (proxy [WindowAdapter] []
        (windowClosing [^WindowEvent _]
          (let [ns @notes-a]
            (.updateClipMidi backend track-idx slot-idx clip-idx
                             resolution
                             (long-array (map :start-tick ns))
                             (int-array (map :pitch ns))
                             (long-array (map :duration-ticks ns))
                             (int-array (map :velocity ns))))
          (.dispose dialog))))

    ;; Notification handler for playhead + MIDI data
    (let [listener (reify java.util.function.Consumer
                     (accept [_ notification]
                       (condp = (.responseType notification)
                         Response/PlayheadInfo
                         (let [phi ^PlayheadInfo (.response notification (PlayheadInfo.))]
                           (reset! bpm-a (.bpm phi))
                           (when (.isPlaying phi)
                             (let [sec (- (.positionSeconds phi) clip-start-time)
                                   tps (/ resolution (/ 60.0 (.bpm phi)))]
                               (reset! playhead (long (* sec tps)))
                               (SwingUtilities/invokeLater #(.repaint grid-panel)))))

                         Response/ClipMidiData
                         (let [cmd ^ClipMidiData (.response notification (ClipMidiData.))]
                           (when (and (= (.trackIndex cmd) track-idx)
                                      (or (= (.slotIndex cmd) slot-idx)
                                          (= (.clipIndex cmd) clip-idx)))
                             (let [new-notes (vec (for [i (range (.eventsLength cmd))]
                                                    (let [ev (.events cmd i)]
                                                      (->Note (.pitch ev) (.tick ev)
                                                              (.durationTicks ev) (.velocity ev)))))]
                               (reset! notes-a new-notes)
                               (SwingUtilities/invokeLater #(.repaint grid-panel)))))
                         nil)))]
      (.addNotificationListener backend listener)
      (.addWindowListener dialog
        (proxy [WindowAdapter] []
          (windowClosed [^WindowEvent _]
            (.removeNotificationListener backend listener)))))

    ;; Request MIDI data from backend
    (.requestClipMidi backend track-idx slot-idx clip-idx)

    ;; Repaint timer
    (let [timer (Timer. 33 (reify ActionListener
                             (actionPerformed [_ _] (.repaint grid-panel))))]
      (.start timer)
      (.addWindowListener dialog
        (proxy [WindowAdapter] []
          (windowClosed [^WindowEvent _] (.stop timer)))))

    (.setSize dialog 900 600)
    (.setLocationRelativeTo dialog owner)
    (.setVisible dialog true)
    dialog))
