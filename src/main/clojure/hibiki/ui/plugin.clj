(ns hibiki.ui.plugin
  "Plugin device chain pane — shows loaded plugins and their parameters.
   Ported from PluginPane.java."
  (:require [hibiki.ui.theme :as t])
  (:import [javax.swing JPanel JLabel JSlider JButton JTextField JScrollPane
                         BorderFactory BoxLayout Box SwingConstants]
           [java.awt BorderLayout Dimension Font Color Graphics Graphics2D RenderingHints]
           [java.awt.event ActionListener FocusListener FocusEvent]
           [javax.swing.event DocumentListener ChangeListener]
           [hibiki.pb.commands Request ShowPluginGui RemovePlugin SetParamValue DeleteClip]
           [hibiki.pb.notifications Notification Notification$ResponseCase]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; State
;; ---------------------------------------------------------------------------

(defonce ^:private plugin-state
  (atom {:selected-track 0
         :track-devices  {}  ;; {track-idx {plugin-idx DevicePanel-map}}
         :instance nil}))

;; ---------------------------------------------------------------------------
;; IPC (protobuf)
;; ---------------------------------------------------------------------------

(defn- send-show-gui
  [^hibiki.BackendManager backend ^long track-idx ^long plugin-idx]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setShowPluginGui (-> (ShowPluginGui/newBuilder)
                               (.setTrackIndex (int track-idx))
                               (.setPluginIndex (int plugin-idx))))
        (.build))))

(defn- send-remove-plugin
  [^hibiki.BackendManager backend ^long track-idx ^long plugin-idx]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setRemovePlugin (-> (RemovePlugin/newBuilder)
                              (.setTrackIndex (int track-idx))
                              (.setPluginIndex (int plugin-idx))))
        (.build))))

(defn- send-param-change
  [^hibiki.BackendManager backend track-idx plugin-idx param-id value]
  (.sendRequest backend
    (-> (Request/newBuilder)
        (.setSetParamValue (-> (SetParamValue/newBuilder)
                               (.setTrackIndex (int track-idx))
                               (.setPluginIndex (int plugin-idx))
                               (.setParamId (int param-id))
                               (.setValue (float value))))
        (.build))))

;; ---------------------------------------------------------------------------
;; Device panel
;; ---------------------------------------------------------------------------

(defn- make-param-slider [^hibiki.BackendManager backend ^long track-idx ^long plugin-idx param-info]
  (let [pname (.getName param-info)
        slider (doto (JSlider. 0 1000 (int (* (.getDefaultValue param-info) 1000)))
                 (.setBackground (t/color :panel-bg)))
        lbl (doto (JLabel. ^String pname)
              (.setForeground (t/color :text-normal))
              (.setFont (t/font :font-ui)))
        panel (doto (JPanel. (BorderLayout.))
                (.setOpaque false)
                (.setMaximumSize (Dimension. 32767 (t/scale 25)))
                (.add lbl BorderLayout/WEST)
                (.add slider BorderLayout/CENTER))]
    (.addChangeListener slider
      (reify ChangeListener
        (stateChanged [_ _]
          (when-not (.getValueIsAdjusting slider)
            (send-param-change backend track-idx plugin-idx
                               (.getId param-info) (/ (.getValue slider) 1000.0))))))
    {:panel panel :name pname}))

