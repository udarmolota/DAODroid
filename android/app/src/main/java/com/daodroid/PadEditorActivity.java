package com.daodroid;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;

/**
 * The on-screen gamepad's layout editor, outside the game: the pad in edit mode over a dark
 * background, landscape like the game. Drag the buttons, then Done; Reset goes back to the
 * default layout. The positions are the ones the game uses (VirtualPadView's preferences).
 */
public class PadEditorActivity extends Activity {
    private VirtualPadView pad;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(40, 14, 14));
        pad = new VirtualPadView(this);
        pad.sendsToGame = false;
        root.addView(pad, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        Button reset = new Button(this);
        reset.setText(R.string.pad_reset);
        reset.setOnClickListener(v -> pad.resetLayout());
        Button done = new Button(this);
        done.setText(R.string.pad_done);
        done.setOnClickListener(v -> {
            pad.finishEditing();
            finish();
        });
        buttons.addView(reset);
        buttons.addView(done);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        lp.bottomMargin = Math.round(24 * getResources().getDisplayMetrics().density);
        root.addView(buttons, lp);
        setContentView(root);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        pad.post(pad::startEditing);
    }

    @Override
    public void onBackPressed() {
        pad.finishEditing();
        super.onBackPressed();
    }
}
