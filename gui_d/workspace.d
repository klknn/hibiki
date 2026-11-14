module gui_d.workspace;

import dlangui;
import dlangui.graphics.drawbuf : DrawBuf;
import dlangui.widgets.layouts : WidgetGroupDefaultDrawing;
import dlangui.widgets.widget : Visibility, Widget;
import gui_d.theme : drawCard, drawPlayheadTriangle, drawThickLine, FlPalette, getFlPalette, truncateLabel;
import gui_d.transport_model : TransportModel;
import std.algorithm : canFind;
import std.math : abs;
import std.utf : toUTF32;

/// FL Studio Workspace window arrangement presets.
enum DesktopLayoutPreset {
    DEFAULT_FL,
    TILE_ALL,
    PRODUCTION,
    MIXING,
    FOCUS_PLAYLIST,
}

/// Snaps a candidate floating window rectangle to `desktop` bounds and visible `siblings` edges
/// within `snapDist` pixels (disabled when `disableSnap` is true, e.g. Alt key held).
Rect snapWindowRect(
    Rect candidate,
    Rect desktop,
    const(Rect)[] siblings,
    int snapDist = 10,
    bool disableSnap = false
) nothrow @safe {
    int w = candidate.width;
    int h = candidate.height;
    if (w < 160) w = 160;
    if (h < 100) h = 100;
    if (desktop.width > 0 && w > desktop.width) w = desktop.width;
    if (desktop.height > 0 && h > desktop.height) h = desktop.height;

    int x = candidate.left;
    int y = candidate.top;

    if (!disableSnap && snapDist > 0) {
        // Snap X against desktop edges & sibling left/right edges
        int bestDx = snapDist + 1;
        int snappedX = x;

        void trySnapX(int target, bool alignRight) {
            const int cur = alignRight ? (x + w) : x;
            const int d = abs(cur - target);
            if (d <= snapDist && d < bestDx) {
                bestDx = d;
                snappedX = alignRight ? (target - w) : target;
            }
        }

        trySnapX(desktop.left, false);
        trySnapX(desktop.right, true);
        foreach (const ref s; siblings) {
            trySnapX(s.left, false);
            trySnapX(s.left, true);
            trySnapX(s.right, false);
            trySnapX(s.right, true);
        }
        x = snappedX;

        // Snap Y against desktop edges & sibling top/bottom edges
        int bestDy = snapDist + 1;
        int snappedY = y;

        void trySnapY(int target, bool alignBottom) {
            const int cur = alignBottom ? (y + h) : y;
            const int d = abs(cur - target);
            if (d <= snapDist && d < bestDy) {
                bestDy = d;
                snappedY = alignBottom ? (target - h) : target;
            }
        }

        trySnapY(desktop.top, false);
        trySnapY(desktop.bottom, true);
        foreach (const ref s; siblings) {
            trySnapY(s.top, false);
            trySnapY(s.top, true);
            trySnapY(s.bottom, false);
            trySnapY(s.bottom, true);
        }
        y = snappedY;
    }

    // Clamp inside desktop while keeping header accessible
    if (desktop.width > 0) {
        if (x < desktop.left) x = desktop.left;
        if (x + w > desktop.right) x = desktop.right - w;
        if (x < desktop.left) x = desktop.left;
    }
    if (desktop.height > 0) {
        if (y < desktop.top) y = desktop.top;
        if (y + h > desktop.bottom) y = desktop.bottom - h;
        if (y < desktop.top) y = desktop.top;
    }

    return Rect(x, y, x + w, y + h);
}

/// Walks up parent chain from `w` to bring its enclosing `FlWindowFrame` to the top of Z-order.
void activateParentWindow(Widget w) {
    Widget cur = w;
    while (cur !is null) {
        if (auto frame = cast(FlWindowFrame)cur) {
            if (auto desk = cast(FlDesktopCanvas)frame.parent) {
                desk.bringToFront(frame);
            }
            return;
        }
        cur = cur.parent;
    }
}

