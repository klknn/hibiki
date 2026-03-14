package hibiki;

import clojure.java.api.Clojure;
import clojure.lang.IFn;

/**
 * JUnit-style runner that invokes clojure.test/run-tests on Clojure test namespaces.
 * Exits with code 1 if any test fails.
 */
public class ClojureTestRunner {
    public static void main(String[] args) {
        // Require clojure.test
        IFn require = Clojure.var("clojure.core", "require");
        require.invoke(Clojure.read("clojure.test"));

        // Load and require all test namespaces
        String[] namespaces = {
            "hibiki.ui.theme-test",
            "hibiki.ui.piano-roll-test",
            "hibiki.ui.session-test",
            "hibiki.ui.timeline-test"
        };

        for (String ns : namespaces) {
            require.invoke(Clojure.read(ns));
        }

        // Run tests
        IFn runTests = Clojure.var("clojure.test", "run-tests");
        int failures = 0;
        int errors = 0;
        for (String ns : namespaces) {
            Object result = runTests.invoke(Clojure.read(ns));
            // result is a map {:test n :pass n :fail n :error n}
            IFn get = Clojure.var("clojure.core", "get");
            Object fail = get.invoke(result, Clojure.read(":fail"));
            Object err = get.invoke(result, Clojure.read(":error"));
            if (fail instanceof Number) failures += ((Number) fail).intValue();
            if (err instanceof Number) errors += ((Number) err).intValue();
        }

        if (failures + errors > 0) {
            System.err.println("FAILURES: " + failures + ", ERRORS: " + errors);
            System.exit(1);
        } else {
            System.out.println("All Clojure tests passed.");
        }
    }
}
