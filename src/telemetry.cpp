// CKey telemetry core. See include/ckey/telemetry.h and
// docs/adoption-and-telemetry.md. Self-contained: no other ckey_ function is
// used, no networking, no clock, no heap allocation.

#include "ckey/telemetry.h"

#include <cstring>
#include <mutex>

namespace {

constexpr uint64_t kTtdEdges[CKEY_TELEMETRY_BUCKETS - 1] = {25, 50, 100, 200, 400, 800, 1600, 3200, 6400};
constexpr uint64_t kFrameEdges[CKEY_TELEMETRY_BUCKETS - 1] = {2, 5, 10, 20, 40, 80, 160, 320, 640};
constexpr uint64_t kMaxObservation = 0xffffffffull; // largest value one observation can add
constexpr size_t kMaxImportBytes = 16384;
constexpr size_t kMaxVersionBytes = 63;

const char *const kSdks[] = {"c", "swift", "android", "web", "dotnet"};
const char *const kPlatforms[] = {"ios", "macos", "android", "web", "windows", "linux", "other"};

enum class Session { Idle, Open, Done };

struct State {
    int mode = CKEY_TELEMETRY_ON;
    char sdk[8] = "c";
    char sdk_version[kMaxVersionBytes + 1] = CKEY_TELEMETRY_DEFAULT_VERSION;
    char platform[8] = "other";
    ckey_telemetry_counters totals{};
    ckey_telemetry_counters delta{};    // unsent, mode 1 only
    ckey_telemetry_counters inflight{}; // taken, awaiting batch_result
    bool inflight_active = false;
    uint64_t inflight_id = 0;
    uint64_t next_id = 1;
    bool delta_has_since = false; // delta_since is valid
    uint64_t delta_since = 0;     // now_ms of the first count in the delta
    bool delta_ready = false;     // restored delta: exempt from the minimum age
    bool has_last_attempt = false; // a batch result was reported
    uint64_t last_attempt = 0;     // now_ms of that report
    Session session = Session::Idle;
    uint64_t session_start = 0;
};

std::mutex g_mutex;
State g_state;

static_assert(sizeof(CKEY_TELEMETRY_DEFAULT_VERSION) <= kMaxVersionBytes + 1,
              "CKEY_TELEMETRY_DEFAULT_VERSION is too long");

inline void sat_add(uint64_t &a, uint64_t b) { a = (a > UINT64_MAX - b) ? UINT64_MAX : a + b; }

void hist_record(ckey_telemetry_histogram &h, const uint64_t (&edges)[CKEY_TELEMETRY_BUCKETS - 1], uint64_t v) {
    size_t i = 0;
    while (i < CKEY_TELEMETRY_BUCKETS - 1 && v > edges[i]) ++i; // inclusive upper edges
    sat_add(h.count, 1);
    sat_add(h.sum, v);
    sat_add(h.buckets[i], 1);
}

void hist_merge(ckey_telemetry_histogram &a, const ckey_telemetry_histogram &b) {
    sat_add(a.count, b.count);
    sat_add(a.sum, b.sum);
    for (size_t i = 0; i < CKEY_TELEMETRY_BUCKETS; ++i) sat_add(a.buckets[i], b.buckets[i]);
}

void merge(ckey_telemetry_counters &a, const ckey_telemetry_counters &b) {
    sat_add(a.sessions_started, b.sessions_started);
    sat_add(a.sessions_succeeded, b.sessions_succeeded);
    sat_add(a.sessions_failed, b.sessions_failed);
    sat_add(a.frames, b.frames);
    for (size_t i = 0; i < CKEY_TELEMETRY_PROFILES; ++i) sat_add(a.decoded[i], b.decoded[i]);
    hist_merge(a.time_to_decode_ms, b.time_to_decode_ms);
    hist_merge(a.frame_ms, b.frame_ms);
}

bool is_empty(const ckey_telemetry_counters &c) {
    static const ckey_telemetry_counters zero{};
    return std::memcmp(&c, &zero, sizeof c) == 0;
}

void clear_delta(State &s) {
    s.delta = {};
    s.delta_has_since = false;
    s.delta_since = 0;
    s.delta_ready = false;
}

void drop_inflight(State &s) {
    s.inflight = {};
    s.inflight_active = false;
    s.inflight_id = 0;
}

// Applies f to the totals and, in mode 1, to the unsent delta. Caller holds
// the lock and has checked that the mode is not 0.
template <class F> void record(State &s, uint64_t now_ms, F f) {
    f(s.totals);
    if (s.mode == CKEY_TELEMETRY_ON) {
        if (!s.delta_has_since) {
            s.delta_has_since = true;
            s.delta_since = now_ms;
        }
        f(s.delta);
    }
}

inline uint64_t elapsed(uint64_t now_ms, uint64_t since) { return now_ms > since ? now_ms - since : 0; }

void begin_session(State &s, uint64_t now_ms) {
    s.session = Session::Open;
    s.session_start = now_ms;
    record(s, now_ms, [](ckey_telemetry_counters &c) { sat_add(c.sessions_started, 1); });
}

void fail_session(State &s, uint64_t now_ms) {
    s.session = Session::Idle;
    record(s, now_ms, [](ckey_telemetry_counters &c) { sat_add(c.sessions_failed, 1); });
}

// Ends an OPEN session as failed if it reached the timeout.
void check_timeout(State &s, uint64_t now_ms) {
    if (s.session == Session::Open && elapsed(now_ms, s.session_start) >= CKEY_TELEMETRY_SESSION_TIMEOUT_MS)
        fail_session(s, now_ms);
}

// ---- Clamping a batch out of the delta --------------------------------

uint64_t take_scalar(uint64_t &v) {
    uint64_t t = v < CKEY_TELEMETRY_MAX_COUNT ? v : CKEY_TELEMETRY_MAX_COUNT;
    v -= t;
    return t;
}

// floor(a * b / c) for c > 0, exact for any 64-bit a and b, with a 128-bit
// intermediate. The callers only ask for quotients that fit in 64 bits.
// MSVC has no __int128, so it gets a portable multiply and long division.
uint64_t mul_div(uint64_t a, uint64_t b, uint64_t c) {
#if defined(__SIZEOF_INT128__) && !defined(CKEY_TELEMETRY_PORTABLE_MULDIV)
    return static_cast<uint64_t>(static_cast<unsigned __int128>(a) * b / c);
#else
    const uint64_t a_lo = a & 0xFFFFFFFFu, a_hi = a >> 32, b_lo = b & 0xFFFFFFFFu, b_hi = b >> 32;
    const uint64_t p0 = a_lo * b_lo, p1 = a_lo * b_hi, p2 = a_hi * b_lo, p3 = a_hi * b_hi;
    const uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFu) + (p2 & 0xFFFFFFFFu);
    const uint64_t lo = (p0 & 0xFFFFFFFFu) | (mid << 32);
    const uint64_t hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    uint64_t q = 0, r = 0;
    for (int i = 127; i >= 0; --i) {
        const uint64_t bit = i >= 64 ? (hi >> (i - 64)) & 1u : (lo >> i) & 1u;
        const bool carry = (r >> 63) != 0;
        r = (r << 1) | bit;
        q <<= 1;
        if (carry || r >= c) {
            r -= c;
            q |= 1u;
        }
    }
    return q;
#endif
}

