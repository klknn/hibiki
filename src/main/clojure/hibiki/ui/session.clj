(ns hibiki.ui.session
  "Session View — clip grid with 4 tracks × 5 slots.
   Ported from SessionView.java + SessionViewIpc.java."
  (:require [hibiki.ui.theme :as t]
            [hibiki.ui.widgets :as w])
  (:import [javax.swing JPanel JButton JLabel JSlider JPopupMenu JMenuItem
                         JCheckBoxMenuItem JOptionPane JFileChooser
                         BorderFactory SwingConstants SwingUtilities BoxLayout Box]
           [java.awt BorderLayout Color Dimension Component Cursor Frame Font]
           [java.awt.event MouseAdapter MouseEvent ActionListener]
           [java.awt.datatransfer DataFlavor]
           [java.awt.dnd DropTarget DropTargetAdapter DropTargetDropEvent
                         DropTargetDragEvent DropTargetEvent DnDConstants]
           [java.io File]
           [com.google.flatbuffers FlatBufferBuilder]
           [hibiki.ipc Request Command PlayClip StopTrack LoadClip
                       SetClipLoop PlayScene DeleteClip
                       Response ClipInfo TrackLevels TrackLevel]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; IPC helpers
;; ---------------------------------------------------------------------------

(defn- send-load-clip
  [^hibiki.BackendManager backend track-idx slot-idx ^String path loop?]
  (let [^FlatBufferBuilder builder (FlatBufferBuilder. 256)
        path-off (.createString builder path)
        cmd-off  (do (LoadClip/startLoadClip builder)
                     (LoadClip/addTrackIndex builder track-idx)
                     (LoadClip/addSlotIndex builder slot-idx)
                     (LoadClip/addPath builder path-off)
                     (LoadClip/addIsLoop builder loop?)
                     (LoadClip/endLoadClip builder))
        req-off  (Request/createRequest builder Command/LoadClip cmd-off)]
    (.finish builder req-off)
    (.sendRequest backend builder)))

(defn- send-play-clip
  [^hibiki.BackendManager backend ^long track-idx ^long slot-idx]
  (let [^FlatBufferBuilder builder (FlatBufferBuilder. 64)
        cmd (do (PlayClip/startPlayClip builder)
                (PlayClip/addTrackIndex builder track-idx)
                (PlayClip/addSlotIndex builder slot-idx)
                (PlayClip/endPlayClip builder))
        req (Request/createRequest builder Command/PlayClip cmd)]
    (.finish builder req)
    (.sendRequest backend builder)))

(defn- send-stop-track [backend track-idx]
  (let [builder (FlatBufferBuilder. 64)
        cmd (do (StopTrack/startStopTrack builder)
                (StopTrack/addTrackIndex builder track-idx)
                (StopTrack/endStopTrack builder))
        req (Request/createRequest builder Command/StopTrack cmd)]
    (.finish builder req)
    (.sendRequest backend builder)))

(defn- send-play-scene [backend slot-idx]
  (let [builder (FlatBufferBuilder. 64)
        cmd (do (PlayScene/startPlayScene builder)
                (PlayScene/addSlotIndex builder slot-idx)
                (PlayScene/endPlayScene builder))
        req (Request/createRequest builder Command/PlayScene cmd)]
    (.finish builder req)
    (.sendRequest backend builder)))

(defn- send-set-clip-loop [backend track-idx slot-idx loop?]
  (let [builder (FlatBufferBuilder. 64)
        cmd (do (SetClipLoop/startSetClipLoop builder)
                (SetClipLoop/addTrackIndex builder track-idx)
                (SetClipLoop/addSlotIndex builder slot-idx)
                (SetClipLoop/addIsLoop builder loop?)
                (SetClipLoop/endSetClipLoop builder))
        req (Request/createRequest builder Command/SetClipLoop cmd)]
    (.finish builder req)
    (.sendRequest backend builder)))

(defn- send-delete-clip [backend track-idx slot-idx]
  (let [builder (FlatBufferBuilder. 64)
        cmd (do (DeleteClip/startDeleteClip builder)
                (DeleteClip/addTrackIndex builder track-idx)
                (DeleteClip/addSlotIndex builder slot-idx)
                (DeleteClip/endDeleteClip builder))
        req (Request/createRequest builder Command/DeleteClip cmd)]
    (.finish builder req)
    (.sendRequest backend builder)))

;; ---------------------------------------------------------------------------
;; State
;; ---------------------------------------------------------------------------

(defonce ^:private session-state
  (atom {:slot-buttons (make-array JButton 5 5)
         :slot-paths   (make-array String 5 5)
         :track-meters (object-array 4)
         :track-strips (object-array 4)
         :track-headers (object-array 4)
         :selected-track 0
         :instance nil}))

(defn get-instance
  "Return the SessionView JPanel, or nil."
  ^JPanel [] (:instance @session-state))

(defn get-selected-track
  "Return the 0-based index of the currently selected track."
  ^long [] (:selected-track @session-state))

;; ---------------------------------------------------------------------------
;; Session View panel
;; ---------------------------------------------------------------------------

(defn- show-clip-context-menu [backend panel btn track-idx slot-idx x y]
  (let [menu (JPopupMenu.)
        load-item (JMenuItem. "Load Clip...")
        edit-item (JMenuItem. "Edit Clip...")
        loop-item (JCheckBoxMenuItem. "Loop")
        delete-item (JMenuItem. "Delete Clip")]
    (.addActionListener load-item
      (reify ActionListener
        (actionPerformed [_ _]
          (let [chooser (JFileChooser. "testdata")]
            (when (= (.showOpenDialog chooser panel) JFileChooser/APPROVE_OPTION)
              (send-load-clip backend track-idx slot-idx
                              (.getAbsolutePath (.getSelectedFile chooser)) false))))))
    (.addActionListener edit-item
      (reify ActionListener
        (actionPerformed [_ _]
          (let [path (aget ^"[Ljava.lang.String;" (aget (:slot-paths @session-state) track-idx) slot-idx)]
            (if (and path (.endsWith ^String path ".mid"))
              ;; TODO: open PianoRoll
              (println "Would open PianoRoll for" path)
              (JOptionPane/showMessageDialog panel "Can only edit MIDI (.mid) clips."
                                             "Error" JOptionPane/ERROR_MESSAGE))))))
    (.addActionListener loop-item
      (reify ActionListener
        (actionPerformed [_ _]
          (send-set-clip-loop backend track-idx slot-idx (.isSelected loop-item)))))
    (.addActionListener delete-item
      (reify ActionListener
        (actionPerformed [_ _] (send-delete-clip backend track-idx slot-idx))))
    (.add menu load-item)
    (.add menu edit-item)
    (.add menu loop-item)
    (.addSeparator menu)
    (.add menu delete-item)
    (.show menu btn x y)))

(defn select-track!
  "Select the given track index (0-based). Updates header highlights."
  [^long track-idx]
  (let [prev (:selected-track @session-state)]
    (when (not= prev track-idx)
      (swap! session-state assoc :selected-track track-idx)
      (dotimes [i 4]
        (when-let [strip (aget (:track-strips @session-state) i)]
          (if (= i track-idx)
            (do (.setBackground ^JPanel strip (.darker (.darker (t/color :accent-blue))))
                (when-let [hdr (aget (:track-headers @session-state) i)]
                  (.setBackground ^JLabel hdr (.darker (t/color :accent-blue)))))
            (do (.setBackground ^JPanel strip (t/color :panel-bg))
                (when-let [hdr (aget (:track-headers @session-state) i)]
                  (.setBackground ^JLabel hdr (t/color :track-header))))))))))

(defn- create-track-strip [backend session-panel track-idx name]
  (let [strip (JPanel.)
        header (doto (JLabel. (str track-idx " " name) SwingConstants/CENTER)
                 (.setAlignmentX Component/CENTER_ALIGNMENT)
                 (.setMinimumSize   (Dimension. (t/scale 110) (t/scale 30)))
                 (.setMaximumSize   (Dimension. (t/scale 110) (t/scale 30)))
                 (.setPreferredSize (Dimension. (t/scale 110) (t/scale 30)))
                 (.setBackground (t/color :track-header))
                 (.setForeground (t/color :text-bright))
                 (.setFont (t/font :font-ui-bold))
                 (.setOpaque true)
                 (.setBorder (BorderFactory/createMatteBorder 0 0 1 0 (t/color :border)))
                 (.setCursor (Cursor/getPredefinedCursor Cursor/HAND_CURSOR)))]
    (.setLayout strip (BoxLayout. strip BoxLayout/Y_AXIS))
    (.setBackground strip (t/color :panel-bg))
    (.setPreferredSize strip (Dimension. (t/scale 110) (t/scale 400)))
    (.setMaximumSize strip (Dimension. (t/scale 110) 32767))
    (.setBorder strip (BorderFactory/createMatteBorder 0 0 0 1 (t/color :border)))

    (aset (:track-strips @session-state) track-idx strip)
    (aset (:track-headers @session-state) track-idx header)

    (.addMouseListener header
      (proxy [MouseAdapter] []
        (mousePressed [^MouseEvent _e] (select-track! track-idx))
        (mouseClicked [^MouseEvent e]
          (when (= (.getClickCount e) 2)
            ;; rename track
            (let [new-name (JOptionPane/showInputDialog session-panel "Enter track name:" name)]
              (when new-name
                (.setText header (str track-idx " " (if (.isEmpty ^String new-name) name new-name)))))))))
    (.add strip header)

    ;; Clip buttons
    (dotimes [i 5]
      (let [btn (doto (JButton. "")
                  (.setAlignmentX Component/CENTER_ALIGNMENT)
                  (.setMinimumSize   (Dimension. (t/scale 100) (t/scale 30)))
                  (.setMaximumSize   (Dimension. (t/scale 100) (t/scale 30)))
                  (.setPreferredSize (Dimension. (t/scale 100) (t/scale 30)))
                  (.setFont (t/font :font-ui))
                  (.setBackground (t/color :panel-bg-light))
                  (.setForeground (t/color :text-normal))
                  (.setBorder (BorderFactory/createLineBorder (t/color :border)))
                  (.setFocusPainted false))]
        (.addActionListener btn
          (reify ActionListener
            (actionPerformed [_ _] (send-play-clip backend track-idx i))))
        (.addMouseListener btn
          (proxy [MouseAdapter] []
            (mousePressed [^MouseEvent e]
              (when (SwingUtilities/isRightMouseButton e)
                (show-clip-context-menu backend session-panel btn track-idx i
                                        (.getX e) (.getY e))))))
        ;; Drop target
        (when-not (java.awt.GraphicsEnvironment/isHeadless)
          (DropTarget. btn
            (proxy [DropTargetAdapter] []
              (drop [^DropTargetDropEvent dtde]
                (try
                  (.acceptDrop dtde DnDConstants/ACTION_COPY)
                  (let [trans (.getTransferable dtde)]
                    (when (.isDataFlavorSupported trans DataFlavor/javaFileListFlavor)
                      (let [files (.getTransferData trans DataFlavor/javaFileListFlavor)]
                        (when-let [^File f (first files)]
                          (let [path (.getAbsolutePath f)
                                lower (.toLowerCase path)]
                            (when (some #(.endsWith lower %) [".mid" ".midi" ".wav" ".mp3" ".ogg" ".flac"])
                              (send-load-clip backend track-idx i path false)))))))
                  (.dropComplete dtde true)
                  (catch Exception _ex (.dropComplete dtde false))))
              (dragEnter [^DropTargetDragEvent _]
                (.setBackground btn (t/color :accent-blue)))
              (dragExit [^DropTargetEvent _]
                (.setBackground btn (t/color :panel-bg-light))))))
        (aset ^"[[Ljavax.swing.JButton;" (:slot-buttons @session-state) track-idx
              (doto (make-array JButton 5) (aset i btn)))
        ;; Actually store per-slot
        (aset ^"[Ljavax.swing.JButton;" (aget ^"[[Ljavax.swing.JButton;" (:slot-buttons @session-state) track-idx) i btn)
        (.add strip (Box/createVerticalStrut (t/scale 2)))
        (.add strip btn)))

    (.add strip (Box/createVerticalGlue))

    ;; Level meter + volume
    (let [{meter-panel :panel set-levels! :set-levels!} (w/make-level-meter)
          controls (let [p (JPanel.)]
                    (.setLayout p (BoxLayout. p BoxLayout/X_AXIS))
                    (.setOpaque p false)
                    (.setMaximumSize p (Dimension. (t/scale 110) (t/scale 150)))
                    p)
          vol-slider (doto (JSlider. JSlider/VERTICAL -70 6 0)
                       (.setMaximumSize (Dimension. (t/scale 30) (t/scale 100)))
                       (.setBackground (t/color :panel-bg)))
          vol-panel (let [p (JPanel.)]
                     (.setLayout p (BoxLayout. p BoxLayout/Y_AXIS))
                     (.setOpaque p false)
                     (.add p vol-slider)
                     p)]
      (aset (:track-meters @session-state) track-idx {:set-levels! set-levels!})
      (.add controls (Box/createHorizontalStrut (t/scale 10)))
      (.add controls meter-panel)
      (.add controls (Box/createHorizontalStrut (t/scale 5)))
      (.add controls vol-panel)
      (.add controls (Box/createHorizontalStrut (t/scale 10)))
      (.add strip controls))

    (.add strip (Box/createVerticalStrut (t/scale 5)))

    ;; Pan slider
    (let [pan (doto (JSlider. -50 50 0)
               (.setMaximumSize (Dimension. (t/scale 90) (t/scale 20)))
               (.setBackground (t/color :panel-bg)))
          lbl (doto (JLabel. "Pan" SwingConstants/CENTER)
                (.setAlignmentX Component/CENTER_ALIGNMENT)
                (.setFont (Font. "SansSerif" Font/PLAIN (t/scale 9)))
                (.setForeground (t/color :text-dim)))]
      (.add strip lbl)
      (.add strip pan))

    ;; Activator button
    (let [active-btn (doto (JButton. (str track-idx))
                       (.setAlignmentX Component/CENTER_ALIGNMENT)
                       (.setFont (t/font :font-ui-bold))
                       (.setFocusPainted false)
                       (.setBorder (BorderFactory/createLineBorder (t/color :border)))
                       (.setBackground (Color. 200 160 50))
                       (.setForeground Color/BLACK)
                       (.addActionListener
                         (reify ActionListener
                           (actionPerformed [_ _] (send-stop-track backend track-idx)))))]
      (.add strip (Box/createVerticalStrut (t/scale 5)))
      (.add strip active-btn)
      (.add strip (Box/createVerticalStrut (t/scale 5))))
    strip))

(defn- create-master-strip [backend]
  (let [strip (let [p (JPanel.)]
                (.setLayout p (BoxLayout. p BoxLayout/Y_AXIS))
                (.setBackground p (t/color :panel-bg))
                (.setPreferredSize p (Dimension. (t/scale 110) (t/scale 400)))
                (.setMaximumSize p (Dimension. (t/scale 110) 32767))
                (.setBorder p (BorderFactory/createMatteBorder 0 0 0 1 (t/color :border)))
                p)
        header (doto (JLabel. "Master" SwingConstants/CENTER)
                 (.setAlignmentX Component/CENTER_ALIGNMENT)
                 (.setMinimumSize   (Dimension. (t/scale 110) (t/scale 30)))
                 (.setMaximumSize   (Dimension. (t/scale 110) (t/scale 30)))
                 (.setPreferredSize (Dimension. (t/scale 110) (t/scale 30)))
                 (.setBackground (t/color :track-header))
                 (.setForeground (t/color :text-bright))
                 (.setFont (t/font :font-ui-bold))
                 (.setOpaque true)
                 (.setBorder (BorderFactory/createMatteBorder 0 0 1 0 (t/color :border))))]
    (.add strip header)
    (dotimes [i 5]
      (let [scene-btn (doto (JButton. (str (inc i) " ►"))
                        (.setAlignmentX Component/CENTER_ALIGNMENT)
                        (.setFont (t/font :font-ui-bold))
                        (.setFocusPainted false)
                        (.setBorder (BorderFactory/createLineBorder (t/color :border)))
                        (.setMinimumSize   (Dimension. (t/scale 100) (t/scale 30)))
                        (.setMaximumSize   (Dimension. (t/scale 100) (t/scale 30)))
                        (.setPreferredSize (Dimension. (t/scale 100) (t/scale 30)))
                        (.addActionListener
                          (reify ActionListener
                            (actionPerformed [_ _] (send-play-scene backend i)))))]
        (.add strip (Box/createVerticalStrut (t/scale 2)))
        (.add strip scene-btn)))
    (.add strip (Box/createVerticalGlue))
    (let [vol (doto (JSlider. JSlider/VERTICAL -70 6 0)
               (.setMaximumSize (Dimension. (t/scale 30) (t/scale 100)))
               (.setBackground (t/color :panel-bg)))
          lbl (doto (JLabel. "Master" SwingConstants/CENTER)
                (.setAlignmentX Component/CENTER_ALIGNMENT)
                (.setFont (Font. "SansSerif" Font/PLAIN (t/scale 9)))
                (.setForeground (t/color :text-dim)))]
      (.add strip lbl)
      (.add strip vol))
    strip))

(defn make-session-view
  "Creates the session view panel. Returns {:panel JPanel}."
  [backend]
  (let [panel (doto (JPanel. (BorderLayout.))
                (.setBackground (t/color :bg-dark)))
        master (create-master-strip backend)
        track-panel (JPanel.)]
    (.setLayout track-panel (BoxLayout. track-panel BoxLayout/X_AXIS))
    (.setBackground track-panel (t/color :bg-dark))

    (dotimes [i 4]
      (.add track-panel (create-track-strip backend panel i (str "Track " i))))

    (let [scroll (doto (javax.swing.JScrollPane. track-panel)
                   (.setHorizontalScrollBarPolicy javax.swing.JScrollPane/HORIZONTAL_SCROLLBAR_AS_NEEDED)
                   (.setVerticalScrollBarPolicy javax.swing.JScrollPane/VERTICAL_SCROLLBAR_NEVER)
                   (.setBorder nil)
                   (.setBackground (t/color :bg-dark)))]
      (.setUnitIncrement (.getHorizontalScrollBar scroll) (t/scale 16))
      (.add panel scroll BorderLayout/CENTER)
      (.add panel master BorderLayout/EAST))

    ;; Notification handler
    (.addNotificationListener backend
      (reify java.util.function.Consumer
        (accept [_ notification]
          (condp = (.responseType notification)
            Response/ClipInfo
            (let [info ^ClipInfo (.response notification (ClipInfo.))]
              (let [ti (.trackIndex info) si (.slotIndex info) nm (.name info)]
                (SwingUtilities/invokeLater
                  #(when (and (>= ti 0) (< ti 4) (>= si 0) (< si 5))
                     (let [btn (aget ^"[Ljavax.swing.JButton;"
                                     (aget ^"[[Ljavax.swing.JButton;" (:slot-buttons @session-state) ti) si)]
                       (when btn
                         (if (.isEmpty ^String nm)
                           (do (.setText btn "")
                               (.setBackground btn (t/color :panel-bg-light))
                               (.setForeground btn (t/color :text-normal)))
                           (do (.setText btn (str "<html><center>" nm "<br>▶</center></html>"))
                               (.setBackground btn (t/color :clip-playing))
                               (.setForeground btn Color/BLACK)))))))))

            Response/TrackLevels
            (let [tl ^TrackLevels (.response notification (TrackLevels.))]
              (SwingUtilities/invokeLater
                #(dotimes [i (.levelsLength tl)]
                   (let [l (.levels tl i)
                         ti (.trackIndex l)]
                     (when (and (>= ti 0) (< ti 4))
                       (when-let [meter (aget (:track-meters @session-state) ti)]
                         ((:set-levels! meter) (.peakL l) (.peakR l))))))))

            Response/ClearProject
            (SwingUtilities/invokeLater
              #(doseq [ti (range 1 5) si (range 5)]
                 (let [btn (aget ^"[Ljavax.swing.JButton;"
                                 (aget ^"[[Ljavax.swing.JButton;" (:slot-buttons @session-state) ti) si)]
                   (when btn
                     (.setText btn "")
                     (.setBackground btn (t/color :panel-bg-light))
                     (.setForeground btn (t/color :text-normal))))))

            nil))))

    (swap! session-state assoc :instance panel)
    {:panel panel}))
