package edu.msuiit.bca182.activity;

import android.content.res.ColorStateList;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.util.ArrayList;
import java.util.List;

/**
 * Prediction history, newest first. New rows are inserted at the top and older pages
 * appended at the bottom with item-range notifications, so the list never redraws
 * everything and scrolling stays smooth.
 */
final class HistoryAdapter extends RecyclerView.Adapter<HistoryAdapter.Holder> {
    private static final int MAX_ROWS = 2000;   // keep memory bounded on long sessions

    private final List<Prediction> rows = new ArrayList<>();
    private final ColorStateList walkColor;
    private final ColorStateList runColor;

    HistoryAdapter(int walkColor, int runColor) {
        this.walkColor = ColorStateList.valueOf(walkColor);
        this.runColor = ColorStateList.valueOf(runColor);
        setHasStableIds(true);
    }

    long newestId() {
        return rows.isEmpty() ? 0 : rows.get(0).id;
    }

    long oldestId() {
        return rows.isEmpty() ? 0 : rows.get(rows.size() - 1).id;
    }

    boolean isEmpty() {
        return rows.isEmpty();
    }

    void clear() {
        int n = rows.size();
        rows.clear();
        notifyItemRangeRemoved(0, n);
    }

    /** Items newest first; only those newer than what is shown are added. Returns how many. */
    int addNewer(List<Prediction> items) {
        long top = newestId();
        List<Prediction> fresh = new ArrayList<>();
        for (Prediction p : items)
            if (p.id > top) fresh.add(p);
        if (fresh.isEmpty()) return 0;
        rows.addAll(0, fresh);
        notifyItemRangeInserted(0, fresh.size());
        trim();
        return fresh.size();
    }

    /** Items newest first; only those older than what is shown are appended. */
    void addOlder(List<Prediction> items) {
        long bottom = rows.isEmpty() ? Long.MAX_VALUE : oldestId();
        int start = rows.size();
        for (Prediction p : items)
            if (p.id < bottom) rows.add(p);
        notifyItemRangeInserted(start, rows.size() - start);
    }

    private void trim() {
        int extra = rows.size() - MAX_ROWS;
        if (extra <= 0) return;
        int from = rows.size() - extra;
        rows.subList(from, rows.size()).clear();
        notifyItemRangeRemoved(from, extra);
    }

    @Override
    public long getItemId(int position) {
        return rows.get(position).id;
    }

    @Override
    public int getItemCount() {
        return rows.size();
    }

    @NonNull
    @Override
    public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_prediction, parent, false);
        return new Holder(v);
    }

    @Override
    public void onBindViewHolder(@NonNull Holder h, int position) {
        Prediction p = rows.get(position);
        h.dot.setBackgroundTintList(p.isRun() ? runColor : walkColor);
        h.activity.setText(p.label());
        h.confidence.setText(p.percent());
        h.time.setText(p.time);
    }

    static final class Holder extends RecyclerView.ViewHolder {
        final View dot;
        final TextView activity, confidence, time;

        Holder(View v) {
            super(v);
            dot = v.findViewById(R.id.item_dot);
            activity = v.findViewById(R.id.item_activity);
            confidence = v.findViewById(R.id.item_confidence);
            time = v.findViewById(R.id.item_time);
        }
    }
}