ckey_telemetry_histogram take_hist(ckey_telemetry_histogram &h) {
    ckey_telemetry_histogram part{};
    uint64_t eff = 0; // bucket total; equals count unless saturated
    for (uint64_t b : h.buckets) sat_add(eff, b);
    if (eff == 0) {
        // Nothing observable; a stray sum without observations is not sendable.
        h = {};
        return part;
    }
    uint64_t n = eff < CKEY_TELEMETRY_MAX_COUNT ? eff : CKEY_TELEMETRY_MAX_COUNT;
    if (mul_div(h.sum, n, eff) > CKEY_TELEMETRY_MAX_SUM)
        n = mul_div(CKEY_TELEMETRY_MAX_SUM, eff, h.sum);
    if (n == 0) n = 1; // unreachable with per-observation values <= 2^32-1
    if (n == eff) {
        part = h;
        part.count = eff;
        h = {};
        return part;
    }
    uint64_t taken = 0;
    for (size_t i = 0; i < CKEY_TELEMETRY_BUCKETS; ++i) {
        part.buckets[i] = mul_div(h.buckets[i], n, eff);
        taken += part.buckets[i];
    }
    for (size_t i = 0; i < CKEY_TELEMETRY_BUCKETS && taken < n; ++i) {
        if (h.buckets[i] > part.buckets[i]) {
            ++part.buckets[i];
            ++taken;
        }
    }
    part.count = n;
    part.sum = mul_div(h.sum, n, eff);
    if (part.sum > CKEY_TELEMETRY_MAX_SUM) part.sum = CKEY_TELEMETRY_MAX_SUM;
    for (size_t i = 0; i < CKEY_TELEMETRY_BUCKETS; ++i) h.buckets[i] -= part.buckets[i];
    h.sum -= part.sum;
    h.count = eff - n;
    return part;
}

