package com.daodroid;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Sets an option in the [xenia] section of the game's config.toml (Xenia's GPU options by
 * their cvar names, which DAORecomp keeps verbatim across its own rewrites). A missing file is
 * created with only that section; the game fills in the rest at start.
 */
final class XeniaConfig {
    private XeniaConfig() {}

    static void set(File config, String key, String value) throws IOException {
        List<String> lines = config.isFile()
                ? new ArrayList<>(Arrays.asList(new String(Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8).split("\n", -1)))
                : new ArrayList<>();
        String line = key + " = " + value;
        int section = -1, end = lines.size();
        for (int i = 0; i < lines.size(); i++) {
            String t = lines.get(i).trim();
            if (t.startsWith("[")) {
                if (section >= 0) {
                    end = i;
                    break;
                }
                if (t.equals("[xenia]")) section = i;
            }
        }
        if (section < 0) {
            if (!lines.isEmpty() && !lines.get(lines.size() - 1).isEmpty()) lines.add("");
            lines.add("[xenia]");
            lines.add(line);
            lines.add("");
        } else {
            boolean done = false;
            for (int i = section + 1; i < end; i++) {
                String t = lines.get(i).trim();
                if (t.startsWith(key + " ") || t.startsWith(key + "=")) {
                    if (lines.get(i).equals(line)) return; // already so
                    lines.set(i, line);
                    done = true;
                    break;
                }
            }
            if (!done) lines.add(section + 1, line);
        }
        //noinspection ResultOfMethodCallIgnored
        config.getParentFile().mkdirs();
        Files.write(config.toPath(), String.join("\n", lines).getBytes(StandardCharsets.UTF_8));
    }
}
