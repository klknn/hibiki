(ns hibiki.echo
  "Hybrid Clojure frontend — reuses Java UI components with Clojure as glue.
   Provides REPL access to running components via defonce atoms."
  (:require [hibiki.echo.repl-panel :as repl-panel])
  (:import [com.formdev.flatlaf FlatDarkLaf]
           [hibiki BackendManager]
           [hibiki.ui MainView Theme]
           [javax.swing JButton JComponent JFrame JPanel JSplitPane KeyStroke
            SwingUtilities UIManager]
           [javax.imageio ImageIO]
           [java.awt BorderLayout FlowLayout Font Taskbar Taskbar$Feature]
           [java.awt.event ActionEvent KeyEvent]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; State — accessible from REPL
;; ---------------------------------------------------------------------------

(defonce ^:private state
  (atom {:frame      nil
         :backend    nil
         :repl-panel nil
         :repl-visible? false}))

(defn frame   ^JFrame          [] (:frame   @state))
(defn backend ^BackendManager  [] (:backend @state))

;; ---------------------------------------------------------------------------
;; HiDPI helpers (ported from GuiMain.java)
;; ---------------------------------------------------------------------------

(defn- detect-gnome-scale []
  (try
    (let [p (.start (ProcessBuilder. ["gsettings" "get"
                                      "org.gnome.desktop.interface" "scaling-factor"]))
          rdr (java.io.BufferedReader. (java.io.InputStreamReader. (.getInputStream p)))
          line (.readLine rdr)]
      (.waitFor p)
      (when (and line (.startsWith line "uint32 "))
        (let [v (Integer/parseInt (.trim (.substring line 7)))]
          (when (pos? v) (str v)))))
    (catch Exception _ nil)))

(defn- setup-hidpi! []
  (let [os (.toLowerCase (System/getProperty "os.name"))]
    (when (.contains os "linux")
      (System/setProperty "sun.java2d.uiScale.enabled" "true")
      (let [gdk (System/getenv "GDK_SCALE")]
        (if (and gdk (not (.isEmpty gdk)))
          (System/setProperty "sun.java2d.uiScale" gdk)
          (when-let [s (detect-gnome-scale)]
            (System/setProperty "sun.java2d.uiScale" s)))))
    (when (.contains os "mac")
      (System/setProperty "apple.laf.useScreenMenuBar" "true")
      (System/setProperty "apple.awt.application.name" "Hibiki")
      (System/setProperty "apple.awt.application.appearance" "system"))))

;; ---------------------------------------------------------------------------
;; REPL panel toggle
;; ---------------------------------------------------------------------------

(defn toggle-repl!
  "Show or hide the embedded REPL panel."
  []
  (when-let [^JFrame f (frame)]
    (SwingUtilities/invokeLater
     (fn []
       (let [{:keys [wrapper split-pane repl-panel repl-visible?]} @state
             cp (.getContentPane f)]
         (if repl-visible?
           ;; Hide REPL — show wrapper only
           (do (.removeAll cp)
               (.add cp ^JPanel wrapper BorderLayout/CENTER)
               (.revalidate cp)
               (.repaint cp)
               (swap! state assoc :repl-visible? false))
           ;; Show REPL — split wrapper + REPL panel
           (do (.removeAll cp)
               (let [^JSplitPane sp split-pane]
                 (.setLeftComponent sp ^JPanel wrapper)
                 (.setRightComponent sp ^JPanel (:panel repl-panel))
                 (.setDividerLocation sp (- (.getWidth f) 400))
                 (.add cp sp BorderLayout/CENTER))
               (.revalidate cp)
               (.repaint cp)
               ((:focus! repl-panel))
               (swap! state assoc :repl-visible? true))))))))

;; ---------------------------------------------------------------------------
;; Entry point
;; ---------------------------------------------------------------------------

(defn -main
  "Launches the Hibiki DAW GUI using Java components with Clojure as glue."
  [& _args]
  (setup-hidpi!)
  (try (UIManager/setLookAndFeel (FlatDarkLaf.))
       (catch Exception _ (System/err println "Failed to initialize LaF")))

  (let [bm (BackendManager/getInstance)]
    (.start bm)
    (swap! state assoc :backend bm)

    (SwingUtilities/invokeLater
     (fn []
       (let [frame (doto (JFrame. "Hibiki DAW")
                     (.setDefaultCloseOperation JFrame/EXIT_ON_CLOSE)
                     (.setSize 1200 800))]
         ;; App icon
         (try
           (when-let [url (.getResource (class bm) "/hibiki/icon.png")]
             (let [img (ImageIO/read url)]
               (.setIconImage frame img)
               (try
                 (when (Taskbar/isTaskbarSupported)
                   (let [tb (Taskbar/getTaskbar)]
                     (when (.isSupported tb Taskbar$Feature/ICON_IMAGE)
                       (.setIconImage tb img))))
                 (catch Exception _))))
           (catch Exception _))

         ;; Create components
         (let [main-view  (MainView.)
               repl       (repl-panel/make-repl-panel)
               split-pane (doto (JSplitPane. JSplitPane/HORIZONTAL_SPLIT)
                            (.setBorder nil)
                            (.setDividerSize 3)
                            (.setContinuousLayout true))

               ;; REPL toggle button
               repl-btn   (doto (JButton. "λ REPL")
                            (.setFont (Font. "SansSerif" Font/BOLD 11))
                            (.setFocusable false)
                            (.setToolTipText "Toggle REPL panel (Ctrl+R)"))]

           ;; Button action
           (.addActionListener repl-btn
             (reify java.awt.event.ActionListener
               (actionPerformed [_ _]
                 (toggle-repl!)
                 (.setText repl-btn
                           (if (:repl-visible? @state) "λ REPL ✕" "λ REPL")))))

           ;; Layout: toolbar bar at top-right, MainView below
           (let [toolbar (doto (JPanel. (FlowLayout. FlowLayout/RIGHT 4 2))
                           (.setOpaque false)
                           (.add repl-btn))
                 wrapper (doto (JPanel. (BorderLayout.))
                           (.add toolbar BorderLayout/NORTH)
                           (.add ^JComponent main-view BorderLayout/CENTER))]
             (.add frame wrapper)

             ;; Store components for toggle
             (swap! state assoc
                    :frame frame
                    :main-view main-view
                    :wrapper wrapper
                    :repl-panel repl
                    :split-pane split-pane
                    :repl-btn repl-btn
                    :repl-visible? false))

           ;; Ctrl+R to toggle REPL panel
           (let [im (.getInputMap (.getRootPane frame) JComponent/WHEN_IN_FOCUSED_WINDOW)
                 am (.getActionMap (.getRootPane frame))]
             (.put im (KeyStroke/getKeyStroke KeyEvent/VK_R KeyEvent/CTRL_DOWN_MASK) "toggleRepl")
             (.put im (KeyStroke/getKeyStroke KeyEvent/VK_R KeyEvent/META_DOWN_MASK) "toggleRepl")
             (.put am "toggleRepl"
                   (proxy [javax.swing.AbstractAction] []
                     (actionPerformed [^ActionEvent _]
                       (toggle-repl!)
                       (.setText repl-btn
                                 (if (:repl-visible? @state) "λ REPL ✕" "λ REPL")))))))

         (.setLocationRelativeTo frame nil)
         (.setVisible frame true))))))
