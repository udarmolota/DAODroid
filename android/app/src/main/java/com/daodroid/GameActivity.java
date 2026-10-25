package com.daodroid;

import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * The game's activity: SDL's, with the game library (libdaodroid.so, the runtime and the
 * translated game code) as the main one. The game's files live in the app's external files
 * folder, {@code Android/data/com.daodroid/files/dao/{game,userdata}} (DAORecomp's install/game
 * copied there); a Vulkan driver to run on instead of the phone's own (a Turnip build) is the
 * first .so in {@code files/dao/driver/}. Phase 5: no launcher screen yet, the game starts at
 * once; what's missing is logged to logcat (tag DAODroid) and userdata/dao.log.
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "DAODroid";

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "daodroid" };
    }

    // The game library (libdaodroid.so: the runtime, Xenia and the translated game code). A
    // game package built on the PC puts it at dao/libdaodroid.so; it is copied into the app's
    // internal files (code can't be mapped from the shared storage) and loaded from there.
    // Without one, the copy built into this APK is used (the development build); a launcher
    // build has none and asks for a package.
    //
    // The version of the interface between this Java side and the library (JNI methods, SDL,
    // command-line options). Keep in step with DAO_ABI_VERSION in cpp/dao_abi.h.
    static final int DAO_ABI = 1;
    private String gameLibrary;

    private static native int nativeAbiVersion();

    @Override
    public void loadLibraries() {
        System.loadLibrary("SDL3");
        File packaged = new File(installRoot(this), "libdaodroid.so");
        if (packaged.isFile()) {
            File internal = copyToInternal(packaged, "game");
            if (internal == null) {
                throw new UnsatisfiedLinkError("Couldn't copy the game library " + packaged);
            }
            System.load(internal.getAbsolutePath());
            gameLibrary = internal.getAbsolutePath();
            Log.i(TAG, "Game library: from the game package (" + packaged + ")");
        } else {
            try {
                System.loadLibrary("daodroid");
            } catch (UnsatisfiedLinkError e) {
                throw new UnsatisfiedLinkError("No game package: put the one you built on your PC into "
                        + installRoot(this) + " (see the README)");
            }
            gameLibrary = getApplicationInfo().nativeLibraryDir + "/libdaodroid.so";
            Log.i(TAG, "Game library: the one built into the APK");
        }
        int abi = nativeAbiVersion();
        if (abi != DAO_ABI) {
            throw new UnsatisfiedLinkError("The game package was built for launcher version " + abi
                    + ", this launcher is version " + DAO_ABI + ": build the package again with the matching tools");
        }
    }

    @Override
    protected String getMainSharedObject() {
        return gameLibrary != null ? gameLibrary : super.getMainSharedObject();
    }

    @Override
    protected String[] getArguments() {
        List<String> args = new ArrayList<>();
        File install = installRoot(this);
        applyEnvFile(new File(new File(install, "userdata"), "env.txt"));
        args.add(install.getAbsolutePath());
        File driver = vulkanDriver(install);
        if (driver != null) {
            args.add("--vulkan-driver=" + driver.getAbsolutePath());
            args.add("--native-lib-dir=" + getApplicationInfo().nativeLibraryDir);
            Log.i(TAG, "Vulkan driver: " + driver.getAbsolutePath());
        } else {
            Log.i(TAG, "Vulkan driver: the phone's own (no .so in " + new File(install, "driver") + ")");
        }
        return args.toArray(new String[0]);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File install = installRoot(this);
        File game = new File(install, "game");
        if (!new File(game, "default.xex").isFile()) {
            Log.e(TAG, "No game at " + game + ": copy DAORecomp's install/game there (see docs/PHASE5_runbook.md)");
        }
        super.onCreate(savedInstanceState);
        // The on-screen gamepad over SDL's surface (mLayout holds the surface).
        if (mLayout != null) {
            mLayout.addView(new VirtualPadView(this), new android.view.ViewGroup.LayoutParams(
                    android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT));
            setUpPerfOverlay();
        }
    }

    // The game's keyboard (XamShowKeyboardUI, e.g. the character's name): an Android dialog
    // with a text field. Called from the game's thread (keyboard_android.cpp), which waits
    // until nativeKeyboardDone gives the text, or null when the player cancels.
    private static native void nativeKeyboardDone(String text);

    @SuppressWarnings("unused") // from native code
    public void showKeyboard(final String title, final String description, final String text, final int maxLength) {
        runOnUiThread(() -> {
            final android.widget.EditText field = new android.widget.EditText(this);
            field.setSingleLine(true);
            field.setText(text);
            field.setSelectAllOnFocus(true);
            field.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE
                    | android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            if (maxLength > 0) {
                field.setFilters(new android.text.InputFilter[] { new android.text.InputFilter.LengthFilter(maxLength) });
            }
            float dp = getResources().getDisplayMetrics().density;
            android.widget.FrameLayout box = new android.widget.FrameLayout(this);
            int pad = Math.round(20 * dp);
            box.setPadding(pad, Math.round(8 * dp), pad, 0);
            box.addView(field);

            final boolean[] answered = { false };
            final android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(this,
                    android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle(title == null || title.isEmpty() ? "Dragon Age" : title)
                    .setMessage(description == null || description.isEmpty() ? null : description)
                    .setView(box)
                    .setPositiveButton(android.R.string.ok, (d, w) -> {
                        answered[0] = true;
                        nativeKeyboardDone(field.getText().toString());
                    })
                    .setNegativeButton(android.R.string.cancel, (d, w) -> {
                        answered[0] = true;
                        nativeKeyboardDone(null);
                    })
                    .create();
            dialog.setOnDismissListener(d -> {
                if (!answered[0]) {
                    answered[0] = true;
                    nativeKeyboardDone(null);
                }
            });
            field.setOnEditorActionListener((v, actionId, event) -> {
                if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE && !answered[0]) {
                    answered[0] = true;
                    nativeKeyboardDone(field.getText().toString());
                    dialog.dismiss();
                    return true;
                }
                return false;
            });
            if (dialog.getWindow() != null) {
                dialog.getWindow().setSoftInputMode(
                        android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
            }
            dialog.show();
            field.requestFocus();
        });
    }

    // The performance bar (fps, CPU, GPU, RAM, thermal), from the launchers; userdata/env.txt
    // DAO_PERF_OVERLAY=0 hides it.
    private static native long nativeGetPresentedFrames();

    private PerfOverlayView perfView;
    private PerfSampler perfSampler;
    private HandlerThread perfThread;
    private Handler perfHandler;
    private final Handler uiHandler = new Handler(Looper.getMainLooper());

    private final Runnable perfTick = new Runnable() {
        @Override public void run() {
            if (perfView == null || perfSampler == null || perfHandler == null) return;
            final PerfSampler.Stats stats = perfSampler.sample(nativeGetPresentedFrames());
            uiHandler.post(() -> { if (perfView != null) perfView.setStats(stats); });
            perfHandler.postDelayed(this, 1000);
        }
    };

    private void setUpPerfOverlay() {
        // getArguments (the game's env) runs later, on SDL's thread: read env.txt here too.
        applyEnvFile(new File(new File(installRoot(this), "userdata"), "env.txt"));
        if ("0".equals(envOverrides.get("DAO_PERF_OVERLAY"))) {
            return;
        }
        float dp = getResources().getDisplayMetrics().density;
        perfView = new PerfOverlayView(this, "VK");
        // SDL's root layout is a RelativeLayout: centre by its rule, not by gravity.
        android.widget.RelativeLayout.LayoutParams lp = new android.widget.RelativeLayout.LayoutParams(
                android.widget.RelativeLayout.LayoutParams.WRAP_CONTENT,
                android.widget.RelativeLayout.LayoutParams.WRAP_CONTENT);
        lp.addRule(android.widget.RelativeLayout.ALIGN_PARENT_TOP);
        lp.addRule(android.widget.RelativeLayout.CENTER_HORIZONTAL);
        lp.setMargins(0, Math.round(4 * dp), 0, 0);
        mLayout.addView(perfView, lp);   // not clickable: touches reach the pad underneath
        perfSampler = new PerfSampler(this);
        perfThread = new HandlerThread("PerfOverlay");
        perfThread.start();
        perfHandler = new Handler(perfThread.getLooper());
        perfHandler.postDelayed(perfTick, 1000);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (perfThread != null) {
            perfThread.quitSafely();
            perfThread = null;
            perfHandler = null;
        }
    }

    /**
     * userdata/env.txt: KEY=VALUE lines set in the game's environment before it starts (the
     * DAO_* switches the headless build takes: DAO_CAPTURE_EVERY, DAO_DUMP_AFTER, DAO_GPU, ...).
     */
    private static final java.util.Map<String, String> envOverrides = new java.util.HashMap<>();

    private static void applyEnvFile(File file) {
        envOverrides.clear();
        if (!file.isFile()) {
            return;
        }
        try (java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(file))) {
            String line;
            while ((line = reader.readLine()) != null) {
                line = line.trim();
                int eq = line.indexOf('=');
                if (line.isEmpty() || line.startsWith("#") || eq <= 0) {
                    continue;
                }
                nativeSetenv(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
                envOverrides.put(line.substring(0, eq).trim(), line.substring(eq + 1).trim());
                Log.i(TAG, "env: " + line);
            }
        } catch (java.io.IOException e) {
            Log.w(TAG, "env.txt: " + e);
        }
    }

    /** {@code Android/data/com.daodroid/files/dao}: game/ and userdata/ in it. */
    static File installRoot(Context context) {
        File external = context.getExternalFilesDir(null);
        File root = new File(external != null ? external : context.getFilesDir(), "dao");
        //noinspection ResultOfMethodCallIgnored
        new File(root, "userdata").mkdirs();
        return root;
    }

    /**
     * The first .so in install/driver, if any, copied into the app's internal files (code
     * can't be mapped from the shared storage), where it is returned from.
     */
    File vulkanDriver(File install) {
        File[] files = new File(install, "driver").listFiles();
        if (files == null) {
            return null;
        }
        for (File file : files) {
            if (file.isFile() && file.getName().endsWith(".so")) {
                return copyToInternal(file, "driver");
            }
        }
        return null;
    }

    /** A copy of file in the app's internal files/dir (refreshed when the original changes). */
    File copyToInternal(File file, String dir) {
        File internal = new File(new File(getFilesDir(), dir), file.getName());
        if (!internal.isFile() || internal.length() != file.length() || internal.lastModified() < file.lastModified()) {
            try {
                //noinspection ResultOfMethodCallIgnored
                internal.getParentFile().mkdirs();
                java.nio.file.Files.copy(file.toPath(), internal.toPath(),
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            } catch (java.io.IOException e) {
                Log.e(TAG, "Copying " + file + " failed", e);
                return null;
            }
        }
        return internal;
    }
}
