package com.daodroid;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.widget.Toast;

import androidx.core.content.FileProvider;

import com.google.android.material.dialog.MaterialAlertDialogBuilder;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** What the launcher does to an instance from its card, its settings and the side menu. */
final class InstanceActions {
    private InstanceActions() {}

    /** Asks which instance when there are several; runs at once with one. */
    static void choose(Activity a, int title, List<Instance> instances, java.util.function.Consumer<Instance> then) {
        if (instances.isEmpty()) {
            Toast.makeText(a, R.string.launcher_empty_title, Toast.LENGTH_LONG).show();
            return;
        }
        if (instances.size() == 1) {
            then.accept(instances.get(0));
            return;
        }
        String[] names = new String[instances.size()];
        for (int i = 0; i < names.length; i++) names[i] = instances.get(i).name;
        new MaterialAlertDialogBuilder(a).setTitle(title)
                .setItems(names, (d, which) -> then.accept(instances.get(which)))
                .show();
    }

    /** A driver .so, or the first .so in a .zip (Adreno Tools packages), as the instance's custom driver. */
    static void installDriver(Activity a, Instance instance, Uri uri) {
        File dir = instance.driverDir();
        try {
            //noinspection ResultOfMethodCallIgnored
            dir.mkdirs();
            String name = displayName(a, uri);
            File target = null;
            if (name != null && name.toLowerCase().endsWith(".zip")) {
                try (InputStream in = a.getContentResolver().openInputStream(uri); ZipInputStream zip = new ZipInputStream(in)) {
                    ZipEntry e;
                    while ((e = zip.getNextEntry()) != null) {
                        if (!e.isDirectory() && e.getName().endsWith(".so")) {
                            clearDrivers(dir);
                            target = new File(dir, new File(e.getName()).getName());
                            copy(zip, target);
                            break;
                        }
                    }
                }
            } else {
                clearDrivers(dir);
                target = new File(dir, name != null && name.endsWith(".so") ? name : "libvulkan_custom.so");
                try (InputStream in = a.getContentResolver().openInputStream(uri)) {
                    copy(in, target);
                }
            }
            if (target == null) {
                Toast.makeText(a, R.string.driver_not_found, Toast.LENGTH_LONG).show();
                return;
            }
            instance.driver = Instance.DRIVER_CUSTOM;
            instance.save();
            Toast.makeText(a, a.getString(R.string.driver_installed, target.getName()), Toast.LENGTH_LONG).show();
        } catch (IOException e) {
            Toast.makeText(a, e.toString(), Toast.LENGTH_LONG).show();
        }
    }

    /**
     * Saves from a .zip: the folders of userdata/content/454108C0/00000001 (from DAORecomp on a
     * PC, or another instance), with or without that path in the zip.
     */
    static void installSaves(Activity a, Instance instance, Uri uri) {
        File saves = new File(instance.userdata(), "content/454108C0/00000001");
        int files = 0;
        try (InputStream in = a.getContentResolver().openInputStream(uri); ZipInputStream zip = new ZipInputStream(in)) {
            ZipEntry e;
            while ((e = zip.getNextEntry()) != null) {
                if (e.isDirectory()) continue;
                String name = e.getName().replace('\\', '/');
                int at = name.indexOf("00000001/");
                String rel = at >= 0 ? name.substring(at + "00000001/".length()) : name;
                if (rel.isEmpty() || rel.contains("..")) continue;
                File target = new File(saves, rel);
                //noinspection ResultOfMethodCallIgnored
                target.getParentFile().mkdirs();
                copy(zip, target);
                files++;
            }
            Toast.makeText(a, a.getString(R.string.saves_installed, files), Toast.LENGTH_LONG).show();
        } catch (IOException e) {
            Toast.makeText(a, e.toString(), Toast.LENGTH_LONG).show();
        }
    }

    /**
     * Xenia's shader and pipeline storage (userdata/shader_cache/shaders/shareable: the guest
     * shaders and the pipeline descriptions, not driver-specific) from another instance of the
     * same game, so a new one starts warm: no stutter and no skipped one-time draws (the HUD
     * portraits) while the first sessions fill its own. Returns the files copied.
     */
    static int copyShaderCache(Instance from, Instance to) throws IOException {
        File src = new File(from.userdata(), "shader_cache/shaders/shareable");
        File dst = new File(to.userdata(), "shader_cache/shaders/shareable");
        File[] files = src.listFiles();
        if (files == null) return 0;
        //noinspection ResultOfMethodCallIgnored
        dst.mkdirs();
        int n = 0;
        for (File f : files) {
            if (!f.isFile()) continue;
            File target = new File(dst, f.getName());
            if (target.isFile() && target.length() >= f.length()) continue; // keep the bigger one
            try (InputStream in = new FileInputStream(f)) {
                copy(in, target);
            }
            n++;
        }
        return n;
    }

