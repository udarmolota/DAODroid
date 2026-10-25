package com.daodroid;

import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.text.format.Formatter;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ImageView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.materialswitch.MaterialSwitch;

import java.io.File;
import java.io.IOException;

/** One game's settings: its details, Play, the driver and graphics switches, saves, the log, Delete. */
public class InstanceActivity extends AppCompatActivity {
    static final String EXTRA_ID = "instance";
    private static final String[] DRIVERS = { Instance.DRIVER_BUILTIN, Instance.DRIVER_SYSTEM, Instance.DRIVER_CUSTOM };
    private static final String[] SYSMEM = { Instance.SYSMEM_AUTO, Instance.SYSMEM_ON, Instance.SYSMEM_OFF };

    private Instance instance;
    private Spinner driver;

    private final ActivityResultLauncher<String[]> pickDriver =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null) {
                    InstanceActions.installDriver(this, instance, uri);
                    driver.setSelection(indexOf(DRIVERS, instance.driver), false);
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_instance);
        instance = Instance.open(this, getIntent().getStringExtra(EXTRA_ID));

        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setTitle(R.string.instance_settings);
        toolbar.setNavigationIcon(androidx.appcompat.R.drawable.abc_ic_ab_back_material);
        toolbar.setNavigationOnClickListener(v -> finish());

        ImageView banner = findViewById(R.id.banner);
        new Thread(() -> {
            Bitmap b = Banner.load(instance);
            if (b != null) runOnUiThread(() -> banner.setImageBitmap(b));
        }).start();

        StringBuilder d = new StringBuilder(instance.buildLabel());
        if (!instance.isDev()) {
            d.append("\n").append(Formatter.formatFileSize(this, instance.manifest.optLong("game_bytes", 0)));
            d.append(", ").append(instance.manifest.optInt("game_files", 0)).append(" files");
            org.json.JSONObject b = instance.manifest.optJSONObject("build");
            if (b != null) d.append("\nDAORecomp ").append(shortCommit(b.optString("daorecomp"))).append(", ").append(b.optString("created"));
        }
        ((TextView) findViewById(R.id.name)).setText(instance.name);
        ((TextView) findViewById(R.id.details)).setText(d);

        driver = findViewById(R.id.driver);
        bindSpinner(driver, R.array.drivers, indexOf(DRIVERS, instance.driver), i -> instance.driver = DRIVERS[i]);
        findViewById(R.id.driver_pick).setOnClickListener(v -> pickDriver.launch(new String[] { "*/*" }));
        bindSpinner(findViewById(R.id.sysmem), R.array.sysmem, indexOf(SYSMEM, instance.sysmem), i -> instance.sysmem = SYSMEM[i]);
        android.widget.RadioGroup lod = findViewById(R.id.lod);
        int[] lodIds = { R.id.lod_full, R.id.lod_low, R.id.lod_ultra_low };
        lod.check(lodIds[Math.max(0, Math.min(2, instance.textureLodBias))]);
        lod.setOnCheckedChangeListener((g, checked) -> {
            for (int i = 0; i < lodIds.length; i++) if (lodIds[i] == checked) instance.textureLodBias = i;
            save();
        });
        View advanced = findViewById(R.id.advanced);
        View chevron = findViewById(R.id.advanced_chevron);
        findViewById(R.id.advanced_header).setOnClickListener(v -> {
            boolean open = advanced.getVisibility() != View.VISIBLE;
            advanced.setVisibility(open ? View.VISIBLE : View.GONE);
            chevron.setRotation(open ? 180 : 0);
        });
        MaterialSwitch prerotate = findViewById(R.id.prerotate);
        prerotate.setChecked(instance.prerotate);
        prerotate.setOnCheckedChangeListener((b, on) -> { instance.prerotate = on; save(); });
        MaterialSwitch perf = findViewById(R.id.perf);
        perf.setChecked(instance.perfOverlay);
        perf.setOnCheckedChangeListener((b, on) -> { instance.perfOverlay = on; save(); });

        env = findViewById(R.id.env);
        env.setText(readEnv());
    }

    private com.google.android.material.textfield.TextInputEditText env;

    @Override
    protected void onPause() {
        super.onPause();
        writeEnv();
    }

    /** userdata/env.txt: KEY=VALUE lines the game starts with (GameActivity.applyEnvFile). */
    private File envFile() {
        return new File(instance.userdata(), "env.txt");
    }

    private String readEnv() {
        try {
            return envFile().isFile() ? new String(java.nio.file.Files.readAllBytes(envFile().toPath()),
                    java.nio.charset.StandardCharsets.UTF_8) : "";
        } catch (IOException e) {
            return "";
        }
    }

    private void writeEnv() {
        if (env == null || env.getText() == null) return;
        String text = env.getText().toString().replace("\r", "").trim();
        try {
            if (text.isEmpty()) {
                //noinspection ResultOfMethodCallIgnored
                envFile().delete();
            } else {
                //noinspection ResultOfMethodCallIgnored
                instance.userdata().mkdirs();
                java.nio.file.Files.write(envFile().toPath(), (text + "\n").getBytes(java.nio.charset.StandardCharsets.UTF_8));
            }
        } catch (IOException e) {
            Toast.makeText(this, e.toString(), Toast.LENGTH_LONG).show();
        }
    }

    private interface OnPick { void picked(int index); }

    private void bindSpinner(Spinner spinner, int array, int selected, OnPick onPick) {
        ArrayAdapter<CharSequence> a = ArrayAdapter.createFromResource(this, array, android.R.layout.simple_spinner_item);
        a.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(a);
        spinner.setSelection(Math.max(0, selected), false);
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                onPick.picked(position);
                save();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {}
        });
    }

    private void save() {
        try {
            instance.save();
        } catch (IOException e) {
            Toast.makeText(this, e.toString(), Toast.LENGTH_LONG).show();
        }
    }

    private static int indexOf(String[] values, String v) {
        for (int i = 0; i < values.length; i++) if (values[i].equals(v)) return i;
        return 0;
    }

    private static String shortCommit(String c) {
        return c != null && c.length() > 8 ? c.substring(0, 8) : c;
    }
}
