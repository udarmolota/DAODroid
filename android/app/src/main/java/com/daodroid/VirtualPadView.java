package com.daodroid;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/**
 * An on-screen Xbox gamepad drawn over the game, laid out as Zomdroid's default gamepad
 * controls (its default_controls.json: positions in fractions of the screen, sizes in its
 * units where the unit is the width / 2560): sticks at fixed places low on both sides, the
 * D-pad above the left one, A/B/X/Y above the right one, bumpers and triggers in the upper
 * corners, Back and Start along the top, L3 and R3 in the bottom corners. Every pointer
 * drives one element; the state goes to the game on each change
 * ({@link VirtualPad#nativeSetState}). A small switch at the top centre hides the pad
 * (touches then pass through to the game's surface); held for a moment, it turns the edit
 * mode on and off, in which the elements are dragged into place (kept in the app's
 * preferences, as fractions of the screen).
 */
public class VirtualPadView extends View {
    private static final int MAX_POINTERS = 10;

    // Elements a pointer can hold.
    private static final int NONE = 0, LEFT_STICK = 1, RIGHT_STICK = 2, DPAD = 3, BUTTON = 4, TOGGLE = 5, DRAG = 6;
    private static final long EDIT_HOLD_MS = 600;
    private static final String PREFS = "virtual_pad";

    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint colorStroke = new Paint(Paint.ANTI_ALIAS_FLAG); // a face button's outline
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pressedFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint arrowFill = new Paint(Paint.ANTI_ALIAS_FLAG); // the D-pad's arrows
    private final android.graphics.Path arrow = new android.graphics.Path();

    private boolean shown = true;
    private boolean editing = false;
    private long toggleDownTime;
    private final Paint editStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    // Drag: which movable thing (a Button, or the strings "dpad", "left", "right") and the grab offset.
    private final Object[] dragTarget = new Object[MAX_POINTERS];
    private final float[] dragDx = new float[MAX_POINTERS], dragDy = new float[MAX_POINTERS];
    private final SharedPreferences prefs;

    // Per pointer: the element and, for a button or the D-pad, its bits.
    private final int[] element = new int[MAX_POINTERS];
    private final int[] bit = new int[MAX_POINTERS];

    // The state sent to the game.
    private int buttons;
    private float leftX, leftY, rightX, rightY, leftTrigger, rightTrigger;

    // Sticks: fixed bases, knobs follow the finger.
    private float leftBaseX, leftBaseY, rightBaseX, rightBaseY, stickRadius, knobRadius;
    private int leftStickPointer = -1, rightStickPointer = -1;
    private float leftKnobX, leftKnobY, rightKnobX, rightKnobY;

    // Buttons: circles and rounded rectangles, laid out in onSizeChanged.
    private static final class Button {
        final int bit; final String label; final boolean trigger;
        int outline; // 0: the common grey; the face buttons have the Xbox colours
        float cx, cy, r, w, h; RectF rect;
        Button(int bit, String label, boolean trigger) { this.bit = bit; this.label = label; this.trigger = trigger; }
        Button(int bit, String label, int outline) { this(bit, label, false); this.outline = outline; }
        boolean hit(float x, float y) {
            if (rect != null) return rect.contains(x, y);
            float dx = x - cx, dy = y - cy;
            return dx * dx + dy * dy <= r * r * 1.3f;
        }
    }
    // Trigger "bits" outside the game's button bits.
    private static final int LT = 0x10000, RT = 0x20000;
    private final Button[] buttonList = {
        new Button(VirtualPad.A, "A", 0xD060C040), new Button(VirtualPad.B, "B", 0xD0E04040),
        new Button(VirtualPad.X, "X", 0xD04090F0), new Button(VirtualPad.Y, "Y", 0xD0F0C830),
        new Button(VirtualPad.LB, "LB", false), new Button(VirtualPad.RB, "RB", false),
        new Button(LT, "LT", true), new Button(RT, "RT", true),
        new Button(VirtualPad.START, "START", false), new Button(VirtualPad.BACK, "BACK", false),
        new Button(VirtualPad.LEFT_THUMB, "L", false), new Button(VirtualPad.RIGHT_THUMB, "R", false),
    };
    private final RectF dpadRect = new RectF();
    private float dpadCx, dpadCy, dpadSize;
    private float toggleCx, toggleCy, toggleR;
    private float unit;

