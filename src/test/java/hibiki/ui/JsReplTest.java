package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.BackendManager;
import hibiki.pb.notifications.ClipMidiData;
import hibiki.pb.notifications.Notification;
import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.junit.Test;
import org.mozilla.javascript.Context;
import org.mozilla.javascript.Scriptable;

public class JsReplTest {
  @Test
  public void testJsWriteMidiIntegration() throws Exception {
    BackendManager bm = BackendManager.getInstance();
    bm.start();

    // Wait for backend to start
    Thread.sleep(1000);

    CompletableFuture<ClipMidiData> midiDataFuture = new CompletableFuture<>();
    bm.addNotificationListener(
        notification -> {
          if (notification.getResponseCase() == Notification.ResponseCase.CLIP_MIDI_DATA) {
            midiDataFuture.complete(notification.getClipMidiData());
          }
        });

    try {
      Context cx = Context.enter();
      try {
        Scriptable scope = cx.initStandardObjects();

        // Load prelude.js
        InputStream in = getClass().getResourceAsStream("/prelude.js");
        assertNotNull("prelude.js should exist in resources", in);
        BufferedReader reader = new BufferedReader(new InputStreamReader(in));
        cx.evaluateReader(scope, reader, "prelude.js", 1, null);

        // 1. Load/create a MIDI clip at track 1, slot 1
        cx.evaluateString(scope, "loadClip(1, 1, \"testdata/test.mid\", false);", "test", 1, null);
        Thread.sleep(500); // Wait for clip creation

        // 2. Run the user's snippet to write midi notes to track 1, slot 1
        String jsCode =
            "var PPQ = 480;\n"
                + "var sixteenth = PPQ / 4;\n"
                + "var notes = [];\n"
                + "var pattern = [true, false, true, true, false, true, false, true];\n"
                + "for (var i = 0; i < pattern.length; i++) {\n"
                + "    if (pattern[i]) {\n"
                + "        notes.push({\n"
                + "            tick: i * sixteenth,\n"
                + "            pitch: 36,\n"
                + "            dur: sixteenth - 10,\n"
                + "            vel: 90\n"
                + "        });\n"
                + "    }\n"
                + "}\n"
                + "writeMidi(1, 1, -1, PPQ, notes);\n";

        cx.evaluateString(scope, jsCode, "test", 1, null);
        Thread.sleep(500); // Wait for midi write to execute in backend

        // 3. Request the midi events of the clip we just wrote to
        cx.evaluateString(scope, "getMidi(1, 1);", "test", 1, null);

        // 4. Verify that the midi data received contains the 5 notes we wrote
        ClipMidiData data = midiDataFuture.get(5, TimeUnit.SECONDS);
        assertNotNull("Should receive CLIP_MIDI_DATA notification", data);
        assertEquals("Should have exactly 5 notes", 5, data.getEventsCount());
        assertEquals(36, data.getEvents(0).getPitch());
        assertEquals(90, data.getEvents(0).getVelocity());
      } finally {
        Context.exit();
      }
    } finally {
      bm.stop();
    }
  }

  @Test
  public void testJsMidiRenderingToWav() throws Exception {
    BackendManager bm = BackendManager.getInstance();
    bm.start();

    // Wait for backend to start
    Thread.sleep(1000);

    CompletableFuture<String> bounceFinishedFuture = new CompletableFuture<>();
    bm.addNotificationListener(
        notification -> {
          if (notification.getResponseCase() == Notification.ResponseCase.BOUNCE_FINISHED) {
            bounceFinishedFuture.complete(notification.getBounceFinished().getPath());
          }
        });

    try {
      Context cx = Context.enter();
      try {
        Scriptable scope = cx.initStandardObjects();

        // Load prelude.js
        InputStream in = getClass().getResourceAsStream("/prelude.js");
        assertNotNull("prelude.js should exist in resources", in);
        BufferedReader reader = new BufferedReader(new InputStreamReader(in));
        cx.evaluateReader(scope, reader, "prelude.js", 1, null);

        // 1. Load Dexed plugin on track 1, plugin slot 0
        cx.evaluateString(scope, "loadPlugin(1, \"testdata/Dexed.vst3\", 0);", "test", 1, null);
        Thread.sleep(500);

        // 2. Add a timeline clip on track 1 at 0.0 seconds with duration 4.0 seconds
        cx.evaluateString(
            scope, "addTimelineClip(1, \"testdata/test.mid\", 0.0, 4.0);", "test", 1, null);
        Thread.sleep(500);

        // 3. Write our kick drum notes to timeline clip 0
        String jsCode =
            "var PPQ = 480;\n"
                + "var sixteenth = PPQ / 4;\n"
                + "var notes = [];\n"
                + "var pattern = [true, false, true, true, false, true, false, true];\n"
                + "for (var i = 0; i < pattern.length; i++) {\n"
                + "    if (pattern[i]) {\n"
                + "        notes.push({\n"
                + "            tick: i * sixteenth,\n"
                + "            pitch: 36,\n"
                + "            dur: sixteenth - 10,\n"
                + "            vel: 90\n"
                + "        });\n"
                + "    }\n"
                + "}\n"
                + "writeMidi(1, -1, 0, PPQ, notes);\n";

        cx.evaluateString(scope, jsCode, "test", 1, null);
        Thread.sleep(500);

        // 4. Trigger bounce/rendering to output_mix.wav
        cx.evaluateString(scope, "bounce(\"output_mix.wav\");", "test", 1, null);

        // 5. Wait for bounce finished notification
        String bouncedPath = bounceFinishedFuture.get(10, TimeUnit.SECONDS);
        assertNotNull("Should receive BOUNCE_FINISHED notification", bouncedPath);

        // 6. Verify output mix WAV file contains non-silent audio frames
        java.io.File mixFile = new java.io.File("output_mix.wav");
        assertTrue("output_mix.wav should exist", mixFile.exists());
        assertTrue("output_mix.wav should not be empty", mixFile.length() > 0);

        javax.sound.sampled.AudioInputStream ais =
            javax.sound.sampled.AudioSystem.getAudioInputStream(mixFile);
        assertNotNull("Should get AudioInputStream", ais);
        assertTrue("Should have audio frames", ais.getFrameLength() > 0);

        byte[] bytes = new byte[4096];
        int read;
        boolean hasSound = false;
        while ((read = ais.read(bytes)) != -1) {
          for (int i = 0; i < read; i++) {
            if (bytes[i] != 0) {
              hasSound = true;
              break;
            }
          }
          if (hasSound) break;
        }
        assertTrue("WAV should contain non-silent samples", hasSound);
        ais.close();
        mixFile.delete();
      } finally {
        Context.exit();
      }
    } finally {
      bm.stop();
    }
  }
}
