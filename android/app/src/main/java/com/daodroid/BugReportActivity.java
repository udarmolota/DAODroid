package com.daodroid;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.Spinner;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.FileProvider;

import com.google.android.material.appbar.MaterialToolbar;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * "Report a bug", as in Zomdroid: choose the game and what goes into the report (the game's
 * log, Android's log of this app, the settings, crash records; the device and build are always
 * in), then send it by email (the chooser lists mail apps only), or save it to a file and send
 * it from anywhere to the address shown.
 */
public class BugReportActivity extends AppCompatActivity {
    private final List<Instance> instances = new ArrayList<>();
    private Spinner instance;
    private CheckBox incLog, incLogcat, incConfig, incCrash;

    private final ActivityResultLauncher<String> save =
            registerForActivityResult(new ActivityResultContracts.CreateDocument("application/zip"), uri -> {
                if (uri == null) return;
                try (OutputStream out = getContentResolver().openOutputStream(uri)) {
                    writeReport(out);
                    Toast.makeText(this, R.string.bug_report_saved, Toast.LENGTH_LONG).show();
                } catch (IOException e) {
                    Toast.makeText(this, e.toString(), Toast.LENGTH_LONG).show();
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_bug_report);
        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material);
        toolbar.setNavigationOnClickListener(v -> finish());

        instances.addAll(Instance.list(this));
        List<String> names = new ArrayList<>();
        for (Instance i : instances) names.add(i.name + " — " + i.buildLabel());
        instance = findViewById(R.id.instance);
        ArrayAdapter<String> a = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, names);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        instance.setAdapter(a);
        String wanted = getIntent().getStringExtra(InstanceActivity.EXTRA_ID);
        for (int i = 0; i < instances.size(); i++) if (instances.get(i).id.equals(wanted)) instance.setSelection(i);

        incLog = findViewById(R.id.inc_log);
        incLogcat = findViewById(R.id.inc_logcat);
        incConfig = findViewById(R.id.inc_config);
        incCrash = findViewById(R.id.inc_crash);

        findViewById(R.id.email).setOnClickListener(v -> sendEmail());
        findViewById(R.id.save).setOnClickListener(v -> save.launch("daodroid_report_" + stamp() + ".zip"));
        findViewById(R.id.copy).setOnClickListener(v -> {
            ClipboardManager cm = getSystemService(ClipboardManager.class);
            cm.setPrimaryClip(ClipData.newPlainText("e-mail", getString(R.string.bug_report_email)));
            Toast.makeText(this, R.string.bug_report_copied, Toast.LENGTH_SHORT).show();
        });
    }

    private Instance selected() {
        int i = instance.getSelectedItemPosition();
        return i >= 0 && i < instances.size() ? instances.get(i) : null;
    }

    private void sendEmail() {
        try {
            File dir = new File(getCacheDir(), "logs");
            //noinspection ResultOfMethodCallIgnored
            dir.mkdirs();
            File zip = new File(dir, "daodroid_report_" + stamp() + ".zip");
            try (OutputStream out = new FileOutputStream(zip)) {
                writeReport(out);
            }
            Uri attach = FileProvider.getUriForFile(this, getPackageName() + ".files", zip);
            Intent send = new Intent(Intent.ACTION_SEND)
                    .setType("application/zip")
                    .putExtra(Intent.EXTRA_EMAIL, new String[] { getString(R.string.bug_report_email) })
                    .putExtra(Intent.EXTRA_SUBJECT, "DAODroid bug report")
                    .putExtra(Intent.EXTRA_TEXT, deviceInfo() + "\n\nWhat happened:\n")
                    .putExtra(Intent.EXTRA_STREAM, attach)
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            // Mail apps only: those that handle mailto: and also take this ACTION_SEND.
            List<Intent> targets = new ArrayList<>();
            for (ResolveInfo r : getPackageManager().queryIntentActivities(
                    new Intent(Intent.ACTION_SENDTO, Uri.parse("mailto:")), 0)) {
                Intent t = new Intent(send).setPackage(r.activityInfo.packageName);
                if (!getPackageManager().queryIntentActivities(t, 0).isEmpty()) targets.add(t);
            }
            if (targets.isEmpty()) {
                Toast.makeText(this, R.string.bug_report_no_mail_app, Toast.LENGTH_LONG).show();
                return;
            }
            Intent chooser = Intent.createChooser(targets.remove(0), getString(R.string.bug_report_send_email));
            chooser.putExtra(Intent.EXTRA_INITIAL_INTENTS, targets.toArray(new Intent[0]));
            startActivity(chooser);
        } catch (IOException e) {
            Toast.makeText(this, e.toString(), Toast.LENGTH_LONG).show();
        }
    }

    private void writeReport(OutputStream out) throws IOException {
        Instance i = selected();
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            put(zip, "device.txt", deviceInfo().getBytes(StandardCharsets.UTF_8));
            if (i != null) {
                put(zip, "instance.txt", (i.name + "\n" + i.buildLabel() + "\ndriver " + i.driver + ", sysmem " + i.sysmem
                        + ", texture LOD " + i.textureLodBias + ", prerotate " + i.prerotate + "\n\n"
                        + i.manifest.toString()).getBytes(StandardCharsets.UTF_8));
                if (incLog.isChecked()) putFile(zip, "dao.log", new File(i.userdata(), "dao.log"));
                if (incConfig.isChecked()) {
                    putFile(zip, "config.toml", new File(i.userdata(), "config.toml"));
                    putFile(zip, "env.txt", new File(i.userdata(), "env.txt"));
                }
            }
            if (incLogcat.isChecked()) put(zip, "logcat.txt", run("logcat", "-d", "-v", "time"));
            if (incCrash.isChecked()) put(zip, "crash.txt", run("logcat", "-d", "-b", "crash", "-v", "time"));
        }
    }

    private String deviceInfo() {
        String version = "?";
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception ignored) {
        }
        String gpu = "?";
        try (InputStream in = new FileInputStream("/sys/class/kgsl/kgsl-3d0/gpu_model")) {
            gpu = new String(readAll(in), StandardCharsets.UTF_8).trim();
        } catch (IOException ignored) {
        }
        return "DAODroid " + version + " (abi " + GameActivity.DAO_ABI + ")"
                + "\nDevice: " + Build.MANUFACTURER + " " + Build.MODEL
                + "\nAndroid: " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")"
                + "\nSoC: " + (Build.VERSION.SDK_INT >= 31 ? Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL : "?")
                + "\nGPU: " + gpu;
    }

    private static byte[] run(String... command) {
        try {
            Process p = new ProcessBuilder(command).redirectErrorStream(true).start();
            byte[] out = readAll(p.getInputStream());
            p.waitFor();
            return out;
        } catch (IOException | InterruptedException e) {
            return e.toString().getBytes(StandardCharsets.UTF_8);
        }
    }

    private static void putFile(ZipOutputStream zip, String name, File f) throws IOException {
        if (!f.isFile()) return;
        try (InputStream in = new FileInputStream(f)) {
            put(zip, name, readAll(in));
        }
    }

    private static void put(ZipOutputStream zip, String name, byte[] data) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(data);
        zip.closeEntry();
    }

    private static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] b = new byte[1 << 16];
        int n;
        while ((n = in.read(b)) > 0) out.write(b, 0, n);
        return out.toByteArray();
    }

    private static String stamp() {
        return new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
    }
}