/// FL Studio 2026 floating, snapping, resizable sub-window frame (Playlist, Channel Rack, Piano Roll, Mixer, Wrapper).
class FlWindowFrame : WidgetGroupDefaultDrawing {
    enum int HEADER_H = 26;
    enum int RESIZE_GRIP = 14;

    private TransportModel* model_;
    private Widget bodyWidget_;
    private string title_;
    private bool isActive_ = false;
    private bool maximized_ = false;
    private Rect bounds_ = Rect(20, 20, 620, 420); // Relative to FlDesktopCanvas origin
    private Rect restoreBounds_;
    private int minW_ = 280;
    private int minH_ = 160;

    private enum DragMode { NONE, MOVE, RESIZE_BR }
    private DragMode dragMode_ = DragMode.NONE;
    private int dragStartMouseX_ = 0;
    private int dragStartMouseY_ = 0;
    private Rect dragStartBounds_;

    void delegate(FlWindowFrame win, int screenX, int screenY) onMenuClick;
    void delegate(FlWindowFrame win) onClose;

    this(string id, string title, TransportModel* model, Widget body,
         Rect initialBounds, int minW = 280, int minH = 160) {
        super(id);
        title_ = title;
        model_ = model;
        bodyWidget_ = body;
        bounds_ = initialBounds;
        restoreBounds_ = initialBounds;
        minW_ = minW;
        minH_ = minH;
        if (bodyWidget_ !is null) {
            addChild(bodyWidget_);
        }
    }

    @property string windowTitle() const pure nothrow @safe { return title_; }
    @property void windowTitle(string t) { title_ = t; invalidate(); }

    @property Rect windowBounds() const pure nothrow @safe { return bounds_; }
    @property void windowBounds(Rect rc) {
        bounds_ = rc;
        maximized_ = false;
        if (auto desk = cast(FlDesktopCanvas)parent) {
            desk.requestLayout();
        }
    }

    @property bool isActive() const pure nothrow @safe { return isActive_; }
    @property void isActive(bool v) { isActive_ = v; invalidate(); }

    @property bool maximized() const pure nothrow @safe { return maximized_; }
    void toggleMaximize() {
        if (maximized_) {
            maximized_ = false;
            bounds_ = restoreBounds_;
        } else {
            restoreBounds_ = bounds_;
            maximized_ = true;
        }
        if (auto desk = cast(FlDesktopCanvas)parent) {
            desk.bringToFront(this);
            desk.requestLayout();
        }
    }

    override void measure(int parentWidth, int parentHeight) {
        if (bodyWidget_ !is null) {
            const int bw = maximized_ ? parentWidth : bounds_.width;
            const int bh = maximized_ ? parentHeight : bounds_.height;
            bodyWidget_.measure(bw - 2, bh - HEADER_H - 1);
        }
        measuredContent(parentWidth, parentHeight, bounds_.width, bounds_.height);
    }

    override void layout(Rect rc) {
        _needLayout = false;
        if (visibility == Visibility.Gone) return;
        _pos = rc;
        if (bodyWidget_ !is null && bodyWidget_.visibility != Visibility.Gone) {
            Rect bodyRc = Rect(rc.left + 1, rc.top + HEADER_H, rc.right - 1, rc.bottom - 1);
            if (bodyRc.width < 1) bodyRc.right = bodyRc.left + 1;
            if (bodyRc.height < 1) bodyRc.bottom = bodyRc.top + 1;
            bodyWidget_.layout(bodyRc);
        }
    }

    private Rect menuBtnRect() const nothrow @safe {
        return Rect(_pos.left + 2, _pos.top + 2, _pos.left + 24, _pos.top + HEADER_H - 2);
    }

    private Rect maxBtnRect() const nothrow @safe {
        return Rect(_pos.right - 46, _pos.top + 3, _pos.right - 25, _pos.top + HEADER_H - 3);
    }

    private Rect closeBtnRect() const nothrow @safe {
        return Rect(_pos.right - 23, _pos.top + 3, _pos.right - 3, _pos.top + HEADER_H - 3);
    }

    private Rect resizeGripRect() const nothrow @safe {
        return Rect(_pos.right - RESIZE_GRIP, _pos.bottom - RESIZE_GRIP, _pos.right, _pos.bottom);
    }

