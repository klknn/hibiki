package hibiki;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.logging.Formatter;
import java.util.logging.Level;
import java.util.logging.LogRecord;

/**
 * Formats log records in the same style as abseil (absl) C++ logging:
 *
 * <pre>
 * I0419 19:28:43.123456 12345 GuiMain.java:48] Starting backend
 * W0419 19:28:43.200000 12345 BackendManager.java:62] hbk-play not found
 * </pre>
 *
 * Severity chars: I=INFO, W=WARNING, E=SEVERE/ERROR, F=SEVERE (fatal).
 */
public class AbslFormatter extends Formatter {
  private static final DateTimeFormatter DATE_FMT =
      DateTimeFormatter.ofPattern("MMdd").withZone(ZoneId.systemDefault());
  private static final DateTimeFormatter TIME_FMT =
      DateTimeFormatter.ofPattern("HH:mm:ss").withZone(ZoneId.systemDefault());

  @Override
  public String format(LogRecord record) {
    Instant instant = record.getInstant();
    long micros = (instant.getNano() / 1000) % 1_000_000;

    String severity = severityChar(record.getLevel());
    String date = DATE_FMT.format(instant);
    String time = TIME_FMT.format(instant);
    long threadId = record.getLongThreadID();

    // Extract short class name (basename)
    String source = record.getSourceClassName();
    if (source != null) {
      int dot = source.lastIndexOf('.');
      if (dot >= 0) source = source.substring(dot + 1);
      source += ".java";
    } else {
      source = "unknown";
    }

    StringBuilder sb = new StringBuilder();
    sb.append(severity)
        .append(date)
        .append(' ')
        .append(time)
        .append(String.format(".%06d", micros))
        .append(' ')
        .append(threadId)
        .append(' ')
        .append(source)
        .append("] ")
        .append(formatMessage(record))
        .append('\n');

    // Append stack trace if present
    if (record.getThrown() != null) {
      StringWriter sw = new StringWriter();
      record.getThrown().printStackTrace(new PrintWriter(sw));
      sb.append(sw);
    }

    return sb.toString();
  }

  private static String severityChar(Level level) {
    int val = level.intValue();
    if (val >= Level.SEVERE.intValue()) return "E";
    if (val >= Level.WARNING.intValue()) return "W";
    return "I";
  }
}
