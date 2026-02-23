package hibiki.ui;

import org.junit.Test;
import static org.junit.Assert.*;
import java.awt.Color;

public class ThemeTest {
    @Test
    public void testThemeColors() {
        assertNotNull("BG_DARK should not be null", Theme.BG_DARK);
        assertNotNull("ACCENT_ORANGE should not be null", Theme.ACCENT_ORANGE);
        assertNotNull("ACCENT_BLUE should not be null", Theme.ACCENT_BLUE);
        assertNotNull("ACCENT_GREEN should not be null", Theme.ACCENT_GREEN);
        
        // Verify some specific values if needed, but primarily checking existence
        assertEquals(new Color(34, 34, 34), Theme.BG_DARK);
    }
    
    @Test
    public void testThemeFonts() {
        assertNotNull("FONT_UI should not be null", Theme.FONT_UI);
        assertNotNull("FONT_DISPLAY should not be null", Theme.FONT_DISPLAY);
    }
}
