package com.daodroid;

import android.content.Context;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/**
 * One installed game: a game package imported into the launcher (docs/PACKAGE_FORMAT.md), or
 * the development build's own game (the library inside the APK, the game in the old dao/
 * folder). Its files: {@code root} (shared storage: game/, userdata/, driver/, instance.json)
 * and, for a package, the game library in the app's internal files (code can't be mapped from
 * shared storage).
 */
final class Instance {
    static final String DEV_ID = "dev";

    // Settings, kept in instance.json.
    static final String DRIVER_BUILTIN = "builtin", DRIVER_SYSTEM = "system", DRIVER_CUSTOM = "custom";
    static final String SYSMEM_AUTO = "auto", SYSMEM_ON = "on", SYSMEM_OFF = "off";

    final String id;
    final File root;
    /** The game library to load, or null for the one inside the APK. */
    final File library;
    String name;
    JSONObject manifest; // the package's manifest.json (empty for the development build)
    String driver = DRIVER_BUILTIN;
    String sysmem = SYSMEM_AUTO;
    int textureLodBias = 1;
    boolean prerotate = true;
    boolean perfOverlay = true;

    private Instance(String id, File root, File library) {
        this.id = id;
        this.root = root;
        this.library = library;
    }

    File game() { return new File(root, "game"); }
    File userdata() { return new File(root, "userdata"); }
    File driverDir() { return new File(root, "driver"); }
    boolean isDev() { return DEV_ID.equals(id); }

    /** The first .so in driver/ (a custom driver), or null. */
    File customDriver() {
        File[] files = driverDir().listFiles();
        if (files != null) {
            for (File f : files) {
                if (f.isFile() && f.getName().endsWith(".so")) return f;
            }
        }
        return null;
    }

    String buildLabel() {
        if (isDev()) return "development build (game built into the APK)";
        JSONObject build = manifest.optJSONObject("build");
        String commit = build != null ? build.optString("daodroid", "?") : "?";
        if (commit.length() > 8) commit = commit.substring(0, 8);
        Object tu = manifest.opt("title_update");
        return "build " + commit + (tu instanceof Number ? ", Title Update " + tu : "");
    }

    void save() throws IOException {
        JSONObject o = new JSONObject();
        try {
            o.put("name", name);
            o.put("manifest", manifest);
            JSONObject s = new JSONObject();
            s.put("driver", driver);
            s.put("sysmem", sysmem);
            s.put("texture_lod_bias", textureLodBias);
            s.put("prerotate", prerotate);
            s.put("perf_overlay", perfOverlay);
            o.put("settings", s);
        } catch (JSONException e) {
            throw new IOException(e);
        }
        //noinspection ResultOfMethodCallIgnored
        root.mkdirs();
        String text;
        try {
            text = o.toString(2);
        } catch (JSONException e) {
            throw new IOException(e);
        }
        Files.write(new File(root, "instance.json").toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    private void load() {
        File f = new File(root, "instance.json");
        if (!f.isFile()) return;
        try {
            JSONObject o = new JSONObject(new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8));
            name = o.optString("name", name);
            JSONObject m = o.optJSONObject("manifest");
            if (m != null) manifest = m;
            JSONObject s = o.optJSONObject("settings");
            if (s != null) {
                driver = s.optString("driver", driver);
                sysmem = s.optString("sysmem", sysmem);
                textureLodBias = s.optInt("texture_lod_bias", textureLodBias);
                prerotate = s.optBoolean("prerotate", prerotate);
                perfOverlay = s.optBoolean("perf_overlay", perfOverlay);
            }
        } catch (IOException | JSONException e) {
            android.util.Log.w("DAODroid", "instance.json of " + id + ": " + e);
        }
    }

    // ---------------------------------------------------------------- the store

    /** dao/ on the shared storage: the development build's game, and instances/. */
    static File daoRoot(Context context) {
        File external = context.getExternalFilesDir(null);
        return new File(external != null ? external : context.getFilesDir(), "dao");
    }

    static File instancesRoot(Context context) {
        return new File(daoRoot(context), "instances");
    }

    static File libraryFor(Context context, String id) {
        return new File(new File(new File(context.getFilesDir(), "instances"), id), "libdaodroid.so");
    }

    /** Whether this APK holds the game library (the development build). */
    static boolean apkHasGameLibrary(Context context) {
        return new File(context.getApplicationInfo().nativeLibraryDir, "libdaodroid.so").isFile();
    }

    static Instance open(Context context, String id) {
        Instance i;
        if (DEV_ID.equals(id)) {
            i = new Instance(id, daoRoot(context), null);
            i.name = "Dragon Age: Origins (development)";
            i.manifest = new JSONObject();
            i.driver = DRIVER_CUSTOM; // the dev build's driver/ folder, as before the launcher
        } else {
            i = new Instance(id, new File(instancesRoot(context), id), libraryFor(context, id));
            i.name = "Dragon Age: Origins";
            i.manifest = new JSONObject();
        }
        i.load();
        return i;
    }

    static List<Instance> list(Context context) {
        List<Instance> out = new ArrayList<>();
        if (apkHasGameLibrary(context) && new File(new File(daoRoot(context), "game"), "default.xex").isFile()) {
            out.add(open(context, DEV_ID));
        }
        File[] dirs = instancesRoot(context).listFiles();
        if (dirs != null) {
            java.util.Arrays.sort(dirs);
            for (File d : dirs) {
                // An instance is complete once its instance.json and library are in place.
                if (d.isDirectory() && new File(d, "instance.json").isFile()
                        && libraryFor(context, d.getName()).isFile()) {
                    out.add(open(context, d.getName()));
                }
            }
        }
        return out;
    }

    static Instance create(Context context, JSONObject manifest) {
        String id = "i" + System.currentTimeMillis();
        Instance i = new Instance(id, new File(instancesRoot(context), id), libraryFor(context, id));
        i.manifest = manifest;
        Object tu = manifest.opt("title_update");
        i.name = "Dragon Age: Origins" + (tu instanceof Number ? " (TU" + tu + ")" : "");
        return i;
    }

    /** Removes the instance's files (its game, saves and library). */
    void delete(Context context) {
        if (isDev()) {
            // The development build's files share dao/ with the instances: only its own go.
            for (String name : new String[] { "game", "userdata", "driver", "title_update", "instance.json", "banner.jpg" }) {
                deleteTree(new File(root, name));
            }
            return;
        }
        deleteTree(root);
        if (library != null) deleteTree(library.getParentFile());
    }

    static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        File[] children = f.listFiles();
        if (children != null) {
            for (File c : children) deleteTree(c);
        }
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }
}
