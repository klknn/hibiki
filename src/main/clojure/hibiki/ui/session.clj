(ns hibiki.ui.session
  "Session View — clip grid with 4 tracks × 5 slots.
   Ported from SessionView.java + SessionViewIpc.java."
  (:require [hibiki.ui.theme :as t]
            [hibiki.ui.widgets :as w]
            [hibiki.ui.piano-roll :as piano-roll])
  (:import [javax.swing JPanel JButton JLabel JSlider JPopupMenu JMenuItem
                         JCheckBoxMenuItem JOptionPane JFileChooser
                         BorderFactory SwingConstants SwingUtilities BoxLayout Box]
           [java.awt BorderLayout Color Dimension Component Cursor Frame Font]
           [java.awt.event MouseAdapter MouseEvent ActionListener]
           [java.awt.datatransfer DataFlavor]
           [java.awt.dnd DropTarget DropTargetAdapter DropTargetDropEvent
                         DropTargetDragEvent DropTargetEvent DnDConstants]
           [java.io File]
           [hibiki.pb.commands Request LoadClip PlayClip StopTrack PlayScene SetClipLoop DeleteClip]
           [hibiki.pb.notifications Notification Notification$ResponseCase]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; IPC helpers (protobuf)
;; ---------------------------------------------------------------------------

(defn- send-load-clip
  [^hibiki.BackendManager backend track-idx slot-idx ^String path loop?]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setLoadClip (-> (LoadClip/newBuilder)
                          (.setTrackIndex (int track-idx))
                          (.setSlotIndex (int slot-idx))
                          (.setPath path)
                          (.setIsLoop (boolean loop?))))
        (.build))))

(defn- send-play-clip
  [^hibiki.BackendManager backend ^long track-idx ^long slot-idx]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setPlayClip (-> (PlayClip/newBuilder)
                          (.setTrackIndex (int track-idx))
                          (.setSlotIndex (int slot-idx))))
        (.build))))

(defn- send-stop-track [^hibiki.BackendManager backend ^long track-idx]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setStopTrack (-> (StopTrack/newBuilder)
                           (.setTrackIndex (int track-idx))))
        (.build))))

(defn- send-play-scene [^hibiki.BackendManager backend ^long slot-idx]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setPlayScene (-> (PlayScene/newBuilder)
                           (.setSlotIndex (int slot-idx))))
        (.build))))

(defn- send-set-clip-loop [^hibiki.BackendManager backend ^long track-idx ^long slot-idx loop?]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setSetClipLoop (-> (SetClipLoop/newBuilder)
                             (.setTrackIndex (int track-idx))
                             (.setSlotIndex (int slot-idx))
                             (.setIsLoop (boolean loop?))))
        (.build))))

(defn- send-delete-clip [^hibiki.BackendManager backend ^long track-idx ^long slot-idx]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setDeleteClip (-> (DeleteClip/newBuilder)
                            (.setTrackIndex (int track-idx))
                            (.setSlotIndex (int slot-idx))))
        (.build))))

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
           (let [path (aget ^"[Ljava.lang.String;" (aget ^"[[Ljava.lang.String;" (:slot-paths @session-state) track-idx) slot-idx)]
            (if (and path (.endsWith ^String path ".mid"))
              (let [owner ^java.awt.Frame (SwingUtilities/getWindowAncestor panel)]
                (piano-roll/open-piano-roll owner (java.io.File. ^String path)
                                           track-idx slot-idx))
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
        (when-let [strip (aget ^"[Ljavax.swing.JPanel;" (:track-strips @session-state) i)]
          (if (= i track-idx)
            (do (.setBackground ^JPanel strip (.darker ^java.awt.Color (.darker ^java.awt.Color (t/color :accent-blue))))
                (when-let [hdr (aget ^"[Ljavax.swing.JLabel;" (:track-headers @session-state) i)]
                  (.setBackground ^JLabel hdr (.darker ^java.awt.Color (t/color :accent-blue)))))
            (do (.setBackground ^JPanel strip (t/color :panel-bg))
                (when-let [hdr (aget ^"[Ljavax.swing.JLabel;" (:track-headers @session-state) i)]
                  (.setBackground ^JLabel hdr (t/color :track-header))))))))))

