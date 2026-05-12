package hibiki.ui;

import static org.junit.Assert.*;

import hibiki.ui.panels.*;
import hibiki.ui.panels.devices.*;
import org.junit.Test;

public class FilmDevicePanelTest {

  /**
   * Verify that handleParamChange() updates the KnobPanel's rendered value, not just the params[]
   * array. Regression: DX7 preset loading would update params[] but knobs would still show
   * defaults.
   */
  @Test
  public void testHandleParamChangeSyncsKnobValues() {
    FilmDevicePanel panel = new FilmDevicePanel(0, 0);

    // Operator 0 level (paramId = OP_LEVEL = 5) defaults to 1.0
    int levelParamId = FilmDevicePanel.OP_LEVEL;
    double defaultLevel = panel.getParamValue(levelParamId);
    assertEquals("Default level should be 1.0", 1.0, defaultLevel, 0.01);

    // Simulate engine pushing a DX7-imported value.
    double newLevel = 0.42;
    panel.handleParamChange(levelParamId, newLevel);

    // The param array should be updated.
    assertEquals(
        "params[] should reflect new value", newLevel, panel.getParamValue(levelParamId), 0.001);

    // The knob's rendered value should ALSO be updated.
    double knobValue = panel.getKnobValue(levelParamId);
    assertEquals("Knob rendered value should match new param value", newLevel, knobValue, 0.001);
  }

  /** Verify that matrix knob values also sync via handleParamChange. */
  @Test
  public void testHandleParamChangeSyncsMatrixKnobs() {
    FilmDevicePanel panel = new FilmDevicePanel(0, 0);

    // Matrix params start after op + filter + global params.
    // First matrix cell = matrixIdx(0, 0).
    int matrixStart =
        FilmDevicePanel.PARAMS_PER_OP * FilmDevicePanel.NUM_OPS
            + FilmDevicePanel.PARAMS_PER_FILTER * FilmDevicePanel.NUM_FILTERS
            + FilmDevicePanel.NUM_GLOBAL;
    double newVal = 0.75;
    panel.handleParamChange(matrixStart, newVal);

    assertEquals("Matrix param should be updated", newVal, panel.getParamValue(matrixStart), 0.001);
    double knobValue = panel.getKnobValue(matrixStart);
    assertEquals("Matrix knob value should sync", newVal, knobValue, 0.001);
  }
}
