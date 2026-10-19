#include "report/report_model.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "core/civil_date.h"
#include "core/periods.h"

namespace nsm {
namespace {

constexpr int64_t kDay = 86400;

struct Rows {
    std::vector<UsageRow> hourly;
    std::vector<DailyRow> daily;
};

Rows Load(UsageStore& store, UtcRange r, const TimeZone& tz, Series series) {
    Rows out;
    if (r.end <= r.begin) return out;
    for (auto& row : store.QueryHourly(r))
        if (row.series == series) out.hourly.push_back(std::move(row));
    for (auto& row : store.QueryDaily(LocalDateOf(r.begin, tz), LocalDateOf(r.end - 1, tz)))
        if (row.series == series) out.daily.push_back(std::move(row));
    return out;
}

void Add(Totals& t, uint64_t rx, uint64_t tx) { t.rx += rx; t.tx += tx; }

Totals SumOf(const Rows& rows) {
    Totals t;
    for (const auto& h : rows.hourly) Add(t, h.rx, h.tx);
    for (const auto& d : rows.daily) Add(t, d.rx, d.tx);
    return t;
}

void SortByTotal(std::vector<NamedTotals>& v) {
    std::sort(v.begin(), v.end(), [](const NamedTotals& a, const NamedTotals& b) {
        if (a.totals.Total() != b.totals.Total()) return a.totals.Total() > b.totals.Total();
        if (a.name != b.name) return a.name < b.name;
        return a.id < b.id;
    });
}

std::wstring DisplayName(const AdapterMeta& m) {
    if (!m.friendlyName.empty()) return m.friendlyName;
    if (!m.name.empty()) return m.name;
    return m.id;
}

int64_t DayNumber(LocalDate d) { return DaysFromCivil(d.year, d.month, d.day); }

// The start of the hour bucket after the one that starts at h. LocalHourStart never decreases and always
// moves on within 3600 s, so search (h, h + 3600]. A plain h + 3600 would skip the short bucket after a
// transition off the hour (Chatham's 02:45 -> 03:45 leaves 03:45-04:00).
int64_t NextHourStart(int64_t h, const TimeZone& tz) {
    int64_t lo = h, hi = h + 3600;   // LocalHourStart(lo) == h < LocalHourStart(hi)
    while (hi - lo > 1) {
        const int64_t mid = lo + (hi - lo) / 2;
        if (LocalHourStart(mid, tz) == h) lo = mid; else hi = mid;
    }
    return hi;
}

}  // namespace

ReportModel::ReportModel(UsageStore& store, const TimeZone& tz, int firstDayOfWeek)
    : store_(store), tz_(tz), firstDayOfWeek_(firstDayOfWeek) {}

SummaryData ReportModel::Summary(UtcRange period, int64_t nowUtc) {
    SummaryData s;
    auto internet = [&](UtcRange r) { return Load(store_, r, tz_, Series::Internet); };
    s.today = SumOf(internet(ResolvePeriod(Period::Today, nowUtc, tz_, firstDayOfWeek_)));
    s.thisWeek = SumOf(internet(ResolvePeriod(Period::ThisWeek, nowUtc, tz_, firstDayOfWeek_)));
    s.thisMonth = SumOf(internet(ResolvePeriod(Period::ThisMonth, nowUtc, tz_, firstDayOfWeek_)));

    const Rows rows = internet(period);
    s.period = SumOf(rows);

    std::map<LocalDate, Totals> perDay;
    for (const auto& h : rows.hourly) Add(perDay[LocalDateOf(h.hourUtc, tz_)], h.rx, h.tx);
    for (const auto& d : rows.daily) Add(perDay[d.day], d.rx, d.tx);
    for (const auto& [day, t] : perDay) {   // ascending dates; strict > keeps the earliest on ties
        if (!s.busiestDay || t.Total() > s.busiestDay->second.Total()) s.busiestDay = std::make_pair(day, t);
    }

    s.topAdapters = AdapterTable(period, false);
    if (s.topAdapters.size() > 3) s.topAdapters.resize(3);
    s.topNetworks = WifiTable(period);
    if (s.topNetworks.size() > 3) s.topNetworks.resize(3);
    return s;
}

Granularity ReportModel::PickGranularity(UtcRange r, int64_t nowUtc) {
    const int64_t span = r.end - r.begin;
    // The same cutoff as UsageStore::RollUp: every hourly row from here on is still stored.
    if (span <= 25 * 3600 && r.begin >= LocalHourStart(nowUtc, tz_) - 90 * kDay) return Granularity::Hour;
    if (span <= 92 * kDay) return Granularity::Day;
    return Granularity::Month;
}

std::vector<TimelineBar> ReportModel::Timeline(UtcRange r, int64_t nowUtc) {
    std::vector<TimelineBar> bars;
    if (r.end <= r.begin) return bars;
    const Granularity g = PickGranularity(r, nowUtc);
    const Rows rows = Load(store_, r, tz_, Series::Internet);
    const bool open = r.begin == 0;   // AllTime: start at the first bucket with data

    if (g == Granularity::Hour) {
        // Local-hour buckets (keys as recorded; re-floored in case the zone changed since).
        std::map<int64_t, Totals> byHour;
        for (const auto& h : rows.hourly) Add(byHour[LocalHourStart(h.hourUtc, tz_)], h.rx, h.tx);
        if (open && byHour.empty()) return bars;
        // From the first bucket at or after r.begin: no bar for an hour that started before the range.
        int64_t first = open ? byHour.begin()->first : LocalHourStart(r.begin, tz_);
        if (first < r.begin) first = NextHourStart(first, tz_);
        for (int64_t h = first; h < r.end; h = NextHourStart(h, tz_)) {
            TimelineBar b;
            b.start = tz_.ToLocal(h);
            if (auto it = byHour.find(h); it != byHour.end()) b.totals = it->second;
            bars.push_back(b);
        }
        return bars;
    }

    // Day and Month combine hourly rows (bucketed by local date) with daily rows.
    std::map<int64_t, Totals> byDay;   // day number -> totals
    for (const auto& h : rows.hourly) Add(byDay[DayNumber(LocalDateOf(h.hourUtc, tz_))], h.rx, h.tx);
    for (const auto& d : rows.daily) Add(byDay[DayNumber(d.day)], d.rx, d.tx);

    if (g == Granularity::Day) {
        if (open && byDay.empty()) return bars;
        const int64_t first = open ? byDay.begin()->first : DayNumber(LocalDateOf(r.begin, tz_));
        const int64_t last = DayNumber(LocalDateOf(r.end - 1, tz_));
        for (int64_t d = first; d <= last; ++d) {
            const LocalDate date = CivilFromDays(d);
            TimelineBar b;
            b.start = LocalDateTime{date.year, date.month, date.day, 0, 0, 0};
            if (auto it = byDay.find(d); it != byDay.end()) b.totals = it->second;
            bars.push_back(b);
        }
        return bars;
    }

    auto monthKey = [](LocalDate d) { return static_cast<int64_t>(d.year) * 12 + (d.month - 1); };
    std::map<int64_t, Totals> byMonth;
    for (const auto& [day, t] : byDay) Add(byMonth[monthKey(CivilFromDays(day))], t.rx, t.tx);
    if (open && byMonth.empty()) return bars;
    const int64_t first = open ? byMonth.begin()->first : monthKey(LocalDateOf(r.begin, tz_));
    const int64_t last = monthKey(LocalDateOf(r.end - 1, tz_));
    for (int64_t m = first; m <= last; ++m) {
        TimelineBar b;
        b.start = LocalDateTime{static_cast<int>(m / 12), static_cast<int>(m % 12) + 1, 1, 0, 0, 0};
        if (auto it = byMonth.find(m); it != byMonth.end()) b.totals = it->second;
        bars.push_back(b);
    }
    return bars;
}

std::vector<NamedTotals> ReportModel::AdapterTable(UtcRange r, bool includeHidden) {
    std::unordered_map<std::wstring, AdapterMeta> meta;
    for (auto& a : store_.Adapters()) meta[a.id] = a;

    const Rows rows = Load(store_, r, tz_, Series::Adapter);
    std::map<std::wstring, Totals> byAdapter;
    for (const auto& h : rows.hourly) Add(byAdapter[h.adapterId], h.rx, h.tx);
    for (const auto& d : rows.daily) Add(byAdapter[d.adapterId], d.rx, d.tx);

    std::vector<NamedTotals> out;
    for (const auto& [id, t] : byAdapter) {
        NamedTotals n;
        n.id = id;
        n.totals = t;
        if (auto it = meta.find(id); it != meta.end()) {
            n.name = DisplayName(it->second);
            n.kind = it->second.kind;
            n.hidden = it->second.hidden;
        } else {
            n.name = id;
        }
        if (n.hidden && !includeHidden) continue;
        out.push_back(std::move(n));
    }
    SortByTotal(out);
    return out;
}

std::vector<NamedTotals> ReportModel::WifiTable(UtcRange r) {
    std::unordered_map<std::wstring, AdapterKind> kinds;
    for (const auto& a : store_.Adapters()) kinds[a.id] = a.kind;
    std::unordered_map<std::wstring, std::wstring> names;
    for (const auto& n : store_.Networks()) names[n.key] = n.name;

    auto isWifi = [&](const std::wstring& id) {
        auto it = kinds.find(id);
        return it != kinds.end() && it->second == AdapterKind::WiFi;
    };

    const Rows rows = Load(store_, r, tz_, Series::Adapter);
    std::map<std::wstring, Totals> byNetwork;
    for (const auto& h : rows.hourly) if (isWifi(h.adapterId)) Add(byNetwork[h.networkKey], h.rx, h.tx);
    for (const auto& d : rows.daily) if (isWifi(d.adapterId)) Add(byNetwork[d.networkKey], d.rx, d.tx);

    std::vector<NamedTotals> out;
    for (const auto& [key, t] : byNetwork) {
        NamedTotals n;
        n.id = key;
        n.kind = AdapterKind::WiFi;
        n.totals = t;
        if (!key.empty()) {
            if (auto it = names.find(key); it != names.end()) n.name = it->second;
        }
        out.push_back(std::move(n));
    }
    SortByTotal(out);
    return out;
}

}  // namespace nsm
