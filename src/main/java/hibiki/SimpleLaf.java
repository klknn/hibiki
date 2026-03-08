package hibiki;

import javax.swing.plaf.basic.BasicLookAndFeel;

public class SimpleLaf extends BasicLookAndFeel {

    public String getDescription() {
        return "This is " + getName();
    }

    public String getID() {
        return "SimpleLaf";
    }

    public String getName() {
        return "Simple Look & Feel";
    }

    public boolean isNativeLookAndFeel() {
        return false;
    }

    public boolean isSupportedLookAndFeel() {
        return true;
    }
}