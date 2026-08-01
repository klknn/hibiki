package hibiki.ui;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.Test;
import org.mozilla.javascript.Context;
import org.mozilla.javascript.Scriptable;

/** Tests TypeScript source detection for the JavaScript REPL. */
public class TypeScriptCompilerTest {
  @Test
  public void detectsTypeScriptMarkerOnFirstLine() {
    assertTrue(
        "A // @ts marker must select the TypeScript compiler",
        TypeScriptCompiler.isTypeScript("// @ts\nconst pitch: number = 60;"));
  }

  @Test
  public void doesNotTreatOrdinaryJavaScriptAsTypeScript() {
    assertFalse(
        "JavaScript without the marker must continue to run directly in Rhino",
        TypeScriptCompiler.isTypeScript("const pitch = 60;"));
  }

  @Test
  public void sdkExamplesAreBundledAndOptIntoTypeScriptCompilation() throws Exception {
    InputStream input = getClass().getResourceAsStream("/examples/sdk/midi-arpeggiator.ts");
    assertNotNull("SDK examples must be packaged with the GUI", input);
    String source = new String(input.readAllBytes(), StandardCharsets.UTF_8);
    assertTrue(
        "Browser-loaded SDK TypeScript must start with the REPL compiler marker",
        TypeScriptCompiler.isTypeScript(source));
  }

  @Test
  public void findsBundledCompilerInBazelRunfiles() throws Exception {
    Path runfiles = Files.createTempDirectory("hibiki-runfiles-");
    Path compiler = runfiles.resolve("hibiki/typescript_repl_compiler");
    Files.createDirectories(compiler.getParent());
    Files.writeString(compiler, "#!/bin/sh\n");

    assertTrue(
        "The REPL must prefer the compiler packaged by Bazel over a global tsc installation",
        TypeScriptCompiler.findBundledCompiler(runfiles).isPresent());
  }

  @Test
  public void usesJavaLauncherRunfilesWhenRunfilesDirIsUnset() throws Exception {
    Path runfiles = Files.createTempDirectory("hibiki-java-runfiles-");
    Path compiler = runfiles.resolve("_main/typescript_repl_compiler");
    Files.createDirectories(compiler.getParent());
    Files.writeString(compiler, "#!/bin/sh\n");

    assertEquals(
        "bazel run Java launchers expose JAVA_RUNFILES rather than RUNFILES_DIR",
        compiler.toString(),
        TypeScriptCompiler.compilerCommand(null, runfiles.toString()));
  }

  @Test
  public void transpilesMarkedSnippetWithBundledCompiler() throws Exception {
    String output = TypeScriptCompiler.transpile("// @ts\nconst pitch: number = 60;");
    assertTrue(
        "The Bazel-packaged compiler must emit ES5 JavaScript without a global tsc installation",
        output.contains("var pitch = 60"));
  }

  @Test
  public void rejectsMutationOfReadonlySdkNamespace() throws Exception {
    try {
      TypeScriptCompiler.transpile("// @ts\nhibiki.transport = {};");
      throw new AssertionError("The public SDK declaration must preserve readonly properties");
    } catch (java.io.IOException expected) {
      assertTrue(
          "The compiler must reject assignment to the canonical readonly transport property",
          expected.getMessage().contains("read-only"));
    }
  }

  @Test
  public void preludeExposesTypedSdkNamespaceOnly() throws Exception {
    Context context = Context.enter();
    try {
      Scriptable scope = context.initStandardObjects();
      InputStream input = getClass().getResourceAsStream("/prelude.js");
      assertNotNull("The SDK prelude must be bundled as a resource", input);
      context.evaluateReader(
          scope, new BufferedReader(new InputStreamReader(input)), "prelude.js", 1, null);
      assertNotNull("The prelude must expose hibiki.transport", scope.get("hibiki", scope));
      assertTrue(
          "Legacy global play() must not be part of the public SDK",
          scope.get("play", scope) == Scriptable.NOT_FOUND);
    } finally {
      Context.exit();
    }
  }
}
