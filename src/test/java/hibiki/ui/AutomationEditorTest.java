package hibiki.ui;

import static org.junit.Assert.*;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import org.junit.Test;

/**
 * Comprehensive tests for automation system logic — interpolation curves, coordinate conversions,
 * point management, and data model behavior.
 */
public class AutomationEditorTest {

  // ═══════════════════════════════════════════════════════════════════════
  // AutoPoint data model
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testAutoPointConstructor() {
    AutomationEditor.AutoPoint p = new AutomationEditor.AutoPoint(4.0f, 0.75f, 0.5f);
    assertEquals(4.0f, p.timeBeats, 0.001f);
    assertEquals(0.75f, p.value, 0.001f);
    assertEquals(0.5f, p.tension, 0.001f);
  }

  @Test
  public void testAutoPointMutation() {
    AutomationEditor.AutoPoint p = new AutomationEditor.AutoPoint(0, 0, 0);
    p.timeBeats = 8.0f;
    p.value = 1.0f;
    p.tension = -0.8f;
    assertEquals(8.0f, p.timeBeats, 0.001f);
    assertEquals(1.0f, p.value, 0.001f);
    assertEquals(-0.8f, p.tension, 0.001f);
  }

  @Test
  public void testAutoPointSorting() {
    List<AutomationEditor.AutoPoint> points = new ArrayList<>();
    points.add(new AutomationEditor.AutoPoint(8.0f, 0.5f, 0));
    points.add(new AutomationEditor.AutoPoint(2.0f, 1.0f, 0));
    points.add(new AutomationEditor.AutoPoint(0.0f, 0.0f, 0));
    points.add(new AutomationEditor.AutoPoint(4.0f, 0.75f, 0));
    points.sort(Comparator.comparingDouble(a -> a.timeBeats));

    assertEquals(0.0f, points.get(0).timeBeats, 0.001f);
    assertEquals(2.0f, points.get(1).timeBeats, 0.001f);
    assertEquals(4.0f, points.get(2).timeBeats, 0.001f);
    assertEquals(8.0f, points.get(3).timeBeats, 0.001f);
  }

  @Test
  public void testAutoPointSortStability() {
    // Two points at same time — sort should be stable
    List<AutomationEditor.AutoPoint> points = new ArrayList<>();
    AutomationEditor.AutoPoint a = new AutomationEditor.AutoPoint(4.0f, 0.2f, 0);
    AutomationEditor.AutoPoint b = new AutomationEditor.AutoPoint(4.0f, 0.8f, 0);
    points.add(a);
    points.add(b);
    points.sort(Comparator.comparingDouble(p -> p.timeBeats));
    // Both at same time, order should be preserved (stable sort)
    assertSame(a, points.get(0));
    assertSame(b, points.get(1));
  }

