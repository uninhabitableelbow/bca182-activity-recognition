package edu.msuiit.bca182.activity;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.android.volley.DefaultRetryPolicy;
import com.android.volley.Request;
import com.android.volley.RequestQueue;
import com.android.volley.Response;
import com.android.volley.VolleyError;
import com.android.volley.toolbox.JsonObjectRequest;
import com.android.volley.toolbox.Volley;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * BCA182 Lab 3 - Android client of the activity recognition web API.
 *
 * Every request goes through Volley, which runs the network work on its own threads and
 * delivers results on the main thread, so the UI never blocks:
 *   GET /api/latest   every second   -> real-time prediction card
 *   GET /api/history  every 2 s      -> new rows at the top of the history list
 *                     on scroll      -> older pages (before_id) at the bottom
 *   GET /api/summary  every 10 s     -> walk/run share of the last 60 minutes
 * A request is only sent when the previous one of the same kind has finished, and all
 * polling stops while the app is in the background.
 */
public class MainActivity extends Activity {
    private static final String DEFAULT_URL = "http://10.59.240.216:5000";
    private static final String DEFAULT_DEVICE = "rt-spark-01";
    private static final long LATEST_MS = 1000, HISTORY_MS = 2000, SUMMARY_MS = 10000;
    private static final int PAGE = 50;
    private static final Object TAG = new Object();

    private RequestQueue queue;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private SharedPreferences prefs;
    private String baseUrl, device;

    private View liveCard, liveDot, summaryWalk, summaryRun, summaryEmpty;
    private TextView serverText, statusText, activityText, detailText, summaryText;
    private RecyclerView list;
    private LinearLayoutManager layout;
    private HistoryAdapter adapter;