    public VirtualPadView(Context context) {
        super(context);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setColor(0x80CCCCCC);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(0x28CCCCCC);
        pressedFill.setStyle(Paint.Style.FILL);
        pressedFill.setColor(0x90C9B38C);
        text.setColor(0xA8FFFFFF);
        arrowFill.setStyle(Paint.Style.FILL);
        arrowFill.setColor(0xA8FFFFFF);
        text.setTextAlign(Paint.Align.CENTER);
        editStroke.setStyle(Paint.Style.STROKE);
        editStroke.setColor(0xC0FFD040);
        for (int i = 0; i < MAX_POINTERS; i++) element[i] = NONE;
        prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        unit = w / 2560f;                 // Zomdroid's pixel scale
        stroke.setStrokeWidth(4 * unit);
        colorStroke.setStyle(Paint.Style.STROKE);
        colorStroke.setStrokeWidth(5 * unit);
        text.setTextSize(44 * unit);
        stickRadius = 160 * unit;
        knobRadius = 90 * unit;
        leftBaseX = 0.15f * w; leftBaseY = 0.75f * h;
        rightBaseX = 0.85f * w; rightBaseY = 0.75f * h;
        leftKnobX = leftBaseX; leftKnobY = leftBaseY; rightKnobX = rightBaseX; rightKnobY = rightBaseY;
        dpadSize = 340 * unit; dpadCx = 0.12f * w; dpadCy = 0.45f * h;
        dpadRect.set(dpadCx - dpadSize / 2, dpadCy - dpadSize / 2, dpadCx + dpadSize / 2, dpadCy + dpadSize / 2);
        float d = 160 * unit, ds = 120 * unit;
        circle(VirtualPad.A, 0.825f * w, 0.525f * h, d / 2); circle(VirtualPad.B, 0.875f * w, 0.425f * h, d / 2);
        circle(VirtualPad.X, 0.775f * w, 0.425f * h, d / 2); circle(VirtualPad.Y, 0.825f * w, 0.325f * h, d / 2);
        circle(VirtualPad.BACK, 0.2f * w, 0.1f * h, ds / 2); circle(VirtualPad.START, 0.8f * w, 0.1f * h, ds / 2);
        circle(VirtualPad.LEFT_THUMB, 0.05f * w, 0.9f * h, ds / 2); circle(VirtualPad.RIGHT_THUMB, 0.95f * w, 0.9f * h, ds / 2);
        float rw = 240 * unit, rh = 120 * unit;
        rect(VirtualPad.LB, 0.1f * w, 0.2f * h, rw, rh); rect(VirtualPad.RB, 0.9f * w, 0.2f * h, rw, rh);
        rect(LT, 0.1f * w, 0.075f * h, rw, rh); rect(RT, 0.9f * w, 0.075f * h, rw, rh);
        toggleCx = 0.5f * w; toggleCy = 0.08f * h; toggleR = 48 * unit;
        editStroke.setStrokeWidth(6 * unit);
        // Saved positions, if the pad was edited.
        for (Button b : buttonList) {
            float x = prefs.getFloat("b" + b.bit + "x", -1), y = prefs.getFloat("b" + b.bit + "y", -1);
            if (x >= 0 && y >= 0) moveButton(b, x * w, y * h);
        }
        float x = prefs.getFloat("dpadx", -1), y = prefs.getFloat("dpady", -1);
        if (x >= 0 && y >= 0) { dpadCx = x * w; dpadCy = y * h; dpadRect.set(dpadCx - dpadSize / 2, dpadCy - dpadSize / 2, dpadCx + dpadSize / 2, dpadCy + dpadSize / 2); }
        x = prefs.getFloat("leftx", -1); y = prefs.getFloat("lefty", -1);
        if (x >= 0 && y >= 0) { leftBaseX = x * w; leftBaseY = y * h; leftKnobX = leftBaseX; leftKnobY = leftBaseY; }
        x = prefs.getFloat("rightx", -1); y = prefs.getFloat("righty", -1);
        if (x >= 0 && y >= 0) { rightBaseX = x * w; rightBaseY = y * h; rightKnobX = rightBaseX; rightKnobY = rightBaseY; }
    }

