package hibiki;

import clojure.java.api.Clojure;
import clojure.lang.IFn;

/**
 * JUnit-style runner that delegates to hibiki.test-runner/-main. Exits with code 1 if any test
 * fails.
 */
public class ClojureTestRunner {
  public static void main(String[] args) {
    IFn require = Clojure.var("clojure.core", "require");
    require.invoke(Clojure.read("hibiki.test-runner"));
    IFn main = Clojure.var("hibiki.test-runner", "-main");
    main.invoke();
  }
}
