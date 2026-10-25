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
 * translated game code) as the main one. It runs one instance (Instance, chosen in the
 * launcher; without one, the development build's game in {@code files/dao/}), with that
 * instance's files ({@code game/}, {@code userdata/}, {@code driver/}) and settings. What's
 * missing is logged to logcat (tag DAODroid) and the instance's userdata/dao.log.
 */
public class GameActivity extends SDLActivity {
    private static final String TAG = "DAODroid";
    /** The built-in Turnip (Mesa, MIT): Turnip Gen8 V38 for the Adreno 8xx, in the APK's libraries. */
    static final String BUILTIN_DRIVER = "libvulkan_turnip_gen8.so";

    /** The instance being played; set from the intent before SDL starts. */
    private static Instance current;

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
        if (current != null && current.library != null) {
            if (!current.library.isFile()) {
                throw new UnsatisfiedLinkError("This game's library is missing: add the game package again");
            }
            System.load(current.library.getAbsolutePath());
            gameLibrary = current.library.getAbsolutePath();
            Log.i(TAG, "Game library: instance " + current.id);
        } else if (packaged.isFile()) {
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
        applyInstanceSettings();
        applyEnvFile(new File(new File(install, "userdata"), "env.txt")); // env.txt wins
        args.add(install.getAbsolutePath());
        File driver = chosenDriver(install);
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
        String id = getIntent() != null ? getIntent().getStringExtra(InstanceActivity.EXTRA_ID) : null;
        current = id != null ? Instance.open(this, id)
                : (Instance.apkHasGameLibrary(this) ? Instance.open(this, Instance.DEV_ID) : null);
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
        if ("0".equals(envOverrides.get("DAO_PERF_OVERLAY")) || (current != null && !current.perfOverlay)) {
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

    /** The instance's folder (game/, userdata/, driver/), or {@code files/dao} without one. */
    static File installRoot(Context context) {
        File root = current != null ? current.root : Instance.daoRoot(context);
        //noinspection ResultOfMethodCallIgnored
        new File(root, "userdata").mkdirs();
        return root;
    }

    /** The instance's settings into the game's environment and its config.toml. */
    private void applyInstanceSettings() {
        if (current == null) return;
        switch (current.sysmem) {
            case Instance.SYSMEM_ON: nativeSetenv("TU_DEBUG", "sysmem"); break;
            case Instance.SYSMEM_OFF: nativeSetenv("TU_DEBUG", ""); break; // set: the library's Adreno 8xx default stays off
            default: break; // the library decides from the GPU
        }
        File config = new File(current.userdata(), "config.toml");
        try {
            XeniaConfig.set(config, "dao_texture_lod_bias", String.valueOf(current.textureLodBias));
            XeniaConfig.set(config, "dao_present_prerotate", current.prerotate ? "true" : "false");
        } catch (java.io.IOException e) {
            Log.w(TAG, "config.toml: " + e);
        }
        Log.i(TAG, "Instance " + current.id + ": driver " + current.driver + ", sysmem " + current.sysmem
                + ", texture LOD " + current.textureLodBias + ", prerotate " + current.prerotate);
    }

    /**
     * The Vulkan driver the instance chose, copied into the app's internal files when it comes
     * from the shared storage (code can't be mapped from there), or null for the phone's own.
     */
    File chosenDriver(File install) {
        String choice = current != null ? current.driver : Instance.DRIVER_CUSTOM;
        if (Instance.DRIVER_SYSTEM.equals(choice)) {
            return null;
        }
        if (Instance.DRIVER_BUILTIN.equals(choice)) {
            File builtin = new File(getApplicationInfo().nativeLibraryDir, BUILTIN_DRIVER);
            if (builtin.isFile()) return builtin;
            Log.w(TAG, "No built-in driver in this APK; using the phone's own");
            return null;
        }
        File[] files = new File(install, "driver").listFiles();
        if (files == null) {
            return null;
        }
        for (File file : files) {
            if (file.isFile() && file.getName().endsWith(".so")) {
                return copyToInternal(file, current != null && !current.isDev() ? "driver/" + current.id : "driver");
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