(defn- make-device-panel [backend track-idx plugin-idx plugin-name is-instrument?]
  (let [header (doto (JLabel. (str (if is-instrument? "🎹 " "🔌 ") plugin-name)
                              SwingConstants/LEFT)
                 (.setForeground (t/color :text-bright))
                 (.setFont (t/font :font-ui-bold))
                 (.setBorder (BorderFactory/createEmptyBorder 4 8 4 8)))
        param-list (let [p (JPanel.)]
                     (.setLayout p (BoxLayout. p BoxLayout/Y_AXIS))
                     (.setBackground p (t/color :bg-dark))
                     p)
        scroll (doto (JScrollPane. param-list)
                 (.setBorder nil))
        _      (.setUnitIncrement (.getVerticalScrollBar scroll) 10)
        show-btn (doto (JButton. "GUI")
                   (.setFont (t/font :font-ui))
                   (.setFocusPainted false)
                   (.addActionListener
                     (reify ActionListener
                       (actionPerformed [_ _] (send-show-gui backend track-idx plugin-idx)))))
        remove-btn (doto (JButton. "✕")
                     (.setFont (t/font :font-ui))
                     (.setFocusPainted false)
                     (.addActionListener
                       (reify ActionListener
                         (actionPerformed [_ _] (send-remove-plugin backend track-idx plugin-idx)))))
        top-bar (doto (JPanel. (BorderLayout.))
                  (.setOpaque false)
                  (.add header BorderLayout/CENTER)
                  (.add (doto (JPanel.)
                          (.setOpaque false)
                          (.add show-btn)
                          (.add remove-btn)) BorderLayout/EAST))
        body (doto (JPanel. (BorderLayout.))
               (.setBackground (t/color :bg-dark))
               (.add (doto (JScrollPane. param-list) (.setBorder nil)) BorderLayout/CENTER))
        panel (doto (JPanel. (BorderLayout.))
                (.setBackground (t/color :bg-medium))
                (.setBorder (BorderFactory/createLineBorder (t/color :border)))
                (.setMaximumSize (Dimension. (t/scale 250) 32767))
                (.setPreferredSize (Dimension. (t/scale 250) (t/scale 300)))
                (.add top-bar BorderLayout/NORTH)
                (.add body BorderLayout/CENTER))]
    {:panel panel
     :param-list param-list
     :set-params! (fn [param-list-data]
                    (.removeAll param-list)
                    (dotimes [i (.getParamsCount param-list-data)]
                      (let [info (.getParams param-list-data i)
                            ps (make-param-slider backend track-idx plugin-idx info)]
                        (.add param-list ^JPanel (:panel ps))))
                    (.revalidate param-list)
                    (.repaint param-list))}))

;; ---------------------------------------------------------------------------
;; Waveform panel
;; ---------------------------------------------------------------------------

(defn- make-waveform-panel
  "Creates a waveform display panel (ported from WaveformPanel.java)."
  [^hibiki.BackendManager backend]
  (let [wf-state (atom {:waveform nil :track-idx -1 :slot-idx -1})
        wf-panel (proxy [JPanel] []
                   (paintComponent [^java.awt.Graphics g]
                     (proxy-super paintComponent g)
                     (when-let [wf (:waveform @wf-state)]
                       (when (pos? (alength ^floats wf))
                         (let [^java.awt.Graphics2D g2 (cast java.awt.Graphics2D g)
                               w (.getWidth ^JPanel this)
                               h (.getHeight ^JPanel this)
                               center-y (/ h 2)
                               n (alength ^floats wf)]
                           (.setRenderingHint g2 java.awt.RenderingHints/KEY_ANTIALIASING
                                              java.awt.RenderingHints/VALUE_ANTIALIAS_ON)
                           (.setColor g2 (t/color :accent-blue))
                           (dotimes [i (dec n)]
                             (let [x1 (int (/ (* i w) n))
                                   x2 (int (/ (* (inc i) w) n))
                                   y1 (int (* (aget ^floats wf i) (/ h 3)))
                                   y2 (int (* (aget ^floats wf (inc i)) (/ h 3)))]
                               (.drawLine g2 x1 (- center-y y1) x2 (- center-y y2))
                               (.drawLine g2 x1 (+ center-y y1) x2 (+ center-y y2)))))))))
        delete-btn (doto (javax.swing.JButton. "Delete Clip")
                     (.setFont (t/font :font-ui))
                     (.setVisible false))
        top-p (doto (JPanel. (java.awt.FlowLayout. java.awt.FlowLayout/RIGHT))
                (.setOpaque false)
                (.add delete-btn))]
    ;; Set up delete action listener
    (.addActionListener delete-btn
      (reify ActionListener
        (actionPerformed [_ _]
          (let [{:keys [track-idx slot-idx]} @wf-state]
            (when (>= track-idx 0)
              (.sendRequest backend
                (-> (Request/newBuilder)
                    (.setDeleteClip (-> (DeleteClip/newBuilder)
                                       (.setTrackIndex (int track-idx))
                                       (.setSlotIndex (int slot-idx))))
                    (.build)))
              (reset! wf-state {:waveform nil :track-idx -1 :slot-idx -1})
              (.setVisible delete-btn false)
              (.setVisible ^JPanel wf-panel false)
              (.revalidate ^JPanel wf-panel)
              (.repaint ^JPanel wf-panel))))))
    (.setLayout wf-panel (BorderLayout.))
    (.setBackground wf-panel (t/color :bg-darker))
    (.setPreferredSize wf-panel (Dimension. (t/scale 300) (t/scale 150)))
    (.setBorder wf-panel (BorderFactory/createLineBorder (t/color :border)))
    (.add wf-panel top-p BorderLayout/NORTH)
    (.setVisible wf-panel false)
    {:panel wf-panel
     :set-waveform! (fn [ti si ^floats wf]
                      (reset! wf-state {:waveform wf :track-idx ti :slot-idx si})
                      (let [has-data (and wf (pos? (alength wf)))]
                        (.setVisible delete-btn has-data)
                        (.setVisible ^JPanel wf-panel has-data))
                      (.revalidate ^JPanel wf-panel)
                      (.repaint ^JPanel wf-panel))}))

