package hibiki;

import clojure.java.api.Clojure;
import clojure.lang.IFn;

/**
 * Bootstrap entry point for the Echo hybrid frontend.
 * Loads the Clojure runtime and calls hibiki.echo/-main.
 */
public class EchoMain {
    public static void main(String[] args) {
        IFn require = Clojure.var("clojure.core", "require");
        require.invoke(Clojure.read("hibiki.echo"));
        IFn main = Clojure.var("hibiki.echo", "-main");
        main.invoke();
    }
}
