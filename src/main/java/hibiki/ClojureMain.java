package hibiki;

import clojure.java.api.Clojure;
import clojure.lang.IFn;

/**
 * Bootstrap entry point for the Clojure GUI frontend.
 * Loads the Clojure runtime and calls hibiki.ui.core/-main.
 */
public class ClojureMain {
    public static void main(String[] args) {
        IFn require = Clojure.var("clojure.core", "require");
        require.invoke(Clojure.read("hibiki.ui.core"));
        IFn main = Clojure.var("hibiki.ui.core", "-main");
        main.invoke();
    }
}
