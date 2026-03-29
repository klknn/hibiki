(ns hibiki.ui.piano-roll
  "Piano Roll MIDI editor — note grid with velocity editing."
  (:require [hibiki.ui.theme :as t])
  (:import [javax.swing JDialog JPanel JScrollPane JLabel JComboBox Timer
                         BorderFactory SwingConstants SwingUtilities BoxLayout Box]
           [java.awt BorderLayout Dimension Color Font Graphics Graphics2D
                     RenderingHints BasicStroke Frame]
           [java.awt.event MouseAdapter MouseEvent MouseMotionAdapter
                           ActionListener WindowAdapter WindowEvent KeyEvent]
           [javax.sound.midi MidiSystem Sequence Track MidiEvent ShortMessage]
           [java.io File]
           [hibiki.pb.notifications Notification Notification$ResponseCase]))

(set! *warn-on-reflection* true)

(defrecord Note [pitch start-tick duration-ticks velocity])

(defn- parse-midi-track [^Track track]
  (let [note-ons (atom {}) notes (atom [])]
    (dotimes [i (.size track)]
      (let [^MidiEvent evt (.get track i) msg (.getMessage evt)]
        (when (instance? ShortMessage msg)
          (let [^ShortMessage sm msg cmd (.getCommand sm)
                pitch (.getData1 sm) vel (.getData2 sm) tick (.getTick evt)]
            (cond
              (and (= cmd ShortMessage/NOTE_ON) (> vel 0))
              (swap! note-ons assoc pitch [tick vel])
              (or (= cmd ShortMessage/NOTE_OFF)
                  (and (= cmd ShortMessage/NOTE_ON) (= vel 0)))
              (when-let [[st v] (get @note-ons pitch)]
                (swap! notes conj (->Note pitch st (- tick st) v))
                (swap! note-ons dissoc pitch)))))))
    @notes))

(defn- load-midi-file [^File file]
  (if (.exists file)
    (let [seq (MidiSystem/getSequence file) tracks (.getTracks seq)
          ti (or (first (for [i (range (alength tracks))
                              :when (> (.size ^Track (aget tracks i)) 1)] i)) 0)
          track (aget tracks ti)]
      {:sequence seq :track track :notes (parse-midi-track track) :resolution (.getResolution seq)})
    (let [seq (Sequence. javax.sound.midi.Sequence/PPQ 480) track (.createTrack seq)]
      {:sequence seq :track track :notes [] :resolution 480})))

(def ^:private NUM_KEYS 128)

(defn- paint-keys [^Graphics2D g kh w]
  (dotimes [k NUM_KEYS]
    (let [pitch (- 127 k) y (* k kh)]
      (if (t/black-key? pitch)
        (do (.setColor g (Color. 40 40 40)) (.fillRect g 0 y w kh))
        (do (.setColor g Color/WHITE) (.fillRect g 0 y w kh)))
      (.setColor g (t/color :border)) (.drawLine g 0 (+ y kh) w (+ y kh))
      (when (zero? (mod pitch 12))
        (.setColor g (t/color :text-dim)) (.setFont g (Font. "SansSerif" Font/PLAIN 9))
        (.drawString g (str "C" (- (/ pitch 12) 2)) (int 2) (int (+ y kh -2)))))))

(defn- paint-grid [^Graphics2D g kh tw tt gi]
  (let [h (* NUM_KEYS kh)]
    (.setColor g (Color. 50 50 50))
    (dotimes [k NUM_KEYS] (.drawLine g 0 (* k kh) (int (* tt tw)) (* k kh)))
    (loop [tick 0]
      (when (<= tick tt)
        (let [x (int (* tick tw))]
          (.setColor g (if (zero? (mod tick (* gi 4))) (Color. 80 80 80) (Color. 50 50 50)))
          (.drawLine g x 0 x h))
        (recur (long (+ tick gi)))))))

(defn- paint-notes [^Graphics2D g notes kh tw]
  (doseq [{:keys [pitch start-tick duration-ticks velocity]} notes]
    (let [y (* (- 127 pitch) kh) x (int (* start-tick tw))
          w (max 1 (int (* duration-ticks tw)))
          a (+ 100 (int (* (/ velocity 127.0) 155)))]
      (.setColor g (Color. 255 90 90 a)) (.fillRect g x y w (dec kh))
      (.setColor g (Color. 255 60 60)) (.drawRect g x y w (dec kh)))))

(defn- paint-playhead [^Graphics2D g pt tw h]
  (when (>= pt 0)
    (let [x (int (* pt tw))]
      (.setColor g Color/RED) (.setStroke g (BasicStroke. 2.0))
      (.drawLine g x 0 x h) (.setStroke g (BasicStroke. 1.0)))))