    private boolean latestBusy, historyBusy, olderBusy, summaryBusy, noMoreHistory, running;
    private int colorWalk, colorRun, colorLive, colorStale, colorError;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_main);
        queue = Volley.newRequestQueue(this);
        prefs = getSharedPreferences("settings", MODE_PRIVATE);
        baseUrl = prefs.getString("url", DEFAULT_URL);
        device = prefs.getString("device", DEFAULT_DEVICE);

        colorWalk = getColor(R.color.walk);
        colorRun = getColor(R.color.run);
        colorLive = getColor(R.color.live);
        colorStale = getColor(R.color.stale);
        colorError = getColor(R.color.error);

        serverText = findViewById(R.id.server_text);
        liveCard = findViewById(R.id.live_card);
        liveDot = findViewById(R.id.live_dot);
        statusText = findViewById(R.id.status_text);
        activityText = findViewById(R.id.activity_text);
        detailText = findViewById(R.id.detail_text);
        summaryWalk = findViewById(R.id.summary_walk);
        summaryRun = findViewById(R.id.summary_run);
        summaryEmpty = findViewById(R.id.summary_empty);
        summaryText = findViewById(R.id.summary_text);
        findViewById(R.id.settings_button).setOnClickListener(v -> showSettings());

        list = findViewById(R.id.history_list);
        layout = new LinearLayoutManager(this);
        adapter = new HistoryAdapter(colorWalk, colorRun);
        list.setLayoutManager(layout);
        list.setAdapter(adapter);
        list.setHasFixedSize(true);
        list.setItemAnimator(null);   // no fade per row: inserts every second stay smooth
        list.addOnScrollListener(new RecyclerView.OnScrollListener() {
            @Override
            public void onScrolled(@NonNull RecyclerView rv, int dx, int dy) {
                if (dy > 0 && layout.findLastVisibleItemPosition() >= adapter.getItemCount() - 10)
                    loadOlder();
            }
        });
        showServer();
    }

    @Override
    protected void onStart() {
        super.onStart();
        running = true;
        handler.post(latestTick);
        handler.post(historyTick);
        handler.post(summaryTick);
    }

    @Override
    protected void onStop() {
        super.onStop();
        running = false;
        handler.removeCallbacksAndMessages(null);
        queue.cancelAll(TAG);
        latestBusy = historyBusy = olderBusy = summaryBusy = false;
    }

    // ---------------------------------------------------------------- polling loops
    private final Runnable latestTick = new Runnable() {
        @Override
        public void run() {
            fetchLatest();
            if (running) handler.postDelayed(this, LATEST_MS);
        }
    };
    private final Runnable historyTick = new Runnable() {
        @Override
        public void run() {
            fetchNewHistory();
            if (running) handler.postDelayed(this, HISTORY_MS);
        }
    };
    private final Runnable summaryTick = new Runnable() {
        @Override
        public void run() {
            fetchSummary();
            if (running) handler.postDelayed(this, SUMMARY_MS);
        }
    };

    // ---------------------------------------------------------------- requests
    private String url(String path, String... params) {
        Uri.Builder b = Uri.parse(baseUrl + path).buildUpon().appendQueryParameter("device", device);
        for (int i = 0; i + 1 < params.length; i += 2) b.appendQueryParameter(params[i], params[i + 1]);
        return b.build().toString();
    }

    private void get(String url, Response.Listener<JSONObject> ok, Response.ErrorListener fail) {
        JsonObjectRequest r = new JsonObjectRequest(Request.Method.GET, url, null, ok, fail);
        r.setRetryPolicy(new DefaultRetryPolicy(3000, 0, 1f));  // the next poll is the retry
        r.setShouldCache(false);
        r.setTag(TAG);
        queue.add(r);
    }

    private void fetchLatest() {
        if (latestBusy) return;
        latestBusy = true;
        get(url("/api/latest"), o -> {
            latestBusy = false;
            showLatest(o);
        }, e -> {
            latestBusy = false;
            if (e.networkResponse != null && e.networkResponse.statusCode == 404)
                showStatus("Waiting for the first prediction from " + device, colorStale);
            else
                showStatus("Cannot reach the server", colorError);
        });
    }

    private void fetchNewHistory() {
        if (historyBusy) return;
        historyBusy = true;
        get(url("/api/history", "limit", String.valueOf(PAGE)), o -> {
            historyBusy = false;
            List<Prediction> items = parseItems(o);
            // A full page with nothing we already show means rows were missed (e.g. the app
            // was paused): start over instead of leaving a gap. (ids are shared by all devices,
            // so they are not consecutive within one device.)
            if (items.size() == PAGE && !adapter.isEmpty()
                    && items.get(items.size() - 1).id > adapter.newestId()) {
                adapter.clear();
                noMoreHistory = false;
            }
            boolean atTop = layout.findFirstVisibleItemPosition() <= 0;
            if (adapter.addNewer(items) > 0 && atTop) list.scrollToPosition(0);
        }, e -> historyBusy = false);
    }

    private void loadOlder() {
        if (olderBusy || noMoreHistory || adapter.isEmpty()) return;
        olderBusy = true;
        get(url("/api/history", "limit", String.valueOf(PAGE), "before_id", String.valueOf(adapter.oldestId())), o -> {
            olderBusy = false;
            List<Prediction> items = parseItems(o);
            if (items.size() < PAGE) noMoreHistory = true;
            adapter.addOlder(items);
        }, e -> olderBusy = false);
    }

    private void fetchSummary() {
        if (summaryBusy) return;
        summaryBusy = true;
        get(url("/api/summary", "minutes", "60"), o -> {
            summaryBusy = false;
            JSONObject s = o.optJSONObject("seconds");
            showSummary(s == null ? 0 : s.optInt("walk"), s == null ? 0 : s.optInt("run"));
        }, e -> summaryBusy = false);
    }

    private static List<Prediction> parseItems(JSONObject o) {
        List<Prediction> out = new ArrayList<>();
        JSONArray a = o.optJSONArray("items");
        if (a != null)
            for (int i = 0; i < a.length(); i++) {
                JSONObject item = a.optJSONObject(i);
                if (item != null) out.add(Prediction.fromJson(item));
            }
        return out;
    }

    // ---------------------------------------------------------------- UI
    private void showLatest(JSONObject o) {
        Prediction p = Prediction.fromJson(o);
        boolean live = o.optBoolean("live");
        double age = o.optDouble("age_seconds", 0);
        activityText.setText(p.label());
        detailText.setText(String.format(Locale.US, "Confidence %s  ·  %s  ·  seq %d", p.percent(), p.time, p.seq));
        if (live) {
            setCardColor(p.isRun() ? colorRun : colorWalk);
            statusText.setText("Live from " + device);
            liveDot.setBackgroundTintList(ColorStateList.valueOf(colorLive));
        } else {
            setCardColor(colorStale);
            statusText.setText("No new data for " + ago(age) + " (device offline?)");
            liveDot.setBackgroundTintList(ColorStateList.valueOf(0xFFFFFFFF));
        }
    }

    private void showStatus(String message, int color) {
        setCardColor(color);
        statusText.setText(message);
        activityText.setText("--");
        detailText.setText("");
        liveDot.setBackgroundTintList(ColorStateList.valueOf(0xFFFFFFFF));
    }

    private void setCardColor(int color) {
        liveCard.setBackgroundTintList(ColorStateList.valueOf(color));
    }

    private void showSummary(int walk, int run) {
        int total = walk + run;
        setWeight(summaryWalk, walk);
        setWeight(summaryRun, run);
        setWeight(summaryEmpty, total == 0 ? 1 : 0);
        summaryText.setText(total == 0 ? "No data yet"
                : String.format(Locale.US, "Walk %s (%d%%)  ·  Run %s (%d%%)",
                        duration(walk), Math.round(100f * walk / total), duration(run), Math.round(100f * run / total)));
    }

    private static void setWeight(View v, float w) {
        LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) v.getLayoutParams();
        if (lp.weight != w) {
            lp.weight = w;
            v.setLayoutParams(lp);
        }
    }

    private static String duration(int seconds) {
        return seconds >= 60 ? (seconds / 60) + " min " + (seconds % 60) + " s" : seconds + " s";
    }

    private static String ago(double seconds) {
        long s = Math.round(seconds);
        if (s < 60) return s + " s";
        if (s < 3600) return (s / 60) + " min";
        return (s / 3600) + " h";
    }

    private void showServer() {
        serverText.setText(baseUrl + "  ·  " + device);
    }

    private void showSettings() {
        View v = getLayoutInflater().inflate(R.layout.dialog_settings, null);
        EditText urlEdit = v.findViewById(R.id.edit_url);
        EditText deviceEdit = v.findViewById(R.id.edit_device);
        urlEdit.setText(baseUrl);
        deviceEdit.setText(device);
        new AlertDialog.Builder(this)
                .setTitle("Server")
                .setView(v)
                .setPositiveButton("Save", (d, w) -> {
                    String u = urlEdit.getText().toString().trim();
                    while (u.endsWith("/")) u = u.substring(0, u.length() - 1);
                    if (!u.isEmpty() && !u.startsWith("http")) u = "http://" + u;
                    String dev = deviceEdit.getText().toString().trim();
                    baseUrl = u.isEmpty() ? DEFAULT_URL : u;
                    device = dev.isEmpty() ? DEFAULT_DEVICE : dev;
                    prefs.edit().putString("url", baseUrl).putString("device", device).apply();
                    showServer();
                    restart();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    /** Drops everything shown and polls the new server/device from scratch. */
    private void restart() {
        queue.cancelAll(TAG);
        latestBusy = historyBusy = olderBusy = summaryBusy = false;
        noMoreHistory = false;
        adapter.clear();
        showStatus("Connecting", colorStale);
        showSummary(0, 0);
        fetchLatest();
        fetchNewHistory();
        fetchSummary();
    }
}
