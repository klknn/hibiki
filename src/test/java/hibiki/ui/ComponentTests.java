package hibiki.ui;

import org.junit.Test;
import static org.junit.Assert.*;
import javax.swing.*;

public class ComponentTests {
    @Test
    public void testTopBarInitialization() {
        TopBar topBar = new TopBar();
        assertNotNull("TopBar should be initialized", topBar);
        assertEquals(Theme.getInstance().BG_DARK, topBar.getBackground());
    }

    @Test
    public void testSessionViewInitialization() {
        SessionView sessionView = new SessionView();
        assertNotNull("SessionView should be initialized", sessionView);
        assertEquals(Theme.getInstance().BG_DARK, sessionView.getBackground());
    }

    @Test
    public void testBrowserPaneInitialization() {
        BrowserPane browserPane = new BrowserPane();
        assertNotNull("BrowserPane should be initialized", browserPane);
        assertEquals(Theme.getInstance().BG_DARK, browserPane.getBackground());
    }

    @Test
    public void testMainViewInitialization() {
        MainView mainView = new MainView();
        assertNotNull("MainView should be initialized", mainView);
        assertEquals(Theme.getInstance().BG_DARK, mainView.getBackground());
    }
}
