(ns hibiki.ui.core
  "Main entry point for the Clojure GUI frontend.
   Creates the JFrame and assembles all panels.
   Ported from GuiMain.java + MainView.java."
  (:require [hibiki.ui.theme :as t]
            [hibiki.ui.widgets :as w]
            [hibiki.ui.session :as session]
            [hibiki.ui.browser :as browser]
            [hibiki.ui.plugin :as plugin]
            [hibiki.ui.timeline :as timeline])
  (:import [javax.swing JFrame JPanel JSplitPane SwingUtilities UIManager
                         AbstractAction JComponent KeyStroke]
           [java.awt BorderLayout CardLayout Dimension FlowLayout Image]
           [java.awt.event KeyEvent]
           [com.formdev.flatlaf FlatDarkLaf]
           [hibiki BackendManager]
           [hibiki.pb HibikiProto HibikiProto$Request HibikiProto$Undo HibikiProto$Redo]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; HiDPI helpers (ported from GuiMain.java)
;; ---------------------------------------------------------------------------

(defn- detect-gnome-scale []
  (try
    (let [p (.start (ProcessBuilder. ["gsettings" "get"
                                      "org.gnome.desktop.interface" "scaling-factor"]))
          line (with-open [r (java.io.BufferedReader. (clojure.java.io/reader (.getInputStream p)))]
                 (.readLine r))]
      (.waitFor p)
      (when (and line (.startsWith ^String line "uint32 "))
        (let [v (Integer/parseInt (.trim ^String (.substring ^String line 7)))]
          (when (pos? v) (str v)))))
    (catch Exception _ nil)))

(defn- setup-hidpi []
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
;; MainView — assembles all panels
;; ---------------------------------------------------------------------------

(defn- make-main-view [^hibiki.BackendManager backend]
  (let [panel           (JPanel. (BorderLayout.))
        center          (JPanel. (CardLayout.))
        {:keys [panel] :as top-bar}  (w/make-top-bar backend)
        top-panel       panel
        {:keys [panel]} (session/make-session-view backend)
        session-panel   panel
        {:keys [panel]} (timeline/make-timeline-view backend)
        timeline-panel  panel
        {:keys [panel]} (browser/make-browser-pane backend)
        browser-panel   panel
        {:keys [panel set-selected-track!]} (plugin/make-plugin-pane backend)
        plugin-panel    panel
        main-panel (JPanel. (BorderLayout.))]

    (.setBackground main-panel (t/color :bg-dark))

    (.add center ^JPanel session-panel "SESSION")
    (.add center ^JPanel timeline-panel "TIMELINE")

    ((:set-view-toggle! top-bar)
     (fn [is-timeline?]
       (let [cl ^CardLayout (.getLayout center)]
         (.show cl center (if is-timeline? "TIMELINE" "SESSION")))))

    (let [v-split (doto (JSplitPane. JSplitPane/VERTICAL_SPLIT center plugin-panel)
                    (.setDividerLocation (int (t/scale 450)))
                    (.setDividerSize (t/scale 2))
                    (.setBorder nil)
                    (.setBackground (t/color :bg-dark)))
          h-split (doto (JSplitPane. JSplitPane/HORIZONTAL_SPLIT browser-panel v-split)
                    (.setDividerLocation (int (t/scale 220)))
                    (.setDividerSize (t/scale 2))
                    (.setBorder nil)
                    (.setBackground (t/color :bg-dark)))]
      (.add main-panel ^JPanel (:panel top-bar) BorderLayout/NORTH)
      (.add main-panel h-split BorderLayout/CENTER))

    ;; Status bar
    (let [footer (doto (JPanel. (FlowLayout. FlowLayout/LEFT 10 2))
                   (.setBackground (t/color :bg-darker))
                   (.setPreferredSize (Dimension. 0 (t/scale 20))))
          lbl    (doto (javax.swing.JLabel. "Status: Ready")
                   (.setForeground (t/color :text-dim))
                   (.setFont (.deriveFont ^java.awt.Font (t/font :font-ui) (float (t/scale 9)))))]
      (.add footer lbl)
      (.add main-panel footer BorderLayout/SOUTH))

    ;; Key bindings
    (let [im (.getInputMap main-panel JComponent/WHEN_IN_FOCUSED_WINDOW)
          am (.getActionMap main-panel)]
      ;; Undo
      (.put im (KeyStroke/getKeyStroke "control Z") "undo")
      (.put im (KeyStroke/getKeyStroke "meta Z") "undo")
      (.put am "undo" (proxy [AbstractAction] []
                        (actionPerformed [_]
                          (.sendRequest backend
                            (-> (HibikiProto$Request/newBuilder)
                                (.setUndo (HibikiProto$Undo/newBuilder))
                                (.build))))))
      ;; Redo
      (.put im (KeyStroke/getKeyStroke "control shift Z") "redo")
      (.put im (KeyStroke/getKeyStroke "meta shift Z") "redo")
      (.put im (KeyStroke/getKeyStroke "control Y") "redo")
      (.put am "redo" (proxy [AbstractAction] []
                        (actionPerformed [_]
                          (.sendRequest backend
                            (-> (HibikiProto$Request/newBuilder)
                                (.setRedo (HibikiProto$Redo/newBuilder))
                                (.build))))))
      ;; Space = Play/Stop
      (.put im (KeyStroke/getKeyStroke KeyEvent/VK_SPACE 0) "playStop")
      (.put am "playStop" (proxy [AbstractAction] []
                            (actionPerformed [_] (.togglePlay backend))))
      ;; Enter = Reset
      (.put im (KeyStroke/getKeyStroke KeyEvent/VK_ENTER 0) "resetPlayhead")
      (.put am "resetPlayhead" (proxy [AbstractAction] []
                                 (actionPerformed [_] (.seek backend (float 0)))))
      ;; Tab = Toggle view
      (let [is-tl (atom false)]
        (.put im (KeyStroke/getKeyStroke KeyEvent/VK_TAB 0) "toggleView")
        (.put am "toggleView" (proxy [AbstractAction] []
                                (actionPerformed [_]
                                  (swap! is-tl not)
                                  (let [cl ^CardLayout (.getLayout center)]
                                    (.show cl center (if @is-tl "TIMELINE" "SESSION")))))))
      ;; 1-4 = Select track
      (doseq [i (range 1 5)]
        (let [key-name (str "selectTrack" i)]
          (.put im (KeyStroke/getKeyStroke (int (+ KeyEvent/VK_0 i)) (int 0)) key-name)
          (.put am key-name (proxy [AbstractAction] []
                              (actionPerformed [_]
                                (timeline/set-selected-track (dec i))
                                (session/select-track! (dec i))))))))

    (.setFocusTraversalKeysEnabled main-panel false)
    (.setFocusable main-panel true)
    main-panel))

;; ---------------------------------------------------------------------------
;; Entry point
;; ---------------------------------------------------------------------------

(defn -main
  "Application entry point — called from ClojureMain.java."
  []
  (setup-hidpi)

  (try
    (UIManager/setLookAndFeel (FlatDarkLaf.))
    (catch Exception _ (System/err "Failed to initialize LaF")))

  (let [backend (BackendManager/getInstance)]
    (.start backend)

    (SwingUtilities/invokeLater
      #(let [frame (doto (JFrame. "Hibiki DAW (Clojure)")
                     (.setDefaultCloseOperation JFrame/EXIT_ON_CLOSE)
                     (.setSize 1200 800))]
         ;; Icon
         (try
           (when-let [url (clojure.java.io/resource "hibiki/icon.png")]
             (let [img (javax.imageio.ImageIO/read url)]
               (.setIconImage frame img)))
           (catch Exception _ nil))

         (.add frame ^JPanel (make-main-view backend))
         (.setLocationRelativeTo frame nil)
         (.setVisible frame true)))))
