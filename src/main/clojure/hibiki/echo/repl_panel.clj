(ns hibiki.echo.repl-panel
  "Embedded Clojure REPL panel for the Echo frontend.
   Provides a Swing-based REPL with input history, Ctrl+Enter eval,
   selected-region eval, and stdout/stderr capture."
  (:import [javax.swing JButton JComponent JPanel JScrollPane JTextArea
            AbstractAction BorderFactory KeyStroke SwingUtilities]
           [java.awt BorderLayout Color Dimension FlowLayout Font]
           [java.awt.event KeyEvent]
           [java.io OutputStream PrintStream]))

(set! *warn-on-reflection* true)

;; ---------------------------------------------------------------------------
;; Stdout/stderr capture
;; ---------------------------------------------------------------------------

(defn- make-output-stream
  "Create a PrintStream that appends text to the append! function."
  ^PrintStream [append!]
  (let [os (proxy [OutputStream] []
             (write
               ([b]
                (cond
                  (instance? Integer b)
                  (append! (str (char (int b))))

                  (bytes? b)
                  (append! (String. ^bytes b "UTF-8"))))
               ([b off len]
                (append! (String. ^bytes b (int off) (int len) "UTF-8"))))
             (flush []))]
    (PrintStream. os true "UTF-8")))

;; ---------------------------------------------------------------------------
;; Eval
;; ---------------------------------------------------------------------------

(defn- eval-input
  "Evaluate a Clojure expression string, capturing stdout/stderr.
   Returns [result-str stdout-str error?]."
  [^String input append!]
  (let [capture-ps (make-output-stream append!)]
    (try
      (let [result (binding [*out* (java.io.OutputStreamWriter. capture-ps)
                             *err* (java.io.OutputStreamWriter. capture-ps)]
                     (eval (read-string input)))]
        (.flush capture-ps)
        [(pr-str result) false])
      (catch Throwable t
        (.flush capture-ps)
        [(.getMessage t) true]))))

;; ---------------------------------------------------------------------------
;; Key consumption
;; ---------------------------------------------------------------------------

(defn- consume-key!
  "Register a no-op action on the given component for a keystroke at
   WHEN_ANCESTOR_OF_FOCUSED_COMPONENT level, preventing propagation
   to WHEN_IN_FOCUSED_WINDOW bindings on parent panels."
  [^JComponent comp ^KeyStroke ks]
  (let [im (.getInputMap comp JComponent/WHEN_ANCESTOR_OF_FOCUSED_COMPONENT)
        am (.getActionMap comp)]
    (.put im ks "consumed")
    (.put am "consumed" (proxy [AbstractAction] []
                          (actionPerformed [_])))))

;; ---------------------------------------------------------------------------
;; Panel
;; ---------------------------------------------------------------------------

