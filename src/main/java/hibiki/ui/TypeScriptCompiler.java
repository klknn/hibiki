package hibiki.ui;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.TimeUnit;

/** Compiles marked TypeScript REPL snippets to ES5 JavaScript for Rhino. */
final class TypeScriptCompiler {
  private static final String MARKER = "// @ts";
  private static final String COMMAND_PROPERTY = "hibiki.typescript.command";
  private static final String DEFAULT_COMMAND = "tsc";

  private TypeScriptCompiler() {}

  /** Returns whether the source opts into TypeScript compilation. */
  static boolean isTypeScript(String source) {
    return source.stripLeading().startsWith(MARKER);
  }

  /** Compiles a TypeScript snippet using the configured {@code tsc} executable. */
  static String transpile(String source) throws IOException, InterruptedException {
    Path sourceFile = Files.createTempFile("hibiki-repl-", ".ts");
    Path declarationsFile = Files.createTempFile("hibiki-repl-", ".d.ts");
    Path outputFile = Files.createTempFile("hibiki-repl-", ".js");
    try {
      Files.writeString(sourceFile, source, StandardCharsets.UTF_8);
      writeDeclarations(declarationsFile);

      String command = System.getProperty(COMMAND_PROPERTY, DEFAULT_COMMAND);
      Process process;
      try {
        process =
            new ProcessBuilder(
                    List.of(
                        command,
                        "--target",
                        "ES5",
                        "--module",
                        "none",
                        "--noEmitOnError",
                        "--outFile",
                        outputFile.toString(),
                        sourceFile.toString(),
                        declarationsFile.toString()))
                .redirectErrorStream(true)
                .start();
      } catch (IOException e) {
        throw new IOException(
            "TypeScript requires the `tsc` compiler. Install TypeScript or set -D"
                + COMMAND_PROPERTY
                + "=/path/to/tsc.",
            e);
      }

      String compilerOutput =
          new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
      if (!process.waitFor(30, TimeUnit.SECONDS)) {
        process.destroyForcibly();
        throw new IOException("TypeScript compilation timed out after 30 seconds.");
      }
      if (process.exitValue() != 0) {
        throw new IOException("TypeScript compilation failed:\n" + compilerOutput.strip());
      }
      return Files.readString(outputFile, StandardCharsets.UTF_8);
    } finally {
      Files.deleteIfExists(sourceFile);
      Files.deleteIfExists(declarationsFile);
      Files.deleteIfExists(outputFile);
    }
  }

  private static void writeDeclarations(Path destination) throws IOException {
    try (InputStream declarations = TypeScriptCompiler.class.getResourceAsStream("/hibiki.d.ts")) {
      if (declarations == null) {
        throw new IOException("Bundled TypeScript declarations are missing.");
      }
      Files.copy(declarations, destination, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }
  }
}
