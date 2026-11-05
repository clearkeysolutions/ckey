// Tests for the telemetry core (include/ckey/telemetry.h). Self-contained:
// links only src/telemetry.cpp.

#include "ckey/telemetry.h"
#include "check.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

// ---- Minimal JSON reader + batch validator per docs/adoption-and-telemetry.md

struct Json {
    enum Kind { Num, Str, Arr, Obj } kind = Num;
    uint64_t num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::string> keys; // object keys, in document order
    std::vector<Json> vals;        // object values, parallel to keys
};

struct Reader {
    const std::string &s;
    size_t i = 0;
    bool ok = true;
    void fail() { ok = false; }
    Json value() {
        Json v;
        if (!ok || i >= s.size()) { fail(); return v; }
        char c = s[i];
        if (c == '{') {
            v.kind = Json::Obj; ++i;
            if (i < s.size() && s[i] == '}') { ++i; return v; }
            for (;;) {
                Json k = value();
                if (!ok || k.kind != Json::Str || i >= s.size() || s[i] != ':') { fail(); return v; }
                ++i;
                Json x = value();
                v.keys.push_back(k.str);
                v.vals.push_back(x);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return v; }
                fail(); return v;
            }
        }
        if (c == '[') {
            v.kind = Json::Arr; ++i;
            if (i < s.size() && s[i] == ']') { ++i; return v; }
            for (;;) {
                v.arr.push_back(value());
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return v; }
                fail(); return v;
            }
        }
        if (c == '"') {
            v.kind = Json::Str; ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' || static_cast<unsigned char>(s[i]) < 0x20) { fail(); return v; }
                v.str += s[i++];
            }
            if (i >= s.size()) { fail(); return v; }
            ++i;
            return v;
        }
        if (c >= '0' && c <= '9') {
            if (c == '0' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') { fail(); return v; }
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                if (v.num > (UINT64_MAX - 9) / 10) { fail(); return v; }
                v.num = v.num * 10 + static_cast<uint64_t>(s[i++] - '0');
            }
            return v;
        }
        fail();
        return v;
    }
};

bool keys_are(const Json &o, std::vector<std::string> keys) {
    return o.kind == Json::Obj && o.keys == keys;
}

bool count_ok(const Json &v) { return v.kind == Json::Num && v.num <= CKEY_TELEMETRY_MAX_COUNT; }

bool hist_ok(const Json &h) {
    if (!keys_are(h, {"count", "sum", "buckets"})) return false;
    const Json &count = h.vals[0], &sum = h.vals[1], &buckets = h.vals[2];
    if (!count_ok(count) || sum.kind != Json::Num || sum.num > CKEY_TELEMETRY_MAX_SUM) return false;
    if (buckets.kind != Json::Arr || buckets.arr.size() != 10) return false;
    uint64_t total = 0;
    for (const Json &b : buckets.arr) {
        if (!count_ok(b)) return false;
        total += b.num;
    }
    return total == count.num;
}

// "D" matches one digit, any other char matches itself.
bool shape_ok(const std::string &v, const std::string &shape) {
    if (v.size() != shape.size()) return false;
    for (size_t i = 0; i < v.size(); ++i)
        if (shape[i] == 'D' ? !(v[i] >= '0' && v[i] <= '9') : v[i] != shape[i]) return false;
    return true;
}

// ^0\.[0-9]+\.[0-9]+(-(build|dev)\.[0-9]{1,6})?$, written independently of the core.
bool version_ok(const std::string &v) {
    size_t i = 0;
    auto digits = [&](size_t min, size_t max) {
        size_t start = i;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') ++i;
        return i - start >= min && i - start <= max;
    };
    auto lit = [&](const std::string &t) {
        if (v.compare(i, t.size(), t) != 0) return false;
        i += t.size();
        return true;
    };
    if (!lit("0.") || !digits(1, SIZE_MAX) || !lit(".") || !digits(1, SIZE_MAX)) return false;
    if (i == v.size()) return true;
    if (!(lit("-build.") || lit("-dev."))) return false;
    return digits(1, 6) && i == v.size();
}

// Validates a batch body against every field rule of schema version 1.
bool batch_valid(const std::string &body) {
    if (body.size() > CKEY_TELEMETRY_MAX_BODY_BYTES) return false;
    Reader r{body};
    Json root = r.value();
    if (!r.ok || r.i != body.size()) return false;
    if (!keys_are(root, {"schema", "sdk", "sdk_version", "platform", "day", "sessions", "frames", "decoded",
                         "time_to_decode_ms", "frame_ms"}))
        return false;
    auto f = [&](size_t i) -> const Json & { return root.vals[i]; };
    if (f(0).kind != Json::Num || f(0).num != 1) return false;
    auto one_of = [](const std::string &v, std::vector<std::string> list) {
        for (auto &x : list)
            if (v == x) return true;
        return false;
    };
    if (f(1).kind != Json::Str || !one_of(f(1).str, {"c", "swift", "android", "web", "dotnet"})) return false;
    if (f(2).kind != Json::Str || !version_ok(f(2).str)) return false;
    if (f(3).kind != Json::Str ||
        !one_of(f(3).str, {"ios", "macos", "android", "web", "windows", "linux", "other"}))
        return false;
    if (f(4).kind != Json::Str || !shape_ok(f(4).str, "DDDD-DD-DD")) return false;
    if (!keys_are(f(5), {"started", "succeeded", "failed"})) return false;
    for (auto &v : f(5).vals)
        if (!count_ok(v)) return false;
    if (!count_ok(f(6))) return false;
    if (!keys_are(f(7), {"p0", "p0_1", "p1", "p2", "p3"})) return false;
    for (auto &v : f(7).vals)
        if (!count_ok(v)) return false;
    return hist_ok(f(8)) && hist_ok(f(9));
}

