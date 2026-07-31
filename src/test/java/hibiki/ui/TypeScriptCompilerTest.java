package hibiki.ui;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

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
}