    override uint getCursorType(int x, int y) {
        if (!maximized_ && resizeGripRect().isPointInside(x, y)) {
            return CursorType.SizeNWSE;
        }
        return CursorType.Arrow;
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        const pal = model_ ? model_.palette() : getFlPalette();

        // Subtle outer drop-shadow frame
        Rect shadowRc = Rect(_pos.left + 1, _pos.top + 1, _pos.right + 2, _pos.bottom + 2);
        buf.fillRect(shadowRc, 0x55000000);

        // Window body frame & border
        const uint borderCol = isActive_ ? pal.borderAccent : pal.borderSubtle;
        buf.fillRect(_pos, pal.bgWindow);
        buf.fillRect(Rect(_pos.left, _pos.top, _pos.right, _pos.top + 1), borderCol);
        buf.fillRect(Rect(_pos.left, _pos.bottom - 1, _pos.right, _pos.bottom), pal.borderStrong);
        buf.fillRect(Rect(_pos.left, _pos.top, _pos.left + 1, _pos.bottom), borderCol);
        buf.fillRect(Rect(_pos.right - 1, _pos.top, _pos.right, _pos.bottom), pal.borderStrong);

        // Caption header bar
        Rect hdr = Rect(_pos.left + 1, _pos.top + 1, _pos.right - 1, _pos.top + HEADER_H);
        buf.fillRect(hdr, isActive_ ? pal.bgWindowHeaderActive : pal.bgWindowHeader);
        buf.fillRect(Rect(hdr.left, hdr.bottom - 1, hdr.right, hdr.bottom), pal.borderStrong);

        // Active top indicator strip
        if (isActive_) {
            buf.fillRect(Rect(hdr.left, hdr.top, hdr.right, hdr.top + 2), pal.accentOrange);
        }

        // Top-left FL Studio Window Menu icon (▼)
        Rect mRc = menuBtnRect();
        drawCard(buf, mRc, pal.bgControl, pal.borderSubtle, 3.0f);
        drawPlayheadTriangle(buf, mRc.middlex, mRc.middley - 2, 4, 5,
                             isActive_ ? pal.accentOrange : pal.fgMuted);

        // Title
        auto f = font;
        if (f !is null) {
            f.drawText(buf, mRc.right + 8, hdr.top + 5,
                       toUTF32(truncateLabel(title_, 44)),
                       isActive_ ? pal.fgText : pal.fgMuted);
        }

        // Maximize / Restore button
        Rect maxRc = maxBtnRect();
        drawCard(buf, maxRc, pal.bgControl, pal.borderSubtle, 3.0f);
        const uint iconCol = pal.fgMuted;
        buf.drawFrame(Rect(maxRc.left + 5, maxRc.top + 5, maxRc.right - 5, maxRc.bottom - 5),
                      iconCol, Rect(1, 2, 1, 1), pal.bgControl);

        // Close button
        Rect clsRc = closeBtnRect();
        drawCard(buf, clsRc, pal.bgControl, pal.borderSubtle, 3.0f);
        drawThickLine(buf, clsRc.left + 6, clsRc.top + 5, clsRc.right - 6, clsRc.bottom - 5, 1.4f, pal.fgMuted);
        drawThickLine(buf, clsRc.right - 6, clsRc.top + 5, clsRc.left + 6, clsRc.bottom - 5, 1.4f, pal.fgMuted);

        // Body widget
        if (bodyWidget_ !is null && bodyWidget_.visibility == Visibility.Visible) {
            Rect bodyClip = Rect(_pos.left + 1, _pos.top + HEADER_H, _pos.right - 1, _pos.bottom - 1);
            auto saver = ClipRectSaver(buf, bodyClip);
            bodyWidget_.onDraw(buf);
        }

        // Subtle bottom-right resize grip diagonal lines
        if (!maximized_) {
            foreach (k; 0 .. 3) {
                const int off = 4 + k * 3;
                drawThickLine(buf, _pos.right - off, _pos.bottom - 2,
                              _pos.right - 2, _pos.bottom - off,
                              1.0f, pal.fgDim);
            }
        }
    }

