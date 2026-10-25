package com.daodroid;

import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.format.Formatter;
import android.view.LayoutInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.ImageView;
import android.widget.PopupMenu;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.GravityCompat;
import androidx.drawerlayout.widget.DrawerLayout;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.appbar.MaterialToolbar;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import com.google.android.material.navigation.NavigationView;
import com.google.android.material.progressindicator.LinearProgressIndicator;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiConsumer;

/**
 * The launcher: the installed games (instances) as cards (the game's own splash from the
 * player's disc, Play, settings, more), "Add game" (a game package built on the player's PC,
 * docs/PACKAGE_FORMAT.md), and the side menu (driver, saves, the pad's layout, the log, help).
 */
public class LauncherActivity extends AppCompatActivity {
    private final List<Instance> instances = new ArrayList<>();
    private final Map<String, Bitmap> banners = new HashMap<>();
    private RecyclerView.Adapter<Holder> adapter;
    private View empty;
    private DrawerLayout drawer;
    private final Handler ui = new Handler(Looper.getMainLooper());

    // The document pickers, with what to do with the pick (set just before launching).
    private BiConsumer<Instance, Uri> onPick;
    private Instance pickFor;
    private final ActivityResultLauncher<String[]> pickPackage =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null) confirmImport(uri);
            });
    private Instance exportFor;
    private final ActivityResultLauncher<String> exportSaves =
            registerForActivityResult(new ActivityResultContracts.CreateDocument("application/zip"), uri -> {
                if (uri != null && exportFor != null) InstanceActions.exportSaves(this, exportFor, uri);
            });
    private final ActivityResultLauncher<String[]> pickFile =
            registerForActivityResult(new ActivityResultContracts.OpenDocument(), uri -> {
                if (uri != null && onPick != null && pickFor != null) {
                    onPick.accept(pickFor, uri);
                    refresh();
                }
            });

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);
        drawer = findViewById(R.id.drawer);
        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.inflateMenu(R.menu.menu_launcher);
        toolbar.setNavigationOnClickListener(v -> drawer.openDrawer(GravityCompat.START));
        toolbar.setOnMenuItemClickListener(this::onMenu);

        NavigationView nav = findViewById(R.id.nav);
        nav.setNavigationItemSelectedListener(item -> {
            drawer.closeDrawer(GravityCompat.START);
            return onMenu(item);
        });
        try {
            String version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
            ((TextView) nav.getHeaderView(0).findViewById(R.id.nav_version)).setText(version);
        } catch (Exception ignored) {
        }

        empty = findViewById(R.id.empty);
        findViewById(R.id.empty_add).setOnClickListener(v -> addGame());
        findViewById(R.id.empty_help).setOnClickListener(v -> showHow());

        RecyclerView list = findViewById(R.id.instances);
        list.setLayoutManager(new LinearLayoutManager(this));
        adapter = new RecyclerView.Adapter<Holder>() {
            @NonNull
            @Override
            public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
                return new Holder(LayoutInflater.from(parent.getContext()).inflate(R.layout.item_instance, parent, false));
            }

            @Override
            public void onBindViewHolder(@NonNull Holder h, int position) {
                bind(h, instances.get(position));
            }

            @Override
            public int getItemCount() { return instances.size(); }
        };
        list.setAdapter(adapter);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private void refresh() {
        instances.clear();
        instances.addAll(Instance.list(this));
        adapter.notifyDataSetChanged();
        empty.setVisibility(instances.isEmpty() ? View.VISIBLE : View.GONE);
    }

    private void bind(Holder h, Instance i) {
        h.name.setText(i.name);
        h.details.setText(i.buildLabel());
        Bitmap b = banners.get(i.id);
        h.banner.setImageBitmap(b);
        if (b == null) {
            new Thread(() -> {
                Bitmap made = Banner.load(i);
                if (made != null) ui.post(() -> {
                    banners.put(i.id, made);
                    int at = instances.indexOf(i);
                    if (at >= 0) adapter.notifyItemChanged(at);
                });
            }).start();
        }
        h.play.setOnClickListener(v -> play(i));
        h.banner.setOnClickListener(v -> play(i));
        h.settings.setOnClickListener(v -> startActivity(
                new Intent(this, InstanceActivity.class).putExtra(InstanceActivity.EXTRA_ID, i.id)));
        h.more.setOnClickListener(v -> {
            PopupMenu menu = new PopupMenu(this, v);
            menu.inflate(R.menu.menu_instance);
            menu.setOnMenuItemClickListener(item -> {
                int id = item.getItemId();
                if (id == R.id.instance_delete) InstanceActions.confirmDelete(this, i, this::refresh);
                return true;
            });
            menu.show();
        });
    }

    private void play(Instance i) {
        startActivity(new Intent(this, GameActivity.class).putExtra(InstanceActivity.EXTRA_ID, i.id));
    }

    private boolean onMenu(MenuItem item) {
        int id = item.getItemId();
        if (id == R.id.action_add || id == R.id.nav_add) addGame();
        else if (id == R.id.action_help || id == R.id.nav_how) showHow();
        else if (id == R.id.nav_driver) InstanceActions.choose(this, R.string.nav_install_driver, instances,
                i -> pick(i, InstanceActions::installDriver));
        else if (id == R.id.nav_saves_import) InstanceActions.choose(this, R.string.saves_import, instances,
                i -> pick(i, InstanceActions::installSaves));
        else if (id == R.id.nav_saves_export) InstanceActions.choose(this, R.string.saves_export, instances, this::exportSavesOf);
        else if (id == R.id.nav_cache) InstanceActions.choose(this, R.string.cache_import_to, instances,
                i -> InstanceActions.importShaderCache(this, i, instances));
        else if (id == R.id.nav_pad) startActivity(new Intent(this, PadEditorActivity.class));
        else if (id == R.id.nav_bug) bugReport(instances.isEmpty() ? null : instances.get(0));
        else if (id == R.id.nav_about) showAbout();
        else return false;
        return true;
    }

    private interface Install { void run(LauncherActivity a, Instance i, Uri uri); }

    private void pick(Instance i, Install install) {
        pickFor = i;
        onPick = (inst, uri) -> install.run(this, inst, uri);
        pickFile.launch(new String[] { "*/*" });
    }

    private void exportSavesOf(Instance i) {
        exportFor = i;
        exportSaves.launch(InstanceActions.savesFileName(i));
    }

    private void bugReport(Instance i) {
        Intent intent = new Intent(this, BugReportActivity.class);
        if (i != null) intent.putExtra(InstanceActivity.EXTRA_ID, i.id);
        startActivity(intent);
    }

    private void addGame() {
        pickPackage.launch(new String[] { "*/*" });
    }

    private void showHow() {
        new MaterialAlertDialogBuilder(this).setTitle(R.string.launcher_how).setMessage(R.string.launcher_how_text)
                .setPositiveButton(android.R.string.ok, null).show();
    }

    private void showAbout() {
        new MaterialAlertDialogBuilder(this).setTitle(R.string.app_name).setMessage(R.string.about_text)
                .setPositiveButton(android.R.string.ok, null)
                .setNeutralButton("GitHub", (d, w) -> startActivity(new Intent(Intent.ACTION_VIEW,
                        Uri.parse("https://github.com/udarmolota/DAODroid"))))
                .show();
    }

    private void confirmImport(Uri uri) {
        new Thread(() -> {
            try {
                JSONObject m = PackageImporter.readManifest(this, uri);
                long need = m.optLong("game_bytes", 0) + 200L * 1024 * 1024;
                java.io.File base = Instance.daoRoot(this).getParentFile();
                long free = base != null ? base.getUsableSpace() : 0;
                ui.post(() -> {
                    if (free > 0 && free < need) {
                        showError(getString(R.string.launcher_no_space,
                                Formatter.formatFileSize(this, need), Formatter.formatFileSize(this, free)));
                        return;
                    }
                    Instance preview = Instance.create(this, m);
                    new MaterialAlertDialogBuilder(this)
                            .setTitle(R.string.launcher_import_title)
                            .setMessage(getString(R.string.launcher_import_message, preview.name, preview.buildLabel(),
                                    Formatter.formatFileSize(this, need), Formatter.formatFileSize(this, free)))
                            .setNegativeButton(android.R.string.cancel, null)
                            .setPositiveButton(R.string.launcher_import_go, (d, w) -> runImport(uri, preview))
                            .show();
                });
            } catch (PackageImporter.Problem e) {
                ui.post(() -> showError(e.getMessage()));
            } catch (Exception e) {
                ui.post(() -> showError(e.toString()));
            }
        }).start();
    }

    private void runImport(Uri uri, Instance instance) {
        View view = getLayoutInflater().inflate(R.layout.dialog_progress, null);
        LinearProgressIndicator bar = view.findViewById(R.id.progress);
        TextView text = view.findViewById(R.id.progress_text);
        AtomicBoolean cancelled = new AtomicBoolean(false);
        AlertDialog dialog = new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.launcher_importing)
                .setView(view)
                .setCancelable(false)
                .setNegativeButton(android.R.string.cancel, (d, w) -> cancelled.set(true))
                .show();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        new Thread(() -> {
            String error = null;
            try {
                long[] last = { 0 };
                PackageImporter.importInto(this, uri, instance, new PackageImporter.Progress() {
                    @Override
                    public void onProgress(long done, long total, String entry) {
                        long now = System.currentTimeMillis();
                        if (now - last[0] < 200) return;
                        last[0] = now;
                        ui.post(() -> {
                            bar.setProgress(total > 0 ? (int) (done * 1000 / total) : 0);
                            text.setText(Formatter.formatFileSize(LauncherActivity.this, done) + " / "
                                    + Formatter.formatFileSize(LauncherActivity.this, total));
                        });
                    }

                    @Override
                    public boolean cancelled() { return cancelled.get(); }
                });
            } catch (Exception e) {
                error = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String problem = cancelled.get() ? null : error;
            ui.post(() -> {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                dialog.dismiss();
                if (problem != null) showError(problem);
                refresh();
            });
        }).start();
    }

    private void showError(String message) {
        new MaterialAlertDialogBuilder(this)
                .setTitle(R.string.launcher_import_failed)
                .setMessage(message)
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final TextView name, details;
        final ImageView banner;
        final View play, settings, more;

        Holder(View v) {
            super(v);
            name = v.findViewById(R.id.name);
            details = v.findViewById(R.id.details);
            banner = v.findViewById(R.id.banner);
            play = v.findViewById(R.id.play);
            settings = v.findViewById(R.id.settings);
            more = v.findViewById(R.id.more);
        }
    }
}
