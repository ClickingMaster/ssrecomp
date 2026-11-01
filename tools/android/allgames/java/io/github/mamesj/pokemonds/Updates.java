// CHECK FOR UPDATES in the Android app (GameActivity.checkUpdates, from the game list's settings):
// the newest release with pokemon-ds-wide-android.apk on github.com/mamesj/pokemon-fulldex. When
// the release has a SHA256SUMS.txt, the APK is downloaded, checked against it, the installed APK
// kept in Android/data/io.github.mamesj.pokemonds/files/update/previous.apk, and handed to
// Android's installer (which asks the player, and only installs an APK signed with the same key).
// Without one, the download's page is offered instead. (The PC's version: frontend/updater.cpp.)
package io.github.mamesj.pokemonds;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Map;

final class Updates {
    private static final String TAG = "PokemonDS";
    private static final String TITLE = "Pokémon Fulldex updates";
    private static final String ASSET = "pokemon-ds-wide-android.apk";
    private static final String RELEASES = "https://api.github.com/repos/mamesj/pokemon-fulldex/releases?per_page=30";
    private static final String DOWNLOADS = "https://github.com/mamesj/pokemon-fulldex/releases/download/";
    private static final String PAGE = "https://github.com/mamesj/pokemon-fulldex/releases";
    private static final String INSTALLED = "io.github.mamesj.pokemonds.UPDATE_INSTALLED";
    private static boolean receiverRegistered;

    private Updates() {}

    private static final class Release {
        String url, sumsUrl, page;
        long updated, built;
    }

    // The list of releases: GitHub's, or for a test a list on the PC (tools/updater/fake_release.py),
    // named in update-test-url.txt in the app's folder (Android/data/io.github.mamesj.pokemonds/files)
    // and reached through adb reverse; downloads must then come from that server. Anything but a
    // server on 127.0.0.1 is ignored.
    private static String releasesUrl(Context c) {
        File test = new File(c.getExternalFilesDir(null), "update-test-url.txt");
        if (test.isFile()) {
            try {
                String url = new String(read(new FileInputStream(test), 4096), "UTF-8").trim();
                if (url.startsWith("http://127.0.0.1:")) return url;
            } catch (IOException e) { Log.e(TAG, "update-test-url.txt", e); }
        }
        return RELEASES;
    }

    private static String downloadPrefix(String releases) {
        if (releases.equals(RELEASES)) return DOWNLOADS;
        int scheme = releases.indexOf("://");
        int path = scheme < 0 ? -1 : releases.indexOf('/', scheme + 3);
        return path < 0 ? releases : releases.substring(0, path + 1);
    }

    private static byte[] read(InputStream in, int max) throws IOException {
        try (InputStream input = in) {
            java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int n;
            while ((n = input.read(buffer)) != -1) {
                if (bytes.size() + n > max) throw new IOException("response too large");
                bytes.write(buffer, 0, n);
            }
            return bytes.toByteArray();
        }
    }

