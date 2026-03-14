(ns hibiki.ui.timeline
  "Timeline arrangement view — multi-track clip timeline with grid,
   playhead, clip rendering, and mouse interaction.
   Ported from TimelineView.java, TimelineRenderer.java,
   TimelineMouseHandler.java, TimelineNotificationHandler.java."
  (:require [hibiki.ui.theme :as t])
  (:import [javax.swing JPanel JScrollPane JLabel JComboBox Timer
                         BorderFactory SwingConstants SwingUtilities BoxLayout Box]
           [java.awt BorderLayout Dimension Color Font Graphics Graphics2D
                     RenderingHints BasicStroke Rectangle Cursor]
           [java.awt.event MouseAdapter MouseEvent MouseMotionAdapter
                           ActionListener ComponentAdapter]
           [com.google.flatbuffers FlatBufferBuilder]
           [hibiki.ipc Request Command AddTimelineClip RemoveTimelineClip
                       ResizeTimelineClip Seek
                       Response Notification PlayheadInfo
                       TimelineClipInfo ClipWaveform]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; Track data
;; ---------------------------------------------------------------------------

(defrecord TrackClip [^int clip-index ^String path ^double start-time
                      ^double duration-beats ^String name waveform-data])

(defrecord TrackTimeline [^int track-idx ^String custom-name clips])

;; ---------------------------------------------------------------------------
;; State
;; ---------------------------------------------------------------------------

(defonce ^:private tl-state
  (atom {:tracks          (vec (for [i (range 4)]
                                 (->TrackTimeline i nil (atom []))))
         :bpm             120.0
         :playhead-sec    0.0
         :is-playing      false
         :scroll-x        0
         :pixels-per-beat 80.0
         :grid-mode       :auto
         :selected-track  0
         :instance        nil}))

(defn get-instance
  "Return the Timeline JPanel, or nil."
  ^JPanel [] (:instance @tl-state))

(defn set-selected-track
  "Set the selected track index (0-based)."
  [^long idx]
  (swap! tl-state assoc :selected-track idx))

(defn- pixels-per-second []
  (let [s @tl-state]
    (/ (:pixels-per-beat s) (/ 60.0 (:bpm s)))))

(defn- seconds-to-x [sec]
  (int (* sec (pixels-per-second))))

(defn- x-to-seconds [x]
  (/ x (pixels-per-second)))

;; ---------------------------------------------------------------------------
;; Rendering
;; ---------------------------------------------------------------------------

(defn- paint-grid [^Graphics2D g w h]
  (let [spb   (/ 60.0 (:bpm @tl-state))
        pps   (pixels-per-second)
        interval (t/seconds-interval (:grid-mode @tl-state) spb)
        auto-int (if (= (:grid-mode @tl-state) :auto)
                   (t/auto-seconds-interval spb pps 15)
                   interval)]
    (.setColor g (Color. 50 50 50))
    (loop [sec 0.0]
      (when (<= sec (/ w pps))
        (let [x (seconds-to-x sec)]
          (.drawLine g x 0 x h))
        (recur (+ sec auto-int))))))

(defn- paint-tracks [^Graphics2D g w h track-height]
  (let [tracks (:tracks @tl-state)
        sel    (:selected-track @tl-state)]
    (dotimes [i (count tracks)]
      (let [y (* i track-height)
            track (nth tracks i)]
        ;; Track background
        (.setColor g (if (= i sel) (.darker (.darker (t/color :accent-blue))) (t/color :bg-medium)))
        (.fillRect g 0 y w track-height)
        ;; Track border
        (.setColor g (t/color :border))
        (.drawLine g 0 (+ y track-height -1) w (+ y track-height -1))
        ;; Clips
        (doseq [^TrackClip clip @(:clips track)]
          (let [cx (seconds-to-x (:start-time clip))
                spb (/ 60.0 (:bpm @tl-state))
                cw (int (* (:duration-beats clip) spb (pixels-per-second)))
                is-midi (and (:path clip) (.endsWith ^String (:path clip) ".mid"))]
            (.setColor g (if is-midi (t/color :clip-midi) (t/color :clip-audio)))
            (.fillRect g cx (+ y 2) cw (- track-height 4))
            (.setColor g (t/color :text-bright))
            (.setFont g (t/font :font-ui))
            (let [name (or (:name clip) (when (:path clip)
                                          (.getName (java.io.File. ^String (:path clip))))
                           "Clip")]
              (.drawString g ^String name (+ cx 4) (+ y 16)))))))))

(defn- paint-playhead [^Graphics2D g h]
  (let [x (seconds-to-x (:playhead-sec @tl-state))]
    (.setColor g Color/RED)
    (.setStroke g (BasicStroke. 2.0))
    (.drawLine g x 0 x h)
    (.setStroke g (BasicStroke. 1.0))))

;; ---------------------------------------------------------------------------
;; Timeline panel
;; ---------------------------------------------------------------------------

(defn make-timeline-view
  "Creates the timeline view panel. Returns {:panel JPanel}."
  [backend]
  (let [track-height (t/scale 80)
        grid-panel (proxy [JPanel] []
                     (paintComponent [^Graphics g]
                       (proxy-super paintComponent g)
                       (let [^Graphics2D g2 (cast Graphics2D g)
                             w (.getWidth this)
                             h (.getHeight this)]
                         (.setRenderingHint g2 RenderingHints/KEY_ANTIALIASING
                                           RenderingHints/VALUE_ANTIALIAS_ON)
                         (paint-grid g2 w h)
                         (paint-tracks g2 w h track-height)
                         (paint-playhead g2 h))))
        header-panel (proxy [JPanel] []
                       (paintComponent [^Graphics g]
                         (proxy-super paintComponent g)
                         (let [^Graphics2D g2 (cast Graphics2D g)]
                           (.setColor g2 (t/color :text-dim))
                           (.setFont g2 (Font. "SansSerif" Font/PLAIN (t/scale 9)))
                           (let [pps (pixels-per-second)]
                             (loop [sec 0.0]
                               (when (<= sec 300)
                                 (let [x (seconds-to-x sec)
                                       min (int (/ sec 60))
                                       s   (mod (int sec) 60)]
                                   (.drawString g2 (format "%d:%02d" min s) x 12)
                                   (.drawLine g2 x 14 x 20))
                                 (recur (+ sec 1.0))))))))
        track-labels (let [p (JPanel.)]
                       (.setLayout p (BoxLayout. p BoxLayout/Y_AXIS))
                       (.setBackground p (t/color :bg-darker))
                       (.setPreferredSize p (Dimension. (t/scale 100) 0))
                       p)
        panel (JPanel. (BorderLayout.))]

    (.setBackground grid-panel (t/color :bg-dark))
    (.setPreferredSize grid-panel (Dimension. 4000 (* 4 track-height)))
    (.setPreferredSize header-panel (Dimension. 4000 (t/scale 20)))
    (.setBackground header-panel (t/color :bg-darker))

    ;; Track labels
    (dotimes [i 4]
      (let [lbl (doto (JLabel. (str "Track " i) SwingConstants/CENTER)
                  (.setPreferredSize (Dimension. (t/scale 100) track-height))
                  (.setMaximumSize (Dimension. (t/scale 100) track-height))
                  (.setMinimumSize (Dimension. (t/scale 100) track-height))
                  (.setForeground (t/color :text-bright))
                  (.setFont (t/font :font-ui-bold))
                  (.setOpaque true)
                  (.setBackground (t/color :bg-darker))
                  (.setBorder (BorderFactory/createMatteBorder 0 0 1 1 (t/color :border))))]
        (.addMouseListener lbl
          (proxy [MouseAdapter] []
            (mousePressed [^MouseEvent _] (set-selected-track i))))
        (.add track-labels lbl)))

    ;; Mouse handler for grid
    (let [drag-state (atom nil)]
      (.addMouseListener grid-panel
        (proxy [MouseAdapter] []
          (mousePressed [^MouseEvent e]
            (let [track-idx (int (/ (.getY e) track-height))]
              (when (and (>= track-idx 0) (< track-idx 4))
                (set-selected-track track-idx)
                ;; Click-to-seek on ruler / grid
                (let [sec (x-to-seconds (.getX e))]
                  (.seek backend (float sec))))
              (.repaint grid-panel)))
          (mouseReleased [^MouseEvent _] (reset! drag-state nil))))
      (.addMouseMotionListener grid-panel
        (proxy [MouseMotionAdapter] []
          (mouseDragged [^MouseEvent e]
            (let [sec (x-to-seconds (.getX e))]
              (.seek backend (float sec)))
            (.repaint grid-panel)))))

    ;; Grid mode selector
    (let [modes (into-array String ["Auto" "1 sec" "Bar" "1/2" "1/4" "1/8" "1/16" "1/32"
                                     "1/4T" "1/8T" "1/16T" "1/32T"])
          combo (JComboBox. modes)]
      (.setFont combo (t/font :font-ui))
      (.setMaximumSize combo (Dimension. (t/scale 80) (t/scale 22)))
      (.addActionListener combo
        (reify ActionListener
          (actionPerformed [_ _]
            (let [idx (.getSelectedIndex combo)
                  mode (nth t/grid-modes idx :auto)]
              (swap! tl-state assoc :grid-mode mode)
              (.repaint grid-panel)))))

      ;; Layout
      (let [top (doto (JPanel. (BorderLayout.))
                  (.add header-panel BorderLayout/CENTER)
                  (.add (doto (JPanel.)
                          (.setPreferredSize (Dimension. (t/scale 100) (t/scale 20)))
                          (.setBackground (t/color :bg-darker))) BorderLayout/WEST))
            center (doto (JPanel. (BorderLayout.))
                     (.add track-labels BorderLayout/WEST)
                     (.add grid-panel BorderLayout/CENTER))
            scroll (doto (JScrollPane. center)
                     (.setColumnHeaderView top)
                     (.setBorder nil)
                     (.setBackground (t/color :bg-dark)))
            toolbar (doto (JPanel. (java.awt.FlowLayout. java.awt.FlowLayout/LEFT 5 2))
                      (.setBackground (t/color :bg-darker))
                      (.setPreferredSize (Dimension. 0 (t/scale 25)))
                      (.add (doto (JLabel. "Grid:") (.setForeground (t/color :text-dim)) (.setFont (t/font :font-ui))))
                      (.add combo))]
        (.add panel toolbar BorderLayout/NORTH)
        (.add panel scroll BorderLayout/CENTER)))

    ;; Notification handler
    (.addNotificationListener backend
      (reify java.util.function.Consumer
        (accept [_ notification]
          (condp = (.responseType notification)
            Response/PlayheadInfo
            (let [phi ^PlayheadInfo (.response notification (PlayheadInfo.))]
              (swap! tl-state assoc
                     :playhead-sec (.positionSec phi)
                     :bpm (.bpm phi)
                     :is-playing (.isPlaying phi))
              (SwingUtilities/invokeLater #(.repaint grid-panel)))

            Response/TimelineClipInfo
            (let [ci ^TimelineClipInfo (.response notification (TimelineClipInfo.))
                  ti (.trackIndex ci)]
              (when (and (>= ti 0) (< ti 4))
                (let [track (nth (:tracks @tl-state) ti)
                      clip (->TrackClip (.clipIndex ci) (.path ci)
                                        (.startTime ci) (.durationBeats ci)
                                        (.name ci) nil)]
                  (swap! (:clips track)
                         (fn [clips]
                           (let [filtered (vec (remove #(= (:clip-index %) (:clip-index clip)) clips))]
                             (conj filtered clip))))
                  (SwingUtilities/invokeLater #(.repaint grid-panel)))))

            nil))))

    ;; Repaint timer for playhead
    (let [timer (Timer. 33 (reify ActionListener
                             (actionPerformed [_ _]
                               (when (:is-playing @tl-state)
                                 (.repaint grid-panel)))))]
      (.start timer))

    (swap! tl-state assoc :instance panel)
    {:panel panel}))