    override bool onMouseEvent(MouseEvent event) {
        if (event.action == MouseAction.FocusOut) {
            return dragMode_ != DragMode.NONE;
        }

        auto desk = cast(FlDesktopCanvas)parent;

        if (event.action == MouseAction.ButtonDown && event.button == MouseButton.Left) {
            if (desk !is null) desk.bringToFront(this);

            if (menuBtnRect().isPointInside(event.x, event.y)) {
                if (onMenuClick !is null) onMenuClick(this, event.x, event.y);
                return true;
            }
            if (maxBtnRect().isPointInside(event.x, event.y)) {
                toggleMaximize();
                return true;
            }
            if (closeBtnRect().isPointInside(event.x, event.y)) {
                visibility = Visibility.Gone;
                if (onClose !is null) onClose(this);
                if (desk !is null) desk.requestLayout();
                return true;
            }

            if (!maximized_ && resizeGripRect().isPointInside(event.x, event.y)) {
                dragMode_ = DragMode.RESIZE_BR;
                dragStartMouseX_ = event.x;
                dragStartMouseY_ = event.y;
                dragStartBounds_ = bounds_;
                return true;
            }

            // Title bar drag
            if (event.y >= _pos.top && event.y <= _pos.top + HEADER_H) {
                if (event.doubleClick) {
                    toggleMaximize();
                    return true;
                }
                if (!maximized_) {
                    dragMode_ = DragMode.MOVE;
                    dragStartMouseX_ = event.x;
                    dragStartMouseY_ = event.y;
                    dragStartBounds_ = bounds_;
                }
                return true;
            }
            return true;
        }

        if (event.action == MouseAction.Move && dragMode_ != DragMode.NONE) {
            const int dx = event.x - dragStartMouseX_;
            const int dy = event.y - dragStartMouseY_;
            const bool altHeld = (event.flags & MouseFlag.Alt) != 0;

            if (dragMode_ == DragMode.MOVE) {
                Rect cand = Rect(dragStartBounds_.left + dx, dragStartBounds_.top + dy,
                                 dragStartBounds_.right + dx, dragStartBounds_.bottom + dy);
                if (desk !is null) {
                    cand = snapWindowRect(cand, desk.desktopLocalRect(), desk.siblingRectsFor(this), 10, altHeld);
                }
                bounds_ = cand;
                if (desk !is null) desk.relayoutWindow(this);
            } else if (dragMode_ == DragMode.RESIZE_BR) {
                int newW = dragStartBounds_.width + dx;
                int newH = dragStartBounds_.height + dy;
                if (newW < minW_) newW = minW_;
                if (newH < minH_) newH = minH_;
                Rect cand = Rect(dragStartBounds_.left, dragStartBounds_.top,
                                 dragStartBounds_.left + newW, dragStartBounds_.top + newH);
                if (desk !is null) {
                    const Rect deskRc = desk.desktopLocalRect();
                    if (cand.right > deskRc.right) cand.right = deskRc.right;
                    if (cand.bottom > deskRc.bottom) cand.bottom = deskRc.bottom;
                    if (cand.width < minW_) cand.right = cand.left + minW_;
                    if (cand.height < minH_) cand.bottom = cand.top + minH_;
                }
                bounds_ = cand;
                if (desk !is null) desk.relayoutWindow(this);
            }
            return true;
        }

        if (event.action == MouseAction.ButtonUp && dragMode_ != DragMode.NONE) {
            dragMode_ = DragMode.NONE;
            return true;
        }

        return super.onMouseEvent(event);
    }
}

/// FL Studio free-form Desktop workspace managing floating, snapping, Z-ordered sub-windows.
class FlDesktopCanvas : WidgetGroupDefaultDrawing {
    private TransportModel* model_;
    private FlWindowFrame[] windows_; // [0] = bottom-most, [$ - 1] = top-most
    private bool initialPresetApplied_ = false;

