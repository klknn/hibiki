(ns hibiki.ui.timeline
  "Timeline arrangement view — multi-track clip timeline with grid,
   playhead, clip rendering, and mouse interaction.
   Ported from TimelineView.java, TimelineRenderer.java,
   TimelineMouseHandler.java, TimelineNotificationHandler.java."
  (:require [hibiki.ui.theme :as t]
            [hibiki.ui.piano-roll :as piano-roll])
  (:import [javax.swing JPanel JScrollPane JLabel JComboBox Timer
                         BorderFactory SwingConstants SwingUtilities BoxLayout Box]
           [java.awt BorderLayout Dimension Color Font Graphics Graphics2D
                     RenderingHints BasicStroke Rectangle Cursor]
           [java.awt.event MouseAdapter MouseEvent MouseMotionAdapter
                           ActionListener ComponentAdapter]
           [com.google.flatbuffers FlatBufferBuilder]
           [hibiki.ipc Request Command AddTimelineClip RemoveTimelineClip
                       ResizeTimelineClip Seek LoadClip
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

(defn- snap-to-bar
  "Snap a time value to the nearest bar boundary."
  [sec]
  (let [spb (/ 60.0 (:bpm @tl-state))
        bar-sec (* spb 4.0)]
    (* (Math/round (/ sec bar-sec)) bar-sec)))

(defn- find-clip-at
  "Find a TrackClip at the given track index and pixel x position, or nil."
  [track-idx x]
  (let [tracks (:tracks @tl-state)
        track (nth tracks track-idx nil)]
    (when track
      (let [pps (pixels-per-second)
            spb (/ 60.0 (:bpm @tl-state))]
        (first (filter (fn [^TrackClip clip]
                         (let [cx (seconds-to-x (:start-time clip))
                               cw (int (* (:duration-beats clip) spb pps))]
                           (and (>= x cx) (<= x (+ cx cw)))))
                       @(:clips track)))))))

;; ---------------------------------------------------------------------------
;; IPC helpers
;; ---------------------------------------------------------------------------

(defn- send-add-timeline-clip
  "Send AddTimelineClip to backend."
  [^hibiki.BackendManager backend track-idx path start-time duration-beats]
  (let [^String p (or path "")
        ^FlatBufferBuilder b (FlatBufferBuilder. 512)
        path-off (.createString b p)
        cmd (AddTimelineClip/createAddTimelineClip b (int track-idx) path-off (float start-time) (float duration-beats))
        req (Request/createRequest b Command/AddTimelineClip cmd)]
    (.finish b req)
    (.sendRequest backend b)))

(defn- send-remove-timeline-clip
  "Send RemoveTimelineClip to backend."
  [^hibiki.BackendManager backend ^long track-idx ^long clip-idx]
  (let [^FlatBufferBuilder b (FlatBufferBuilder. 128)]
    (RemoveTimelineClip/startRemoveTimelineClip b)
    (RemoveTimelineClip/addTrackIndex b (int track-idx))
    (RemoveTimelineClip/addClipIndex b (int clip-idx))
    (let [cmd (RemoveTimelineClip/endRemoveTimelineClip b)
          req (Request/createRequest b Command/RemoveTimelineClip cmd)]
      (.finish b req)
      (.sendRequest backend b))))

(defn- send-load-clip
  "Send LoadClip to backend for timeline."
  [^hibiki.BackendManager backend ^long track-idx ^String path ^double start-time]
  (let [^FlatBufferBuilder b (FlatBufferBuilder. 512)
        path-off (.createString b path)]
    (LoadClip/startLoadClip b)
    (LoadClip/addTrackIndex b (int track-idx))
    (LoadClip/addSlotIndex b (int -1))   ;; -1 = timeline
    (LoadClip/addPath b path-off)
    (let [cmd (LoadClip/endLoadClip b)
          req (Request/createRequest b Command/LoadClip cmd)]
      (.finish b req)
      (.sendRequest backend b))))

;; ---------------------------------------------------------------------------
;; Context menus
;; ---------------------------------------------------------------------------

(defn- show-clip-context-menu
  "Show context menu for an existing clip."
  [^hibiki.BackendManager backend grid-panel track-idx ^TrackClip clip x y]
  (let [menu (javax.swing.JPopupMenu.)
        edit-item (javax.swing.JMenuItem. "Edit Clip...")
        delete-item (javax.swing.JMenuItem. "Delete Clip")]
    (.addActionListener edit-item
      (reify java.awt.event.ActionListener
        (actionPerformed [_ _]
          (let [path (:path clip)
                ^String p (or path "")
                is-midi (or (nil? path) (.isEmpty p)
                            (.endsWith p ".mid"))]
            (if is-midi
              (try
                (let [file (java.io.File. (if (or (nil? path) (.isEmpty p))
                                            "New Clip.mid" p))
                      owner ^java.awt.Frame (javax.swing.SwingUtilities/getWindowAncestor ^java.awt.Component grid-panel)]
                  (piano-roll/open-piano-roll owner file track-idx -1
                                             :clip-idx (:clip-index clip)
                                             :clip-start-time (:start-time clip)))
                (catch Exception ex
                  (javax.swing.JOptionPane/showMessageDialog grid-panel
                    (str "Could not open piano roll: " (.getMessage ex)) "Error"
                    javax.swing.JOptionPane/ERROR_MESSAGE)))
              (javax.swing.JOptionPane/showMessageDialog grid-panel
                "Can only edit MIDI (.mid) clips." "Error"
                javax.swing.JOptionPane/ERROR_MESSAGE))))))
    (.addActionListener delete-item
      (reify java.awt.event.ActionListener
        (actionPerformed [_ _]
          (send-remove-timeline-clip backend track-idx (:clip-index clip))
          (let [track (nth (:tracks @tl-state) track-idx)]
            (swap! (:clips track) (fn [clips] (vec (remove #(= (:clip-index %) (:clip-index clip)) clips))))
            (.repaint ^javax.swing.JPanel grid-panel)))))
    (.add menu edit-item)
    (.addSeparator menu)
    (.add menu delete-item)
    (.show menu ^java.awt.Component grid-panel (int x) (int y))))

(defn- show-empty-context-menu
  "Show context menu for empty timeline area."
  [^hibiki.BackendManager backend grid-panel track-idx click-time x y]
  (let [menu (javax.swing.JPopupMenu.)
        create-item (javax.swing.JMenuItem. "Create New Clip")
        load-item (javax.swing.JMenuItem. "Load Clip...")]
    (.addActionListener create-item
      (reify java.awt.event.ActionListener
        (actionPerformed [_ _]
          (let [snap-time (snap-to-bar click-time)]
            (send-add-timeline-clip backend track-idx "" snap-time 4)))))
    (.addActionListener load-item
      (reify java.awt.event.ActionListener
        (actionPerformed [_ _]
          (let [chooser (javax.swing.JFileChooser. "testdata")]
            (when (= (.showOpenDialog chooser ^java.awt.Component grid-panel)
                     javax.swing.JFileChooser/APPROVE_OPTION)
              (let [path (.getAbsolutePath (.getSelectedFile chooser))
                    snap-time (snap-to-bar click-time)]
                (send-add-timeline-clip backend track-idx path snap-time 0)))))))
    (.add menu create-item)
    (.add menu load-item)
    (.show menu ^java.awt.Component grid-panel (int x) (int y))))

;; ---------------------------------------------------------------------------
;; Rendering
;; ---------------------------------------------------------------------------

(defn- paint-grid [^Graphics2D g w h]
  (let [spb   (/ 60.0 (:bpm @tl-state))
        pps   (pixels-per-second)
        bar-sec (* spb 4.0)
        grid-mode (:grid-mode @tl-state)
        grid-sec (if (= grid-mode :auto)
                   (t/auto-seconds-interval spb pps 15)
                   (t/seconds-interval grid-mode spb))
        max-sec (/ (double w) pps)]
    ;; Layer 1: grid subdivision lines (faintest)
    (when (and (> grid-sec 0) (>= (* grid-sec pps) 2))
      (.setColor g (Color. 255 255 255 15))
      (loop [t 0.0]
        (when (<= t max-sec)
          (.drawLine g (int (* t pps)) 0 (int (* t pps)) h)
          (recur (+ t grid-sec)))))
    ;; Layer 2: beat lines (medium)
    (when (and (>= (* spb pps) 4) (< grid-sec spb))
      (.setColor g (Color. 255 255 255 25))
      (loop [t 0.0]
        (when (<= t max-sec)
          (.drawLine g (int (* t pps)) 0 (int (* t pps)) h)
          (recur (+ t spb)))))
    ;; Layer 3: bar lines (brightest)
    (when (>= (* bar-sec pps) 4)
      (.setColor g (Color. 255 255 255 40))
      (loop [t 0.0]
        (when (<= t max-sec)
          (.drawLine g (int (* t pps)) 0 (int (* t pps)) h)
          (recur (+ t bar-sec)))))))

(defn- paint-track-backgrounds [^Graphics2D g w track-height]
  (let [tracks (:tracks @tl-state)
        sel    (:selected-track @tl-state)]
    (dotimes [i (count tracks)]
      (let [y (* i track-height)]
        (.setColor g (if (= i sel)
                       (.darker ^Color (.darker ^Color (t/color :accent-blue)))
                       (if (even? i) (t/color :bg-dark) (t/color :bg-darker))))
        (.fillRect g 0 y w track-height)
        (.setColor g (.darker ^Color (t/color :bg-medium)))
        (.drawLine g 0 (+ y track-height -1) w (+ y track-height -1))))))

(defn- paint-track-clips [^Graphics2D g track-height]
  (let [tracks (:tracks @tl-state)
        pps    (pixels-per-second)
        spb    (/ 60.0 (:bpm @tl-state))]
    (dotimes [i (count tracks)]
      (let [y (* i track-height)
            track (nth tracks i)]
        (doseq [^TrackClip clip @(:clips track)]
          (let [cx (int (* (:start-time clip) pps))
                cw (int (* (:duration-beats clip) spb pps))
                cy (+ y 5)
                ch (- track-height 10)
                ^String p (or (:path clip) "")
                is-midi (or (.isEmpty p) (.endsWith p ".mid") (.endsWith p ".midi"))]
            ;; Clip body
            (.setColor g (.darker ^Color (t/color :accent-blue)))
            (.fillRoundRect g cx cy cw ch 8 8)
            ;; Clip border
            (.setColor g (t/color :accent-blue))
            (.drawRoundRect g cx cy cw ch 8 8)
            ;; Clip label
            (.setColor g Color/WHITE)
            (.setFont g (.deriveFont ^Font (t/font :font-ui) (float (t/scale 10))))
            (let [clip-name (or (:name clip)
                               (when-not (.isEmpty p)
                                 (.getName (java.io.File. p)))
                               "Clip")]
              (.drawString g ^String clip-name (int (+ cx 5)) (int (+ cy 15))))))))))

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
  [^hibiki.BackendManager backend]
  (let [track-height (t/scale 80)
        drag-state (atom nil)
        grid-panel (let [p (proxy [JPanel] []
                          (paintComponent [^Graphics g]
                            (let [^JPanel this this] (proxy-super paintComponent g))
                            (let [^Graphics2D g2 (cast Graphics2D g)
                                  w (.getWidth ^JPanel this)
                                  h (.getHeight ^JPanel this)]
                              (.setRenderingHint g2 RenderingHints/KEY_ANTIALIASING
                                                RenderingHints/VALUE_ANTIALIAS_ON)
                              (paint-track-backgrounds g2 w track-height)
                              (paint-grid g2 w h)
                              (paint-track-clips g2 track-height)
                              ;; Draw drag-to-create ghost
                              (when-let [ds @drag-state]
                                (when (:creating ds)
                                  (let [start-x (seconds-to-x (:start-time ds))
                                        cur-x (:current-x ds)
                                        gx (int (min start-x cur-x))
                                        gw (int (Math/abs (double (- cur-x start-x))))
                                        gy (+ (* (:track-idx ds) track-height) 5)
                                        gh (- track-height 10)]
                                    (.setComposite g2 (java.awt.AlphaComposite/getInstance java.awt.AlphaComposite/SRC_OVER (float 0.6)))
                                    (.setColor g2 (Color. 100 200 100))
                                    (.fillRoundRect g2 gx gy gw gh 8 8)
                                    (.setColor g2 (Color. 150 255 150))
                                    (.drawRoundRect g2 gx gy gw gh 8 8)
                                    (.setColor g2 Color/WHITE)
                                    (.setFont g2 (.deriveFont ^Font (t/font :font-ui) Font/BOLD (float (t/scale 10))))
                                    (.drawString g2 "New Clip" (int (+ gx 5)) (int (+ gy 15)))
                                    (.setComposite g2 (java.awt.AlphaComposite/SrcOver)))))
                              (paint-playhead g2 h))))]
                     p)
        header-panel (proxy [JPanel] []
                       (paintComponent [^java.awt.Graphics g]
                         (proxy-super paintComponent g)
                         (let [^Graphics2D g2 (cast Graphics2D g)
                               pps (pixels-per-second)
                               spb (/ 60.0 (:bpm @tl-state))
                               bar-sec (* spb 4.0)
                               h-height (.getHeight ^JPanel this)]
                           (.setColor g2 (t/color :bg-darker))
                           (.fillRect g2 0 0 (.getWidth ^JPanel this) h-height)
                           (.setColor g2 (t/color :text-dim))
                           (.setFont g2 (Font. "SansSerif" Font/PLAIN (t/scale 9)))
                           ;; Draw bar numbers
                           (loop [bar 0]
                             (when (< bar 200)
                               (let [x (int (* bar bar-sec pps))]
                                 (when (< x (.getWidth ^JPanel this))
                                   (.drawLine g2 x (- h-height 10) x h-height)
                                   (.drawString g2 (str (inc bar)) (int (+ x 3)) (int (- h-height 12)))
                                   (recur (inc bar)))))))))
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
                  (.setBorder (BorderFactory/createMatteBorder (int 0) (int 0) (int 1) (int 1) ^Color (t/color :border))))]
        (.addMouseListener lbl
          (proxy [MouseAdapter] []
            (mousePressed [^MouseEvent _] (set-selected-track i))))
        (.add track-labels lbl)))

    ;; Mouse handler for grid
    (let [drag-state drag-state]
      (.addMouseListener grid-panel
        (proxy [MouseAdapter] []
          (mousePressed [^MouseEvent e]
            (let [track-idx (int (/ (.getY e) track-height))]
              (when (and (>= track-idx 0) (< track-idx 4))
                (set-selected-track track-idx)
                (if (javax.swing.SwingUtilities/isRightMouseButton e)
                  ;; Right-click context menu
                  (let [clip (find-clip-at track-idx (.getX e))]
                    (if clip
                      (show-clip-context-menu backend grid-panel track-idx clip (.getX e) (.getY e))
                      (let [click-time (x-to-seconds (.getX e))]
                        (show-empty-context-menu backend grid-panel track-idx click-time (.getX e) (.getY e)))))
                  ;; Left-click
                  (let [clip (find-clip-at track-idx (.getX e))]
                    (if clip
                      (let [sec (x-to-seconds (.getX e))]
                        (.seek backend (float sec)))
                      (let [start-time (snap-to-bar (x-to-seconds (.getX e)))]
                        (reset! drag-state {:creating true
                                           :track-idx track-idx
                                           :start-time start-time
                                           :current-x (.getX e)}))))))
              (.repaint grid-panel)))
          (mouseReleased [^MouseEvent e]
            (when-let [ds @drag-state]
              (when (:creating ds)
                (let [end-time (Math/max (double 0.0) (double (x-to-seconds (.getX e))))
                      end-time (if (.isShiftDown e) end-time (snap-to-bar end-time))
                      duration (- end-time (:start-time ds))]
                  (when (> duration 0.1)
                    (let [duration-beats (* duration (/ (:bpm @tl-state) 60.0))]
                      (send-add-timeline-clip backend (:track-idx ds) ""
                                              (:start-time ds) duration-beats)))))
              (reset! drag-state nil)
              (.repaint grid-panel)))))
      (.addMouseMotionListener grid-panel
        (proxy [MouseMotionAdapter] []
          (mouseDragged [^MouseEvent e]
            (if-let [ds @drag-state]
              (do (swap! drag-state assoc :current-x (.getX e))
                  (.repaint grid-panel))
              (let [sec (x-to-seconds (.getX e))]
                (.seek backend (float sec))))
            (.repaint grid-panel)))))

    ;; Grid mode selector
    (let [modes (into-array String ["Auto" "1 sec" "Bar" "1/2" "1/4" "1/8" "1/16" "1/32"
                                     "1/4T" "1/8T" "1/16T" "1/32T"])
          combo (JComboBox. ^"[Ljava.lang.Object;" modes)]
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
        (accept [_ notif]
          (let [^Notification notification notif]
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
                                        (.startTime ci) (.duration ci)
                                        (.name ci) nil)]
                  (swap! (:clips track)
                         (fn [clips]
                           (let [filtered (vec (remove #(= (:clip-index %) (:clip-index clip)) clips))]
                             (conj filtered clip))))
                  (SwingUtilities/invokeLater #(.repaint grid-panel)))))

            nil)))))

    ;; Repaint timer for playhead
    (let [timer (Timer. 33 (reify ActionListener
                             (actionPerformed [_ _]
                               (when (:is-playing @tl-state)
                                 (.repaint grid-panel)))))]
      (.start timer))

    (swap! tl-state assoc :instance panel)
    {:panel panel}))
