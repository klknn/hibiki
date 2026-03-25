(ns hibiki.echo
  "Hybrid Clojure frontend — reuses Java UI components with Clojure as glue.
   Provides REPL access to running components via defonce atoms."
  (:import [com.formdev.flatlaf FlatDarkLaf]
           [hibiki BackendManager]
           [hibiki.ui MainView Theme]
           [javax.swing JFrame SwingUtilities UIManager]
           [javax.imageio ImageIO]
           [java.awt Taskbar Taskbar$Feature]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; State — accessible from REPL
;; ---------------------------------------------------------------------------

(defonce ^:private state
  (atom {:frame   nil
         :backend nil}))

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

         ;; Compose Java MainView directly
         (.add frame (MainView.))

         (.setLocationRelativeTo frame nil)
         (.setVisible frame true)
         (swap! state assoc :frame frame))))))
