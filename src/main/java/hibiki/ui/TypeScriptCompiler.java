package hibiki.ui;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Optional;
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

      String command = compilerCommand();
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

  private static String compilerCommand() {
    return compilerCommand(System.getenv("RUNFILES_DIR"), System.getenv("JAVA_RUNFILES"));
  }

  /** Resolve the compiler for Bazel's shell and Java launcher runfiles conventions. */
  static String compilerCommand(String runfilesDir, String javaRunfilesDir) {
    String configured = System.getProperty(COMMAND_PROPERTY);
    if (configured != null && !configured.isBlank()) {
      return configured;
    }
    for (String candidate :
        new String[] {javaRunfilesDir, runfilesDir, System.getenv("TEST_SRCDIR")}) {
      if (candidate != null && !candidate.isBlank()) {
        Optional<Path> bundledCompiler = findBundledCompiler(Path.of(candidate));
        if (bundledCompiler.isPresent()) {
          return bundledCompiler.get().toString();
        }
      }
    }
    return DEFAULT_COMMAND;
  }

  /** Locate the TypeScript launcher included in the Bazel runfiles tree. */
  static Optional<Path> findBundledCompiler(Path runfilesDir) {
    for (String workspace : List.of("hibiki", "_main")) {
      Path compiler = runfilesDir.resolve(workspace).resolve("typescript_repl_compiler");
      if (Files.isRegularFile(compiler)) {
        return Optional.of(compiler);
      }
    }
    return Optional.empty();
  }

  private static void writeDeclarations(Path destination) throws IOException {
    for (String resource : List.of("/hibiki-sdk.d.ts", "/hibiki-globals.d.ts")) {
      try (InputStream declarations = TypeScriptCompiler.class.getResourceAsStream(resource)) {
        if (declarations == null) {
          throw new IOException("Bundled TypeScript declaration is missing: " + resource);
        }
        Files.writeString(
            destination, "\n", StandardCharsets.UTF_8, java.nio.file.StandardOpenOption.APPEND);
        Files.write(
            destination, declarations.readAllBytes(), java.nio.file.StandardOpenOption.APPEND);
      }
    }
  }
}
