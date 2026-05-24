package hibiki.ui.panels.devices;

import static org.junit.Assert.*;

import hibiki.BackendManager;
import hibiki.IpcClient;
import hibiki.pb.commands.*;
import hibiki.pb.core.ParamInfo;
import hibiki.pb.notifications.DrumPadNotification;
import java.awt.event.ActionEvent;
import java.util.ArrayList;
import java.util.List;
import javax.swing.JButton;
import org.junit.Before;
import org.junit.Test;

public class DrumMachineDevicePanelTest {

  private java.io.ByteArrayOutputStream interceptedBytes;

  @Before
  public void setUp() throws Exception {
    interceptedBytes = new java.io.ByteArrayOutputStream();
    java.io.DataOutputStream outStream = new java.io.DataOutputStream(interceptedBytes);

    // Get IpcClient from BackendManager via reflection
    java.lang.reflect.Field ipcClientField = BackendManager.class.getDeclaredField("ipcClient");
    ipcClientField.setAccessible(true);
    IpcClient ipcClient = (IpcClient) ipcClientField.get(BackendManager.getInstance());

    // Set 'out' field on IpcClient
    java.lang.reflect.Field outField = IpcClient.class.getDeclaredField("out");
    outField.setAccessible(true);
    outField.set(ipcClient, outStream);
  }

  private Request getLatestRequest() throws Exception {
    List<Request> reqs = getAllRequests();
    for (int i = reqs.size() - 1; i >= 0; i--) {
      Request r = reqs.get(i);
      if (!r.hasSendVirtualMidi()) {
        return r;
      }
    }
    return null;
  }

  private List<Request> getAllRequests() throws Exception {
    List<Request> list = new ArrayList<>();
    byte[] bytes = interceptedBytes.toByteArray();
    if (bytes.length == 0) return list;
    java.io.DataInputStream dis =
        new java.io.DataInputStream(new java.io.ByteArrayInputStream(bytes));
    while (dis.available() > 0) {
      int size = Integer.reverseBytes(dis.readInt());
      byte[] payload = new byte[size];
      dis.readFully(payload);
      list.add(Request.parseFrom(payload));
    }
    return list;
  }

  private void clearRequestLog() {
    interceptedBytes.reset();
  }

  @Test
  public void testInitialization() {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(1, 2);
    assertNotNull(panel);
    assertEquals(1, panel.trackIndex);
    assertEquals(2, panel.pluginIndex);
    assertEquals(0, panel.currentBank);
    assertEquals(0, panel.selectedPad);

    // Check that we have 16 pad buttons and 64 pads cached
    assertEquals(16, panel.padButtons.length);
    for (int i = 0; i < 16; i++) {
      assertNotNull(panel.padButtons[i]);
    }
    assertEquals(64, panel.pads.length);
  }

  @Test
  public void testPadSelectionAndTrigger() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // Click Pad 2 (button index 1)
    JButton btn2 = panel.padButtons[1];
    assertNotNull(btn2);

    // Click it (triggers action listener)
    btn2.getActionListeners()[0].actionPerformed(
        new ActionEvent(btn2, ActionEvent.ACTION_PERFORMED, ""));

    // selectedPad should change to 1 (pad 2)
    assertEquals(1, panel.selectedPad);

    // Should trigger note-on and note-off requests
    List<Request> reqs = getAllRequests();
    assertTrue(reqs.size() > 0);