  @Test
  public void testAutoPointBoundaryValues() {
    // Value range 0-1, tension -1..1
    AutomationEditor.AutoPoint p0 = new AutomationEditor.AutoPoint(0, 0.0f, -1.0f);
    AutomationEditor.AutoPoint p1 = new AutomationEditor.AutoPoint(Float.MAX_VALUE, 1.0f, 1.0f);
    assertEquals(0.0f, p0.value, 0.001f);
    assertEquals(1.0f, p1.value, 0.001f);
    assertEquals(-1.0f, p0.tension, 0.001f);
    assertEquals(1.0f, p1.tension, 0.001f);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // Interpolation logic (via reflection — must match C++ engine)
  // ═══════════════════════════════════════════════════════════════════════

  /** Helper to invoke the private interpolate method via reflection. */
  private float interpolate(float v0, float v1, float t, float tension) throws Exception {
    Method m =
        AutomationEditor.class.getDeclaredMethod(
            "interpolate", float.class, float.class, float.class, float.class);
    m.setAccessible(true);
    return (float) m.invoke(null, v0, v1, t, tension);
  }

  @Test
  public void testInterpolate_linear_midpoint() throws Exception {
    // tension=0 → linear: midpoint should be 0.5
    float result = interpolate(0.0f, 1.0f, 0.5f, 0.0f);
    assertEquals(0.5f, result, 0.001f);
  }

  @Test
  public void testInterpolate_linear_various() throws Exception {
    assertEquals(0.0f, interpolate(0.0f, 1.0f, 0.0f, 0.0f), 0.001f);
    assertEquals(0.25f, interpolate(0.0f, 1.0f, 0.25f, 0.0f), 0.001f);
    assertEquals(0.75f, interpolate(0.0f, 1.0f, 0.75f, 0.0f), 0.001f);
    assertEquals(1.0f, interpolate(0.0f, 1.0f, 1.0f, 0.0f), 0.001f);
  }

  @Test
  public void testInterpolate_linear_nonUnitRange() throws Exception {
    // v0=0.2, v1=0.8, t=0.5, tension=0 → 0.5
    float result = interpolate(0.2f, 0.8f, 0.5f, 0.0f);
    assertEquals(0.5f, result, 0.001f);
  }

  @Test
  public void testInterpolate_easeIn_positive_tension() throws Exception {
    // Positive tension → ease-in (slow start, fast finish)
    // With tension=1.0: exponent=2^1=2, curved_t=0.5^2=0.25, result=0.25
    float result = interpolate(0.0f, 1.0f, 0.5f, 1.0f);
    assertEquals(0.25f, result, 0.001f);
  }

  @Test
  public void testInterpolate_easeOut_negative_tension() throws Exception {
    // Negative tension → ease-out (fast start, slow finish)
    // With tension=-1.0: exponent=2^(-1)=0.5, curved_t=0.5^0.5=sqrt(0.5)≈0.707
    float result = interpolate(0.0f, 1.0f, 0.5f, -1.0f);
    assertEquals((float) Math.sqrt(0.5), result, 0.001f);
  }

  @Test
  public void testInterpolate_strongEaseIn() throws Exception {
    // tension=2.0 → exponent=4, curved_t=0.5^4=0.0625
    float result = interpolate(0.0f, 1.0f, 0.5f, 2.0f);
    assertEquals(0.0625f, result, 0.001f);
  }

  @Test
  public void testInterpolate_boundaries() throws Exception {
    // t <= 0 → returns v0
    assertEquals(0.0f, interpolate(0.0f, 1.0f, 0.0f, 0.5f), 0.001f);
    assertEquals(0.0f, interpolate(0.0f, 1.0f, -0.1f, 0.5f), 0.001f);
    // t >= 1 → returns v1
    assertEquals(1.0f, interpolate(0.0f, 1.0f, 1.0f, 0.5f), 0.001f);
    assertEquals(1.0f, interpolate(0.0f, 1.0f, 1.5f, 0.5f), 0.001f);
  }

  @Test
  public void testInterpolate_sameValues() throws Exception {
    // v0 == v1 → always returns the same value regardless of t/tension
    assertEquals(0.5f, interpolate(0.5f, 0.5f, 0.0f, 1.0f), 0.001f);
    assertEquals(0.5f, interpolate(0.5f, 0.5f, 0.5f, -1.0f), 0.001f);
    assertEquals(0.5f, interpolate(0.5f, 0.5f, 1.0f, 0.0f), 0.001f);
  }

  @Test
  public void testInterpolate_reverseRange() throws Exception {
    // v0 > v1 (descending) with linear
    assertEquals(0.5f, interpolate(1.0f, 0.0f, 0.5f, 0.0f), 0.001f);
    assertEquals(0.75f, interpolate(1.0f, 0.0f, 0.25f, 0.0f), 0.001f);
  }

  @Test
  public void testInterpolate_symmetry() throws Exception {
    // Linear interpolation with tension=0 should be symmetric
    float fwd = interpolate(0.0f, 1.0f, 0.3f, 0.0f);
    float rev = interpolate(1.0f, 0.0f, 0.7f, 0.0f);
    assertEquals(fwd, rev, 0.001f);
  }

  @Test
  public void testInterpolate_matchesCppFormula() throws Exception {
    // Verify the exact formula: curved_t = pow(t, pow(2, tension))
    // tension=0.5, t=0.6: exponent=2^0.5≈1.4142, curved_t=0.6^1.4142
    float tension = 0.5f;
    float t = 0.6f;
    float exponent = (float) Math.pow(2.0, tension);
    float expected = (float) Math.pow(t, exponent);
    float result = interpolate(0.0f, 1.0f, t, tension);
    assertEquals(expected, result, 0.0001f);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // AutomationEditor construction and setters
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testEditorConstruction() {
    AutomationEditor editor = new AutomationEditor(0, 0, 0, 42, "Cutoff");
    assertNotNull(editor);
    assertEquals(new java.awt.Dimension(800, 60), editor.getPreferredSize());
  }

  @Test
  public void testSetLaneData() {
    AutomationEditor editor = new AutomationEditor(0, 0, 0, 0, "");
    List<AutomationEditor.AutoPoint> pts = new ArrayList<>();
    pts.add(new AutomationEditor.AutoPoint(0, 0.0f, 0));
    pts.add(new AutomationEditor.AutoPoint(4, 1.0f, 0.5f));
    pts.add(new AutomationEditor.AutoPoint(8, 0.5f, -0.3f));
    editor.setLaneData(1, 2, 99, "Resonance", pts);
    // Should not throw; data is stored internally
  }

  @Test
  public void testSetViewParams() {
    AutomationEditor editor = new AutomationEditor(0, 0, 0, 0, "");
    editor.setViewParams(80.0f, 2.0f, 150);
    // Should not throw
  }

  @Test
  public void testSetLaneData_emptyPoints() {
    AutomationEditor editor = new AutomationEditor(0, 0, 0, 0, "");
    editor.setLaneData(0, 0, 0, "Empty", new ArrayList<>());
  }

  @Test
  public void testSetLaneData_replacesExisting() {
    AutomationEditor editor = new AutomationEditor(0, 0, 0, 0, "");
    List<AutomationEditor.AutoPoint> pts1 = new ArrayList<>();
    pts1.add(new AutomationEditor.AutoPoint(0, 0.5f, 0));
    editor.setLaneData(0, 0, 0, "First", pts1);

    // Replace with different data
    List<AutomationEditor.AutoPoint> pts2 = new ArrayList<>();
    pts2.add(new AutomationEditor.AutoPoint(2, 0.8f, 0.5f));
    pts2.add(new AutomationEditor.AutoPoint(6, 0.1f, -0.5f));
    editor.setLaneData(1, 1, 1, "Second", pts2);
    // Should not throw
  }

  // ═══════════════════════════════════════════════════════════════════════
  // AutomationEditor coordinate conversions (via reflection)
  // ═══════════════════════════════════════════════════════════════════════

  private AutomationEditor createEditorWithSize(int w, int h) {
    AutomationEditor editor = new AutomationEditor(0, 0, 0, 0, "Test");
    editor.setSize(w, h);
    return editor;
  }

  @Test
  public void testBeatToX_roundTrip() throws Exception {
    AutomationEditor editor = createEditorWithSize(800, 100);
    editor.setViewParams(40.0f, 0.0f, 120);

    Method beatToX = AutomationEditor.class.getDeclaredMethod("beatToX", float.class);
    Method xToBeat = AutomationEditor.class.getDeclaredMethod("xToBeat", float.class);
    beatToX.setAccessible(true);
    xToBeat.setAccessible(true);

    // Forward: beat 0 → x = headerWidth (120)
    float x0 = (float) beatToX.invoke(editor, 0.0f);
    assertEquals(120.0f, x0, 0.001f);

    // Forward: beat 4 → x = 120 + 4*40 = 280
    float x4 = (float) beatToX.invoke(editor, 4.0f);
    assertEquals(280.0f, x4, 0.001f);

    // Roundtrip
    float beat = (float) xToBeat.invoke(editor, x4);
    assertEquals(4.0f, beat, 0.001f);
  }

  @Test
  public void testBeatToX_withScroll() throws Exception {
    AutomationEditor editor = createEditorWithSize(800, 100);
    editor.setViewParams(40.0f, 2.0f, 120);

    Method beatToX = AutomationEditor.class.getDeclaredMethod("beatToX", float.class);
    beatToX.setAccessible(true);

    // Beat 2 with scrollOffset 2 → x = 120 + (2-2)*40 = 120
    float x = (float) beatToX.invoke(editor, 2.0f);
    assertEquals(120.0f, x, 0.001f);

    // Beat 6 with scrollOffset 2 → x = 120 + (6-2)*40 = 280
    float x6 = (float) beatToX.invoke(editor, 6.0f);
    assertEquals(280.0f, x6, 0.001f);
  }

  @Test
  public void testValueToY_roundTrip() throws Exception {
    AutomationEditor editor = createEditorWithSize(800, 100);

    Method valueToY = AutomationEditor.class.getDeclaredMethod("valueToY", float.class);
    Method yToValue = AutomationEditor.class.getDeclaredMethod("yToValue", float.class);
    valueToY.setAccessible(true);
    yToValue.setAccessible(true);

    // Value 0.0 → bottom (y near height)
    float y0 = (float) valueToY.invoke(editor, 0.0f);
    float y1 = (float) valueToY.invoke(editor, 1.0f);
    assertTrue("Value 0 should be below value 1", y0 > y1);

    // Value 0.5 → middle
    float yMid = (float) valueToY.invoke(editor, 0.5f);
    assertEquals((y0 + y1) / 2.0f, yMid, 1.0f);

    // Roundtrip
    float recovered = (float) yToValue.invoke(editor, yMid);
    assertEquals(0.5f, recovered, 0.01f);
  }

  @Test
  public void testYToValue_clamped() throws Exception {
    AutomationEditor editor = createEditorWithSize(800, 100);

    Method yToValue = AutomationEditor.class.getDeclaredMethod("yToValue", float.class);
    yToValue.setAccessible(true);

    // Very large Y (below panel) → clamped to 0
    float vLow = (float) yToValue.invoke(editor, 9999.0f);
    assertEquals(0.0f, vLow, 0.001f);

    // Very small Y (above panel) → clamped to 1
    float vHigh = (float) yToValue.invoke(editor, -9999.0f);
    assertEquals(1.0f, vHigh, 0.001f);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // AutomationMouseHandler state management
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testHandlerConstruction() {
    TimelineView view = new TimelineView();
    AutomationMouseHandler handler = new AutomationMouseHandler(view);
    assertFalse(handler.isEditing());
  }

  @Test
  public void testHandlerNotEditing_initialState() {
    TimelineView view = new TimelineView();
    AutomationMouseHandler handler = new AutomationMouseHandler(view);
    assertEquals(-1, handler.autoTrackIdx);
    assertEquals(-1, handler.autoLaneIdx);
    assertEquals(-1, handler.autoClipIdx);
  }

  @Test
  public void testFindAutomationLaneAtY_noLanes() {
    TimelineView view = new TimelineView();
    AutomationMouseHandler handler = new AutomationMouseHandler(view);
    // Default tracks have no automation expanded
    assertEquals(-1, handler.findAutomationLaneAtY(0, 100));
  }

  @Test
  public void testFindAutomationLaneAtY_invalidTrack() {
    TimelineView view = new TimelineView();
    AutomationMouseHandler handler = new AutomationMouseHandler(view);
    assertEquals(-1, handler.findAutomationLaneAtY(-1, 100));
    assertEquals(-1, handler.findAutomationLaneAtY(999, 100));
  }

  @Test
  public void testFindAutomationLaneAtY_withExpandedLane() {
    TimelineView view = new TimelineView();
    AutomationMouseHandler handler = new AutomationMouseHandler(view);

    // Set up a track with automation expanded
    TimelineView.TrackTimeline track = view.tracks.get(0);
    track.automationExpanded = true;
    TimelineView.AutomationLaneData lane = new TimelineView.AutomationLaneData();
    lane.laneIndex = 0;
    lane.paramName = "Volume";
    track.automationLanes.add(lane);

    // Y position in automation area depends on theme scaling
    int scaleTimeRuler = Theme.getInstance().scale(TimelineView.TIME_RULER_HEIGHT);
    int baseTrackH = Theme.getInstance().scale(view.getBaseTrackHeight());
    int autoLaneH = Theme.getInstance().scale(view.getAutomationLaneHeight());
    int autoStartY = scaleTimeRuler + baseTrackH;

    // Click in the automation lane area
    int laneIdx = handler.findAutomationLaneAtY(0, autoStartY + autoLaneH / 2);
    assertEquals(0, laneIdx);
  }

  @Test
  public void testHandleRelease_notEditing_returnsFalse() {
    TimelineView view = new TimelineView();
    AutomationMouseHandler handler = new AutomationMouseHandler(view);
    boolean consumed = handler.handleRelease(null);
    assertFalse(consumed);
  }

  @Test
  public void testHandleDrag_notEditing_returnsFalse() {
    TimelineView view = new TimelineView();
    AutomationMouseHandler handler = new AutomationMouseHandler(view);
    boolean consumed = handler.handleDrag(null);
    assertFalse(consumed);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // AutomationLaneData model
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testAutomationLaneDataDefaults() {
    TimelineView.AutomationLaneData lane = new TimelineView.AutomationLaneData();
    assertEquals(0, lane.laneIndex);
    assertNotNull(lane.paramName);
    assertNotNull(lane.clips);
    assertTrue(lane.clips.isEmpty());
  }

  @Test
  public void testAutomationLaneDataWithClips() {
    TimelineView.AutomationLaneData lane = new TimelineView.AutomationLaneData();
    lane.laneIndex = 2;
    lane.paramName = "Cutoff";

    TimelineView.ClipRect clip = new TimelineView.ClipRect();
    clip.startTime = 0;
    clip.duration = 4;
    clip.name = "Auto 1";
    clip.isAutomation = true;
    clip.automationPoints = new ArrayList<>();
    clip.automationPoints.add(new AutomationEditor.AutoPoint(0, 0.0f, 0));
    clip.automationPoints.add(new AutomationEditor.AutoPoint(2, 1.0f, 0.5f));
    clip.automationPoints.add(new AutomationEditor.AutoPoint(4, 0.3f, -0.2f));
    lane.clips.add(clip);

    assertEquals(1, lane.clips.size());
    assertEquals(3, lane.clips.get(0).automationPoints.size());
    assertEquals("Cutoff", lane.paramName);
  }

  // ═══════════════════════════════════════════════════════════════════════
  // Off-screen rendering of AutomationEditor (regression)
  // ═══════════════════════════════════════════════════════════════════════

  @Test
  public void testPaintWithNoPoints() {
    AutomationEditor editor = createEditorWithSize(800, 100);
    java.awt.image.BufferedImage img =
        new java.awt.image.BufferedImage(800, 100, java.awt.image.BufferedImage.TYPE_INT_ARGB);
    editor.paint(img.getGraphics());
    // Should not throw
  }

  @Test
  public void testPaintWithOnePoint() {
    AutomationEditor editor = createEditorWithSize(800, 100);
    List<AutomationEditor.AutoPoint> pts = new ArrayList<>();
    pts.add(new AutomationEditor.AutoPoint(4, 0.5f, 0));
    editor.setLaneData(0, 0, 0, "Test", pts);
    java.awt.image.BufferedImage img =
        new java.awt.image.BufferedImage(800, 100, java.awt.image.BufferedImage.TYPE_INT_ARGB);
    editor.paint(img.getGraphics());
  }

  @Test
  public void testPaintWithMultiplePoints_variousTensions() {
    AutomationEditor editor = createEditorWithSize(800, 100);
    List<AutomationEditor.AutoPoint> pts = new ArrayList<>();
    pts.add(new AutomationEditor.AutoPoint(0, 0.0f, 0));
    pts.add(new AutomationEditor.AutoPoint(2, 0.8f, 0.5f));
    pts.add(new AutomationEditor.AutoPoint(4, 0.2f, -0.8f));
    pts.add(new AutomationEditor.AutoPoint(6, 1.0f, 0));
    pts.add(new AutomationEditor.AutoPoint(8, 0.5f, 0.3f));
    editor.setLaneData(0, 0, 0, "Multi", pts);
    java.awt.image.BufferedImage img =
        new java.awt.image.BufferedImage(800, 100, java.awt.image.BufferedImage.TYPE_INT_ARGB);
    editor.paint(img.getGraphics());
  }

  @Test
  public void testPaintWithScrollOffset() {
    AutomationEditor editor = createEditorWithSize(800, 100);
    editor.setViewParams(60.0f, 4.0f, 100);
    List<AutomationEditor.AutoPoint> pts = new ArrayList<>();
    pts.add(new AutomationEditor.AutoPoint(0, 0.0f, 0));
    pts.add(new AutomationEditor.AutoPoint(8, 1.0f, 0));
    editor.setLaneData(0, 0, 0, "Scrolled", pts);
    java.awt.image.BufferedImage img =
        new java.awt.image.BufferedImage(800, 100, java.awt.image.BufferedImage.TYPE_INT_ARGB);
    editor.paint(img.getGraphics());
  }
}
