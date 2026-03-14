(ns hibiki.ui.browser
  "File browser panel — shows plugins, MIDI, and audio files in a tree.
   Ported from BrowserPane.java."
  (:require [hibiki.ui.theme :as t]
            [hibiki.ui.session :as session])
  (:import [javax.swing JPanel JTree JScrollPane JLabel BorderFactory SwingConstants SwingUtilities Timer]
           [javax.swing.tree DefaultMutableTreeNode DefaultTreeModel DefaultTreeCellRenderer TreePath]
           [java.awt BorderLayout Dimension]
           [java.awt.event MouseAdapter MouseEvent]
           [java.io File]
           [com.google.flatbuffers FlatBufferBuilder]
           [hibiki.ipc Request Command LoadPlugin LoadClip ListPlugins
                       PluginList PluginDescription Response Notification]
           [hibiki BackendManager]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; FileItem — wraps each tree leaf with type, path, and plugin metadata
;; ---------------------------------------------------------------------------

(defrecord FileItem [^File file ^String type ^String display-name ^String vendor plugin-index]
  Object
  (toString [_] display-name))

(defn file-item
  "Create a FileItem for a tree node."
  (^FileItem [^File file ^String type ^String display-name]
   (->FileItem file type display-name "" 0))
  (^FileItem [^File file ^String type ^String display-name ^String vendor plugin-index]
   (->FileItem file type display-name vendor plugin-index)))

;; ---------------------------------------------------------------------------
;; IPC helpers (type-annotated)
;; ---------------------------------------------------------------------------

(defn- send-load-plugin
  "Send LoadPlugin IPC command."
  [^BackendManager backend ^String path ^long plugin-index]
  (let [^FlatBufferBuilder b (FlatBufferBuilder. 1024)
        po (.createString b path)
        track-idx (int (session/get-selected-track))
        cmd (LoadPlugin/createLoadPlugin b track-idx po plugin-index)
        req (Request/createRequest b Command/LoadPlugin cmd)]
    (.finish b req)
    (.sendRequest backend b)))

(defn- send-load-clip
  "Send LoadClip IPC command."
  [^BackendManager backend ^String path loop?]
  (let [^FlatBufferBuilder b (FlatBufferBuilder. 1024)
        po (.createString b path)
        track-idx (int (session/get-selected-track))
        cmd (LoadClip/createLoadClip b track-idx (int 0) po loop?)
        req (Request/createRequest b Command/LoadClip cmd)]
    (.finish b req)
    (.sendRequest backend b)))

(defn- send-list-plugins
  "Send ListPlugins IPC command to discover plugins in a VST3 bundle."
  [^BackendManager backend ^String path]
  (let [^FlatBufferBuilder b (FlatBufferBuilder. 256)
        po (.createString b path)
        cmd (do (ListPlugins/startListPlugins b)
                (ListPlugins/addPath b po)
                (ListPlugins/endListPlugins b))
        req (Request/createRequest b Command/ListPlugins cmd)]
    (.finish b req)
    (.sendRequest backend b)))

;; ---------------------------------------------------------------------------
;; File scanning
;; ---------------------------------------------------------------------------

(def ^:private audio-exts #{".wav" ".mp3" ".ogg" ".flac" ".aiff"})
(def ^:private midi-exts  #{".mid" ".midi"})

(defn- scan-directory
  "Recursively scan a directory for plugins, MIDI, and audio files.
   plugins-node, midi-node, audio-node may be nil to skip that category."
  [^File dir ^DefaultMutableTreeNode plugins-node
   ^DefaultMutableTreeNode midi-node ^DefaultMutableTreeNode audio-node
   ^BackendManager backend]
  (when (and dir (.isDirectory dir))
    (let [files (.listFiles dir)]
      (when files
        (doseq [^File f (sort-by #(.getName ^File %) files)]
          (let [^String fname (.getName f)
                ^String lower (.toLowerCase fname)]
            (cond
              (.isDirectory f)
              (if (.endsWith lower ".vst3")
                ;; VST3 bundle: send ListPlugins IPC to discover plugins
                (send-list-plugins backend (.getAbsolutePath f))
                ;; Recurse into subdirectory
                (scan-directory f plugins-node midi-node audio-node backend))

              (and midi-node (some #(.endsWith lower ^String %) midi-exts))
              (.add midi-node (DefaultMutableTreeNode.
                                (file-item f "midi" fname)))

              (and audio-node (some #(.endsWith lower ^String %) audio-exts))
              (.add audio-node (DefaultMutableTreeNode.
                                 (file-item f "audio" fname))))))))))

;; ---------------------------------------------------------------------------
;; Browser pane
;; ---------------------------------------------------------------------------

(defn make-browser-pane
  "Creates the file browser panel. Returns {:panel JPanel}."
  ^java.util.Map [^BackendManager backend]
  (let [root         (DefaultMutableTreeNode. "Hibiki")
        plugins-node (DefaultMutableTreeNode. "Plugins")
        midi-node    (DefaultMutableTreeNode. "MIDI Files")
        audio-node   (DefaultMutableTreeNode. "Audio Clips")
        _            (do (.add root plugins-node)
                         (.add root midi-node)
                         (.add root audio-node))
        model  (DefaultTreeModel. root)

        ;; Custom tree renderer
        renderer (doto (DefaultTreeCellRenderer.)
                   (.setBackgroundNonSelectionColor (t/color :bg-dark))
                   (.setTextNonSelectionColor (t/color :text-normal))
                   (.setTextSelectionColor (t/color :text-bright))
                   (.setBackgroundSelectionColor (t/color :panel-bg-light))
                   (.setBorderSelectionColor (t/color :border))
                   (.setLeafIcon nil)
                   (.setOpenIcon nil)
                   (.setClosedIcon nil))

        tree   (doto (JTree. model)
                 (.setBackground (t/color :bg-dark))
                 (.setForeground (t/color :text-normal))
                 (.setFont (t/font :font-ui))
                 (.setRowHeight (t/scale 20))
                 (.setCellRenderer renderer)
                 (.setRootVisible false)
                 (.setShowsRootHandles true))

        header (doto (JLabel. "BROWSER" SwingConstants/LEFT)
                 (.setBorder (BorderFactory/createEmptyBorder 5 10 5 0))
                 (.setBackground (t/color :track-header))
                 (.setForeground (t/color :text-bright))
                 (.setFont (t/font :font-ui-bold))
                 (.setOpaque true)
                 (.setPreferredSize (Dimension. 0 (t/scale 30))))

        scroll (doto (JScrollPane. tree)
                 (.setBorder nil)
                 (.setBackground (t/color :bg-dark)))

        panel  (doto (JPanel. (BorderLayout.))
                 (.setBackground (t/color :bg-dark))
                 (.setPreferredSize (Dimension. (t/scale 220) 0))
                 (.setBorder (BorderFactory/createMatteBorder (int 0) (int 0) (int 0) (int 1) ^java.awt.Color (t/color :border)))
                 (.add header BorderLayout/NORTH)
                 (.add scroll BorderLayout/CENTER))

        ;; Mutable state for debounced plugin refresh
        bundles-discovered (java.util.concurrent.ConcurrentHashMap.)
        refresh-timer (atom nil)]

    ;; Scan directories
    (let [testdata (File. "testdata")]
      (when (.exists testdata) (scan-directory testdata plugins-node midi-node audio-node backend)))
    (let [^String home (System/getProperty "user.home")]
      (scan-directory (File. home ".vst3") plugins-node nil nil backend)
      ;; Platform-specific VST3 dirs
      (let [^String os (.toLowerCase (System/getProperty "os.name"))]
        (cond
          (.contains os "mac")
          (do (scan-directory (File. "/Library/Audio/Plug-ins/VST3") plugins-node nil nil backend)
              (scan-directory (File. (str home "/Library/Audio/Plug-ins/VST3")) plugins-node nil nil backend))
          (.contains os "win")
          (scan-directory (File. "C:\\Program Files\\Common Files\\VST3") plugins-node nil nil backend)
          :else
          (do (scan-directory (File. "/usr/lib/vst3") plugins-node nil nil backend)
              (scan-directory (File. "/usr/local/lib/vst3") plugins-node nil nil backend)))))
    (.reload model)

    ;; Double-click handler
    (.addMouseListener tree
      (proxy [MouseAdapter] []
        (mouseClicked [^MouseEvent e]
          (when (= (.getClickCount e) 2)
            (let [^TreePath path (.getPathForLocation tree (.getX e) (.getY e))]
              (when path
                (let [^DefaultMutableTreeNode node (.getLastPathComponent path)]
                  (when (.isLeaf node)
                    (let [user-obj (.getUserObject node)]
                      (when (instance? FileItem user-obj)
                        (let [{:keys [^File file ^String type ^int plugin-index]} user-obj
                              ^String abs-path (.getAbsolutePath file)]
                          (case type
                            "vst"   (send-load-plugin backend abs-path plugin-index)
                            "midi"  (send-load-clip backend abs-path false)
                            "audio" (send-load-clip backend abs-path true)
                            nil))))))))))))

    ;; Notification handler — refresh plugins tree on PluginList response
    (.addNotificationListener backend
      (reify java.util.function.Consumer
        (accept [_ notif]
          (let [^Notification notif notif]
            (when (= (.responseType notif) Response/PluginList)
              (let [^PluginList pl (.response notif (PluginList.))
                    ^String path (.path pl)
                    plugins (vec (for [i (range (.pluginsLength pl))]
                                   (let [^PluginDescription pd (.plugins pl i)]
                                     (file-item (File. path) "vst"
                                                (.name pd) (.vendor pd) (.index pd)))))]
                (.put bundles-discovered path plugins)
                (SwingUtilities/invokeLater
                  (fn []
                    (when-let [^Timer old @refresh-timer] (.stop old))
                    (let [^Timer tm (Timer. 300
                                     (reify java.awt.event.ActionListener
                                       (actionPerformed [_ _]
                                         (.removeAllChildren plugins-node)
                                         (doseq [[_ items] bundles-discovered]
                                           (doseq [item items]
                                             (.add plugins-node
                                               (DefaultMutableTreeNode. item))))
                                         (.reload model plugins-node))))]
                      (.setRepeats tm false)
                      (.start tm)
                      (reset! refresh-timer tm))))))))))

    {:panel panel}))