ckey_telemetry_counters take_part(ckey_telemetry_counters &d) {
    ckey_telemetry_counters p{};
    p.sessions_started = take_scalar(d.sessions_started);
    p.sessions_succeeded = take_scalar(d.sessions_succeeded);
    p.sessions_failed = take_scalar(d.sessions_failed);
    p.frames = take_scalar(d.frames);
    for (size_t i = 0; i < CKEY_TELEMETRY_PROFILES; ++i) p.decoded[i] = take_scalar(d.decoded[i]);
    p.time_to_decode_ms = take_hist(d.time_to_decode_ms);
    p.frame_ms = take_hist(d.frame_ms);
    return p;
}

// ---- Bounded JSON writer ----------------------------------------------

struct Writer {
    char *out;
    size_t cap;
    size_t len = 0;
    void ch(char c) {
        if (out && len + 1 < cap) out[len] = c;
        ++len;
    }
    void str(const char *s) {
        while (*s) ch(*s++);
    }
    void num(uint64_t v) {
        char tmp[20];
        size_t n = 0;
        do {
            tmp[n++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v);
        while (n) ch(tmp[--n]);
    }
    void key_num(const char *key, uint64_t v) {
        ch('"');
        str(key);
        str("\":");
        num(v);
    }
    // NUL-terminates when it fits; returns whether it did.
    bool finish() {
        if (!out || len + 1 > cap) return false;
        out[len] = '\0';
        return true;
    }
};

void write_hist(Writer &w, const char *name, const ckey_telemetry_histogram &h) {
    w.ch('"');
    w.str(name);
    w.str("\":{");
    w.key_num("count", h.count);
    w.ch(',');
    w.key_num("sum", h.sum);
    w.str(",\"buckets\":[");
    for (size_t i = 0; i < CKEY_TELEMETRY_BUCKETS; ++i) {
        if (i) w.ch(',');
        w.num(h.buckets[i]);
    }
    w.str("]}");
}

const char *const kProfileKeys[CKEY_TELEMETRY_PROFILES] = {"p0", "p0_1", "p1", "p2", "p3"};

// The counter fields, in documented order, without the enclosing braces.
void write_counters(Writer &w, const ckey_telemetry_counters &c) {
    w.str("\"sessions\":{");
    w.key_num("started", c.sessions_started);
    w.ch(',');
    w.key_num("succeeded", c.sessions_succeeded);
    w.ch(',');
    w.key_num("failed", c.sessions_failed);
    w.str("},");
    w.key_num("frames", c.frames);
    w.str(",\"decoded\":{");
    for (size_t i = 0; i < CKEY_TELEMETRY_PROFILES; ++i) {
        if (i) w.ch(',');
        w.key_num(kProfileKeys[i], c.decoded[i]);
    }
    w.str("},");
    write_hist(w, "time_to_decode_ms", c.time_to_decode_ms);
    w.ch(',');
    write_hist(w, "frame_ms", c.frame_ms);
}

// ---- Strict, bounded state parser -------------------------------------

struct Parser {
    const char *s;
    size_t n;
    size_t i = 0;
    void ws() {
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool lit(const char *t) {
        ws();
        size_t l = std::strlen(t);
        if (n - i < l || std::memcmp(s + i, t, l) != 0) return false;
        i += l;
        return true;
    }
    bool key(const char *name) {
        ws();
        if (i >= n || s[i] != '"') return false;
        ++i;
        size_t l = std::strlen(name);
        if (n - i < l + 1 || std::memcmp(s + i, name, l) != 0 || s[i + l] != '"') return false;
        i += l + 1;
        return lit(":");
    }
    bool u64(uint64_t &v) {
        ws();
        if (i >= n || s[i] < '0' || s[i] > '9') return false;
        if (s[i] == '0' && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '9') return false; // leading zero
        uint64_t r = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            uint64_t d = static_cast<uint64_t>(s[i] - '0');
            if (r > (UINT64_MAX - d) / 10) return false;
            r = r * 10 + d;
            ++i;
        }
        v = r;
        return true;
    }
    bool key_u64(const char *name, uint64_t &v) { return key(name) && u64(v); }
};

bool parse_hist(Parser &p, const char *name, ckey_telemetry_histogram &h) {
    if (!p.key(name) || !p.lit("{") || !p.key_u64("count", h.count) || !p.lit(",") || !p.key_u64("sum", h.sum) ||
        !p.lit(",") || !p.key("buckets") || !p.lit("["))
        return false;
    for (size_t i = 0; i < CKEY_TELEMETRY_BUCKETS; ++i) {
        if (i && !p.lit(",")) return false;
        if (!p.u64(h.buckets[i])) return false;
    }
    if (!p.lit("]") || !p.lit("}")) return false;
    // Consistency: buckets add up to count, and the sum is achievable.
    uint64_t total = 0;
    for (uint64_t b : h.buckets) {
        if (total > UINT64_MAX - b) return false;
        total += b;
    }
    if (total != h.count) return false;
    // sum > count * kMaxObservation, without overflow: a product that does
    // not fit in 64 bits exceeds every 64-bit sum.
    if (h.count <= UINT64_MAX / kMaxObservation && h.sum > h.count * kMaxObservation) return false;
    return true;
}

bool parse_counters(Parser &p, ckey_telemetry_counters &c) {
    if (!p.lit("{") || !p.key("sessions") || !p.lit("{") || !p.key_u64("started", c.sessions_started) ||
        !p.lit(",") || !p.key_u64("succeeded", c.sessions_succeeded) || !p.lit(",") ||
        !p.key_u64("failed", c.sessions_failed) || !p.lit("}") || !p.lit(",") || !p.key_u64("frames", c.frames) ||
        !p.lit(",") || !p.key("decoded") || !p.lit("{"))
        return false;
    for (size_t i = 0; i < CKEY_TELEMETRY_PROFILES; ++i) {
        if (i && !p.lit(",")) return false;
        if (!p.key_u64(kProfileKeys[i], c.decoded[i])) return false;
    }
    return p.lit("}") && p.lit(",") && parse_hist(p, "time_to_decode_ms", c.time_to_decode_ms) && p.lit(",") &&
           parse_hist(p, "frame_ms", c.frame_ms) && p.lit("}");
}

// ---- Validation ---------------------------------------------------------

bool in_list(const char *v, const char *const *list, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(v, list[i]) == 0) return true;
    return false;
}

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

