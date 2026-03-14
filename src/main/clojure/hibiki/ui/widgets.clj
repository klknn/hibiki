(ns hibiki.ui.widgets
  "Reusable UI widgets: TopBar, LevelMeter, ZoomControlPanel.
   Ported from TopBar.java, LevelMeter.java, ZoomControlPanel.java."
  (:require [hibiki.ui.theme :as t])
  (:import [javax.swing JPanel JButton JLabel JSlider JToggleButton JComboBox
                         BorderFactory SwingConstants SwingUtilities BoxLayout Box JFileChooser]
           [java.awt Color Dimension BorderLayout FlowLayout Font Graphics Graphics2D
                     RenderingHints BasicStroke]
           [java.awt.event ActionListener]
           [javax.swing.event ChangeListener]))

(set! *warn-on-reflection* true)

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
                         w (.getWidth ^JPanel this)
                         h (.getHeight ^JPanel this)
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

(declare ^:private show-settings-dialog)

(defn make-top-bar
  "Creates the top toolbar. Returns {:panel JPanel :set-view-toggle fn}.
   backend: hibiki.BackendManager instance."
  [^hibiki.BackendManager backend]
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
    (doseq [^JPanel p [left center right]]
      (.setOpaque p false))

    (.add left ^JButton play-btn)
    (.add left ^JButton stop-btn)
    (.add left (Box/createHorizontalStrut (t/scale 10)))
    (.add left ^JLabel bpm-lbl)

    (.add center ^JToggleButton session-btn)
    (.add center ^JToggleButton timeline-btn)

    (.add right ^JButton save-btn)
    (.add right ^JButton load-btn)
    (.add right (Box/createHorizontalStrut (t/scale 5)))
    (.add right ^JButton (make-btn "⚙" #(show-settings-dialog panel)))

    (.add panel left BorderLayout/WEST)
    (.add panel center BorderLayout/CENTER)
    (.add panel right BorderLayout/EAST)

    {:panel panel
     :bpm-label bpm-lbl
     :set-view-toggle! (fn [f] (reset! view-toggle-atom f))}))

;; ---------------------------------------------------------------------------
;; Settings dialog
;; ---------------------------------------------------------------------------

(defn- show-settings-dialog
  "Opens the Settings dialog (ported from SettingsDialog.java)."
  [^java.awt.Component parent]
  (let [owner (SwingUtilities/getWindowAncestor parent)
        dialog (doto (javax.swing.JDialog. ^java.awt.Frame owner "Settings" true)
                 (.setSize (t/scale 400) (t/scale 300))
                 (.setLocationRelativeTo owner))
        ;; Audio tab
        audio-panel (doto (JPanel. (java.awt.BorderLayout.))
                      (.setBorder (BorderFactory/createEmptyBorder 20 20 20 20))
                      (.add (doto (JLabel. "Audio Engine: ALSA (alsa_playback.hbk-play)")
                              (.setFont (t/font :font-ui))) java.awt.BorderLayout/NORTH))
        ;; Appearance tab
        appearance (JPanel. (java.awt.GridBagLayout.))
        gbc (doto (java.awt.GridBagConstraints.)
              (-> .-fill (set! java.awt.GridBagConstraints/HORIZONTAL))
              (-> .-insets (set! (java.awt.Insets. 5 5 5 5))))
        theme-combo (JComboBox. ^"[Ljava.lang.Object;"
                      (into-array Object (map name (keys t/presets))))
        scale-combo (JComboBox. ^"[Ljava.lang.Object;"
                      (into-array Object ["50%" "75%" "100%" "125%" "150%" "175%" "200%"]))
        font-spinner (javax.swing.JSpinner. (javax.swing.SpinnerNumberModel.
                                              (int 12) (int 8) (int 24) (int 1)))
        font-combo (JComboBox. ^"[Ljava.lang.Object;"
                     (into-array Object (.getAvailableFontFamilyNames
                                          (java.awt.GraphicsEnvironment/getLocalGraphicsEnvironment))))
        laf-names (into-array Object
                    (concat ["FlatDarkLaf"]
                            (map #(.getName ^javax.swing.UIManager$LookAndFeelInfo %)
                                 (javax.swing.UIManager/getInstalledLookAndFeels))))
        laf-combo (JComboBox. ^"[Ljava.lang.Object;" laf-names)]

    (.setBorder appearance (BorderFactory/createEmptyBorder 20 20 20 20))

    ;; Theme row
    (set! (. gbc gridx) 0) (set! (. gbc gridy) 0)
    (.add appearance (JLabel. "Theme:") gbc)
    (set! (. gbc gridx) 1)
    (.add appearance theme-combo gbc)

    ;; Scale row
    (set! (. gbc gridx) 0) (set! (. gbc gridy) 1)
    (.add appearance (JLabel. "UI Scaling:") gbc)
    (set! (. gbc gridx) 1)
    (.add appearance scale-combo gbc)

    ;; Font size row 
    (set! (. gbc gridx) 0) (set! (. gbc gridy) 2)
    (.add appearance (JLabel. "Font Size:") gbc)
    (set! (. gbc gridx) 1)
    (.add appearance font-spinner gbc)

    ;; Font family row
    (set! (. gbc gridx) 0) (set! (. gbc gridy) 3)
    (.add appearance (JLabel. "Font:") gbc)
    (set! (. gbc gridx) 1)
    (.add appearance font-combo gbc)

    ;; LookAndFeel row
    (set! (. gbc gridx) 0) (set! (. gbc gridy) 4)
    (.add appearance (JLabel. "Look & Feel:") gbc)
    (set! (. gbc gridx) 1)
    (.add appearance laf-combo gbc)

    ;; Apply button
    (set! (. gbc gridx) 1) (set! (. gbc gridy) 5)
    (let [apply-btn (doto (JButton. "Apply")
                      (.addActionListener
                        (reify ActionListener
                          (actionPerformed [_ _]
                            (let [preset-name (str (.getSelectedItem theme-combo))
                                  preset-key (keyword preset-name)
                                  scale-str (str (.getSelectedItem scale-combo))
                                  scaling (/ (Integer/parseInt (.replace ^String scale-str "%" "")) 100.0)
                                  font-size (.getValue font-spinner)
                                  font-family (str (.getSelectedItem font-combo))
                                  selected-laf (str (.getSelectedItem laf-combo))]
                              ;; Apply theme
                              (t/update-theme! :preset preset-key :scaling scaling
                                               :font-size (int font-size) :font-family font-family)
                              ;; Apply LookAndFeel
                              (try
                                (cond
                                  (= selected-laf "FlatDarkLaf")
                                  (javax.swing.UIManager/setLookAndFeel (com.formdev.flatlaf.FlatDarkLaf.))
                                  :else
                                  (doseq [^javax.swing.UIManager$LookAndFeelInfo info
                                          (javax.swing.UIManager/getInstalledLookAndFeels)]
                                    (when (= (.getName info) selected-laf)
                                      (javax.swing.UIManager/setLookAndFeel (.getClassName info)))))
                                (doseq [w (java.awt.Window/getWindows)]
                                  (SwingUtilities/updateComponentTreeUI w))
                                (catch Exception ex
                                  (javax.swing.JOptionPane/showMessageDialog
                                    dialog (str "Failed to apply Look & Feel: " (.getMessage ex))))))))))]
      (.add appearance apply-btn gbc))

    ;; Tabs
    (let [tabs (javax.swing.JTabbedPane.)]
      (.addTab tabs "Audio" audio-panel)
      (.addTab tabs "Appearance" appearance)
      (.add (.getContentPane dialog) tabs java.awt.BorderLayout/CENTER))

    ;; Close button
    (let [bottom (JPanel. (FlowLayout. FlowLayout/RIGHT))
          close-btn (doto (JButton. "Close")
                      (.addActionListener (reify ActionListener
                                            (actionPerformed [_ _] (.dispose dialog)))))]
      (.add bottom close-btn)
      (.add (.getContentPane dialog) bottom java.awt.BorderLayout/SOUTH))

    (.setVisible dialog true)))