    this(string id, TransportModel* model) {
        super(id);
        model_ = model;
        layoutWidth = FILL_PARENT;
        layoutHeight = FILL_PARENT;
    }

    void addWindow(FlWindowFrame win) {
        if (win is null) return;
        win.parent = this;
        windows_ ~= win;
        updateActiveFlags();
        requestLayout();
    }

    /// Returns child count matching `windows_`.
    override @property int childCount() const {
        return cast(int)windows_.length;
    }

    /// Returns children in REVERSE Z-order (`i = 0` is topmost!) so `dlangui`'s
    /// `Window.dispatchMouseEvent` hit-tests topmost floating windows first.
    override inout(Widget) child(int index) inout {
        if (index < 0 || index >= cast(int)windows_.length) return null;
        return windows_[windows_.length - 1 - index];
    }

    override @property bool needDraw() {
        foreach (w; windows_) {
            if (w.visibility == Visibility.Visible && w.needDraw) return true;
        }
        return _needDraw;
    }

    /// Brings `win` to the front of Z-order and marks it active.
    void bringToFront(FlWindowFrame win) {
        if (win is null) return;
        ptrdiff_t idx = -1;
        foreach (i, w; windows_) {
            if (w is win) {
                idx = i;
                break;
            }
        }
        if (idx >= 0 && idx < cast(ptrdiff_t)windows_.length - 1) {
            promoteToTop(windows_, cast(size_t)idx);
        }
        updateActiveFlags();
        invalidate();
    }

    private void updateActiveFlags() {
        FlWindowFrame topVis = null;
        foreach_reverse (w; windows_) {
            if (w.visibility == Visibility.Visible) {
                topVis = w;
                break;
            }
        }
        foreach (w; windows_) {
            w.isActive = (w is topVis);
        }
    }

    FlWindowFrame findWindow(string id) {
        foreach (w; windows_) {
            if (w.id == id) return w;
        }
        return null;
    }

    void toggleWindow(string id) {
        auto win = findWindow(id);
        if (win is null) return;
        if (win.visibility == Visibility.Visible) {
            win.visibility = Visibility.Gone;
        } else {
            win.visibility = Visibility.Visible;
            bringToFront(win);
        }
        updateActiveFlags();
        requestLayout();
    }

    void showWindow(string id) {
        auto win = findWindow(id);
        if (win is null) return;
        win.visibility = Visibility.Visible;
        bringToFront(win);
        requestLayout();
    }

    Rect desktopLocalRect() const nothrow @safe {
        const int w = _pos.width > 0 ? _pos.width : 1180;
        const int h = _pos.height > 0 ? _pos.height : 760;
        return Rect(0, 0, w, h);
    }

    Rect[] siblingRectsFor(const(FlWindowFrame) exclude) {
        Rect[] res;
        foreach (w; windows_) {
            if (w !is exclude && w.visibility == Visibility.Visible && !w.maximized) {
                res ~= w.windowBounds;
            }
        }
        return res;
    }

    void relayoutWindow(FlWindowFrame win) {
        if (win is null || win.visibility == Visibility.Gone) return;
        Rect r = win.maximized
            ? _pos
            : Rect(_pos.left + win.windowBounds.left, _pos.top + win.windowBounds.top,
                   _pos.left + win.windowBounds.right, _pos.top + win.windowBounds.bottom);
        win.measure(r.width, r.height);
        win.layout(r);
        invalidate();
    }