// ^0\.[0-9]+\.[0-9]+(-(build|dev)\.[0-9]{1,6})?$
bool valid_version(const char *v) {
    size_t len = strnlen(v, kMaxVersionBytes + 1);
    if (len > kMaxVersionBytes) return false;
    size_t i = 0;
    if (v[i++] != '0' || v[i++] != '.') return false;
    for (int part = 0; part < 2; ++part) {
        size_t start = i;
        while (is_digit(v[i])) ++i;
        if (i == start) return false;
        if (part == 0) {
            if (v[i] != '.') return false;
            ++i;
        }
    }
    if (v[i] == '\0') return true;
    if (v[i++] != '-') return false;
    if (std::strncmp(v + i, "build.", 6) == 0) i += 6;
    else if (std::strncmp(v + i, "dev.", 4) == 0) i += 4;
    else return false;
    size_t start = i;
    while (is_digit(v[i])) ++i;
    return i > start && i - start <= 6 && v[i] == '\0';
}

bool valid_day(const char *d) {
    if (strnlen(d, 11) != 10) return false;
    for (int i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            if (d[i] != '-') return false;
        } else if (!is_digit(d[i])) {
            return false;
        }
    }
    int y = (d[0] - '0') * 1000 + (d[1] - '0') * 100 + (d[2] - '0') * 10 + (d[3] - '0');
    int m = (d[5] - '0') * 10 + (d[6] - '0');
    int day = (d[8] - '0') * 10 + (d[9] - '0');
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12 || day < 1) return false;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    int max = kDays[m - 1] + ((m == 2 && leap) ? 1 : 0);
    return day <= max;
}

void reset_counting(State &s) {
    s.totals = {};
    clear_delta(s);
    drop_inflight(s);
    s.session = Session::Idle;
    s.session_start = 0;
}

} // namespace