(defn open-piano-roll
  [^Frame owner ^File midi-file track-idx slot-idx
   & {:keys [clip-idx clip-start-time] :or {clip-idx -1 clip-start-time 0.0}}]
  (let [backend (hibiki.BackendManager/getInstance)
        md (load-midi-file midi-file) notes-a (atom (vec (:notes md)))
        kh 12 tick-scale (atom 1.0) btw 1.0 tw #(* btw @tick-scale)
        gm (atom :auto) playhead (atom -1) bpm-a (atom 120.0)
        res (:resolution md) tt (.getTickLength ^Sequence (:sequence md))
        gi #(let [m @gm v (tw)]
              (if (= m :auto) (t/auto-tick-interval res v 15) (t/tick-interval m res)))
        gp (proxy [JPanel] []
             (paintComponent [^Graphics g]
               (proxy-super paintComponent g)
               (let [^Graphics2D g2 (cast Graphics2D g) v (tw)]
                 (.setRenderingHint g2 RenderingHints/KEY_ANTIALIASING RenderingHints/VALUE_ANTIALIAS_ON)
                 (paint-grid g2 kh v tt (gi)) (paint-notes g2 @notes-a kh v)
                 (paint-playhead g2 @playhead v (* NUM_KEYS kh)))))
        kp (proxy [JPanel] []
             (paintComponent [^Graphics g]
               (proxy-super paintComponent g)
               (paint-keys (cast Graphics2D g) kh (.getWidth ^JPanel this))))
        dlg (JDialog. owner (str "Piano Roll — " (.getName midi-file)) false)]
    (.setBackground gp (t/color :bg-dark))
    (.setPreferredSize gp (Dimension. (int (* tt (tw))) (* NUM_KEYS kh)))
    (.setBackground kp (t/color :bg-dark))
    (.setPreferredSize kp (Dimension. 60 (* NUM_KEYS kh)))
    (.addMouseListener gp
      (proxy [MouseAdapter] []
        (mousePressed [^MouseEvent e]
          (let [v (tw) tick (long (/ (.getX e) v))
                pitch (- 127 (int (/ (.getY e) kh))) snap (gi)
                snapped (* (long (/ tick snap)) snap)]
            (if (SwingUtilities/isRightMouseButton e)
              (swap! notes-a (fn [ns] (vec (remove #(and (= (:pitch %) pitch)
                (<= (:start-tick %) tick) (> (+ (:start-tick %) (:duration-ticks %)) tick)) ns))))
              (swap! notes-a conj (->Note pitch snapped snap 100)))
            (.repaint gp)))))
    (let [modes (into-array String ["Auto" "Bar" "1/2" "1/4" "1/8" "1/16" "1/32"])
          combo (JComboBox. ^"[Ljava.lang.Object;" modes)]
      (.setFont combo (t/font :font-ui))
      (.addActionListener combo (reify ActionListener
        (actionPerformed [_ _]
          (reset! gm (nth [:auto :bar :half :quarter :eighth :sixteenth :thirty-second]
                          (.getSelectedIndex combo)))
          (.repaint gp))))
      (let [tb (doto (JPanel. (java.awt.FlowLayout. java.awt.FlowLayout/LEFT 5 2))
                 (.setBackground (t/color :bg-darker))
                 (.add (doto (JLabel. "Grid:") (.setForeground (t/color :text-dim)))) (.add combo))
            sc (doto (JScrollPane. gp) (.setRowHeaderView kp) (.setBorder nil) (.setBackground (t/color :bg-dark)))]
        (.setLayout (.getContentPane dlg) (BorderLayout.))
        (.add (.getContentPane dlg) tb BorderLayout/NORTH)
        (.add (.getContentPane dlg) sc BorderLayout/CENTER)))
    (.addWindowListener dlg
      (proxy [WindowAdapter] []
        (windowClosing [^WindowEvent _]
          (let [ns @notes-a]
            (.updateClipMidi backend track-idx slot-idx clip-idx res
              (long-array (map :start-tick ns)) (int-array (map :pitch ns))
              (long-array (map :duration-ticks ns)) (int-array (map :velocity ns))))
          (.dispose dlg))))
    (let [listener (reify java.util.function.Consumer
                     (accept [_ notif]
                       (let [^Notification n notif]
                         (case (.getResponseCase n)
                           Notification$ResponseCase/PLAYHEAD_INFO
                           (let [phi (.getPlayheadInfo n)]
                             (reset! bpm-a (.getBpm phi))
                             (when (.getIsPlaying phi)
                               (let [sec (- (.getPositionSec phi) clip-start-time)
                                     tps (/ res (/ 60.0 (.getBpm phi)))]
                                 (reset! playhead (long (* sec tps)))
                                 (SwingUtilities/invokeLater #(.repaint gp)))))
                           Notification$ResponseCase/CLIP_MIDI_DATA
                           (let [cmd (.getClipMidiData n)]
                             (when (and (= (.getTrackIndex cmd) track-idx)
                                        (or (= (.getSlotIndex cmd) slot-idx)
                                            (= (.getClipIndex cmd) clip-idx)))
                               (let [nn (vec (for [i (range (.getEventsCount cmd))]
                                               (let [ev (.getEvents cmd i)]
                                                 (->Note (.getPitch ev) (.getTick ev)
                                                         (.getDurationTicks ev) (.getVelocity ev)))))]
                                 (reset! notes-a nn)
                                 (SwingUtilities/invokeLater #(.repaint gp)))))
                           nil))))]
      (.addNotificationListener backend listener)
      (.addWindowListener dlg (proxy [WindowAdapter] []
        (windowClosed [^WindowEvent _] (.removeNotificationListener backend listener)))))
    (.requestClipMidi backend track-idx slot-idx clip-idx)
    (let [timer (Timer. 33 (reify ActionListener (actionPerformed [_ _] (.repaint gp))))]
      (.start timer)
      (.addWindowListener dlg (proxy [WindowAdapter] []
        (windowClosed [^WindowEvent _] (.stop timer)))))
    (.setSize dlg 900 600) (.setLocationRelativeTo dlg owner) (.setVisible dlg true) dlg))