    /// Arranges the FL Studio 2026 sub-windows according to `preset`.
    void applyLayoutPreset(DesktopLayoutPreset preset, int desktopW = 0, int desktopH = 0) {
        const int W = (desktopW > 0) ? desktopW : (_pos.width > 100 ? _pos.width : 1180);
        const int H = (desktopH > 0) ? desktopH : (_pos.height > 100 ? _pos.height : 760);
        enum int G = 6; // Snapped gap

        auto pl = findWindow("win_playlist");
        auto cr = findWindow("win_channel_rack");
        auto pr = findWindow("win_piano_roll");
        auto mx = findWindow("win_mixer");
        auto pg = findWindow("win_plugin");

        final switch (preset) {
            case DesktopLayoutPreset.DEFAULT_FL:
                // Authentic FL Studio 2026 default floating layout:
                // Playlist fills top/left, Mixer docks lower-right, Channel Rack floats crisp on top-right,
                // Piano Roll sits bottom-left overlapping cleanly (toggle/click to focus).
                if (pl) {
                    pl.visibility = Visibility.Visible;
                    pl.windowBounds = Rect(0, 0, (W * 62) / 100, (H * 56) / 100);
                }
                if (pr) {
                    pr.visibility = Visibility.Visible;
                    pr.windowBounds = Rect(0, (H * 56) / 100 + G, (W * 52) / 100, H);
                }
                if (mx) {
                    mx.visibility = Visibility.Visible;
                    mx.windowBounds = Rect((W * 52) / 100 + G, (H * 46) / 100, W, H);
                }
                if (cr) {
                    cr.visibility = Visibility.Visible;
                    cr.windowBounds = Rect((W * 62) / 100 + G, 0, W, (H * 46) / 100 - G);
                }
                if (pg) {
                    pg.visibility = Visibility.Gone;
                    pg.windowBounds = Rect(W / 4, H / 5, (W * 3) / 4, (H * 4) / 5);
                }
                if (pl) bringToFront(pl);
                if (cr) bringToFront(cr);
                break;

            case DesktopLayoutPreset.TILE_ALL:
                const int halfW = (W - G) / 2;
                const int halfH = (H - G) / 2;
                if (pl) { pl.visibility = Visibility.Visible; pl.windowBounds = Rect(0, 0, halfW, halfH); }
                if (cr) { cr.visibility = Visibility.Visible; cr.windowBounds = Rect(halfW + G, 0, W, halfH); }
                if (pr) { pr.visibility = Visibility.Visible; pr.windowBounds = Rect(0, halfH + G, halfW, H); }
                if (mx) { mx.visibility = Visibility.Visible; mx.windowBounds = Rect(halfW + G, halfH + G, W, H); }
                if (pg) pg.visibility = Visibility.Gone;
                break;

            case DesktopLayoutPreset.PRODUCTION:
                // Beatmaking & Arrangement: Playlist + Channel Rack + Piano Roll, Mixer hidden
                if (pl) { pl.visibility = Visibility.Visible; pl.windowBounds = Rect(0, 0, (W * 58) / 100, (H * 55) / 100); }
                if (cr) { cr.visibility = Visibility.Visible; cr.windowBounds = Rect((W * 58) / 100 + G, 0, W, (H * 55) / 100); }
                if (pr) { pr.visibility = Visibility.Visible; pr.windowBounds = Rect(0, (H * 55) / 100 + G, W, H); }
                if (mx) mx.visibility = Visibility.Gone;
                if (pg) pg.visibility = Visibility.Gone;
                break;

            case DesktopLayoutPreset.MIXING:
                // Mixing view: Mixer large at bottom, Playlist strip on top
                if (pl) { pl.visibility = Visibility.Visible; pl.windowBounds = Rect(0, 0, W, (H * 36) / 100); }
                if (mx) { mx.visibility = Visibility.Visible; mx.windowBounds = Rect(0, (H * 36) / 100 + G, W, H); }
                if (cr) cr.visibility = Visibility.Gone;
                if (pr) pr.visibility = Visibility.Gone;
                if (pg) pg.visibility = Visibility.Gone;
                if (mx) bringToFront(mx);
                break;

            case DesktopLayoutPreset.FOCUS_PLAYLIST:
                if (pl) {
                    pl.visibility = Visibility.Visible;
                    pl.windowBounds = Rect(0, 0, W, H);
                    bringToFront(pl);
                }
                if (cr) cr.visibility = Visibility.Gone;
                if (pr) pr.visibility = Visibility.Gone;
                if (mx) mx.visibility = Visibility.Gone;
                if (pg) pg.visibility = Visibility.Gone;
                break;
        }
        updateActiveFlags();
        requestLayout();
    }