(defn make-repl-panel
  "Creates a REPL panel with output area, multiline input, and Eval button.
   Returns {:panel JPanel :focus! (fn [] ...)}."
  []
  (let [output (doto (JTextArea.)
                 (.setEditable false)
                 (.setFont (Font. "Monospaced" Font/PLAIN 13))
                 (.setBackground (Color. 30 30 30))
                 (.setForeground (Color. 200 200 200))
                 (.setCaretColor (Color. 200 200 200))
                 (.setLineWrap true)
                 (.setWrapStyleWord true))
        out-scroll (doto (JScrollPane. output)
                     (.setBorder nil))
        history  (atom [])
        hist-idx (atom -1)
        input  (doto (JTextArea. 3 40)
                 (.setFont (Font. "Monospaced" Font/PLAIN 13))
                 (.setBackground (Color. 40 40 40))
                 (.setForeground (Color. 220 220 220))
                 (.setCaretColor (Color. 220 220 220))
                 (.setLineWrap true)
                 (.setWrapStyleWord true)
                 (.setBorder (BorderFactory/createEmptyBorder 4 6 4 6)))
        in-scroll (doto (JScrollPane. input)
                    (.setBorder (BorderFactory/createMatteBorder 1 0 0 0 (Color. 60 60 60)))
                    (.setPreferredSize (Dimension. 0 70)))

        append! (fn [^String text]
                  (SwingUtilities/invokeLater
                   (fn []
                     (.append output text)
                     ;; Auto-scroll to bottom
                     (.setCaretPosition output (.getLength (.getDocument output))))))

        do-eval! (fn []
                   ;; If there's a selection, evaluate only the selected region
                   (let [sel (.getSelectedText input)
                         text (if (and sel (not (.isEmpty (.trim sel))))
                                (.trim sel)
                                (.trim (.getText input)))]
                     (when (not (.isEmpty text))
                       (when-not (and sel (not (.isEmpty (.trim sel))))
                         ;; Only clear input if evaluating the full text
                         (.setText input ""))
                       (swap! history conj text)
                       (reset! hist-idx -1)
                       (append! (str "=> " text "\n"))
                       ;; Evaluate on background thread
                       (future
                         (let [[result error?] (eval-input text append!)]
                           (append! (str (if error? "ERROR: " "") result "\n")))))))

        ;; Eval button
        eval-btn (doto (JButton. "Eval")
                   (.setFont (Font. "SansSerif" Font/PLAIN 11))
                   (.setFocusable false)
                   (.setToolTipText "Evaluate (Ctrl+Enter)")
                   (.addActionListener
                    (reify java.awt.event.ActionListener
                      (actionPerformed [_ _] (do-eval!)))))

        ;; Hint label
        hint (doto (javax.swing.JLabel. "Ctrl+Enter = eval  |  Ctrl+↑↓ = history")
               (.setFont (Font. "SansSerif" Font/PLAIN 10))
               (.setForeground (Color. 100 100 100)))

        ;; Bottom bar
        btn-bar (doto (JPanel. (BorderLayout.))
                  (.setBackground (Color. 35 35 35))
                  (.setPreferredSize (Dimension. 0 28))
                  (.add hint BorderLayout/WEST)
                  (.add eval-btn BorderLayout/EAST))
        input-panel (doto (JPanel. (BorderLayout.))
                      (.add in-scroll BorderLayout/CENTER)
                      (.add btn-bar BorderLayout/SOUTH))

        panel  (doto (JPanel. (BorderLayout.))
                 (.setBackground (Color. 30 30 30))
                 (.setBorder (BorderFactory/createMatteBorder 0 1 0 0 (Color. 50 50 50)))
                 (.add out-scroll BorderLayout/CENTER)
                 (.add input-panel BorderLayout/SOUTH))]

    ;; Welcome message
    (.append output "── Hibiki Echo REPL ──\n")
    (.append output "Ctrl+Enter to evaluate. Select text for partial eval.\n")

    ;; Auto-import common classes so users can start right away
    (future
      (try
        (eval '(do
                 (import '[hibiki BackendManager]
                         '[hibiki.ui SessionView TimelineView PluginPane Theme MainView]
                         '[hibiki.pb HibikiProto HibikiProto$Request HibikiProto$Notification])
                 (require '[hibiki.echo.dev :as dev])
                 (def bm (BackendManager/getInstance))))
        (append! "Ready. `bm`, `dev/*`, and all protobuf classes are available.\n")
        (append! "Try: (dev/theme! :solarized-dark)\n\n")
        (catch Throwable t
          (append! (str "Auto-import warning: " (.getMessage t) "\n\n")))))

    ;; Ctrl+Enter to evaluate (on the input text area)
    (let [im (.getInputMap input JComponent/WHEN_FOCUSED)
          am (.getActionMap input)]
      (.put im (KeyStroke/getKeyStroke KeyEvent/VK_ENTER KeyEvent/CTRL_DOWN_MASK) "eval")
      (.put im (KeyStroke/getKeyStroke KeyEvent/VK_ENTER KeyEvent/META_DOWN_MASK) "eval")
      (.put am "eval" (proxy [AbstractAction] []
                        (actionPerformed [_] (do-eval!)))))

    ;; Ctrl+Up/Down for history
    (let [im (.getInputMap input JComponent/WHEN_FOCUSED)
          am (.getActionMap input)]
      (.put im (KeyStroke/getKeyStroke KeyEvent/VK_UP KeyEvent/CTRL_DOWN_MASK) "histUp")
      (.put am "histUp" (proxy [AbstractAction] []
                          (actionPerformed [_]
                            (let [h @history n (count h)]
                              (when (pos? n)
                                (swap! hist-idx (fn [i] (min (inc i) (dec n))))
                                (.setText input (nth h (- (dec n) @hist-idx))))))))
      (.put im (KeyStroke/getKeyStroke KeyEvent/VK_DOWN KeyEvent/CTRL_DOWN_MASK) "histDown")
      (.put am "histDown" (proxy [AbstractAction] []
                            (actionPerformed [_]
                              (let [h @history n (count h)]
                                (when (pos? n)
                                  (swap! hist-idx (fn [i] (max (dec i) -1)))
                                  (.setText input (if (neg? @hist-idx)
                                                    ""
                                                    (nth h (- (dec n) @hist-idx))))))))))

    ;; Consume keys that MainView's WHEN_IN_FOCUSED_WINDOW bindings would steal
    (doseq [ks [(KeyStroke/getKeyStroke KeyEvent/VK_SPACE 0)
                (KeyStroke/getKeyStroke KeyEvent/VK_ENTER 0)
                (KeyStroke/getKeyStroke KeyEvent/VK_TAB 0)
                (KeyStroke/getKeyStroke KeyEvent/VK_1 0)
                (KeyStroke/getKeyStroke KeyEvent/VK_2 0)
                (KeyStroke/getKeyStroke KeyEvent/VK_3 0)
                (KeyStroke/getKeyStroke KeyEvent/VK_4 0)]]
      (consume-key! panel ks))

    {:panel panel
     :focus! (fn [] (.requestFocusInWindow input))}))
