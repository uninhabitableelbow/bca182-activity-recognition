package edu.msuiit.bca182.activity;

import org.json.JSONObject;

import java.time.OffsetDateTime;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;

/** One row of the cloud's predictions table, as returned by /api/latest and /api/history. */
final class Prediction {
    private static final DateTimeFormatter TIME = DateTimeFormatter.ofPattern("HH:mm:ss");

    final long id;
    final long seq;
    final String activity;     // "walk" or "run"
    final double confidence;   // 0..1
    final String time;         // local HH:mm:ss

    private Prediction(long id, long seq, String activity, double confidence, String time) {
        this.id = id;
        this.seq = seq;
        this.activity = activity;
        this.confidence = confidence;
        this.time = time;
    }

    static Prediction fromJson(JSONObject o) {
        return new Prediction(
                o.optLong("id"),
                o.optLong("seq"),
                o.optString("activity", "?"),
                o.optDouble("confidence", 0),
                localTime(o.optString("created_at", "")));
    }

    boolean isRun() {
        return "run".equals(activity);
    }

    String label() {
        return activity.toUpperCase();
    }

    String percent() {
        return Math.round(confidence * 100) + "%";
    }

    private static String localTime(String iso) {
        try {
            return OffsetDateTime.parse(iso).atZoneSameInstant(ZoneId.systemDefault()).format(TIME);
        } catch (Exception e) {
            return iso;
        }
    }
}