    override void measure(int parentWidth, int parentHeight) {
        foreach (w; windows_) {
            if (w.visibility != Visibility.Gone) {
                const int bw = w.maximized ? parentWidth : w.windowBounds.width;
                const int bh = w.maximized ? parentHeight : w.windowBounds.height;
                w.measure(bw, bh);
            }
        }
        measuredContent(parentWidth, parentHeight, parentWidth, parentHeight);
    }

    override void layout(Rect rc) {
        _needLayout = false;
        if (visibility == Visibility.Gone) return;
        _pos = rc;
        if (!initialPresetApplied_ && rc.width > 200 && rc.height > 200) {
            initialPresetApplied_ = true;
            applyLayoutPreset(DesktopLayoutPreset.DEFAULT_FL, rc.width, rc.height);
        }
        foreach (w; windows_) {
            if (w.visibility != Visibility.Gone) {
                Rect wr = w.maximized
                    ? rc
                    : Rect(rc.left + w.windowBounds.left, rc.top + w.windowBounds.top,
                           rc.left + w.windowBounds.right, rc.top + w.windowBounds.bottom);
                w.layout(wr);
            }
        }
    }

    override void onDraw(DrawBuf buf) {
        if (visibility != Visibility.Visible) return;
        const pal = model_ ? model_.palette() : getFlPalette();
        buf.fillRect(_pos, pal.bgDesktop);

        // Subtle FL Studio workspace backdrop grid
        enum int GRID_STEP = 48;
        for (int gx = _pos.left + GRID_STEP; gx < _pos.right; gx += GRID_STEP) {
            buf.fillRect(Rect(gx, _pos.top, gx + 1, _pos.bottom), 0x10FFFFFF);
        }
        for (int gy = _pos.top + GRID_STEP; gy < _pos.bottom; gy += GRID_STEP) {
            buf.fillRect(Rect(_pos.left, gy, _pos.right, gy + 1), 0x10FFFFFF);
        }

        // Draw windows bottom -> top so windows_[$-1] renders on top
        Rect deskClip = _pos;
        auto saver = ClipRectSaver(buf, deskClip);
        foreach (w; windows_) {
            if (w.visibility == Visibility.Visible) {
                w.onDraw(buf);
            }
        }
    }
}

/// Moves element at `idx` to the end (top-most Z-order) of `arr`.
void promoteToTop(T)(ref T[] arr, size_t idx) {
    if (idx + 1 >= arr.length) return;
    auto moved = arr[idx];
    foreach (k; idx .. arr.length - 1) {
        arr[k] = arr[k + 1];
    }
    arr[$ - 1] = moved;
}

unittest {
    // 1. Window edge snapping to desktop & sibling window
    Rect desktop = Rect(0, 0, 1200, 800);
    Rect[] siblings = [Rect(0, 0, 600, 400)];
    // Dragging near x=606 should snap left edge to sibling right edge x=600
    Rect snapped = snapWindowRect(Rect(606, 5, 956, 355), desktop, siblings, 10, false);
    assert(snapped.left == 600 && snapped.top == 0,
           "Expected window to snap to sibling right=600 and desktop top=0");

    // Dragging near bottom-right corner snaps to desktop bounds
    Rect snappedBR = snapWindowRect(Rect(845, 444, 1195, 794), desktop, siblings, 10, false);
    assert(snappedBR.right == 1200 && snappedBR.bottom == 800,
           "Expected window to snap to desktop right/bottom");

    // Holding Alt disables snapping
    Rect freeRect = snapWindowRect(Rect(606, 5, 956, 355), desktop, siblings, 10, true);
    assert(freeRect.left == 606 && freeRect.top == 5,
           "Expected Alt override to bypass snapping");

    // 2. Z-order promotion (last element is top-most)
    string[] zOrder = ["win_piano_roll", "win_playlist", "win_mixer", "win_channel_rack"];
    promoteToTop(zOrder, 1); // Bring win_playlist to top
    assert(zOrder[$ - 1] == "win_playlist");
    assert(zOrder[0] == "win_piano_roll");
    assert(zOrder[1] == "win_mixer");
    assert(zOrder[2] == "win_channel_rack");
}
