(ns hibiki.ui.widgets
  "Reusable UI widgets: TopBar, LevelMeter, ZoomControlPanel.
   Ported from TopBar.java, LevelMeter.java, ZoomControlPanel.java."
  (:require [hibiki.ui.theme :as t])
  (:import [javax.swing JPanel JButton JLabel JSlider JToggleButton JComboBox
                         BorderFactory SwingConstants BoxLayout Box JFileChooser]
           [java.awt Color Dimension BorderLayout FlowLayout Font Graphics Graphics2D
                     RenderingHints BasicStroke]
           [java.awt.event ActionListener]
           [javax.swing.event ChangeListener]))

;; ---------------------------------------------------------------------------
;; LevelMeter — stereo VU meter
;; ---------------------------------------------------------------------------

(defn make-level-meter
  "Creates a stereo level meter JPanel. Returns {:panel JPanel :set-levels! fn}."
  []
  (let [levels (atom [0.0 0.0])
        panel  (proxy [JPanel] []
                 (paintComponent [^Graphics g]
                   (proxy-super paintComponent g)
                   (let [^Graphics2D g2 (cast Graphics2D g)
                         w (.getWidth this)
                         h (.getHeight this)
                         [l r] @levels
                         bar-w (/ (- w 2) 2)
                         lh (int (* h (min 1.0 (max 0.0 l))))
                         rh (int (* h (min 1.0 (max 0.0 r))))]
                     (.setColor g2 (t/color :bg-darker))
                     (.fillRect g2 0 0 w h)
                     (.setColor g2 (t/color :accent-green))
                     (.fillRect g2 0 (- h lh) bar-w lh)
                     (.fillRect g2 (+ bar-w 2) (- h rh) bar-w rh))))]
    (.setPreferredSize panel (Dimension. (t/scale 12) (t/scale 100)))
    (.setOpaque panel false)
    {:panel panel
     :set-levels! (fn [l r]
                    (reset! levels [l r])
                    (.repaint panel))}))

;; ---------------------------------------------------------------------------
;; ZoomControlPanel — horizontal zoom slider
;; ---------------------------------------------------------------------------

(defn make-zoom-panel
  "Creates a zoom slider panel. Returns {:panel JPanel :on-zoom fn-setter}."
  [on-zoom-fn]
  (let [panel  (JPanel.)
        slider (JSlider. 1 200 100)]
    (.setLayout panel (BorderLayout.))
    (.setOpaque panel false)
    (.setPreferredSize slider (Dimension. (t/scale 100) (t/scale 20)))
    (.setBackground slider (t/color :panel-bg))
    (.addChangeListener slider
      (reify ChangeListener
        (stateChanged [_ _e]
          (on-zoom-fn (/ (.getValue slider) 100.0)))))
    (let [lbl (JLabel. "Zoom" SwingConstants/CENTER)]
      (.setForeground lbl (t/color :text-dim))
      (.setFont lbl (Font. "SansSerif" Font/PLAIN (t/scale 9)))
      (.add panel lbl BorderLayout/WEST))
    (.add panel slider BorderLayout/CENTER)
    {:panel panel}))

;; ---------------------------------------------------------------------------
;; TopBar — transport controls, BPM, view toggle
;; ---------------------------------------------------------------------------

(defn make-top-bar
  "Creates the top toolbar. Returns {:panel JPanel :set-view-toggle fn}.
   backend: hibiki.BackendManager instance."
  [backend]
  (let [panel    (JPanel. (BorderLayout.))
        left     (JPanel. (FlowLayout. FlowLayout/LEFT 5 2))
        center   (JPanel. (FlowLayout. FlowLayout/CENTER 5 2))
        right    (JPanel. (FlowLayout. FlowLayout/RIGHT 5 2))
        th       t/theme
        view-toggle-atom (atom nil)

        make-btn (fn [text action]
                   (doto (JButton. ^String text)
                     (.setFont (t/font :font-ui))
                     (.setFocusPainted false)
                     (.addActionListener
                       (reify ActionListener
                         (actionPerformed [_ _e] (action))))))

        play-btn (make-btn "▶" #(.startPlayback backend))
        stop-btn (make-btn "■" #(.stopPlayback backend))
        bpm-lbl  (doto (JLabel. "120 BPM")
                   (.setForeground (t/color :text-bright))
                   (.setFont (t/font :font-display)))

        ;; Session / Timeline toggle
        session-btn  (doto (JToggleButton. "Session")
                       (.setSelected true)
                       (.setFont (t/font :font-ui-bold))
                       (.setFocusPainted false))
        timeline-btn (doto (JToggleButton. "Timeline")
                       (.setFont (t/font :font-ui-bold))
                       (.setFocusPainted false))

        save-btn (make-btn "Save" #(let [chooser (JFileChooser. "testdata")]
                                     (when (= (.showSaveDialog chooser panel)
                                              JFileChooser/APPROVE_OPTION)
                                       ;; TODO: project save via backend
                                       )))
        load-btn (make-btn "Load" #(let [chooser (JFileChooser. "testdata")]
                                     (when (= (.showOpenDialog chooser panel)
                                              JFileChooser/APPROVE_OPTION)
                                       ;; TODO: project load via backend
                                       )))]

    ;; Toggle logic
    (.addActionListener session-btn
      (reify ActionListener
        (actionPerformed [_ _]
          (.setSelected timeline-btn false)
          (.setSelected session-btn true)
          (when-let [f @view-toggle-atom] (f false)))))
    (.addActionListener timeline-btn
      (reify ActionListener
        (actionPerformed [_ _]
          (.setSelected session-btn false)
          (.setSelected timeline-btn true)
          (when-let [f @view-toggle-atom] (f true)))))

    (.setBackground panel (t/color :bg-darker))
    (.setPreferredSize panel (Dimension. 0 (t/scale 35)))
    (doseq [p [left center right]]
      (.setOpaque p false))

    (.add left play-btn)
    (.add left stop-btn)
    (.add left (Box/createHorizontalStrut (t/scale 10)))
    (.add left bpm-lbl)

    (.add center session-btn)
    (.add center timeline-btn)

    (.add right save-btn)
    (.add right load-btn)

    (.add panel left BorderLayout/WEST)
    (.add panel center BorderLayout/CENTER)
    (.add panel right BorderLayout/EAST)

    {:panel panel
     :bpm-label bpm-lbl
     :set-view-toggle! (fn [f] (reset! view-toggle-atom f))}))
