package hibiki;

import static org.junit.Assert.*;

import java.util.logging.Level;
import java.util.logging.LogRecord;
import org.junit.Test;

/** Safety-net tests for AbslFormatter — ensures formatting doesn't crash. */
public class AbslFormatterTest {

  @Test
  public void testFormatInfo() {
    AbslFormatter fmt = new AbslFormatter();
    LogRecord r = new LogRecord(Level.INFO, "hello world");
    String out = fmt.format(r);
    assertTrue(out.startsWith("I"));
    assertTrue(out.contains("hello world"));
    assertTrue(out.endsWith("\n"));
  }

  @Test
  public void testFormatWarning() {
    AbslFormatter fmt = new AbslFormatter();
    LogRecord r = new LogRecord(Level.WARNING, "warn msg");
    String out = fmt.format(r);
    assertTrue(out.startsWith("W"));
  }

  @Test
  public void testFormatSevere() {
    AbslFormatter fmt = new AbslFormatter();
    LogRecord r = new LogRecord(Level.SEVERE, "error msg");
    String out = fmt.format(r);
    assertTrue(out.startsWith("E"));
  }

  @Test
  public void testFormatWithException() {
    AbslFormatter fmt = new AbslFormatter();
    LogRecord r = new LogRecord(Level.SEVERE, "crash");
    r.setThrown(new RuntimeException("test exception"));
    String out = fmt.format(r);
    assertTrue(out.contains("test exception"));
  }

  @Test
  public void testFormatWithSourceClass() {
    AbslFormatter fmt = new AbslFormatter();
    LogRecord r = new LogRecord(Level.INFO, "msg");
    r.setSourceClassName("hibiki.ui.TimelineView");
    String out = fmt.format(r);
    assertTrue(out.contains("TimelineView.java"));
  }

  @Test
  public void testFormatWithNullSource() {
    AbslFormatter fmt = new AbslFormatter();
    LogRecord r = new LogRecord(Level.INFO, "msg");
    r.setSourceClassName(null);
    String out = fmt.format(r);
    assertTrue(out.contains("unknown"));
  }
}