;; ---------------------------------------------------------------------------
;; Plugin pane (device chain)
;; ---------------------------------------------------------------------------

(defn make-plugin-pane
  "Creates the plugin parameter pane. Returns {:panel JPanel :set-selected-track! fn}."
  [^hibiki.BackendManager backend]
  (let [chain-content (let [p (JPanel.)]
                        (.setLayout p (BoxLayout. p BoxLayout/X_AXIS))
                        (.setBackground p (t/color :bg-dark))
                        p)
        scroll (doto (JScrollPane. chain-content)
                 (.setHorizontalScrollBarPolicy javax.swing.JScrollPane/HORIZONTAL_SCROLLBAR_AS_NEEDED)
                 (.setVerticalScrollBarPolicy javax.swing.JScrollPane/VERTICAL_SCROLLBAR_NEVER)
                 (.setBorder nil)
                 (.setBackground (t/color :bg-dark)))
        waveform (make-waveform-panel backend)
        panel (doto (JPanel. (BorderLayout.))
                (.setBackground (t/color :bg-dark))
                (.setPreferredSize (Dimension. 0 (t/scale 200)))
                (.add scroll BorderLayout/CENTER))]

    ;; Notification handler for param updates and waveform
    (.addNotificationListener backend
      (reify java.util.function.Consumer
        (accept [_ notif]
          (let [^Notification notification notif]
          (case (.getResponseCase notification)
            Notification$ResponseCase/PARAM_LIST
            (let [pl (.getParamList notification)
                  ti (.getTrackIndex pl)
                  pi (.getPluginIndex pl)
                  pname (.getPluginName pl)
                  is-inst (.getIsInstrument pl)]
              (javax.swing.SwingUtilities/invokeLater
                #(let [devices (get-in @plugin-state [:track-devices ti] {})
                       existing (get devices pi)]
                   (if existing
                     ((:set-params! existing) pl)
                     (let [dp (make-device-panel backend ti pi pname is-inst)]
                       ((:set-params! dp) pl)
                       (swap! plugin-state assoc-in [:track-devices ti pi] dp)
                       (when (= ti (:selected-track @plugin-state))
                         (.add chain-content ^JPanel (:panel dp))
                         (.revalidate chain-content)
                         (.repaint chain-content)))))))

            Notification$ResponseCase/CLIP_WAVEFORM
            (let [cw (.getClipWaveform notification)
                  ti (.getTrackIndex cw)
                  si (.getSlotIndex cw)
                  len (.getWaveformCount cw)
                  wf-data (float-array len)]
              (dotimes [i len]
                (aset wf-data i (.getWaveform cw i)))
              (javax.swing.SwingUtilities/invokeLater
                #(do ((:set-waveform! waveform) ti si wf-data)
                     (when (= ti (:selected-track @plugin-state))
                       ;; Rebuild chain with waveform
                       (.removeAll chain-content)
                       (when (.isVisible ^JPanel (:panel waveform))
                         (.add chain-content ^JPanel (:panel waveform)))
                       (doseq [[_ dp] (sort-by key (get-in @plugin-state [:track-devices ti] {}))]
                         (.add chain-content ^JPanel (:panel dp)))
                       (.revalidate chain-content)
                       (.repaint chain-content)))))

            nil)))))

    (swap! plugin-state assoc :instance panel)
    {:panel panel
     :set-selected-track! (fn [idx]
                            (swap! plugin-state assoc :selected-track idx)
                            (.removeAll chain-content)
                            (when (.isVisible ^JPanel (:panel waveform))
                              (.add chain-content ^JPanel (:panel waveform)))
                            (doseq [[_ dp] (sort-by key (get-in @plugin-state [:track-devices idx] {}))]
                              (.add chain-content ^JPanel (:panel dp)))
                            (.revalidate chain-content)
                            (.repaint chain-content))}))
