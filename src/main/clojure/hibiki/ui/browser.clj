(ns hibiki.ui.browser
  "File browser panel — shows plugins, MIDI, and audio files in a tree.
   Ported from BrowserPane.java."
  (:require [hibiki.ui.theme :as t])
  (:import [javax.swing JPanel JTree JScrollPane BorderFactory]
           [javax.swing.tree DefaultMutableTreeNode DefaultTreeModel]
           [java.awt BorderLayout Dimension]
           [java.awt.event MouseAdapter MouseEvent]
           [java.awt.datatransfer StringSelection]
           [java.io File]
           [com.google.flatbuffers FlatBufferBuilder]
           [hibiki.ipc Request Command LoadPlugin LoadClip ListPlugins
                       PluginList PluginDescription Response Notification]))

;; ---------------------------------------------------------------------------
;; IPC helpers
;; ---------------------------------------------------------------------------

(defn- send-load-plugin [backend path plugin-index]
  (let [b (FlatBufferBuilder. 256)
        po (.createString b path)
        cmd (do (LoadPlugin/startLoadPlugin b)
                (LoadPlugin/addPath b po)
                (LoadPlugin/addPluginIndex b plugin-index)
                (LoadPlugin/endLoadPlugin b))
        req (Request/createRequest b Command/LoadPlugin cmd)]
    (.finish b req)
    (.sendRequest backend b)))

(defn- send-list-plugins [backend path]
  (let [b (FlatBufferBuilder. 256)
        po (.createString b path)
        cmd (do (ListPlugins/startListPlugins b)
                (ListPlugins/addPath b po)
                (ListPlugins/endListPlugins b))
        req (Request/createRequest b Command/ListPlugins cmd)]
    (.finish b req)
    (.sendRequest backend b)))

(defn- send-load-clip [backend path loop?]
  (let [b (FlatBufferBuilder. 256)
        po (.createString b path)
        session-view (try (.. (Class/forName "hibiki.ui.SessionView")
                              (getMethod "getInstance" (into-array Class []))
                              (invoke nil (object-array 0)))
                          (catch Exception _ nil))
        track-idx (if session-view (.getSelectedTrack session-view) 0)
        cmd (do (LoadClip/startLoadClip b)
                (LoadClip/addTrackIndex b track-idx)
                (LoadClip/addSlotIndex b 0)
                (LoadClip/addPath b po)
                (LoadClip/addIsLoop b loop?)
                (LoadClip/endLoadClip b))
        req (Request/createRequest b Command/LoadClip cmd)]
    (.finish b req)
    (.sendRequest backend b)))

;; ---------------------------------------------------------------------------
;; File scanning
;; ---------------------------------------------------------------------------

(def ^:private audio-exts #{".wav" ".mp3" ".ogg" ".flac" ".aiff"})
(def ^:private midi-exts  #{".mid" ".midi"})
(def ^:private plugin-exts #{".vst3" ".so" ".dll" ".component"})

(defn- scan-directory [dir plugins-node midi-node audio-node]
  (when (.isDirectory dir)
    (doseq [^File f (sort-by #(.getName ^File %) (.listFiles dir))]
      (let [name (.getName f)
            lower (.toLowerCase name)]
        (cond
          (.isDirectory f)
          (if (or (.endsWith lower ".vst3") (.endsWith lower ".component"))
            (.add plugins-node (DefaultMutableTreeNode. name))
            (scan-directory f plugins-node midi-node audio-node))

          (some #(.endsWith lower %) midi-exts)
          (.add midi-node (DefaultMutableTreeNode. name))

          (some #(.endsWith lower %) audio-exts)
          (.add audio-node (DefaultMutableTreeNode. name)))))))

;; ---------------------------------------------------------------------------
;; Browser pane
;; ---------------------------------------------------------------------------

(defn make-browser-pane
  "Creates the file browser panel. Returns {:panel JPanel}."
  [backend]
  (let [root         (DefaultMutableTreeNode. "Browser")
        plugins-node (DefaultMutableTreeNode. "Plugins")
        midi-node    (DefaultMutableTreeNode. "MIDI")
        audio-node   (DefaultMutableTreeNode. "Audio")
        _            (do (.add root plugins-node)
                         (.add root midi-node)
                         (.add root audio-node))
        model  (DefaultTreeModel. root)
        tree   (doto (JTree. model)
                 (.setBackground (t/color :bg-dark))
                 (.setForeground (t/color :text-normal))
                 (.setFont (t/font :font-ui))
                 (.setRootVisible false)
                 (.setShowsRootHandles true))
        scroll (doto (JScrollPane. tree)
                 (.setBorder nil)
                 (.setBackground (t/color :bg-dark)))
        panel  (doto (JPanel. (BorderLayout.))
                 (.setBackground (t/color :bg-dark))
                 (.setPreferredSize (Dimension. (t/scale 220) 0))
                 (.add scroll BorderLayout/CENTER))]

    ;; Scan default directories
    (let [home (File. (System/getProperty "user.home"))
          vst3-dir (File. home ".vst3")]
      (when (.exists vst3-dir) (scan-directory vst3-dir plugins-node midi-node audio-node)))
    (let [testdata (File. "testdata")]
      (when (.exists testdata) (scan-directory testdata plugins-node midi-node audio-node)))
    (.reload model)

    ;; Double-click handler
    (.addMouseListener tree
      (proxy [MouseAdapter] []
        (mouseClicked [^MouseEvent e]
          (when (= (.getClickCount e) 2)
            (let [path (.getPathForLocation tree (.getX e) (.getY e))]
              (when path
                (let [node ^DefaultMutableTreeNode (.getLastPathComponent path)]
                  (when (.isLeaf node)
                    (let [name (str (.getUserObject node))
                          lower (.toLowerCase name)]
                      (cond
                        (some #(.endsWith lower %) midi-exts)
                        (send-load-clip backend (str "testdata/" name) false)

                        (some #(.endsWith lower %) audio-exts)
                        (send-load-clip backend (str "testdata/" name) false)

                        (some #(.endsWith lower %) plugin-exts)
                        (send-load-plugin backend name 0)))))))))))

    ;; Notification handler for plugin list refresh
    (.addNotificationListener backend
      (reify java.util.function.Consumer
        (accept [_ notification]
          (when (= (.responseType notification) Response/PluginList)
            (let [pl ^PluginList (.response notification (PluginList.))]
              (javax.swing.SwingUtilities/invokeLater
                #(do (.removeAllChildren plugins-node)
                     (dotimes [i (.pluginsLength pl)]
                       (let [pd (.plugins pl i)
                             display (str (.name pd) " (" (.vendor pd) ")")]
                         (.add plugins-node (DefaultMutableTreeNode. display))))
                     (.reload model))))))))

    {:panel panel}))