    private void moveButton(Button b, float cx, float cy) {
        b.cx = cx; b.cy = cy;
        if (b.rect != null) b.rect.set(cx - b.w / 2, cy - b.h / 2, cx + b.w / 2, cy + b.h / 2);
    }

    private void savePositions() {
        SharedPreferences.Editor e = prefs.edit();
        float w = getWidth(), h = getHeight();
        for (Button b : buttonList) e.putFloat("b" + b.bit + "x", b.cx / w).putFloat("b" + b.bit + "y", b.cy / h);
        e.putFloat("dpadx", dpadCx / w).putFloat("dpady", dpadCy / h);
        e.putFloat("leftx", leftBaseX / w).putFloat("lefty", leftBaseY / h);
        e.putFloat("rightx", rightBaseX / w).putFloat("righty", rightBaseY / h);
        e.apply();
    }

    private void circle(int bit, float cx, float cy, float r) {
        Button b = find(bit); b.cx = cx; b.cy = cy; b.r = r; b.rect = null;
    }

    private void rect(int bit, float cx, float cy, float w, float h) {
        Button b = find(bit); b.w = w; b.h = h; b.rect = new RectF(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2); b.cx = cx; b.cy = cy;
    }

    private Button find(int bit) {
        for (Button b : buttonList) if (b.bit == bit) return b;
        throw new IllegalArgumentException();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        // The switch.
        canvas.drawCircle(toggleCx, toggleCy, toggleR, shown ? fill : pressedFill);
        canvas.drawCircle(toggleCx, toggleCy, toggleR, stroke);
        canvas.drawText(editing ? "EDIT" : "V", toggleCx, toggleCy + 15 * unit, text);
        if (!shown) return;
        if (editing) {
            for (Button b : buttonList) {
                if (b.rect != null) canvas.drawRoundRect(b.rect, 20 * unit, 20 * unit, editStroke);
                else canvas.drawCircle(b.cx, b.cy, b.r, editStroke);
            }
            canvas.drawRect(dpadRect, editStroke);
            canvas.drawCircle(leftBaseX, leftBaseY, stickRadius, editStroke);
            canvas.drawCircle(rightBaseX, rightBaseY, stickRadius, editStroke);
        }
        for (Button b : buttonList) {
            boolean pressed = b.trigger ? (b.bit == LT ? leftTrigger > 0 : rightTrigger > 0) : (buttons & b.bit) != 0;
            Paint p = pressed ? pressedFill : fill;
            if (b.rect != null) {
                canvas.drawRoundRect(b.rect, 20 * unit, 20 * unit, p);
                canvas.drawRoundRect(b.rect, 20 * unit, 20 * unit, stroke);
            } else {
                canvas.drawCircle(b.cx, b.cy, b.r, p);
                Paint outline = stroke;
                if (b.outline != 0) {
                    colorStroke.setColor(b.outline);
                    outline = colorStroke;
                }
                canvas.drawCircle(b.cx, b.cy, b.r, outline);
            }
            canvas.drawText(b.label, b.cx, b.cy + 15 * unit, text);
        }
        // D-pad: a cross of four arms.
        float arm = dpadSize / 2, t = dpadSize / 6;
        drawDpadArm(canvas, dpadCx, dpadCy - arm + t, t, VirtualPad.DPAD_UP);
        drawDpadArm(canvas, dpadCx, dpadCy + arm - t, t, VirtualPad.DPAD_DOWN);
        drawDpadArm(canvas, dpadCx - arm + t, dpadCy, t, VirtualPad.DPAD_LEFT);
        drawDpadArm(canvas, dpadCx + arm - t, dpadCy, t, VirtualPad.DPAD_RIGHT);
        drawStick(canvas, leftBaseX, leftBaseY, leftKnobX, leftKnobY, leftStickPointer >= 0);
        drawStick(canvas, rightBaseX, rightBaseY, rightKnobX, rightKnobY, rightStickPointer >= 0);
    }