    /** The other instance with the biggest shader cache, or null. */
    static Instance warmestOther(List<Instance> all, Instance self) {
        Instance best = null;
        long bestSize = 0;
        for (Instance i : all) {
            if (i.id.equals(self.id)) continue;
            long size = 0;
            File[] files = new File(i.userdata(), "shader_cache/shaders/shareable").listFiles();
            if (files != null) for (File f : files) size += f.length();
            if (size > bestSize) {
                best = i;
                bestSize = size;
            }
        }
        return best;
    }

    /** Asks which game to take the shader cache from, then copies it. */
    static void importShaderCache(Activity a, Instance to, List<Instance> all) {
        List<Instance> others = new java.util.ArrayList<>();
        for (Instance i : all) if (!i.id.equals(to.id)) others.add(i);
        if (others.isEmpty()) {
            Toast.makeText(a, R.string.cache_no_other, Toast.LENGTH_LONG).show();
            return;
        }
        choose(a, R.string.cache_import_from, others, from -> {
            try {
                int n = copyShaderCache(from, to);
                Toast.makeText(a, n > 0 ? a.getString(R.string.cache_imported, n, from.name)
                        : a.getString(R.string.cache_up_to_date, to.name), Toast.LENGTH_LONG).show();
            } catch (IOException e) {
                Toast.makeText(a, e.toString(), Toast.LENGTH_LONG).show();
            }
        });
    }

    /** The instance's saves folder (the game's save content, user 1). */
    static File savesDir(Instance instance) {
        return new File(instance.userdata(), "content/454108C0/00000001");
    }

    /** The saves as a .zip into the document the player picked (CreateDocument). */
    static void exportSaves(Activity a, Instance instance, Uri uri) {
        File saves = savesDir(instance);
        int files = 0;
        try (OutputStream out = a.getContentResolver().openOutputStream(uri);
             java.util.zip.ZipOutputStream zip = new java.util.zip.ZipOutputStream(out)) {
            files = addTree(zip, saves, "content/454108C0/00000001/");
        } catch (IOException e) {
            Toast.makeText(a, e.toString(), Toast.LENGTH_LONG).show();
            return;
        }
        Toast.makeText(a, a.getString(R.string.saves_exported, files), Toast.LENGTH_LONG).show();
    }

    static String savesFileName(Instance instance) {
        return "DAO-saves-" + new java.text.SimpleDateFormat("yyyyMMdd-HHmm", java.util.Locale.ROOT).format(new java.util.Date()) + ".zip";
    }

    static int addTree(java.util.zip.ZipOutputStream zip, File dir, String prefix) throws IOException {
        int n = 0;
        File[] children = dir.listFiles();
        if (children == null) return 0;
        java.util.Arrays.sort(children);
        for (File c : children) {
            if (c.isDirectory()) {
                n += addTree(zip, c, prefix + c.getName() + "/");
            } else {
                zip.putNextEntry(new ZipEntry(prefix + c.getName()));
                try (InputStream in = new FileInputStream(c)) {
                    byte[] b = new byte[1 << 16];
                    int r;
                    while ((r = in.read(b)) > 0) zip.write(b, 0, r);
                }
                zip.closeEntry();
                n++;
            }
        }
        return n;
    }

    static void shareLog(Activity a, Instance instance) {
        File log = new File(instance.userdata(), "dao.log");
        if (!log.isFile()) {
            Toast.makeText(a, R.string.instance_no_log, Toast.LENGTH_LONG).show();
            return;
        }
        try {
            File shared = new File(new File(a.getCacheDir(), "logs"), "dao-" + instance.id + ".log");
            //noinspection ResultOfMethodCallIgnored
            shared.getParentFile().mkdirs();
            try (InputStream in = new FileInputStream(log)) {
                copy(in, shared);
            }
            Uri uri = FileProvider.getUriForFile(a, a.getPackageName() + ".files", shared);
            a.startActivity(Intent.createChooser(new Intent(Intent.ACTION_SEND)
                    .setType("text/plain")
                    .putExtra(Intent.EXTRA_STREAM, uri)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION), a.getString(R.string.instance_export_log)));
        } catch (IOException e) {
            Toast.makeText(a, e.toString(), Toast.LENGTH_LONG).show();
        }
    }

    static void confirmDelete(Activity a, Instance instance, Runnable after) {
        new MaterialAlertDialogBuilder(a)
                .setMessage(a.getString(R.string.instance_delete_confirm, instance.name))
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(R.string.instance_delete_go, (d, w) -> {
                    instance.delete(a);
                    after.run();
                })
                .show();
    }

    private static void clearDrivers(File dir) {
        File[] old = dir.listFiles();
        if (old != null) {
            for (File f : old) {
                if (f.getName().endsWith(".so")) //noinspection ResultOfMethodCallIgnored
                    f.delete();
            }
        }
    }

    static String displayName(Activity a, Uri uri) {
        try (android.database.Cursor c = a.getContentResolver().query(uri,
                new String[] { android.provider.OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        }
        return uri.getLastPathSegment();
    }

    private static void copy(InputStream in, File target) throws IOException {
        try (OutputStream out = new FileOutputStream(target)) {
            byte[] b = new byte[1 << 16];
            int n;
            while ((n = in.read(b)) > 0) out.write(b, 0, n);
        }
    }
}