// Field getters on a parsed batch.
Json parse(const std::string &s) {
    Reader r{s};
    Json j = r.value();
    CKEY_CHECK(r.ok && r.i == s.size());
    return j;
}
const Json &field(const Json &o, const char *k) {
    for (size_t i = 0; i < o.keys.size(); ++i)
        if (o.keys[i] == k) return o.vals[i];
    CKEY_CHECK(false);
    return o;
}

// ---- Helpers ---------------------------------------------------------------

const char *kDay = "2026-11-05";

std::string take(uint64_t *id = nullptr) {
    char buf[4096];
    size_t len = 99;
    uint64_t bid = 99;
    CKEY_CHECK(ckey_telemetry_take_batch(kDay, buf, sizeof buf, &len, &bid) == CKEY_OK);
    CKEY_CHECK(len == std::strlen(buf));
    CKEY_CHECK((len == 0) == (bid == 0));
    if (id) *id = bid;
    if (len) CKEY_CHECK(batch_valid(buf));
    return std::string(buf, len);
}

std::string export_state() {
    size_t len = 0;
    CKEY_CHECK(ckey_telemetry_export_state(nullptr, 0, &len) == CKEY_INSUFFICIENT_BUFFER);
    std::string out(len + 1, '\0');
    size_t len2 = 0;
    CKEY_CHECK(ckey_telemetry_export_state(out.data(), out.size(), &len2) == CKEY_OK);
    CKEY_CHECK(len2 == len);
    out.resize(len);
    return out;
}

ckey_telemetry_counters snap() {
    ckey_telemetry_counters c;
    std::memset(&c, 0xab, sizeof c);
    CKEY_CHECK(ckey_telemetry_snapshot(&c) == CKEY_OK);
    return c;
}

bool is_zero(const ckey_telemetry_counters &c) {
    ckey_telemetry_counters z{};
    return std::memcmp(&c, &z, sizeof c) == 0;
}