    private void drawDpadArm(Canvas canvas, float cx, float cy, float t, int bit) {
        canvas.drawCircle(cx, cy, t, (buttons & bit) != 0 ? pressedFill : fill);
        canvas.drawCircle(cx, cy, t, stroke);
        // An arrow pointing the arm's way, so the four don't look alike.
        float dx = 0, dy = 0;
        if (bit == VirtualPad.DPAD_UP) dy = -1;
        else if (bit == VirtualPad.DPAD_DOWN) dy = 1;
        else if (bit == VirtualPad.DPAD_LEFT) dx = -1;
        else dx = 1;
        float tip = t * 0.45f, back = t * 0.25f, half = t * 0.38f;
        arrow.reset();
        arrow.moveTo(cx + dx * tip, cy + dy * tip);
        arrow.lineTo(cx - dx * back - dy * half, cy - dy * back + dx * half);
        arrow.lineTo(cx - dx * back + dy * half, cy - dy * back - dx * half);
        arrow.close();
        canvas.drawPath(arrow, arrowFill);
    }

    private void drawStick(Canvas canvas, float baseX, float baseY, float knobX, float knobY, boolean held) {
        canvas.drawCircle(baseX, baseY, stickRadius, fill);
        canvas.drawCircle(baseX, baseY, stickRadius, stroke);
        canvas.drawCircle(knobX, knobY, knobRadius, held ? pressedFill : fill);
        canvas.drawCircle(knobX, knobY, knobRadius, stroke);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        int id = event.getPointerId(index);
        if (id >= MAX_POINTERS) return false;
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                float x = event.getX(index), y = event.getY(index);
                if (Math.hypot(x - toggleCx, y - toggleCy) <= toggleR * 1.3f) {
                    element[id] = TOGGLE;
                    toggleDownTime = event.getEventTime();
                    return true;
                }
                if (!shown) return false;
                element[id] = editing ? pickDrag(x, y, id) : pick(x, y, id);
                if (element[id] == NONE) return event.getPointerCount() > 1; // keep the gesture if we hold others
                return true;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int pid = event.getPointerId(i);
                    if (pid >= MAX_POINTERS) continue;
                    if (element[pid] == LEFT_STICK || element[pid] == RIGHT_STICK) moveStick(pid, event.getX(i), event.getY(i));
                    else if (element[pid] == DPAD) moveDpad(pid, event.getX(i), event.getY(i));
                    else if (element[pid] == DRAG) drag(pid, event.getX(i), event.getY(i));
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                if (action == MotionEvent.ACTION_CANCEL) {
                    for (int i = 0; i < MAX_POINTERS; i++) release(i);
                } else {
                    if (element[id] == TOGGLE && event.getEventTime() - toggleDownTime >= EDIT_HOLD_MS) {
                        editing = !editing;
                        if (!editing) savePositions();
                        element[id] = NONE;
                        invalidate();
                        return true;
                    }
                    release(id);
                }
                return true;
            default:
                return false;
        }
    }

    private int pick(float x, float y, int id) {
        for (Button b : buttonList) {
            if (b.hit(x, y)) {
                bit[id] = b.bit;
                if (b.bit == LT) leftTrigger = 1; else if (b.bit == RT) rightTrigger = 1; else buttons |= b.bit;
                send();
                return BUTTON;
            }
        }
        if (dpadRect.contains(x, y)) {
            bit[id] = dpadBits(x, y);
            buttons |= bit[id];
            send();
            return DPAD;
        }
        if (leftStickPointer < 0 && Math.hypot(x - leftBaseX, y - leftBaseY) <= stickRadius * 1.5f) {
            leftStickPointer = id;
            moveStick(id, x, y);
            return LEFT_STICK;
        }
        if (rightStickPointer < 0 && Math.hypot(x - rightBaseX, y - rightBaseY) <= stickRadius * 1.5f) {
            rightStickPointer = id;
            moveStick(id, x, y);
            return RIGHT_STICK;
        }
        return NONE;
    }

    private int pickDrag(float x, float y, int id) {
        Object target = null; float cx = 0, cy = 0;
        for (Button b : buttonList) if (b.hit(x, y)) { target = b; cx = b.cx; cy = b.cy; break; }
        if (target == null && dpadRect.contains(x, y)) { target = "dpad"; cx = dpadCx; cy = dpadCy; }
        if (target == null && Math.hypot(x - leftBaseX, y - leftBaseY) <= stickRadius) { target = "left"; cx = leftBaseX; cy = leftBaseY; }
        if (target == null && Math.hypot(x - rightBaseX, y - rightBaseY) <= stickRadius) { target = "right"; cx = rightBaseX; cy = rightBaseY; }
        if (target == null) return NONE;
        dragTarget[id] = target; dragDx[id] = cx - x; dragDy[id] = cy - y;
        return DRAG;
    }

    private void drag(int id, float x, float y) {
        float cx = Math.max(0, Math.min(getWidth(), x + dragDx[id])), cy = Math.max(0, Math.min(getHeight(), y + dragDy[id]));
        Object t = dragTarget[id];
        if (t instanceof Button) moveButton((Button) t, cx, cy);
        else if ("dpad".equals(t)) { dpadCx = cx; dpadCy = cy; dpadRect.set(cx - dpadSize / 2, cy - dpadSize / 2, cx + dpadSize / 2, cy + dpadSize / 2); }
        else if ("left".equals(t)) { leftBaseX = cx; leftBaseY = cy; leftKnobX = cx; leftKnobY = cy; }
        else if ("right".equals(t)) { rightBaseX = cx; rightBaseY = cy; rightKnobX = cx; rightKnobY = cy; }
        invalidate();
    }

    private int dpadBits(float x, float y) {
        float dx = (x - dpadCx) / (dpadSize / 2), dy = (y - dpadCy) / (dpadSize / 2);
        int bits = 0;
        if (dy < -0.3f) bits |= VirtualPad.DPAD_UP;
        if (dy > 0.3f) bits |= VirtualPad.DPAD_DOWN;
        if (dx < -0.3f) bits |= VirtualPad.DPAD_LEFT;
        if (dx > 0.3f) bits |= VirtualPad.DPAD_RIGHT;
        return bits;
    }

    private void moveDpad(int id, float x, float y) {
        int bits = dpadBits(x, y);
        if (bits == bit[id]) return;
        buttons = (buttons & ~bit[id]) | bits;
        bit[id] = bits;
        send();
    }

    private void moveStick(int id, float x, float y) {
        boolean left = element[id] == LEFT_STICK || id == leftStickPointer;
        float baseX = left ? leftBaseX : rightBaseX, baseY = left ? leftBaseY : rightBaseY;
        float dx = x - baseX, dy = y - baseY;
        float len = (float) Math.hypot(dx, dy);
        float max = stickRadius;
        if (len > max) { dx *= max / len; dy *= max / len; len = max; }
        // A small dead zone, then linear.
        float dead = 0.1f, mag = len / max;
        float scale = mag <= dead ? 0 : (mag - dead) / (1 - dead) / Math.max(mag, 1e-3f);
        float vx = dx / max * scale, vy = -dy / max * scale;
        if (left) { leftX = vx; leftY = vy; leftKnobX = baseX + dx; leftKnobY = baseY + dy; }
        else { rightX = vx; rightY = vy; rightKnobX = baseX + dx; rightKnobY = baseY + dy; }
        send();
    }

    private void release(int id) {
        switch (element[id]) {
            case LEFT_STICK: leftStickPointer = -1; leftX = leftY = 0; leftKnobX = leftBaseX; leftKnobY = leftBaseY; break;
            case RIGHT_STICK: rightStickPointer = -1; rightX = rightY = 0; rightKnobX = rightBaseX; rightKnobY = rightBaseY; break;
            case DPAD: buttons &= ~bit[id]; break;
            case BUTTON:
                if (bit[id] == LT) leftTrigger = 0; else if (bit[id] == RT) rightTrigger = 0; else buttons &= ~bit[id];
                break;
            case DRAG:
                dragTarget[id] = null;
                break;
            case TOGGLE:
                shown = !shown;
                if (!shown) { // drop everything held
                    for (int i = 0; i < MAX_POINTERS; i++) if (i != id) release(i);
                    buttons = 0; leftX = leftY = rightX = rightY = leftTrigger = rightTrigger = 0;
                }
                break;
            default: break;
        }
        element[id] = NONE;
        send();
    }

    private void send() {
        VirtualPad.nativeSetState(buttons, leftX, leftY, rightX, rightY, leftTrigger, rightTrigger);
        invalidate();
    }
}
