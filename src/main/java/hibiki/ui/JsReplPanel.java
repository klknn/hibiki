package hibiki.ui;

import java.awt.*;
import java.awt.event.*;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import javax.swing.*;
import org.mozilla.javascript.Context;
import org.mozilla.javascript.Scriptable;
import org.mozilla.javascript.Undefined;

/**
 * Embedded JavaScript REPL panel for the Java frontend using Mozilla Rhino. Provides a Swing-based
 * REPL with input history, Ctrl+Enter eval, selected-region eval, and stdout/stderr capture.
 */
public class JsReplPanel extends JPanel {
  private final JTextArea output;
  private final JTextArea input;
  private final List<String> history = new ArrayList<>();
  private int historyIndex = -1;
  private Scriptable scope;

  public JsReplPanel() {
    setLayout(new BorderLayout());
    setBackground(new Color(30, 30, 30));
    setBorder(BorderFactory.createMatteBorder(0, 1, 0, 0, new Color(50, 50, 50)));

    // Output area
    output = new JTextArea();
    output.setEditable(false);
    output.setFont(new Font("Monospaced", Font.PLAIN, 13));
    output.setBackground(new Color(30, 30, 30));
    output.setForeground(new Color(200, 200, 200));
    output.setCaretColor(new Color(200, 200, 200));
    output.setLineWrap(true);
    output.setWrapStyleWord(true);

    JScrollPane outScroll = new JScrollPane(output);
    outScroll.setBorder(null);

    // Input area
    input = new JTextArea(3, 40);
    input.setFont(new Font("Monospaced", Font.PLAIN, 13));
    input.setBackground(new Color(40, 40, 40));
    input.setForeground(new Color(220, 220, 220));
    input.setCaretColor(new Color(220, 220, 220));
    input.setLineWrap(true);
    input.setWrapStyleWord(true);
    input.setBorder(BorderFactory.createEmptyBorder(4, 6, 4, 6));

    JScrollPane inScroll = new JScrollPane(input);
    inScroll.setBorder(BorderFactory.createMatteBorder(1, 0, 0, 0, new Color(60, 60, 60)));
    inScroll.setPreferredSize(new Dimension(0, 70));

    // Eval button
    JButton evalBtn = new JButton("Eval");
    evalBtn.setFont(new Font("SansSerif", Font.PLAIN, 11));
    evalBtn.setFocusable(false);
    evalBtn.setToolTipText("Evaluate (Ctrl+Enter)");
    evalBtn.addActionListener(e -> doEval());

    // Hint label
    JLabel hint = new JLabel("Ctrl+Enter = eval  |  Ctrl+↑↓ = history");
    hint.setFont(new Font("SansSerif", Font.PLAIN, 10));
    hint.setForeground(new Color(100, 100, 100));

    // Bottom bar
    JPanel btnBar = new JPanel(new BorderLayout());
    btnBar.setBackground(new Color(35, 35, 35));
    btnBar.setPreferredSize(new Dimension(0, 28));
    btnBar.add(hint, BorderLayout.WEST);
    btnBar.add(evalBtn, BorderLayout.EAST);

    JPanel inputPanel = new JPanel(new BorderLayout());
    inputPanel.add(inScroll, BorderLayout.CENTER);
    inputPanel.add(btnBar, BorderLayout.SOUTH);

    add(outScroll, BorderLayout.CENTER);
    add(inputPanel, BorderLayout.SOUTH);

    // Welcome message
    output.append("── Hibiki JavaScript REPL ──\n");
    output.append("Ctrl+Enter to evaluate. Select text for partial eval.\n");

    // Initialize Rhino on a background thread
    new Thread(
            () -> {
              try {
                Context cx = Context.enter();
                try {
                  scope = cx.initStandardObjects();

                  // Load prelude.js
                  InputStream in = getClass().getResourceAsStream("/prelude.js");
                  if (in != null) {
                    BufferedReader reader = new BufferedReader(new InputStreamReader(in));
                    cx.evaluateReader(scope, reader, "prelude.js", 1, null);
                  } else {
                    appendOutput("Error: /prelude.js not found in resources.\n");
                  }
                  appendOutput(
                      "Ready. Use the `hibiki` SDK namespace (for example,"
                          + " hibiki.transport.play()).\n\n");
                } finally {
                  Context.exit();
                }
              } catch (Throwable t) {
                appendOutput("Rhino JS initialization error: " + t.getMessage() + "\n\n");
              }
            },
            "repl-init")
        .start();

    // Ctrl+Enter to evaluate
    InputMap im = input.getInputMap(JComponent.WHEN_FOCUSED);
    ActionMap am = input.getActionMap();
    im.put(KeyStroke.getKeyStroke(KeyEvent.VK_ENTER, KeyEvent.CTRL_DOWN_MASK), "eval");
    im.put(KeyStroke.getKeyStroke(KeyEvent.VK_ENTER, KeyEvent.META_DOWN_MASK), "eval");
    am.put(
        "eval",
        new AbstractAction() {
          @Override
          public void actionPerformed(ActionEvent e) {
            doEval();
          }
        });

    // Ctrl+Up for history
    im.put(KeyStroke.getKeyStroke(KeyEvent.VK_UP, KeyEvent.CTRL_DOWN_MASK), "histUp");
    am.put(
        "histUp",
        new AbstractAction() {
          @Override
          public void actionPerformed(ActionEvent e) {
            if (!history.isEmpty()) {
              historyIndex = Math.min(historyIndex + 1, history.size() - 1);
              input.setText(history.get(history.size() - 1 - historyIndex));
            }
          }
        });

    // Ctrl+Down for history
    im.put(KeyStroke.getKeyStroke(KeyEvent.VK_DOWN, KeyEvent.CTRL_DOWN_MASK), "histDown");
    am.put(
        "histDown",
        new AbstractAction() {
          @Override
          public void actionPerformed(ActionEvent e) {
            if (!history.isEmpty()) {
              historyIndex = Math.max(historyIndex - 1, -1);
              if (historyIndex < 0) {
                input.setText("");
              } else {
                input.setText(history.get(history.size() - 1 - historyIndex));
              }
            }
          }
        });

    // Consume keys that MainView's WHEN_IN_FOCUSED_WINDOW bindings would steal
    consumeKey(KeyStroke.getKeyStroke(KeyEvent.VK_SPACE, 0));
    consumeKey(KeyStroke.getKeyStroke(KeyEvent.VK_ENTER, 0));
    consumeKey(KeyStroke.getKeyStroke(KeyEvent.VK_TAB, 0));
    consumeKey(KeyStroke.getKeyStroke(KeyEvent.VK_1, 0));
    consumeKey(KeyStroke.getKeyStroke(KeyEvent.VK_2, 0));
    consumeKey(KeyStroke.getKeyStroke(KeyEvent.VK_3, 0));
    consumeKey(KeyStroke.getKeyStroke(KeyEvent.VK_4, 0));
  }