    // Check first request is SendVirtualMidi for note 37 (36 + 1)
    Request req = reqs.get(0);
    assertTrue(req.hasSendVirtualMidi());
    assertEquals(37, req.getSendVirtualMidi().getNote());
    assertTrue(req.getSendVirtualMidi().getNoteOn());
  }

  @Test
  public void testMixerControls() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // Test volume slider change
    panel.padVolSlider.setValue(80);
    Request vReq = getLatestRequest();
    assertNotNull(vReq);
    assertTrue(vReq.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_SET_VOLUME, vReq.getDrumPad().getAction());
    assertEquals(0.8f, vReq.getDrumPad().getParamValue(), 1e-4);
    clearRequestLog();

    // Test pan slider change
    panel.padPanSlider.setValue(25); // 25 / 50 = 0.5
    Request pReq = getLatestRequest();
    assertNotNull(pReq);
    assertTrue(pReq.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_SET_PAN, pReq.getDrumPad().getAction());
    assertEquals(0.5f, pReq.getDrumPad().getParamValue(), 1e-4);
    clearRequestLog();

    // Test mute toggle
    panel.padMuteBtn.setSelected(true);
    panel.padMuteBtn.getActionListeners()[0].actionPerformed(
        new ActionEvent(panel.padMuteBtn, ActionEvent.ACTION_PERFORMED, ""));
    Request mReq = getLatestRequest();
    assertNotNull(mReq);
    assertTrue(mReq.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_SET_MUTE, mReq.getDrumPad().getAction());
    assertEquals(1.0f, mReq.getDrumPad().getParamValue(), 1e-4);
    clearRequestLog();

    // Test solo toggle
    panel.padSoloBtn.setSelected(true);
    panel.padSoloBtn.getActionListeners()[0].actionPerformed(
        new ActionEvent(panel.padSoloBtn, ActionEvent.ACTION_PERFORMED, ""));
    Request sReq = getLatestRequest();
    assertNotNull(sReq);
    assertTrue(sReq.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_SET_SOLO, sReq.getDrumPad().getAction());
    assertEquals(1.0f, sReq.getDrumPad().getParamValue(), 1e-4);
  }

  @Test
  public void testInstrumentDropdown() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // Change combo selection to "Sampler" (index 1)
    panel.pluginCombo.setSelectedIndex(1);
    Request r1 = getLatestRequest();
    assertNotNull(r1);
    assertTrue(r1.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, r1.getDrumPad().getAction());
    assertEquals("builtin://sampler", r1.getDrumPad().getPluginPath());
    clearRequestLog();

    // Change to "3xOsc" (index 2)
    panel.pluginCombo.setSelectedIndex(2);
    Request r2 = getLatestRequest();
    assertNotNull(r2);
    assertTrue(r2.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, r2.getDrumPad().getAction());
    assertEquals("builtin://3xosc", r2.getDrumPad().getPluginPath());
    clearRequestLog();

    // Change to "Empty" (index 0)
    panel.pluginCombo.setSelectedIndex(0);
    Request r0 = getLatestRequest();
    assertNotNull(r0);
    assertTrue(r0.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_REMOVE_PLUGIN, r0.getDrumPad().getAction());
  }

  @Test
  public void testNotificationHandling() {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);

    // Send TYPE_PAD_STATE notification for pad 5
    DrumPadNotification notif =
        DrumPadNotification.newBuilder()
            .setType(DrumPadNotification.Type.TYPE_PAD_STATE)
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPadIndex(5)
            .setPluginPath("builtin://sampler")
            .setVolume(0.7f)
            .setPan(-0.3f)
            .setMute(true)
            .setSolo(false)
            .setSampleName("snare.wav")
            .addParams(ParamInfo.newBuilder().setId(10).setName("Pitch").setCurrentValue(0.2f))
            .setTriggerNote(41)
            .build();

    panel.handlePadNotification(notif);

    // Verify local cache state updated
    assertEquals("builtin://sampler", panel.pads[5].pluginPath);
    assertEquals(0.7f, panel.pads[5].volume, 1e-4);
    assertEquals(-0.3f, panel.pads[5].pan, 1e-4);
    assertTrue(panel.pads[5].mute);
    assertFalse(panel.pads[5].solo);
    assertEquals("snare.wav", panel.pads[5].sampleName);
    assertEquals(1, panel.pads[5].params.size());
    assertEquals("Pitch", panel.pads[5].params.get(0).getName());
    assertEquals(0.2f, panel.pads[5].params.get(0).getCurrentValue(), 1e-4);

    // Since pad 5 is not selected (currently 0), visual editor should NOT show its values yet
    assertNotEquals("Selected: Pad 6 (F2)", panel.selectedPadLabel.getText());

    // Select pad 5
    panel.selectedPad = 5;
    panel.handlePadNotification(notif); // will refresh editor

    assertEquals("Selected: Pad 6 (F2)", panel.selectedPadLabel.getText());
    assertEquals("Sampler", panel.pluginCombo.getSelectedItem());
    assertEquals(70, panel.padVolSlider.getValue());
    assertEquals(-15, panel.padPanSlider.getValue()); // -0.3 * 50 = -15
    assertTrue(panel.padMuteBtn.isSelected());
    assertFalse(panel.padSoloBtn.isSelected());
    assertTrue(panel.samplerLoadPanel.isVisible());
    assertEquals("snare.wav", panel.sampleNameLabel.getText());
  }

  @Test
  public void testBankSwitching() {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);

    // Switch to Bank B (index 1)
    panel.currentBank = 1;

    // In Bank B, button index 0 corresponds to pad index 16
    JButton btn0 = panel.padButtons[0];
    btn0.getActionListeners()[0].actionPerformed(
        new ActionEvent(btn0, ActionEvent.ACTION_PERFORMED, ""));

    assertEquals(16, panel.selectedPad);
  }

  @Test
  public void testTriggerNoteControls() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // Change trigger note spinner to 72 (C5)
    panel.padTriggerNoteSpinner.setValue(72);
    // Since change listener is triggered, verify the request is sent
    Request req = getLatestRequest();
    assertNotNull(req);
    assertTrue(req.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_SET_TRIGGER_NOTE, req.getDrumPad().getAction());
    assertEquals(72, req.getDrumPad().getTriggerNote());
    assertEquals("C5", panel.padTriggerNoteLabel.getText());
  }

  @Test
  public void testDropdownInstrumentOptions() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);

    // Check that model contains other instruments
    javax.swing.ComboBoxModel<String> model = panel.pluginCombo.getModel();

    // Check list of options
    List<String> items = new ArrayList<>();
    for (int i = 0; i < model.getSize(); i++) {
      items.add(model.getElementAt(i));
    }

    assertTrue(items.contains("DR8 Kick"));
    assertTrue(items.contains("Film"));
    assertTrue(items.contains("Acid Bass"));
    assertTrue(items.contains("Organ"));
    assertTrue(items.contains("Load VST3..."));

    // Select "DR8 Kick"
    clearRequestLog();
    panel.pluginCombo.setSelectedItem("DR8 Kick");
    Request r1 = getLatestRequest();
    assertNotNull(r1);
    assertTrue(r1.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, r1.getDrumPad().getAction());
    assertEquals("builtin://dr8_kick", r1.getDrumPad().getPluginPath());
  }

  @Test
  public void testDnDStringBuiltin() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // Simulate drag and drop from browser of a builtin plugin onto pad 0
    panel.handleStringDrop(0, "builtin:builtin://acid_bass");

    Request req = getLatestRequest();
    assertNotNull(req);
    assertTrue(req.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, req.getDrumPad().getAction());
    assertEquals("builtin://acid_bass", req.getDrumPad().getPluginPath());
    assertEquals(0, req.getDrumPad().getPadIndex());
  }

  @Test
  public void testDnDStringVst() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // Simulate drag and drop from browser of a VST plugin onto pad 2
    panel.handleStringDrop(2, "vst:/path/to/Dexed.vst3");

    Request req = getLatestRequest();
    assertNotNull(req);
    assertTrue(req.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, req.getDrumPad().getAction());
    assertEquals("/path/to/Dexed.vst3", req.getDrumPad().getPluginPath());
    assertEquals(2, req.getDrumPad().getPadIndex());

    // Also test with "plugin" prefix
    clearRequestLog();
    panel.handleStringDrop(3, "plugin:/path/to/Dexed.vst3");
    req = getLatestRequest();
    assertNotNull(req);
    assertTrue(req.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, req.getDrumPad().getAction());
    assertEquals("/path/to/Dexed.vst3", req.getDrumPad().getPluginPath());
    assertEquals(3, req.getDrumPad().getPadIndex());
  }

  @Test
  public void testDnDStringAudio() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // Simulate drag and drop from browser of an audio sample onto pad 1
    panel.handleStringDrop(1, "audio:/path/to/kick.wav");

    // Since it's audio, it should first load Sampler if not loaded, then load sample
    List<Request> reqs = getAllRequests();
    assertTrue(reqs.size() >= 2);

    // First request: load Sampler
    Request r1 = reqs.get(0);
    assertTrue(r1.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, r1.getDrumPad().getAction());
    assertEquals("builtin://sampler", r1.getDrumPad().getPluginPath());
    assertEquals(1, r1.getDrumPad().getPadIndex());

    // Second request: load sample
    Request r2 = reqs.get(1);
    assertTrue(r2.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_SAMPLE, r2.getDrumPad().getAction());
    assertEquals("/path/to/kick.wav", r2.getDrumPad().getSamplePath());
    assertEquals(1, r2.getDrumPad().getPadIndex());
  }

  @Test
  public void testDnDFileListDrop() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // Drop a VST3 file
    java.io.File vstFile = new java.io.File("/path/to/Dexed.vst3");
    panel.handleFileListDrop(4, List.of(vstFile));

    Request req = getLatestRequest();
    assertNotNull(req);
    assertTrue(req.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, req.getDrumPad().getAction());
    assertEquals(vstFile.getAbsolutePath(), req.getDrumPad().getPluginPath());
    assertEquals(4, req.getDrumPad().getPadIndex());

    // Drop a WAV file
    clearRequestLog();
    java.io.File wavFile = new java.io.File("/path/to/snare.wav");
    panel.handleFileListDrop(5, List.of(wavFile));

    List<Request> reqs = getAllRequests();
    assertTrue(reqs.size() >= 2);
    // First loads sampler, then loads sample
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, reqs.get(0).getDrumPad().getAction());
    assertEquals("builtin://sampler", reqs.get(0).getDrumPad().getPluginPath());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_SAMPLE, reqs.get(1).getDrumPad().getAction());
    assertEquals(wavFile.getAbsolutePath(), reqs.get(1).getDrumPad().getSamplePath());
    assertEquals(5, reqs.get(1).getDrumPad().getPadIndex());
  }

  @Test
  public void testEditBtnForVst3() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // 1. Load a VST3 plugin on pad 0 using handleStringDrop
    panel.handleStringDrop(0, "vst:/path/to/Dexed.vst3");
    // Verify that the command to load plugin was sent
    Request loadReq = getLatestRequest();
    assertNotNull(loadReq);
    assertTrue(loadReq.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, loadReq.getDrumPad().getAction());
    assertEquals("/path/to/Dexed.vst3", loadReq.getDrumPad().getPluginPath());
    clearRequestLog();

    // 2. Select pad 0
    panel.selectedPad = 0;
    // Set the pad state to simulate plugin loaded on pad 0
    DrumPadNotification notif =
        DrumPadNotification.newBuilder()
            .setType(DrumPadNotification.Type.TYPE_PAD_STATE)
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPadIndex(0)
            .setPluginPath("/path/to/Dexed.vst3")
            .build();
    panel.handlePadNotification(notif); // updates UI state

    // Verify edit button is enabled
    assertTrue(panel.editBtn.isEnabled());

    // 3. Click the Edit button
    panel.editBtn.setSelected(true);
    panel.editBtn.getActionListeners()[0].actionPerformed(
        new ActionEvent(panel.editBtn, ActionEvent.ACTION_PERFORMED, ""));

    // 4. Verify that ACTION_SHOW_GUI is sent to the backend
    Request editReq = getLatestRequest();
    assertNotNull(editReq);
    assertTrue(editReq.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_SHOW_GUI, editReq.getDrumPad().getAction());
    assertEquals(0, editReq.getDrumPad().getPadIndex());

    // And verify that editBtn is reset to unselected since it's a VST3 (no embeddable child UI)
    assertFalse(panel.editBtn.isSelected());
  }

  @Test
  public void testEffectComboOptions() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);

    // Check that effect model contains the expected effects
    javax.swing.ComboBoxModel<String> model = panel.effectCombo.getModel();

    List<String> items = new ArrayList<>();
    for (int i = 0; i < model.getSize(); i++) {
      items.add(model.getElementAt(i));
    }

    assertTrue(items.contains("EQ Eight"));
    assertTrue(items.contains("Compressor"));
    assertTrue(items.contains("Delay"));
    assertTrue(items.contains("Reverb"));
    assertTrue(items.contains("Limiter"));
    assertTrue(items.contains("Bitcrusher"));
    assertTrue(items.contains("Load VST3..."));

    // Select "EQ Eight" (index 1)
    clearRequestLog();
    panel.effectCombo.setSelectedItem("EQ Eight");
    Request r1 = getLatestRequest();
    assertNotNull(r1);
    assertTrue(r1.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_LOAD_PLUGIN, r1.getDrumPad().getAction());
    assertEquals("builtin://eq", r1.getDrumPad().getPluginPath());
    assertTrue(r1.getDrumPad().getTargetEffect());
    clearRequestLog();

    // Select "Empty" (index 0)
    panel.effectCombo.setSelectedIndex(0);
    Request r0 = getLatestRequest();
    assertNotNull(r0);
    assertTrue(r0.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_REMOVE_PLUGIN, r0.getDrumPad().getAction());
    assertTrue(r0.getDrumPad().getTargetEffect());
  }

  @Test
  public void testEffectEditBtnVst3() throws Exception {
    DrumMachineDevicePanel panel = new DrumMachineDevicePanel(0, 0);
    clearRequestLog();

    // 1. Select pad 0
    panel.selectedPad = 0;

    // 2. Set the pad state to simulate VST3 effect loaded on pad 0
    DrumPadNotification notif =
        DrumPadNotification.newBuilder()
            .setType(DrumPadNotification.Type.TYPE_PAD_STATE)
            .setTrackIndex(0)
            .setPluginIndex(0)
            .setPadIndex(0)
            .setEffectPath("/path/to/Vst3Effect.vst3")
            .build();
    panel.handlePadNotification(notif); // updates UI state

    // Verify effectEditBtn is enabled
    assertTrue(panel.effectEditBtn.isEnabled());

    // 3. Click the Effect Edit button
    panel.effectEditBtn.setSelected(true);
    panel.effectEditBtn.getActionListeners()[0].actionPerformed(
        new ActionEvent(panel.effectEditBtn, ActionEvent.ACTION_PERFORMED, ""));

    // 4. Verify that ACTION_SHOW_GUI with target_effect = true is sent to the backend
    Request editReq = getLatestRequest();
    assertNotNull(editReq);
    assertTrue(editReq.hasDrumPad());
    assertEquals(DrumPadCmd.Action.ACTION_SHOW_GUI, editReq.getDrumPad().getAction());
    assertEquals(0, editReq.getDrumPad().getPadIndex());
    assertTrue(editReq.getDrumPad().getTargetEffect());

    // Verify that effectEditBtn is reset to unselected since it's a VST3 (no embeddable child UI)
    assertFalse(panel.effectEditBtn.isSelected());
  }
}
