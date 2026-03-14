(ns hibiki.ui.plugin
  "Plugin device chain pane — shows loaded plugins and their parameters.
   Ported from PluginPane.java."
  (:require [hibiki.ui.theme :as t])
  (:import [javax.swing JPanel JLabel JSlider JButton JTextField JScrollPane
                         BorderFactory BoxLayout Box SwingConstants]
           [java.awt BorderLayout Dimension Font Color Graphics Graphics2D RenderingHints]
           [java.awt.event ActionListener FocusListener FocusEvent]
           [javax.swing.event DocumentListener ChangeListener]
           [com.google.flatbuffers FlatBufferBuilder]
           [hibiki.ipc Request Command ShowPluginGui RemovePlugin SetParamValue
                       ParamList ParamInfo Response Notification]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; State
;; ---------------------------------------------------------------------------

(defonce ^:private plugin-state
  (atom {:selected-track 0
         :track-devices  {}  ;; {track-idx {plugin-idx DevicePanel-map}}
         :instance nil}))

;; ---------------------------------------------------------------------------
;; IPC
;; ---------------------------------------------------------------------------

(defn- send-show-gui
  [^hibiki.BackendManager backend ^long track-idx ^long plugin-idx]
  (let [^FlatBufferBuilder b (FlatBufferBuilder. 64)
        cmd (do (ShowPluginGui/startShowPluginGui b)
                (ShowPluginGui/addTrackIndex b track-idx)
                (ShowPluginGui/addPluginIndex b plugin-idx)
                (ShowPluginGui/endShowPluginGui b))
        req (Request/createRequest b Command/ShowPluginGui cmd)]
    (.finish b req)
    (.sendRequest backend b)))

(defn- send-remove-plugin
  [^hibiki.BackendManager backend ^long track-idx ^long plugin-idx]
  (let [^FlatBufferBuilder b (FlatBufferBuilder. 64)
        cmd (do (RemovePlugin/startRemovePlugin b)
                (RemovePlugin/addTrackIndex b track-idx)
                (RemovePlugin/addPluginIndex b plugin-idx)
                (RemovePlugin/endRemovePlugin b))
        req (Request/createRequest b Command/RemovePlugin cmd)]
    (.finish b req)
    (.sendRequest backend b)))

(defn- send-param-change
  [^hibiki.BackendManager backend track-idx plugin-idx param-id value]
  (let [^FlatBufferBuilder b (FlatBufferBuilder. 64)
        cmd (do (SetParamValue/startSetParamValue b)
                (SetParamValue/addTrackIndex b track-idx)
                (SetParamValue/addPluginIndex b plugin-idx)
                (SetParamValue/addParamId b param-id)
                (SetParamValue/addValue b (double value))
                (SetParamValue/endSetParamValue b))
        req (Request/createRequest b Command/SetParamValue cmd)]
    (.finish b req)
    (.sendRequest backend b)))

;; ---------------------------------------------------------------------------
;; Device panel
;; ---------------------------------------------------------------------------

(defn- make-param-slider [backend track-idx plugin-idx info]
  (let [name (.name info)
        slider (doto (JSlider. 0 1000 (int (* (.defaultValue info) 1000)))
                 (.setBackground (t/color :panel-bg)))
        lbl (doto (JLabel. name)
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
                               (.id info) (/ (.getValue slider) 1000.0))))))
    {:panel panel :name name}))

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
                    (dotimes [i (.paramsLength param-list-data)]
                      (let [info (.params param-list-data i)
                            ps (make-param-slider backend track-idx plugin-idx info)]
                        (.add param-list (:panel ps))))
                    (.revalidate param-list)
                    (.repaint param-list))}))

;; ---------------------------------------------------------------------------
;; Plugin pane (device chain)
;; ---------------------------------------------------------------------------

(defn make-plugin-pane
  "Creates the plugin parameter pane. Returns {:panel JPanel :set-selected-track! fn}."
  [backend]
  (let [chain-content (let [p (JPanel.)]
                        (.setLayout p (BoxLayout. p BoxLayout/X_AXIS))
                        (.setBackground p (t/color :bg-dark))
                        p)
        scroll (doto (JScrollPane. chain-content)
                 (.setHorizontalScrollBarPolicy javax.swing.JScrollPane/HORIZONTAL_SCROLLBAR_AS_NEEDED)
                 (.setVerticalScrollBarPolicy javax.swing.JScrollPane/VERTICAL_SCROLLBAR_NEVER)
                 (.setBorder nil)
                 (.setBackground (t/color :bg-dark)))
        panel (doto (JPanel. (BorderLayout.))
                (.setBackground (t/color :bg-dark))
                (.setPreferredSize (Dimension. 0 (t/scale 200)))
                (.add scroll BorderLayout/CENTER))]

    ;; Notification handler for param updates
    (.addNotificationListener backend
      (reify java.util.function.Consumer
        (accept [_ notification]
          (when (= (.responseType notification) Response/ParamList)
            (let [pl ^ParamList (.response notification (ParamList.))
                  ti (.trackIndex pl)
                  pi (.pluginIndex pl)
                  pname (.pluginName pl)
                  is-inst (.isInstrument pl)]
              (javax.swing.SwingUtilities/invokeLater
                #(let [devices (get-in @plugin-state [:track-devices ti] {})
                       existing (get devices pi)]
                   (if existing
                     ((:set-params! existing) pl)
                     (let [dp (make-device-panel backend ti pi pname is-inst)]
                       (swap! plugin-state assoc-in [:track-devices ti pi] dp)
                       (when (= ti (:selected-track @plugin-state))
                         (.add chain-content (:panel dp))
                         (.revalidate chain-content)
                         (.repaint chain-content)))))))))))

    (swap! plugin-state assoc :instance panel)
    {:panel panel
     :set-selected-track! (fn [idx]
                            (swap! plugin-state assoc :selected-track idx)
                            (.removeAll chain-content)
                            (doseq [[_ dp] (sort-by key (get-in @plugin-state [:track-devices idx] {}))]
                              (.add chain-content (:panel dp)))
                            (.revalidate chain-content)
                            (.repaint chain-content))}))