  /** Focus the input area. */
  public void focusInput() {
    input.requestFocusInWindow();
  }

  /** Load a bundled SDK example into the REPL editor without executing it. */
  public void loadSdkExample(String resourceName) throws IOException {
    try (InputStream source = getClass().getResourceAsStream("/examples/sdk/" + resourceName)) {
      if (source == null) {
        throw new IOException("Bundled SDK example not found: " + resourceName);
      }
      input.setText(new String(source.readAllBytes(), StandardCharsets.UTF_8));
      input.setCaretPosition(0);
      focusInput();
      appendOutput("Loaded SDK example: " + resourceName + "\n");
    }
  }

  private void appendOutput(String text) {
    SwingUtilities.invokeLater(
        () -> {
          output.append(text);
          output.setCaretPosition(output.getDocument().getLength());
        });
  }

  private void doEval() {
    String sel = input.getSelectedText();
    String text;
    if (sel != null && !sel.trim().isEmpty()) {
      text = sel.trim();
    } else {
      text = input.getText().trim();
    }
    if (text.isEmpty()) return;

    // Only clear input if evaluating the full text (not a selection)
    if (sel == null || sel.trim().isEmpty()) {
      input.setText("");
    }

    history.add(text);
    historyIndex = -1;
    appendOutput("=> " + text + "\n");

    // Evaluate on background thread
    final String expr = text;
    new Thread(
            () -> {
              try {
                String executableSource = expr;
                if (TypeScriptCompiler.isTypeScript(expr)) {
                  executableSource = TypeScriptCompiler.transpile(expr);
                }

                // Redirect stdout/stderr to capture output
                PrintStream oldOut = System.out;
                PrintStream oldErr = System.err;
                PrintStream captureStream =
                    new PrintStream(
                        new OutputStream() {
                          private final StringBuilder buf = new StringBuilder();

                          @Override
                          public void write(int b) {
                            buf.append((char) b);
                            if (b == '\n') {
                              String line = buf.toString();
                              buf.setLength(0);
                              appendOutput(line);
                            }
                          }

                          @Override
                          public void flush() {
                            if (buf.length() > 0) {
                              String remaining = buf.toString();
                              buf.setLength(0);
                              appendOutput(remaining);
                            }
                          }
                        },
                        true);

                System.setOut(captureStream);
                System.setErr(captureStream);
                try {
                  Context cx = Context.enter();
                  try {
                    // Evaluate in the global scope
                    Object result = cx.evaluateString(scope, executableSource, "repl", 1, null);
                    captureStream.flush();
                    if (result != null
                        && result != Context.getUndefinedValue()
                        && !(result instanceof Undefined)) {
                      String resultStr = Context.toString(result);
                      appendOutput(resultStr + "\n");
                    }
                  } finally {
                    Context.exit();
                  }
                } catch (Throwable t) {
                  captureStream.flush();
                  String msg = t.getMessage();
                  if (t.getCause() != null) {
                    msg = t.getCause().getMessage();
                  }
                  appendOutput("ERROR: " + msg + "\n");
                } finally {
                  System.setOut(oldOut);
                  System.setErr(oldErr);
                }
              } catch (Throwable t) {
                appendOutput("ERROR: " + t.getMessage() + "\n");
              }
            },
            "repl-eval")
        .start();
  }

  /**
   * Register a no-op action on this panel for a keystroke at WHEN_ANCESTOR_OF_FOCUSED_COMPONENT
   * level, preventing propagation to WHEN_IN_FOCUSED_WINDOW bindings on parent panels.
   */
  private void consumeKey(KeyStroke ks) {
    InputMap im = getInputMap(JComponent.WHEN_ANCESTOR_OF_FOCUSED_COMPONENT);
    ActionMap am = getActionMap();
    im.put(ks, "consumed");
    am.put(
        "consumed",
        new AbstractAction() {
          @Override
          public void actionPerformed(ActionEvent e) {
            // no-op — consume the key
          }
        });
  }
}