(defn- create-track-strip [^hibiki.BackendManager backend ^JPanel session-panel ^long track-idx ^String name]
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
                 (.setBorder (BorderFactory/createMatteBorder (int 0) (int 0) (int 1) (int 0) ^java.awt.Color (t/color :border)))
                 (.setCursor (Cursor/getPredefinedCursor Cursor/HAND_CURSOR)))]
    (.setLayout strip (BoxLayout. strip BoxLayout/Y_AXIS))
    (.setBackground strip (t/color :panel-bg))
    (.setPreferredSize strip (Dimension. (t/scale 110) (t/scale 400)))
    (.setMaximumSize strip (Dimension. (t/scale 110) 32767))
    (.setBorder strip (BorderFactory/createMatteBorder (int 0) (int 0) (int 0) (int 1) ^java.awt.Color (t/color :border)))

    (aset ^"[Ljavax.swing.JPanel;" (:track-strips @session-state) track-idx strip)
    (aset ^"[Ljavax.swing.JLabel;" (:track-headers @session-state) track-idx header)

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
        (aset ^"[Ljavax.swing.JButton;"
              (aget ^"[[Ljavax.swing.JButton;" (:slot-buttons @session-state) track-idx)
              i btn)
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
      (aset ^"[Ljava.lang.Object;" (:track-meters @session-state) track-idx {:set-levels! set-levels!})
      (.add controls (Box/createHorizontalStrut (t/scale 10)))
      (.add controls ^JPanel meter-panel)
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

(defn- create-master-strip [^hibiki.BackendManager backend]
  (let [strip (let [p (JPanel.)]
                (.setLayout p (BoxLayout. p BoxLayout/Y_AXIS))
                (.setBackground p (t/color :panel-bg))
                (.setPreferredSize p (Dimension. (t/scale 110) (t/scale 400)))
                (.setMaximumSize p (Dimension. (t/scale 110) 32767))
                (.setBorder p (BorderFactory/createMatteBorder (int 0) (int 0) (int 0) (int 1) ^java.awt.Color (t/color :border)))
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
                 (.setBorder (BorderFactory/createMatteBorder (int 0) (int 0) (int 1) (int 0) ^java.awt.Color (t/color :border))))]
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
  [^hibiki.BackendManager backend]
  (let [panel (doto (JPanel. (BorderLayout.))
                (.setBackground (t/color :bg-dark)))
        master (create-master-strip backend)
        track-panel (JPanel.)]
    (.setLayout track-panel (BoxLayout. track-panel BoxLayout/X_AXIS))
    (.setBackground track-panel (t/color :bg-dark))

    (dotimes [i 4]
      (.add track-panel ^JPanel (create-track-strip backend panel i (str "Track " i))))

    (let [scroll (doto (javax.swing.JScrollPane. track-panel)
                   (.setHorizontalScrollBarPolicy javax.swing.JScrollPane/HORIZONTAL_SCROLLBAR_AS_NEEDED)
                   (.setVerticalScrollBarPolicy javax.swing.JScrollPane/VERTICAL_SCROLLBAR_NEVER)
                   (.setBorder nil)
                   (.setBackground (t/color :bg-dark)))]
      (.setUnitIncrement (.getHorizontalScrollBar scroll) (t/scale 16))
      (.add panel scroll BorderLayout/CENTER)
      (.add panel ^JPanel master BorderLayout/EAST))

    ;; Notification handler
    (.addNotificationListener backend
      (reify java.util.function.Consumer
        (accept [_ notif]
          (let [^Notification notification notif]
          (case (.getResponseCase notification)
            Notification$ResponseCase/CLIP_INFO
            (let [info (.getClipInfo notification)]
              (let [ti (.getTrackIndex info) si (.getSlotIndex info) nm (.getName info)]
                (SwingUtilities/invokeLater
                  #(when (and (>= ti 0) (< ti 4) (>= si 0) (< si 5))
                     (let [^JButton btn (aget ^"[Ljavax.swing.JButton;"
                                              (aget ^"[[Ljavax.swing.JButton;" (:slot-buttons @session-state) ti) si)]
                       (when btn
                         (if (.isEmpty ^String nm)
                           (do (.setText btn "")
                               (.setBackground btn (t/color :panel-bg-light))
                               (.setForeground btn (t/color :text-normal)))
                           (do (.setText btn (str "<html><center>" nm "<br>▶</center></html>"))
                               (.setBackground btn (t/color :clip-playing))
                               (.setForeground btn Color/BLACK)))))))))

            Notification$ResponseCase/TRACK_LEVELS
            (let [tl (.getTrackLevels notification)]
              (SwingUtilities/invokeLater
                #(dotimes [i (.getLevelsCount tl)]
                   (let [l (.getLevels tl i)
                         ti (.getTrackIndex l)]
                     (when (and (>= ti 0) (< ti 4))
                        (when-let [meter (aget ^"[Ljava.lang.Object;" (:track-meters @session-state) ti)]
                         ((:set-levels! meter) (.getPeakL l) (.getPeakR l))))))))

            Notification$ResponseCase/CLEAR_PROJECT
            (SwingUtilities/invokeLater
              #(doseq [ti (range 1 5) si (range 5)]
                 (let [^JButton btn (aget ^"[Ljavax.swing.JButton;"
                                          (aget ^"[[Ljavax.swing.JButton;" (:slot-buttons @session-state) ti) si)]
                   (when btn
                     (.setText btn "")
                     (.setBackground btn (t/color :panel-bg-light))
                     (.setForeground btn (t/color :text-normal))))))

            nil)))))

    (swap! session-state assoc :instance panel)
    {:panel panel}))