bool same(const ckey_telemetry_counters &a, const ckey_telemetry_counters &b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// The known scenario used for the exact-JSON test (see expected string).
void record_scenario() {
    ckey_telemetry_session_begin(1000);
    ckey_telemetry_frame(1000, 1);   // frame bucket 0 (<= 2)
    ckey_telemetry_frame(1010, 2);   // bucket 0 (edge, inclusive)
    ckey_telemetry_frame(1020, 3);   // bucket 1
    CKEY_CHECK(ckey_telemetry_session_success(1025, CKEY_TELEMETRY_PROFILE_P0) == CKEY_OK); // ttd 25 -> bucket 0
    ckey_telemetry_session_begin(2000);
    ckey_telemetry_frame(2000, 640); // bucket 8 (edge)
    ckey_telemetry_frame(2010, 641); // bucket 9
    CKEY_CHECK(ckey_telemetry_session_success(2026, CKEY_TELEMETRY_PROFILE_P1) == CKEY_OK); // ttd 26 -> bucket 1
    ckey_telemetry_session_begin(3000);
    ckey_telemetry_frame(3000, 10);  // bucket 2
    ckey_telemetry_session_end(3100); // failed
    ckey_telemetry_frame(40000, 5);  // implicit begin; bucket 1
    ckey_telemetry_frame(70000, 20); // 30 s elapsed: timeout fail, implicit begin; bucket 3
    ckey_telemetry_session_end(70001); // failed
}

const char *kScenarioJson =
    "{\"schema\":1,\"sdk\":\"swift\",\"sdk_version\":\"0.1.0-build.61\",\"platform\":\"ios\",\"day\":\"2026-11-05\","
    "\"sessions\":{\"started\":5,\"succeeded\":2,\"failed\":3},\"frames\":8,"
    "\"decoded\":{\"p0\":1,\"p0_1\":0,\"p1\":1,\"p2\":0,\"p3\":0},"
    "\"time_to_decode_ms\":{\"count\":2,\"sum\":51,\"buckets\":[1,1,0,0,0,0,0,0,0,0]},"
    "\"frame_ms\":{\"count\":8,\"sum\":1322,\"buckets\":[2,2,1,1,0,0,0,0,1,1]}}";

// ---- Tests -------------------------------------------------------------------

void test_defaults_and_modes() {
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(ckey_telemetry_get_mode() == CKEY_TELEMETRY_ON);
    CKEY_CHECK(CKEY_TELEMETRY_OFF == 0 && CKEY_TELEMETRY_ON == 1 && CKEY_TELEMETRY_LOCAL == 2);
    CKEY_CHECK(std::strcmp(CKEY_TELEMETRY_ENDPOINT, "https://openckey.org/api/telemetry/v1/batch") == 0);
    CKEY_CHECK(CKEY_TELEMETRY_SCHEMA == 1);
    for (int bad : {-1, 3, 99, 1000, INT32_MIN}) {
        CKEY_CHECK(ckey_telemetry_set_mode(bad) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_telemetry_get_mode() == CKEY_TELEMETRY_ON);
    }
    CKEY_CHECK(ckey_telemetry_set_mode(2) == CKEY_OK);
    for (int bad : {-1, 3, 99}) {
        CKEY_CHECK(ckey_telemetry_set_mode(bad) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_telemetry_get_mode() == CKEY_TELEMETRY_LOCAL);
    }
    CKEY_CHECK(ckey_telemetry_set_mode(0) == CKEY_OK);
    CKEY_CHECK(ckey_telemetry_set_mode(3) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_get_mode() == CKEY_TELEMETRY_OFF);
    CKEY_CHECK(ckey_telemetry_snapshot(nullptr) == CKEY_INVALID_ARGUMENT);
}

void test_default_client_json() {
    ckey_telemetry_reset_for_testing();
    ckey_telemetry_frame(0, 1);
    std::string b = take();
    Json j = parse(b);
    CKEY_CHECK(field(j, "sdk").str == "c");
    CKEY_CHECK(field(j, "sdk_version").str == CKEY_TELEMETRY_DEFAULT_VERSION);
    CKEY_CHECK(field(j, "platform").str == "other");
}

void test_mode_off() {
    ckey_telemetry_reset_for_testing();
    record_scenario();
    CKEY_CHECK(!is_zero(snap()));
    uint64_t id = 0;
    CKEY_CHECK(!take(&id).empty());
    ckey_telemetry_frame(80000, 3); // unsent delta beside the in-flight batch
    CKEY_CHECK(ckey_telemetry_set_mode(0) == CKEY_OK);
    CKEY_CHECK(is_zero(snap())); // purged
    CKEY_CHECK(ckey_telemetry_batch_result(id, 500, 90000) == CKEY_INVALID_ARGUMENT); // in-flight discarded
    record_scenario(); // no-ops
    CKEY_CHECK(ckey_telemetry_session_success(1, 0) == CKEY_OK);
    CKEY_CHECK(is_zero(snap()));
    CKEY_CHECK(take().empty());
    CKEY_CHECK(!ckey_telemetry_should_send(UINT64_MAX / 2));
    // 0 -> 1 starts from zero.
    CKEY_CHECK(ckey_telemetry_set_mode(1) == CKEY_OK);
    CKEY_CHECK(is_zero(snap()));
    CKEY_CHECK(take().empty());
    ckey_telemetry_frame(100, 7);
    ckey_telemetry_counters c = snap();
    CKEY_CHECK(c.frames == 1 && c.sessions_started == 1);
    // 0 -> 2 also from zero.
    CKEY_CHECK(ckey_telemetry_set_mode(0) == CKEY_OK);
    CKEY_CHECK(ckey_telemetry_set_mode(2) == CKEY_OK);
    CKEY_CHECK(is_zero(snap()));
}

void test_on_to_local() {
    ckey_telemetry_reset_for_testing();
    record_scenario();
    ckey_telemetry_counters before = snap();
    uint64_t id = 0;
    CKEY_CHECK(!take(&id).empty());
    ckey_telemetry_frame(80000, 4); // unsent
    before = snap();
    CKEY_CHECK(ckey_telemetry_set_mode(2) == CKEY_OK);
    CKEY_CHECK(same(snap(), before)); // local totals kept
    // In-flight result ignored, never merged back, whatever the status.
    for (int st : {0, 429, 500, 204, 400})
        CKEY_CHECK(ckey_telemetry_batch_result(id, st, 90000) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(take().empty()); // mode 2 never sends
    ckey_telemetry_frame(90000, 4);
    CKEY_CHECK(snap().frames == before.frames + 1); // still counts locally
    CKEY_CHECK(!ckey_telemetry_should_send(UINT64_MAX / 2));
    // Back to 1: the dropped delta and mode-2 counts are never sent.
    CKEY_CHECK(ckey_telemetry_set_mode(1) == CKEY_OK);
    CKEY_CHECK(take().empty());
    CKEY_CHECK(ckey_telemetry_batch_result(id, 500, 90000) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(take().empty());
}

void test_local_to_on_fresh_delta() {
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(ckey_telemetry_set_mode(2) == CKEY_OK);
    record_scenario();
    CKEY_CHECK(snap().frames == 8);
    CKEY_CHECK(ckey_telemetry_set_mode(1) == CKEY_OK);
    CKEY_CHECK(snap().frames == 8); // totals kept
    CKEY_CHECK(take().empty());     // fresh delta
    ckey_telemetry_session_begin(100000);
    ckey_telemetry_frame(100000, 3);
    Json j = parse(take());
    CKEY_CHECK(field(j, "frames").num == 1);
    CKEY_CHECK(field(field(j, "sessions"), "started").num == 1);
    CKEY_CHECK(snap().frames == 9);
}

void test_sessions() {
    ckey_telemetry_reset_for_testing();
    // Explicit begin + success; later frames/decodes in the same session are not new sessions.
    ckey_telemetry_session_begin(0);
    ckey_telemetry_frame(10, 1);
    CKEY_CHECK(ckey_telemetry_session_success(100, CKEY_TELEMETRY_PROFILE_P2) == CKEY_OK);
    ckey_telemetry_frame(110, 1);
    CKEY_CHECK(ckey_telemetry_session_success(120, CKEY_TELEMETRY_PROFILE_P2) == CKEY_OK); // ignored
    ckey_telemetry_frame(100000, 1); // DONE does not time out or start a session
    ckey_telemetry_session_end(100001); // DONE closes without failing
    ckey_telemetry_counters c = snap();
    CKEY_CHECK(c.sessions_started == 1 && c.sessions_succeeded == 1 && c.sessions_failed == 0);
    CKEY_CHECK(c.frames == 3 && c.decoded[3] == 1 && c.time_to_decode_ms.count == 1 && c.time_to_decode_ms.sum == 100);
    CKEY_CHECK(c.time_to_decode_ms.buckets[2] == 1); // 100 -> bucket 2 (<= 100)
    // After end, the next frame implicitly begins.
    ckey_telemetry_frame(200000, 1);
    c = snap();
    CKEY_CHECK(c.sessions_started == 2);
    // Begin while open fails the open one.
    ckey_telemetry_session_begin(200500);
    c = snap();
    CKEY_CHECK(c.sessions_started == 3 && c.sessions_failed == 1);
    // End with nothing open is a no-op; end twice fails once.
    ckey_telemetry_session_end(200600);
    ckey_telemetry_session_end(200700);
    c = snap();
    CKEY_CHECK(c.sessions_failed == 2);
    // Timeout boundary: 29,999 ms is still the same session; 30,000 ms times out.
    ckey_telemetry_session_begin(300000);
    ckey_telemetry_frame(329999, 1);
    CKEY_CHECK(snap().sessions_started == 4);
    ckey_telemetry_frame(330000, 1);
    c = snap();
    CKEY_CHECK(c.sessions_started == 5 && c.sessions_failed == 3);
    // Success after a timeout: old session fails, new one starts at now with ttd 0.
    CKEY_CHECK(ckey_telemetry_session_success(360000, CKEY_TELEMETRY_PROFILE_P0_1) == CKEY_OK);
    c = snap();
    CKEY_CHECK(c.sessions_started == 6 && c.sessions_failed == 4 && c.sessions_succeeded == 2);
    CKEY_CHECK(c.decoded[1] == 1 && c.time_to_decode_ms.buckets[0] == 1 && c.time_to_decode_ms.sum == 100);
    // Success with no session: implicit begin, ttd 0.
    ckey_telemetry_session_end(360001);
    CKEY_CHECK(ckey_telemetry_session_success(400000, CKEY_TELEMETRY_PROFILE_P3) == CKEY_OK);
    c = snap();
    CKEY_CHECK(c.sessions_started == 7 && c.sessions_succeeded == 3 && c.decoded[4] == 1);
    // Success just before the timeout is a success, ttd 29,999.
    ckey_telemetry_session_begin(500000);
    CKEY_CHECK(ckey_telemetry_session_success(529999, CKEY_TELEMETRY_PROFILE_P0) == CKEY_OK);
    c = snap();
    CKEY_CHECK(c.sessions_succeeded == 4 && c.time_to_decode_ms.buckets[9] == 1);
    // Clock going backwards counts as 0 ms elapsed.
    ckey_telemetry_session_begin(600000);
    CKEY_CHECK(ckey_telemetry_session_success(500, CKEY_TELEMETRY_PROFILE_P0) == CKEY_OK);
    c = snap();
    CKEY_CHECK(c.time_to_decode_ms.buckets[0] == 3);
    // Invalid profile: nothing recorded.
    ckey_telemetry_counters before = snap();
    ckey_telemetry_session_begin(700000);
    before = snap();
    CKEY_CHECK(ckey_telemetry_session_success(700010, 5) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_session_success(700010, UINT32_MAX) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(same(snap(), before));
    // Bucket sums equal counts throughout.
    c = snap();
    for (const ckey_telemetry_histogram *h : {&c.time_to_decode_ms, &c.frame_ms}) {
        uint64_t t = 0;
        for (uint64_t b : h->buckets) t += b;
        CKEY_CHECK(t == h->count);
    }
}

void test_histogram_edges() {
    const uint64_t ttd_edges[9] = {25, 50, 100, 200, 400, 800, 1600, 3200, 6400};
    const uint32_t frame_edges[9] = {2, 5, 10, 20, 40, 80, 160, 320, 640};
    for (size_t i = 0; i < 9; ++i) {
        for (uint64_t delta : {0u, 1u}) {
            ckey_telemetry_reset_for_testing();
            ckey_telemetry_session_begin(1000);
            CKEY_CHECK(ckey_telemetry_session_success(1000 + ttd_edges[i] + delta, 0) == CKEY_OK);
            ckey_telemetry_counters c = snap();
            size_t want = i + delta;
            for (size_t b = 0; b < 10; ++b) CKEY_CHECK(c.time_to_decode_ms.buckets[b] == (b == want ? 1u : 0u));
            CKEY_CHECK(c.time_to_decode_ms.sum == ttd_edges[i] + delta);

            ckey_telemetry_reset_for_testing();
            ckey_telemetry_frame(0, frame_edges[i] + static_cast<uint32_t>(delta));
            c = snap();
            for (size_t b = 0; b < 10; ++b) CKEY_CHECK(c.frame_ms.buckets[b] == (b == want ? 1u : 0u));
        }
    }
    ckey_telemetry_reset_for_testing();
    ckey_telemetry_frame(0, 0);
    ckey_telemetry_frame(1, UINT32_MAX);
    ckey_telemetry_counters c = snap();
    CKEY_CHECK(c.frame_ms.buckets[0] == 1 && c.frame_ms.buckets[9] == 1 && c.frame_ms.sum == UINT32_MAX);
}

void test_exact_json_and_buffer() {
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(ckey_telemetry_set_client("swift", "0.1.0-build.61", "ios") == CKEY_OK);
    record_scenario();
    const size_t want = std::strlen(kScenarioJson);
    // Size query and too-small buffers move nothing.
    size_t len = 0;
    uint64_t id = 7;
    CKEY_CHECK(ckey_telemetry_take_batch(kDay, nullptr, 0, &len, &id) == CKEY_INSUFFICIENT_BUFFER);
    CKEY_CHECK(len == want && id == 0);
    std::vector<char> small(want); // no room for the NUL
    CKEY_CHECK(ckey_telemetry_take_batch(kDay, small.data(), small.size(), &len, &id) == CKEY_INSUFFICIENT_BUFFER);
    CKEY_CHECK(len == want && id == 0);
    CKEY_CHECK(ckey_telemetry_take_batch(kDay, nullptr, 10, &len, &id) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_take_batch(kDay, small.data(), small.size(), nullptr, &id) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_take_batch(kDay, small.data(), small.size(), &len, nullptr) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_take_batch(nullptr, small.data(), small.size(), &len, &id) == CKEY_INVALID_ARGUMENT);
    for (const char *bad : {"2026-13-01", "2026-02-29", "2026-1-05", "2026-11-5", "2026/11/05", "2026-11-050",
                            "2026-11-00", "", "20261105xx", "2026-04-31"})
        CKEY_CHECK(ckey_telemetry_take_batch(bad, small.data(), small.size(), &len, &id) == CKEY_INVALID_ARGUMENT);
    std::vector<char> exact(want + 1);
    CKEY_CHECK(ckey_telemetry_take_batch(kDay, exact.data(), exact.size(), &len, &id) == CKEY_OK);
    CKEY_CHECK(len == want && id != 0);
    CKEY_CHECK(std::string(exact.data()) == kScenarioJson);
    CKEY_CHECK(batch_valid(exact.data()));
    // In flight: nothing more to take.
    CKEY_CHECK(take().empty());
    CKEY_CHECK(ckey_telemetry_batch_result(id, 204, 100000) == CKEY_OK);
    // Leap day accepted.
    ckey_telemetry_frame(100001, 1);
    char buf[4096];
    CKEY_CHECK(ckey_telemetry_take_batch("2024-02-29", buf, sizeof buf, &len, &id) == CKEY_OK && len > 0);
    CKEY_CHECK(batch_valid(buf));
}

void test_set_client() {
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(ckey_telemetry_set_client("dotnet", "0.12.3-dev.5", "windows") == CKEY_OK);
    for (const char *s : {"c", "swift", "android", "web", "dotnet"})
        CKEY_CHECK(ckey_telemetry_set_client(s, "0.1.0", "linux") == CKEY_OK);
    for (const char *p : {"ios", "macos", "android", "web", "windows", "linux", "other"})
        CKEY_CHECK(ckey_telemetry_set_client("web", "0.1.0", p) == CKEY_OK);
    CKEY_CHECK(ckey_telemetry_set_client("android", "0.2.3-build.123456", "android") == CKEY_OK);
    const char *bad_versions[] = {"1.0.0", "0.1", "0.1.", "0..1", "0.1.0-", "0.1.0-build.", "0.1.0-build.1234567",
                                  "0.1.0-rc.1", "0.1.0-build.1x", "v0.1.0", "0.1.0 ", "", "0.a.0",
                                  "0.1.0-dev.1.2", "00.1.0x"};
    for (const char *v : bad_versions) CKEY_CHECK(ckey_telemetry_set_client("c", v, "other") == CKEY_INVALID_ARGUMENT);
    std::string long_version = "0." + std::string(70, '1') + ".0";
    CKEY_CHECK(ckey_telemetry_set_client("c", long_version.c_str(), "other") == CKEY_INVALID_ARGUMENT);
    for (const char *s : {"C", "java", "", "swift "})
        CKEY_CHECK(ckey_telemetry_set_client(s, "0.1.0", "other") == CKEY_INVALID_ARGUMENT);
    for (const char *p : {"iOS", "freebsd", ""})
        CKEY_CHECK(ckey_telemetry_set_client("c", "0.1.0", p) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_set_client(nullptr, "0.1.0", "ios") == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_set_client("c", nullptr, "ios") == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_set_client("c", "0.1.0", nullptr) == CKEY_INVALID_ARGUMENT);
    // Failed calls changed nothing: the last good client is reported.
    ckey_telemetry_frame(0, 1);
    Json j = parse(take());
    CKEY_CHECK(field(j, "sdk").str == "android" && field(j, "sdk_version").str == "0.2.3-build.123456" &&
               field(j, "platform").str == "android");
    // The client survives mode changes.
    CKEY_CHECK(ckey_telemetry_set_mode(0) == CKEY_OK && ckey_telemetry_set_mode(1) == CKEY_OK);
    ckey_telemetry_frame(0, 1);
    CKEY_CHECK(field(parse(take()), "sdk").str == "android");
}

void test_batch_results() {
    struct Case { int status; bool kept; };
    const Case cases[] = {{204, false}, {200, false}, {400, false}, {413, false}, {404, false},
                          {429, true},  {500, true},  {503, true},  {0, true},     {302, true}};
    for (const Case &k : cases) {
        ckey_telemetry_reset_for_testing();
        record_scenario();
        uint64_t id = 0;
        std::string first = take(&id);
        CKEY_CHECK(!first.empty());
        CKEY_CHECK(ckey_telemetry_batch_result(id + 1, k.status, 80000) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_telemetry_batch_result(0, k.status, 80000) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_telemetry_batch_result(id, -1, 80000) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_telemetry_batch_result(id, 1000, 80000) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(ckey_telemetry_batch_result(id, k.status, 80000) == CKEY_OK);
        CKEY_CHECK(ckey_telemetry_batch_result(id, k.status, 80000) == CKEY_INVALID_ARGUMENT); // once only
        uint64_t id2 = 0;
        std::string again = take(&id2);
        if (k.kept) {
            CKEY_CHECK(again == first && id2 != id);
        } else {
            CKEY_CHECK(again.empty());
        }
        // Totals are unaffected by sending.
        CKEY_CHECK(snap().frames == 8);
    }
    // A failed batch merges with counts recorded while it was in flight.
    ckey_telemetry_reset_for_testing();
    ckey_telemetry_frame(0, 1);
    uint64_t id = 0;
    CKEY_CHECK(!take(&id).empty());
    ckey_telemetry_frame(5, 1);
    CKEY_CHECK(ckey_telemetry_batch_result(id, 0, 10) == CKEY_OK);
    Json j = parse(take());
    CKEY_CHECK(field(j, "frames").num == 2);
}

void test_should_send() {
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(!ckey_telemetry_should_send(0));
    CKEY_CHECK(!ckey_telemetry_should_send(10000000)); // empty delta
    ckey_telemetry_frame(1000, 1);
    CKEY_CHECK(!ckey_telemetry_should_send(1000));
    CKEY_CHECK(!ckey_telemetry_should_send(60999)); // delta younger than 60 s
    CKEY_CHECK(ckey_telemetry_should_send(61000));
    uint64_t id = 0;
    CKEY_CHECK(!take(&id).empty());
    CKEY_CHECK(!ckey_telemetry_should_send(62000)); // in flight
    ckey_telemetry_frame(62000, 1);
    CKEY_CHECK(!ckey_telemetry_should_send(10000000)); // still in flight
    CKEY_CHECK(ckey_telemetry_batch_result(id, 204, 70000) == CKEY_OK);
    CKEY_CHECK(!ckey_telemetry_should_send(70000 + 3599999)); // hourly cadence
    CKEY_CHECK(ckey_telemetry_should_send(70000 + 3600000));
    CKEY_CHECK(!take(&id).empty());
    // Failure: retried no sooner than an hour after the reported result.
    CKEY_CHECK(ckey_telemetry_batch_result(id, 503, 4000000) == CKEY_OK);
    CKEY_CHECK(!ckey_telemetry_should_send(4000000));
    CKEY_CHECK(!ckey_telemetry_should_send(4000000 + 3599999));
    CKEY_CHECK(ckey_telemetry_should_send(4000000 + 3600000)); // no minimum age for a kept batch
    CKEY_CHECK(!take(&id).empty());
    CKEY_CHECK(ckey_telemetry_batch_result(id, 204, 8000000) == CKEY_OK);
    CKEY_CHECK(!ckey_telemetry_should_send(UINT64_MAX / 2)); // nothing left
    // New delta after an hour still needs 60 s of age.
    ckey_telemetry_frame(20000000, 1);
    CKEY_CHECK(!ckey_telemetry_should_send(20000000 + 59999));
    CKEY_CHECK(ckey_telemetry_should_send(20000000 + 60000));
    // Never in modes 0 and 2.
    CKEY_CHECK(ckey_telemetry_set_mode(2) == CKEY_OK);
    ckey_telemetry_frame(20000000, 1);
    CKEY_CHECK(!ckey_telemetry_should_send(UINT64_MAX / 2));
    CKEY_CHECK(ckey_telemetry_set_mode(0) == CKEY_OK);
    CKEY_CHECK(!ckey_telemetry_should_send(UINT64_MAX / 2));
}

void test_persistence() {
    ckey_telemetry_reset_for_testing();
    record_scenario();
    uint64_t id = 0;
    CKEY_CHECK(!take(&id).empty()); // in flight: not exported
    ckey_telemetry_session_begin(100000);
    ckey_telemetry_frame(100000, 33);
    CKEY_CHECK(ckey_telemetry_session_success(100050, CKEY_TELEMETRY_PROFILE_P2) == CKEY_OK);
    const std::string state = export_state();
    const ckey_telemetry_counters totals = snap();
    CKEY_CHECK(state.rfind("{\"state\":1,\"totals\":{\"sessions\":{\"started\":6,", 0) == 0);
    CKEY_CHECK(state.find("\"unsent\":{\"sessions\":{\"started\":1,\"succeeded\":1,\"failed\":0},\"frames\":1,") !=
               std::string::npos);
    // Buffer too small.
    size_t len = 0;
    std::vector<char> small(state.size());
    CKEY_CHECK(ckey_telemetry_export_state(small.data(), small.size(), &len) == CKEY_INSUFFICIENT_BUFFER);
    CKEY_CHECK(len == state.size());
    CKEY_CHECK(ckey_telemetry_export_state(nullptr, 5, &len) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_export_state(small.data(), small.size(), nullptr) == CKEY_INVALID_ARGUMENT);

    // Round trip into a fresh process state.
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(ckey_telemetry_import_state(state.c_str()) == CKEY_OK);
    CKEY_CHECK(ckey_telemetry_get_mode() == CKEY_TELEMETRY_ON);
    CKEY_CHECK(same(snap(), totals));
    CKEY_CHECK(export_state() == state);
    CKEY_CHECK(ckey_telemetry_should_send(0)); // restored delta: no minimum age, no cadence yet
    Json j = parse(take());
    CKEY_CHECK(field(j, "frames").num == 1);
    CKEY_CHECK(field(field(j, "decoded"), "p2").num == 1);

    // Whitespace between tokens is accepted.
    std::string spaced;
    for (char c : state) {
        spaced += c;
        if (c == ',' || c == ':' || c == '{' || c == '[') spaced += " \n\t";
    }
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(ckey_telemetry_import_state(("  " + spaced + "\r\n").c_str()) == CKEY_OK);
    CKEY_CHECK(export_state() == state);

    // Import adds.
    CKEY_CHECK(ckey_telemetry_import_state(state.c_str()) == CKEY_OK);
    CKEY_CHECK(snap().frames == 2 * totals.frames);

    // Malformed input is rejected and leaves the state unchanged.
    const std::string current = export_state();
    auto replaced = [&](const std::string &from, const std::string &to) {
        std::string s = state;
        size_t p = s.find(from);
        CKEY_CHECK(p != std::string::npos);
        s.replace(p, from.size(), to);
        return s;
    };
    std::vector<std::string> bad = {
        "",
        "{}",
        "null",
        state.substr(0, state.size() - 1),                    // truncated
        state + "x",                                           // trailing data
        state + "}",
        replaced("{\"state\":1", "{\"state\":2"),              // unknown version
        replaced("\"frames\":9", "\"frames\":09"),             // leading zero
        replaced("\"frames\":9", "\"frames\":-9"),
        replaced("\"frames\":9", "\"frames\":9.0"),
        replaced("\"frames\":9", "\"frames\":1e3"),
        replaced("\"frames\":9", "\"frames\":18446744073709551616"), // overflow
        replaced("\"frames\":9", "\"frames\":\"9\""),
        replaced("\"frames\":9,", "\"frames\":9,\"extra\":1,"),    // unknown key
        replaced("\"frames\":9,", ""),                              // missing key
        replaced("\"started\":6,\"succeeded\":3", "\"succeeded\":3,\"started\":6"), // reordered
        replaced("\"totals\"", "\"Totals\""),
        replaced("{\"count\":3,", "{\"count\":4,"),               // buckets do not sum to count
        replaced("\"unsent\"", "\"unsent \""),
        replaced("[", "[ 1,"),                                     // 11 buckets
    };
    // A sum no observations could produce.
    bad.push_back(replaced("{\"count\":3,\"sum\":101,", "{\"count\":3,\"sum\":99999999999999,"));
    std::string huge(20000, ' ');
    bad.push_back(state + huge);
    for (const std::string &b : bad) {
        CKEY_CHECK(ckey_telemetry_import_state(b.c_str()) == CKEY_INVALID_ARGUMENT);
        CKEY_CHECK(export_state() == current);
    }
    CKEY_CHECK(ckey_telemetry_import_state(nullptr) == CKEY_INVALID_ARGUMENT);
    // Sanity: the replacements above targeted the intended text.
    CKEY_CHECK(state.find("\"frames\":9,") != std::string::npos);

    // Mode 0: import rejected, export is all zeros.
    CKEY_CHECK(ckey_telemetry_set_mode(0) == CKEY_OK);
    CKEY_CHECK(ckey_telemetry_import_state(state.c_str()) == CKEY_INVALID_ARGUMENT);
    CKEY_CHECK(ckey_telemetry_get_mode() == CKEY_TELEMETRY_OFF);
    CKEY_CHECK(is_zero(snap()));
    const std::string zero_state = export_state();
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(export_state() == zero_state);

    // Mode 2: totals imported, the unsent delta never.
    CKEY_CHECK(ckey_telemetry_set_mode(2) == CKEY_OK);
    CKEY_CHECK(ckey_telemetry_import_state(state.c_str()) == CKEY_OK);
    CKEY_CHECK(ckey_telemetry_get_mode() == CKEY_TELEMETRY_LOCAL);
    CKEY_CHECK(same(snap(), totals));
    CKEY_CHECK(ckey_telemetry_set_mode(1) == CKEY_OK);
    CKEY_CHECK(take().empty());
    CKEY_CHECK(!ckey_telemetry_should_send(UINT64_MAX / 2));
}

std::string zero_counters_json() {
    return "{\"sessions\":{\"started\":0,\"succeeded\":0,\"failed\":0},\"frames\":0,"
           "\"decoded\":{\"p0\":0,\"p0_1\":0,\"p1\":0,\"p2\":0,\"p3\":0},"
           "\"time_to_decode_ms\":{\"count\":0,\"sum\":0,\"buckets\":[0,0,0,0,0,0,0,0,0,0]},"
           "\"frame_ms\":{\"count\":0,\"sum\":0,\"buckets\":[0,0,0,0,0,0,0,0,0,0]}}";
}

void test_clamping() {
    ckey_telemetry_reset_for_testing();
    // Unsent: 25M frames and started; frame_ms with an average of 4,000,000 ms
    // so its sum hits 1e12 first; time_to_decode with 15M observations.
    const std::string unsent =
        "{\"sessions\":{\"started\":25000000,\"succeeded\":15000000,\"failed\":3},\"frames\":25000000,"
        "\"decoded\":{\"p0\":15000000,\"p0_1\":0,\"p1\":0,\"p2\":0,\"p3\":0},"
        "\"time_to_decode_ms\":{\"count\":15000000,\"sum\":150000000,\"buckets\":[5000000,10000000,0,0,0,0,0,0,0,0]},"
        "\"frame_ms\":{\"count\":1000000,\"sum\":4000000000000,\"buckets\":[0,0,0,0,0,0,0,0,0,1000000]}}";
    const std::string state = "{\"state\":1,\"totals\":" + unsent + ",\"unsent\":" + unsent + "}";
    CKEY_CHECK(ckey_telemetry_import_state(state.c_str()) == CKEY_OK);

    uint64_t frames = 0, started = 0, failed = 0, ttd_count = 0, ttd_sum = 0, fm_count = 0, fm_sum = 0;
    uint64_t ttd_b0 = 0, ttd_b1 = 0;
    int batches = 0;
    for (;;) {
        uint64_t id = 0;
        std::string b = take(&id); // take() validates every batch
        if (b.empty()) break;
        ++batches;
        Json j = parse(b);
        if (batches == 1) {
            CKEY_CHECK(field(j, "frames").num == 10000000);
            const Json &fm = field(j, "frame_ms");
            CKEY_CHECK(field(fm, "count").num == 250000 && field(fm, "sum").num == 1000000000000ull);
            const Json &ttd = field(j, "time_to_decode_ms");
            CKEY_CHECK(field(ttd, "count").num == 10000000);
            CKEY_CHECK(field(ttd, "buckets").arr[0].num == 3333334 && field(ttd, "buckets").arr[1].num == 6666666);
            CKEY_CHECK(field(ttd, "sum").num == 100000000);
        }
        frames += field(j, "frames").num;
        started += field(field(j, "sessions"), "started").num;
        failed += field(field(j, "sessions"), "failed").num;
        const Json &ttd = field(j, "time_to_decode_ms");
        ttd_count += field(ttd, "count").num;
        ttd_sum += field(ttd, "sum").num;
        ttd_b0 += field(ttd, "buckets").arr[0].num;
        ttd_b1 += field(ttd, "buckets").arr[1].num;
        const Json &fm = field(j, "frame_ms");
        fm_count += field(fm, "count").num;
        fm_sum += field(fm, "sum").num;
        CKEY_CHECK(ckey_telemetry_batch_result(id, 204, 0) == CKEY_OK);
        CKEY_CHECK(batches < 10);
    }
    CKEY_CHECK(batches == 4); // frame_ms needs 4 batches of 250,000
    CKEY_CHECK(frames == 25000000 && started == 25000000 && failed == 3);
    CKEY_CHECK(ttd_count == 15000000 && ttd_sum == 150000000 && ttd_b0 == 5000000 && ttd_b1 == 10000000);
    CKEY_CHECK(fm_count == 1000000 && fm_sum == 4000000000000ull);
    // Excess carried in the export while pending, and retained across a failed send.
    ckey_telemetry_reset_for_testing();
    CKEY_CHECK(ckey_telemetry_import_state(state.c_str()) == CKEY_OK);
    uint64_t id = 0;
    std::string first = take(&id);
    CKEY_CHECK(ckey_telemetry_batch_result(id, 500, 0) == CKEY_OK);
    CKEY_CHECK(take(&id) == first);
    CKEY_CHECK(ckey_telemetry_batch_result(id, 204, 0) == CKEY_OK);
    const std::string rest = export_state();
    CKEY_CHECK(rest.find("\"unsent\":{\"sessions\":{\"started\":15000000,") != std::string::npos);
    // Local totals saturate instead of wrapping.
    ckey_telemetry_reset_for_testing();
    const std::string max_state = "{\"state\":1,\"totals\":" + std::string(
        "{\"sessions\":{\"started\":18446744073709551615,\"succeeded\":0,\"failed\":0},\"frames\":18446744073709551615,"
        "\"decoded\":{\"p0\":0,\"p0_1\":0,\"p1\":0,\"p2\":0,\"p3\":0},"
        "\"time_to_decode_ms\":{\"count\":0,\"sum\":0,\"buckets\":[0,0,0,0,0,0,0,0,0,0]},"
        "\"frame_ms\":{\"count\":0,\"sum\":0,\"buckets\":[0,0,0,0,0,0,0,0,0,0]}}") +
        ",\"unsent\":" + zero_counters_json() + "}";
    CKEY_CHECK(ckey_telemetry_import_state(max_state.c_str()) == CKEY_OK);
    ckey_telemetry_frame(0, 1);
    CKEY_CHECK(ckey_telemetry_import_state(max_state.c_str()) == CKEY_OK);
    ckey_telemetry_counters c = snap();
    CKEY_CHECK(c.frames == UINT64_MAX && c.sessions_started == UINT64_MAX && c.frame_ms.count == 1);
    Json j = parse(take());
    CKEY_CHECK(field(j, "frames").num == 1);
}

void test_threads() {
    ckey_telemetry_reset_for_testing();
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t] {
            for (uint64_t i = 0; i < 5000; ++i) {
                ckey_telemetry_frame(i, static_cast<uint32_t>(i % 50));
                if (i % 100 == 0) ckey_telemetry_session_success(i, static_cast<uint32_t>(t));
                if (i % 777 == 0) {
                    uint64_t id = 0;
                    char buf[4096];
                    size_t len = 0;
                    if (ckey_telemetry_take_batch(kDay, buf, sizeof buf, &len, &id) == CKEY_OK && id)
                        ckey_telemetry_batch_result(id, (i % 2) ? 500 : 204, i);
                }
            }
        });
    }
    for (auto &th : threads) th.join();
    ckey_telemetry_counters c = snap();
    CKEY_CHECK(c.frames == 20000 && c.frame_ms.count == 20000);
    uint64_t t = 0;
    for (uint64_t b : c.frame_ms.buckets) t += b;
    CKEY_CHECK(t == 20000);
}

} // namespace

int main() {
    // Self-check of the validator against the doc's own example.
    const std::string doc_example =
        "{\"schema\":1,\"sdk\":\"swift\",\"sdk_version\":\"0.1.0-build.61\",\"platform\":\"ios\",\"day\":\"2026-11-05\","
        "\"sessions\":{\"started\":12,\"succeeded\":10,\"failed\":2},\"frames\":3120,"
        "\"decoded\":{\"p0\":7,\"p0_1\":1,\"p1\":1,\"p2\":1,\"p3\":0},"
        "\"time_to_decode_ms\":{\"count\":10,\"sum\":4210,\"buckets\":[0,1,3,4,1,1,0,0,0,0]},"
        "\"frame_ms\":{\"count\":3120,\"sum\":40110,\"buckets\":[10,400,2000,600,100,10,0,0,0,0]}}";
    CKEY_CHECK(batch_valid(doc_example));
    CKEY_CHECK(batch_valid(kScenarioJson));
    std::string broken = doc_example;
    broken.replace(broken.find("\"count\":10,"), 11, "\"count\":11,");
    CKEY_CHECK(!batch_valid(broken)); // bucket sum mismatch
    CKEY_CHECK(!batch_valid(doc_example.substr(0, doc_example.size() - 1) + ",\"x\":1}")); // unknown key

    test_defaults_and_modes();
    test_default_client_json();
    test_mode_off();
    test_on_to_local();
    test_local_to_on_fresh_delta();
    test_sessions();
    test_histogram_edges();
    test_exact_json_and_buffer();
    test_set_client();
    test_batch_results();
    test_should_send();
    test_persistence();
    test_clamping();
    test_threads();
    ckey_telemetry_reset_for_testing();
    std::puts("telemetry tests passed");
    return 0;
}
