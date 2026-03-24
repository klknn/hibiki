(ns hibiki.repl
  "Starts a socket REPL on port 5555 and launches the GUI.")

(defn -main [& _args]
  (clojure.core.server/start-server
   {:port 5555 :name "repl" :accept 'clojure.core.server/repl})
  (require 'hibiki.ui.core)
  ((resolve 'hibiki.ui.core/-main)))