extern "C" {

ckey_status ckey_telemetry_set_mode(int mode) {
    if (mode != CKEY_TELEMETRY_OFF && mode != CKEY_TELEMETRY_ON && mode != CKEY_TELEMETRY_LOCAL)
        return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    if (mode == s.mode) return CKEY_OK;
    if (mode == CKEY_TELEMETRY_OFF || s.mode == CKEY_TELEMETRY_OFF) {
        // any -> 0 purges; 0 -> 1|2 starts from zero (already zero).
        reset_counting(s);
    } else {
        // 1 -> 2: keep totals, drop the delta and the in-flight batch.
        // 2 -> 1: the delta starts at zero (it is always empty in mode 2).
        clear_delta(s);
        drop_inflight(s);
    }
    s.mode = mode;
    return CKEY_OK;
}

int ckey_telemetry_get_mode(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state.mode;
}

ckey_status ckey_telemetry_set_client(const char *sdk, const char *sdk_version, const char *platform) {
    if (!sdk || !sdk_version || !platform) return CKEY_INVALID_ARGUMENT;
    if (!in_list(sdk, kSdks, sizeof kSdks / sizeof kSdks[0])) return CKEY_INVALID_ARGUMENT;
    if (!in_list(platform, kPlatforms, sizeof kPlatforms / sizeof kPlatforms[0])) return CKEY_INVALID_ARGUMENT;
    if (!valid_version(sdk_version)) return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    std::strcpy(s.sdk, sdk);                 // lengths bounded by the lists above
    std::strcpy(s.sdk_version, sdk_version); // bounded by valid_version
    std::strcpy(s.platform, platform);
    return CKEY_OK;
}

void ckey_telemetry_session_begin(uint64_t now_ms) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    if (s.mode == CKEY_TELEMETRY_OFF) return;
    if (s.session == Session::Open) fail_session(s, now_ms);
    begin_session(s, now_ms);
}

void ckey_telemetry_frame(uint64_t now_ms, uint32_t processing_ms) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    if (s.mode == CKEY_TELEMETRY_OFF) return;
    check_timeout(s, now_ms);
    if (s.session == Session::Idle) begin_session(s, now_ms);
    record(s, now_ms, [processing_ms](ckey_telemetry_counters &c) {
        sat_add(c.frames, 1);
        hist_record(c.frame_ms, kFrameEdges, processing_ms);
    });
}

ckey_status ckey_telemetry_session_success(uint64_t now_ms, uint32_t profile_code) {
    if (profile_code >= CKEY_TELEMETRY_PROFILES) return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    if (s.mode == CKEY_TELEMETRY_OFF) return CKEY_OK;
    check_timeout(s, now_ms);
    if (s.session == Session::Done) return CKEY_OK; // later decodes of the same session are not counted
    if (s.session == Session::Idle) begin_session(s, now_ms);
    uint64_t ttd = elapsed(now_ms, s.session_start);
    s.session = Session::Done;
    record(s, now_ms, [ttd, profile_code](ckey_telemetry_counters &c) {
        sat_add(c.sessions_succeeded, 1);
        sat_add(c.decoded[profile_code], 1);
        hist_record(c.time_to_decode_ms, kTtdEdges, ttd);
    });
    return CKEY_OK;
}

void ckey_telemetry_session_end(uint64_t now_ms) {
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    if (s.mode == CKEY_TELEMETRY_OFF) return;
    if (s.session == Session::Open) fail_session(s, now_ms);
    s.session = Session::Idle;
}

ckey_status ckey_telemetry_snapshot(ckey_telemetry_counters *out) {
    if (!out) return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    *out = g_state.totals; // zero in mode 0 (purged on entry, never recorded)
    return CKEY_OK;
}

int ckey_telemetry_should_send(uint64_t now_ms) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const State &s = g_state;
    if (s.mode != CKEY_TELEMETRY_ON || s.inflight_active || is_empty(s.delta)) return 0;
    if (s.has_last_attempt && elapsed(now_ms, s.last_attempt) < CKEY_TELEMETRY_SEND_INTERVAL_MS) return 0;
    if (!s.delta_ready && s.delta_has_since && elapsed(now_ms, s.delta_since) < CKEY_TELEMETRY_MIN_DELTA_AGE_MS)
        return 0;
    return 1;
}

