package hibiki.ui;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import org.mozilla.javascript.Context;
import org.mozilla.javascript.Scriptable;

/** Evaluates a JavaScript or marked TypeScript file in the same Rhino scope as the REPL. */
public final class JsScriptRunner {
  private JsScriptRunner() {}

  /** Load the SDK prelude, compile marked TypeScript, and evaluate the supplied source file. */
  public static void run(Path sourceFile) throws Exception {
    String source = Files.readString(sourceFile, StandardCharsets.UTF_8);
    if (TypeScriptCompiler.isTypeScript(source)) {
      source = TypeScriptCompiler.transpile(source);
    }

    Context context = Context.enter();
    try {
      Scriptable scope = context.initStandardObjects();
      try (InputStream prelude = JsScriptRunner.class.getResourceAsStream("/prelude.js")) {
        if (prelude == null) {
          throw new IllegalStateException("Bundled SDK prelude is missing.");
        }
        context.evaluateReader(
            scope,
            new BufferedReader(new InputStreamReader(prelude, StandardCharsets.UTF_8)),
            "prelude.js",
            1,
            null);
      }
      context.evaluateString(scope, source, sourceFile.toString(), 1, null);
    } finally {
      Context.exit();
    }
  }
}
