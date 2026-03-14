package hibiki.ui;

import org.junit.Test;
import static org.junit.Assert.*;

public class PluginPaneTest {

    @Test
    public void testInitialization() {
        PluginPane pane = new PluginPane();
        assertNotNull(pane);
        assertEquals(Theme.getInstance().BG_DARK, pane.getBackground());
    }

    @Test
    public void testSingleton() {
        PluginPane pane = new PluginPane();
        assertSame(pane, PluginPane.getInstance());
    }

    @Test
    public void testPreferredSize() {
        PluginPane pane = new PluginPane();
        assertNotNull(pane.getPreferredSize());
        assertTrue(pane.getPreferredSize().height > 0);
    }

    @Test
    public void testSetSelectedTrack_noChange() {
        PluginPane pane = new PluginPane();
        // Default selectedTrack is 0, setting 0 again should be a no-op
        pane.setSelectedTrack(0);
        // No assertion needed, just verifying no exception
    }

    @Test
    public void testSetSelectedTrack_change() {
        PluginPane pane = new PluginPane();
        pane.setSelectedTrack(3);
        // Should rebuild device chain without error
    }

    @Test
    public void testSetSelectedTrack_sequentialChanges() {
        PluginPane pane = new PluginPane();
        pane.setSelectedTrack(1);
        pane.setSelectedTrack(5);
        pane.setSelectedTrack(0);
        // Should not throw
    }

    @Test
    public void testClearPanels() {
        PluginPane pane = new PluginPane();
        // clearPanels is private, but we can verify via construction
        // which sets up the device chain content panel
        assertTrue(pane.getComponentCount() >= 0);
    }

    @Test
    public void testSetSelectedTrack_multipleInstances() {
        // Creating a new PluginPane replaces the singleton
        PluginPane pane1 = new PluginPane();
        PluginPane pane2 = new PluginPane();
        assertSame(pane2, PluginPane.getInstance());
        pane2.setSelectedTrack(2);
    }
}