ckey_status ckey_telemetry_take_batch(const char *utc_day, char *out_json, size_t cap, size_t *out_len,
                                      uint64_t *out_batch_id) {
    if (!utc_day || !out_len || !out_batch_id || (!out_json && cap != 0)) return CKEY_INVALID_ARGUMENT;
    if (!valid_day(utc_day)) return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    *out_len = 0;
    *out_batch_id = 0;
    if (s.mode != CKEY_TELEMETRY_ON || s.inflight_active || is_empty(s.delta)) {
        if (cap) out_json[0] = '\0';
        return CKEY_OK;
    }
    ckey_telemetry_counters remaining = s.delta;
    ckey_telemetry_counters part = take_part(remaining);
    Writer w{out_json, cap};
    w.str("{\"schema\":");
    w.num(CKEY_TELEMETRY_SCHEMA);
    w.str(",\"sdk\":\"");
    w.str(s.sdk);
    w.str("\",\"sdk_version\":\"");
    w.str(s.sdk_version);
    w.str("\",\"platform\":\"");
    w.str(s.platform);
    w.str("\",\"day\":\"");
    w.str(utc_day);
    w.str("\",");
    write_counters(w, part);
    w.ch('}');
    *out_len = w.len;
    if (!w.finish()) return CKEY_INSUFFICIENT_BUFFER; // nothing moved
    s.delta = remaining;
    s.inflight = part;
    s.inflight_active = true;
    s.inflight_id = s.next_id++;
    if (s.next_id == 0) s.next_id = 1;
    if (is_empty(s.delta)) clear_delta(s);
    else s.delta_ready = true; // a clamped remainder is not subject to the minimum age
    *out_batch_id = s.inflight_id;
    return CKEY_OK;
}

ckey_status ckey_telemetry_batch_result(uint64_t batch_id, int http_status, uint64_t now_ms) {
    if (http_status < 0 || http_status > 999) return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    if (batch_id == 0 || !s.inflight_active || batch_id != s.inflight_id) return CKEY_INVALID_ARGUMENT;
    bool keep = http_status == 0 || http_status == 429 || http_status < 200 || (http_status >= 300 && http_status < 400) ||
                http_status >= 500;
    if (keep && s.mode == CKEY_TELEMETRY_ON) {
        merge(s.delta, s.inflight);
        s.delta_ready = true; // already batched once; the hourly cadence still applies
    }
    drop_inflight(s);
    s.has_last_attempt = true;
    s.last_attempt = now_ms;
    return CKEY_OK;
}

ckey_status ckey_telemetry_export_state(char *out, size_t cap, size_t *len) {
    if (!len || (!out && cap != 0)) return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    const State &s = g_state;
    Writer w{out, cap};
    w.str("{\"state\":1,\"totals\":{");
    write_counters(w, s.totals);
    w.str("},\"unsent\":{");
    write_counters(w, s.delta);
    w.str("}}");
    *len = w.len;
    return w.finish() ? CKEY_OK : CKEY_INSUFFICIENT_BUFFER;
}

ckey_status ckey_telemetry_import_state(const char *json) {
    if (!json) return CKEY_INVALID_ARGUMENT;
    size_t n = strnlen(json, kMaxImportBytes + 1);
    if (n > kMaxImportBytes) return CKEY_INVALID_ARGUMENT;
    Parser p{json, n};
    ckey_telemetry_counters totals{}, unsent{};
    uint64_t version = 0;
    if (!p.lit("{") || !p.key_u64("state", version) || version != 1 || !p.lit(",") || !p.key("totals") ||
        !parse_counters(p, totals) || !p.lit(",") || !p.key("unsent") || !parse_counters(p, unsent) || !p.lit("}"))
        return CKEY_INVALID_ARGUMENT;
    p.ws();
    if (p.i != n) return CKEY_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    State &s = g_state;
    if (s.mode == CKEY_TELEMETRY_OFF) return CKEY_INVALID_ARGUMENT;
    merge(s.totals, totals);
    if (s.mode == CKEY_TELEMETRY_ON && !is_empty(unsent)) {
        merge(s.delta, unsent);
        s.delta_ready = true;
    }
    return CKEY_OK;
}

void ckey_telemetry_reset_for_testing(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    uint64_t next_id = g_state.next_id; // keep ids unique across resets
    g_state = State{};
    g_state.next_id = next_id;
}

} // extern "C"