    private static HttpURLConnection open(String url) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setInstanceFollowRedirects(true); // (GitHub's downloads are redirects to its file servers)
        c.setRequestProperty("Accept", "application/vnd.github+json");
        c.setRequestProperty("User-Agent", "PokemonFulldex-Android");
        if (c.getResponseCode() != 200) {
            int code = c.getResponseCode();
            c.disconnect();
            throw new IOException("HTTP " + code + " from " + url);
        }
        return c;
    }

    private static String get(String url, int max) throws IOException {
        HttpURLConnection c = open(url);
        try { return new String(read(c.getInputStream(), max), "UTF-8"); }
        finally { c.disconnect(); }
    }

    private static String hex(byte[] digest) {
        StringBuilder s = new StringBuilder();
        for (byte b : digest) s.append(String.format("%02x", b & 0xff));
        return s.toString();
    }

    private static String sha256(File f) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(f)) {
            byte[] buffer = new byte[1 << 16];
            int n;
            while ((n = in.read(buffer)) != -1) md.update(buffer, 0, n);
        }
        return hex(md.digest());
    }

    // SHA256SUMS.txt ("<sha256>  <file>" lines): file -> lowercase hash.
    private static Map<String, String> parseSums(String text) {
        Map<String, String> sums = new HashMap<>();
        for (String line : text.split("\n")) {
            line = line.trim();
            if (line.length() < 66 || !line.substring(0, 64).matches("[0-9a-fA-F]{64}")) continue;
            String name = line.substring(64).trim();
            if (name.startsWith("*")) name = name.substring(1);
            sums.put(name, line.substring(0, 64).toLowerCase(java.util.Locale.US));
        }
        return sums;
    }

    private static long buildId(String sums) {
        for (String line : sums.split("\n")) {
            String[] words = line.trim().split("\\s+");
            if (words.length == 4 && words[0].equals("#") && words[1].equals("fulldex-build") && words[2].equals(ASSET)) {
                try { return Math.max(0L, Math.multiplyExact(Long.parseLong(words[3]), 1000L)); }
                catch (ArithmeticException | NumberFormatException ignored) { return 0L; }
            }
        }
        return 0L;
    }

    private static Release newest(String json, String prefix) throws Exception {
        org.json.JSONArray releases = new org.json.JSONArray(json);
        java.text.SimpleDateFormat dates = new java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", java.util.Locale.US);
        dates.setTimeZone(java.util.TimeZone.getTimeZone("UTC"));
        Release best = null;
        for (int i = 0; i < releases.length(); i++) {
            org.json.JSONObject release = releases.getJSONObject(i);
            if (release.optBoolean("draft") || release.optBoolean("prerelease")) continue;
            org.json.JSONArray assets = release.optJSONArray("assets");
            if (assets == null) continue;
            Release r = new Release();
            for (int j = 0; j < assets.length(); j++) {
                org.json.JSONObject asset = assets.getJSONObject(j);
                String url = asset.optString("browser_download_url");
                if (!url.startsWith(prefix)) continue;
                String name = asset.optString("name");
                if (ASSET.equals(name) && asset.optLong("size") > 0) {
                    r.url = url;
                    r.updated = dates.parse(asset.getString("updated_at")).getTime();
                } else if ("SHA256SUMS.txt".equals(name)) {
                    r.sumsUrl = url;
                }
            }
            if (r.url == null || (best != null && r.updated <= best.updated)) continue;
            r.page = release.optString("html_url", PAGE);
            best = r;
        }
        return best;
    }

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences("update", Context.MODE_PRIVATE);
    }

    private static void say(final Activity a, final String message) {
        a.runOnUiThread(() -> {
            if (a.isFinishing() || a.isDestroyed()) return;
            new AlertDialog.Builder(a).setTitle(TITLE).setMessage(message).setPositiveButton("OK", null).show();
        });
    }

    private static void open(Activity a, String url) {
        try { a.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))); }
        catch (Exception e) { Log.e(TAG, "Cannot open " + url, e); }
    }

    // Network work never blocks the SDL or Android UI thread.
    // launch: only show a dialog when a new version is available (the list just opened).
    static void check(final Activity a) { check(a, false); }
    static void check(final Activity a, final boolean launch) {
        new Thread(() -> {
            try {
                String list = releasesUrl(a);
                Release r = newest(get(list, 4 << 20), downloadPrefix(list));
                String digest = null;
                if (r != null && r.sumsUrl != null) {
                    try { String sums = get(r.sumsUrl, 1 << 20); digest = parseSums(sums).get(ASSET); r.built = buildId(sums); }
                    catch (IOException e) { Log.e(TAG, "SHA256SUMS.txt", e); }
                }
                SharedPreferences p = prefs(a);
                // (An update handed to Android's installer: now installed if this app is it.)
                String pending = p.getString("pending_sha", null);
                if (pending != null) {
                    if (pending.equals(sha256(new File(a.getApplicationInfo().sourceDir))))
                        p.edit().putString("installed_sha", pending).apply();
                    p.edit().remove("pending_sha").apply();
                }
                String installed = p.getString("installed_sha", null);
                boolean newer = false;
                if (r != null) {
                    newer = r.built > 0 ? r.built > BuildInfo.BUILT_AT
                            : installed != null && digest != null ? !digest.equals(installed)
                            : r.updated > BuildInfo.BUILT_AT;
                }
                Log.i(TAG, "update check: " + (r == null ? "no APK" : r.url + " sha256 " + digest + (newer ? " (newer)" : " (installed)")));
                final Release release = r;
                final String sha = digest;
                final boolean isNewer = newer;
                a.runOnUiThread(() -> {
                    if (a.isFinishing() || a.isDestroyed()) return;
                    if (launch && (release == null || !isNewer)) return;
                    AlertDialog.Builder d = new AlertDialog.Builder(a).setTitle(TITLE).setNegativeButton(launch ? "Later" : "Close", null);
                    if (release == null) d.setMessage("No Android APK was found in the latest releases.");
                    else if (isNewer && sha != null) {
                        d.setMessage("A new version is available. Install it now?\n\nIt is checked against the release's "
                                + "SHA256SUMS.txt first, and the version you have is kept. Your ROMs, saves and settings stay.");
                        d.setPositiveButton("Install", (x, y) -> install(a, release, sha));
                        d.setNeutralButton("Open page", (x, y) -> open(a, release.page));
                    } else if (isNewer) {
                        d.setMessage("A new version is available. Open its download?");
                        d.setPositiveButton("Open download", (x, y) -> open(a, release.url));
                    } else {
                        d.setMessage("You have the newest version.");
                    }
                    d.show();
                });
            } catch (Exception e) {
                Log.e(TAG, "Update check failed", e);
                if (!launch) say(a, "Could not check GitHub. Check your connection and try again later.");
            }
        }, "Fulldex-update-check").start();
    }

    private static void install(final Activity a, final Release r, final String digest) {
        // (Android 8 on: the player allows this app to install apps, once, in Android's settings.)
        if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) {
            new AlertDialog.Builder(a).setTitle(TITLE)
                .setMessage("Android needs your permission for Pokémon Fulldex to install its update. "
                        + "Turn on \"Allow from this source\" on the next page, then choose CHECK FOR UPDATES again.")
                .setPositiveButton("OK", (x, y) -> {
                    try {
                        a.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                Uri.parse("package:" + a.getPackageName())));
                    } catch (Exception e) { Log.e(TAG, "unknown sources settings", e); }
                })
                .setNegativeButton("Cancel", null).show();
            return;
        }
        final AlertDialog progress = new AlertDialog.Builder(a).setTitle(TITLE)
            .setMessage("Downloading the new version...").setCancelable(false).show();
        new Thread(() -> {
            File apk = new File(a.getCacheDir(), "update.apk");
            String error = null;
            try {
                MessageDigest md = MessageDigest.getInstance("SHA-256");
                HttpURLConnection c = open(r.url);
                try (InputStream in = c.getInputStream(); OutputStream out = new FileOutputStream(apk)) {
                    byte[] buffer = new byte[1 << 16];
                    long total = 0;
                    int n;
                    while ((n = in.read(buffer)) != -1) {
                        total += n;
                        if (total > (512L << 20)) throw new IOException("download too large");
                        md.update(buffer, 0, n);
                        out.write(buffer, 0, n);
                    }
                } finally { c.disconnect(); }
                String got = hex(md.digest());
                if (!got.equals(digest)) {
                    Log.e(TAG, "update checksum " + got + ", the release says " + digest);
                    error = "The download was damaged (its checksum does not match the release's). Nothing was changed.";
                } else {
                    // The version installed now, kept where a PC sees it over USB.
                    File keep = new File(a.getExternalFilesDir(null), "update");
                    if (!keep.isDirectory() && !keep.mkdirs()) throw new IOException("cannot make " + keep);
                    copy(new File(a.getApplicationInfo().sourceDir), new File(keep, "previous.apk"));
                    prefs(a).edit().putString("pending_sha", digest).apply();
                    hand(a, apk);
                }
            } catch (Exception e) {
                Log.e(TAG, "Update failed", e);
                error = "The update could not be downloaded. Nothing was changed. (" + e.getMessage() + ")";
            } finally {
                apk.delete();
            }
            final String message = error;
            a.runOnUiThread(() -> {
                progress.dismiss();
                if (message != null) say(a, message);
            });
        }, "Fulldex-update-install").start();
    }

    private static void copy(File from, File to) throws IOException {
        File part = new File(to.getPath() + ".part");
        try (InputStream in = new FileInputStream(from); OutputStream out = new FileOutputStream(part)) {
            byte[] buffer = new byte[1 << 16];
            int n;
            while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
        }
        if (!part.renameTo(to)) {
            to.delete();
            if (!part.renameTo(to)) throw new IOException("cannot write " + to);
        }
    }

    // The checked APK to Android's installer, which asks the player and then replaces the app
    // (ending this process).
    private static void hand(final Activity a, File apk) throws IOException {
        final Context app = a.getApplicationContext();
        synchronized (Updates.class) {
            if (!receiverRegistered) {
                BroadcastReceiver receiver = new BroadcastReceiver() {
                    @Override
                    public void onReceive(Context c, Intent i) {
                        int status = i.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
                        Log.i(TAG, "installer: status " + status + " " + i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE));
                        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
                            Intent confirm = i.getParcelableExtra(Intent.EXTRA_INTENT);
                            if (confirm != null) {
                                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                                c.startActivity(confirm);
                            }
                        } else if (status != PackageInstaller.STATUS_SUCCESS) {
                            prefs(c).edit().remove("pending_sha").apply();
                            if (status != PackageInstaller.STATUS_FAILURE_ABORTED)
                                say(a, "Android did not install the update: " + i.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)
                                        + "\n\nThe version you have is unchanged.");
                        }
                    }
                };
                IntentFilter filter = new IntentFilter(INSTALLED);
                if (Build.VERSION.SDK_INT >= 33) app.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
                else app.registerReceiver(receiver, filter);
                receiverRegistered = true;
            }
        }
        PackageInstaller installer = app.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(app.getPackageName());
        int id = installer.createSession(params);
        try (PackageInstaller.Session session = installer.openSession(id)) {
            try (InputStream in = new FileInputStream(apk); OutputStream out = session.openWrite("base.apk", 0, apk.length())) {
                byte[] buffer = new byte[1 << 16];
                int n;
                while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
                session.fsync(out);
            }
            Intent done = new Intent(INSTALLED).setPackage(app.getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            session.commit(PendingIntent.getBroadcast(app, id, done, flags).getIntentSender());
        }
    }
}
