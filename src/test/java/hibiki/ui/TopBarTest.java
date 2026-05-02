package hibiki.ui;

import static org.junit.Assert.*;

import org.junit.Test;

/**
 * Tests for TopBar — focus on the secToBarBeatSub / barBeatSubToSec conversion logic and
 * updatePosition.
 */
public class TopBarTest {

  private TopBar createTopBar() {
    return new TopBar();
  }

  // ── secToBarBeatSub ───────────────────────────────────────────

  @Test
  public void testSecToBarBeatSub_origin() {
    TopBar bar = createTopBar();
    assertEquals("1. 1. 1", bar.secToBarBeatSub(0.0f, 120.0f));
  }

  @Test
  public void testSecToBarBeatSub_oneBeatAt120() {
    TopBar bar = createTopBar();
    // At 120 BPM, 1 beat = 0.5 sec
    String result = bar.secToBarBeatSub(0.5f, 120.0f);
    assertEquals("1. 2. 1", result);
  }

  @Test
  public void testSecToBarBeatSub_oneBarAt120() {
    TopBar bar = createTopBar();
    // At 120 BPM, 4 beats = 2.0 sec (one bar in 4/4)
    String result = bar.secToBarBeatSub(2.0f, 120.0f);
    assertEquals("2. 1. 1", result);
  }

  @Test
  public void testSecToBarBeatSub_zeroBpm() {
    TopBar bar = createTopBar();
    assertEquals("1. 1. 1", bar.secToBarBeatSub(5.0f, 0.0f));
  }

  @Test
  public void testSecToBarBeatSub_negativeBpm() {
    TopBar bar = createTopBar();
    assertEquals("1. 1. 1", bar.secToBarBeatSub(5.0f, -10.0f));
  }

  @Test
  public void testSecToBarBeatSub_at140bpm() {
    TopBar bar = createTopBar();
    // At 140 BPM, beat = 60/140 sec. 4 beats = 240/140 = ~1.714 sec
    float barDuration = 4.0f * 60.0f / 140.0f;
    String result = bar.secToBarBeatSub(barDuration, 140.0f);
    assertEquals("2. 1. 1", result);
  }

  @Test
  public void testSecToBarBeatSub_fiveBars() {
    TopBar bar = createTopBar();
    // 5 bars at 120 BPM = 5 * 4 beats * 0.5s = 10s
    String result = bar.secToBarBeatSub(10.0f, 120.0f);
    assertEquals("6. 1. 1", result);
  }

  // ── barBeatSubToSec ───────────────────────────────────────────

  @Test
  public void testBarBeatSubToSec_origin() {
    TopBar bar = createTopBar();
    float sec = bar.barBeatSubToSec("1.1.1", 120.0f);
    assertEquals(0.0f, sec, 0.001f);
  }

  @Test
  public void testBarBeatSubToSec_secondBeat() {
    TopBar bar = createTopBar();
    float sec = bar.barBeatSubToSec("1.2.1", 120.0f);
    // At 120 BPM, 1 beat = 0.5s
    assertEquals(0.5f, sec, 0.001f);
  }

  @Test
  public void testBarBeatSubToSec_secondBar() {
    TopBar bar = createTopBar();
    float sec = bar.barBeatSubToSec("2.1.1", 120.0f);
    // At 120 BPM, 1 bar = 4 beats * 0.5s = 2.0s
    assertEquals(2.0f, sec, 0.001f);
  }

  @Test
  public void testBarBeatSubToSec_withSpaces() {
    TopBar bar = createTopBar();
    float sec = bar.barBeatSubToSec("2. 1. 1", 120.0f);
    assertEquals(2.0f, sec, 0.001f);
  }

  @Test
  public void testBarBeatSubToSec_zeroBpmReturnsNeg1() {
    TopBar bar = createTopBar();
    float sec = bar.barBeatSubToSec("1.1.1", 0.0f);
    assertEquals(-1.0f, sec, 0.001f);
  }

  @Test
  public void testBarBeatSubToSec_invalidTextReturnsNeg1() {
    TopBar bar = createTopBar();
    assertEquals(-1.0f, bar.barBeatSubToSec("abc", 120.0f), 0.001f);
    assertEquals(-1.0f, bar.barBeatSubToSec("", 120.0f), 0.001f);
    assertEquals(-1.0f, bar.barBeatSubToSec("1", 120.0f), 0.001f);
  }

  @Test
  public void testBarBeatSubToSec_nonNumericReturnsNeg1() {
    TopBar bar = createTopBar();
    assertEquals(-1.0f, bar.barBeatSubToSec("a.b.c", 120.0f), 0.001f);
  }

  @Test
  public void testBarBeatSubToSec_twoPartsOnly() {
    TopBar bar = createTopBar();
    // "1.1" → bar=1, beat=1, sub defaults to 0
    float sec = bar.barBeatSubToSec("1.1", 120.0f);
    assertEquals(0.0f, sec, 0.001f);
  }

  @Test
  public void testBarBeatSubToSec_roundTrip() {
    TopBar bar = createTopBar();
    // Convert to string and back
    float original = 3.5f;
    String str = bar.secToBarBeatSub(original, 120.0f);
    float recovered = bar.barBeatSubToSec(str, 120.0f);
    assertEquals(original, recovered, 0.01f);
  }

  // ── Singleton ─────────────────────────────────────────────────

  @Test
  public void testGetInstance() {
    TopBar bar = createTopBar();
    assertSame(bar, TopBar.getInstance());
  }

  // ── getBeatsPerBar ────────────────────────────────────────────

  @Test
  public void testGetBeatsPerBar_default() {
    TopBar bar = createTopBar();
    assertEquals(4, bar.getBeatsPerBar());
  }

  // ── VirtualKeyboard ───────────────────────────────────────────

  @Test
  public void testGetVirtualKeyboard() {
    TopBar bar = createTopBar();
    assertNotNull(bar.getVirtualKeyboard());
  }

  // ── updatePosition ────────────────────────────────────────────

  @Test
  public void testUpdatePosition_noThrow() {
    TopBar bar = createTopBar();
    // Should not throw with various values
    bar.updatePosition(0.0f, 120.0f, false, 0, 0);
    bar.updatePosition(5.0f, 140.0f, true, 2.0f, 8.0f);
    bar.updatePosition(100.0f, 60.0f, false, 0, 0);
  }

  // ── Listener interfaces ───────────────────────────────────────

  @Test
  public void testSetViewToggleListener() {
    TopBar bar = createTopBar();
    final boolean[] called = {false};
    bar.setViewToggleListener(isTimeline -> called[0] = true);
    // Just verify the setter works — can't easily trigger the callback without clicking
    assertFalse(called[0]);
  }

  @Test
  public void testSetReplToggleListener() {
    TopBar bar = createTopBar();
    final boolean[] called = {false};
    bar.setReplToggleListener(() -> called[0] = true);
    assertFalse(called[0]);
  }
}
