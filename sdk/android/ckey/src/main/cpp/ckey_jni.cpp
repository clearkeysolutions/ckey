// JNI bridge for the CKey Android SDK (org.openckey:ckey).
//
// A thin layer over the C ABI in include/ckey/ckey.h and include/ckey/telemetry.h.
// It never reimplements encoding, decoding or rendering: it converts Java
// arrays to C buffers, calls the core, and converts the results back.
//
// The Kotlin side is org.openckey.ckey.CKeyNative (an `object` whose
// @JvmStatic external funs compile to static native methods). Every function
// here must match one of those declarations; sdk/android/jni-host-test checks
// the signatures against javac -h output and runs them on a desktop JVM.
//
// Build switches (set by src/main/cpp/CMakeLists.txt from Gradle properties):
//   CKEY_JNI_SCAN       grayscale localization + decoding (ckey_scan)
//   CKEY_JNI_RENDER     styles, raster, PNG, SVG, PDF (ckey_render)
//   CKEY_JNI_TELEMETRY  aggregate counters (ckey_telemetry)
//   CKEY_JNI_CERTIFY    optical certification (needs scan + render)
// The codec (encode, capacities) is always present. A function whose module is
// compiled out is simply absent; the Kotlin wrapper checks nativeFeatures()
// first and reports UnsupportedOperationException instead of linking errors.
//
// Symbols are passed between Java and C as an opaque byte[] that holds a
// ckey_scalable_symbol verbatim. That avoids native handles (no leaks, no
// finalizers); every incoming blob is size- and ABI-checked before use.

#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

#ifndef CKEY_JNI_SCAN
#define CKEY_JNI_SCAN 1
#endif
#ifndef CKEY_JNI_RENDER
#define CKEY_JNI_RENDER 1
#endif
#ifndef CKEY_JNI_TELEMETRY
#define CKEY_JNI_TELEMETRY 1
#endif
#ifndef CKEY_JNI_CERTIFY
#define CKEY_JNI_CERTIFY 0
#endif

#include "ckey/ckey.h"
#if CKEY_JNI_TELEMETRY
#include "ckey/telemetry.h"
#endif


#define CKEY_JNI_FN(name) JNIEXPORT JNICALL Java_org_openckey_ckey_CKeyNative_##name

namespace {

// Feature bits returned by nativeFeatures(); mirrored in CKeyNative.kt.
constexpr jint kFeatureScan = 1;
constexpr jint kFeatureRender = 2;
constexpr jint kFeatureTelemetry = 4;
constexpr jint kFeatureCertify = 8;

// Style marshalling: ints and doubles, in this order (mirrored in CKeyStyle.kt).
//   ints:    0 background, 1 background_edge, 2 ink, 3 ink_shade (0xRRGGBB),
//            4..7 accents[0..3], 8 accent_count, 9 accent_percent,
//            10 modules, 11 rings, 12 rays, 13 logo_plate, 14 policy
//   doubles: 0 bevel, 1 shadow, 2 texture, 3 logo_radius
constexpr jsize kStyleInts = 15;
constexpr jsize kStyleDoubles = 4;

// Decode info (mirrored in CKeyDecoder.kt):
//   0 profile code (ckey_telemetry_profile numbering: 0 P0, 1 P0.1, 2 P1, 3 P2, 4 P3)
//   1 version (Profile 0 only, else 0), 2 layout (Profile 0 only, else 0),
//   3 border candidates found, 4 last core status
constexpr jsize kDecodeInfo = 5;
constexpr size_t kMaxCandidates = 16;
// Profile 0 payloads reach 4,619 bytes (v31 at level M); like the iOS reader,
// give the decoder a 64 KB buffer so no valid symbol is refused for space.
constexpr size_t kScalableOutput = 65535;

jclass g_ckey_exception = nullptr;   // org.openckey.ckey.CKeyException
jmethodID g_ckey_exception_ctor = nullptr;  // (ILjava/lang/String;)V
jclass g_string = nullptr;
jmethodID g_string_from_bytes = nullptr;    // String(byte[], String)
jmethodID g_string_get_bytes = nullptr;     // byte[] getBytes(String)
jstring g_utf8 = nullptr;                   // "UTF-8"

// ---- Exceptions -------------------------------------------------------

void throw_class(JNIEnv *env, const char *cls, const char *message) {
    if (env->ExceptionCheck()) return;
    jclass c = env->FindClass(cls);
    if (c) env->ThrowNew(c, message);
}

jstring new_utf8_string(JNIEnv *env, const char *text, size_t length);

void throw_ckey(JNIEnv *env, int status, const char *message) {
    if (env->ExceptionCheck()) return;
    if (!g_ckey_exception || !g_ckey_exception_ctor) {
        throw_class(env, "java/lang/IllegalStateException", message);
        return;
    }
    const size_t len = message ? strnlen(message, 1024) : 0;
    jstring jmessage = new_utf8_string(env, message ? message : "", len);
    if (!jmessage) return;  // OutOfMemoryError pending
    jobject ex = env->NewObject(g_ckey_exception, g_ckey_exception_ctor, static_cast<jint>(status), jmessage);
    if (ex) env->Throw(static_cast<jthrowable>(ex));
}

void throw_ckey(JNIEnv *env, int status, const ckey_diagnostics &diag, const char *fallback) {
    char message[sizeof(diag.message) + 1];
    std::memcpy(message, diag.message, sizeof(diag.message));
    message[sizeof(diag.message)] = '\0';
    throw_ckey(env, status, message[0] ? message : fallback);
}

void throw_iae(JNIEnv *env, const char *message) { throw_class(env, "java/lang/IllegalArgumentException", message); }
void throw_oom(JNIEnv *env) { throw_class(env, "java/lang/OutOfMemoryError", "CKey native allocation failed"); }

// Run a body and convert any C++ exception into a Java exception, so nothing
// ever unwinds through the JNI boundary.
template <typename R, typename F>
R guarded(JNIEnv *env, R fallback, F &&body) {
    try {
        return body();
    } catch (const std::bad_alloc &) {
        throw_oom(env);
    } catch (...) {
        throw_class(env, "java/lang/IllegalStateException", "CKey native error");
    }
    return fallback;
}

// ---- Strings ------------------------------------------------------------
// NewStringUTF/GetStringUTFChars use *modified* UTF-8, which differs from
// standard UTF-8 for supplementary characters and NUL. Diagnostics, SVG and
// logo URLs are converted through java.lang.String's UTF-8 codec instead.

jstring new_utf8_string(JNIEnv *env, const char *text, size_t length) {
    if (length > static_cast<size_t>(std::numeric_limits<jsize>::max())) { throw_oom(env); return nullptr; }
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(length));
    if (!bytes) return nullptr;
    if (length) env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(length), reinterpret_cast<const jbyte *>(text));
    jstring s = static_cast<jstring>(env->NewObject(g_string, g_string_from_bytes, bytes, g_utf8));
    env->DeleteLocalRef(bytes);
    return s;
}

// Returns false with a Java exception pending on failure. A null jstring
// yields ok = true and present = false.
bool utf8_from_jstring(JNIEnv *env, jstring s, std::string &out, bool &present) {
    out.clear();
    present = s != nullptr;
    if (!s) return true;
    jbyteArray bytes = static_cast<jbyteArray>(env->CallObjectMethod(s, g_string_get_bytes, g_utf8));
    if (env->ExceptionCheck() || !bytes) return false;
    const jsize n = env->GetArrayLength(bytes);
    out.resize(static_cast<size_t>(n));
    if (n) env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte *>(&out[0]));
    env->DeleteLocalRef(bytes);
    if (out.find('\0') != std::string::npos) { throw_iae(env, "string contains NUL"); return false; }
    return true;
}

jbyteArray new_byte_array(JNIEnv *env, const uint8_t *data, size_t length) {
    if (length > static_cast<size_t>(std::numeric_limits<jsize>::max())) { throw_oom(env); return nullptr; }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(length));
    if (out && length) env->SetByteArrayRegion(out, 0, static_cast<jsize>(length), reinterpret_cast<const jbyte *>(data));
    return out;
}

// ---- Symbol blob --------------------------------------------------------

bool read_symbol(JNIEnv *env, jbyteArray blob, ckey_scalable_symbol &symbol) {
    if (!blob) { throw_class(env, "java/lang/NullPointerException", "symbol"); return false; }
    if (env->GetArrayLength(blob) != static_cast<jsize>(sizeof(ckey_scalable_symbol))) {
        throw_iae(env, "not a CKey symbol (size mismatch)");
        return false;
    }
    env->GetByteArrayRegion(blob, 0, static_cast<jsize>(sizeof symbol), reinterpret_cast<jbyte *>(&symbol));
    if (symbol.abi_version != CKEY_ABI_VERSION || symbol.version < 1 || symbol.version > CKEY_SCALABLE_VERSION_COUNT ||
        symbol.ecc_level > CKEY_ECC_H || symbol.codeword_length > CKEY_SCALABLE_MAX_CODEWORD_BYTES) {
        throw_iae(env, "not a CKey symbol (header mismatch)");
        return false;
    }
    return true;
}

#if CKEY_JNI_RENDER
// ---- Style --------------------------------------------------------------

ckey_rgb rgb_from_int(jint v) {
    const uint32_t u = static_cast<uint32_t>(v);
    return ckey_rgb{static_cast<uint8_t>((u >> 16) & 0xff), static_cast<uint8_t>((u >> 8) & 0xff), static_cast<uint8_t>(u & 0xff)};
}
jint int_from_rgb(ckey_rgb c) {
    return static_cast<jint>((static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b);
}

// A style read from Java, keeping its logo URL alive for the duration of a call.
struct StyleArg {
    ckey_style style{};
    std::string logo;
    bool present = false;  // false: use NULL (the core's default style)
    const ckey_style *get() const { return present ? &style : nullptr; }
};

bool read_style(JNIEnv *env, jintArray ints, jdoubleArray doubles, jstring logo_href, StyleArg &out) {
    ckey_style_default(&out.style);  // never hard-code fields the ABI may add
    out.present = false;
    if (!ints && !doubles && !logo_href) return true;
    if (!ints || !doubles || env->GetArrayLength(ints) != kStyleInts || env->GetArrayLength(doubles) != kStyleDoubles) {
        throw_iae(env, "style arrays have the wrong shape");
        return false;
    }
    jint i[kStyleInts];
    jdouble d[kStyleDoubles];
    env->GetIntArrayRegion(ints, 0, kStyleInts, i);
    env->GetDoubleArrayRegion(doubles, 0, kStyleDoubles, d);
    ckey_style &s = out.style;
    s.background = rgb_from_int(i[0]);
    s.background_edge = rgb_from_int(i[1]);
    s.ink = rgb_from_int(i[2]);
    s.ink_shade = rgb_from_int(i[3]);
    for (int k = 0; k < 4; ++k) s.accents[k] = rgb_from_int(i[4 + k]);
    // Range checks are the core's job (ckey_check_scalable_style); negative
    // values are mapped to an out-of-range value so the core refuses them.
    auto u32 = [](jint v) { return v < 0 ? 0xffffffffu : static_cast<uint32_t>(v); };
    s.accent_count = u32(i[8]);
    s.accent_percent = u32(i[9]);
    s.modules = static_cast<ckey_module_shape>(u32(i[10]));
    s.rings = static_cast<ckey_ring_style>(u32(i[11]));
    s.rays = static_cast<ckey_ray_cap>(u32(i[12]));
    s.logo_plate = u32(i[13]);
    s.policy = u32(i[14]);
    s.bevel = d[0];
    s.shadow = d[1];
    s.texture = d[2];
    s.logo_radius = d[3];
    bool has_logo = false;
    if (!utf8_from_jstring(env, logo_href, out.logo, has_logo)) return false;
    s.logo_href = has_logo && !out.logo.empty() ? out.logo.c_str() : nullptr;
    out.present = true;
    return true;
}

void write_style(JNIEnv *env, const ckey_style &s, jintArray ints, jdoubleArray doubles) {
    jint i[kStyleInts];
    jdouble d[kStyleDoubles];
    i[0] = int_from_rgb(s.background);
    i[1] = int_from_rgb(s.background_edge);
    i[2] = int_from_rgb(s.ink);
    i[3] = int_from_rgb(s.ink_shade);
    for (int k = 0; k < 4; ++k) i[4 + k] = int_from_rgb(s.accents[k]);
    i[8] = static_cast<jint>(s.accent_count);
    i[9] = static_cast<jint>(s.accent_percent);
    i[10] = static_cast<jint>(s.modules);
    i[11] = static_cast<jint>(s.rings);
    i[12] = static_cast<jint>(s.rays);
    i[13] = static_cast<jint>(s.logo_plate);
    i[14] = static_cast<jint>(s.policy);
    d[0] = s.bevel;
    d[1] = s.shadow;
    d[2] = s.texture;
    d[3] = s.logo_radius;
    env->SetIntArrayRegion(ints, 0, kStyleInts, i);
    env->SetDoubleArrayRegion(doubles, 0, kStyleDoubles, d);
}

bool check_style_shape(JNIEnv *env, jintArray ints, jdoubleArray doubles) {
    if (!ints || !doubles || env->GetArrayLength(ints) != kStyleInts || env->GetArrayLength(doubles) != kStyleDoubles) {
        throw_iae(env, "style arrays have the wrong shape");
        return false;
    }
    return true;
}

// Raster options + optional logo bitmap (straight RGBA8, tightly packed).
struct RasterArg {
    ckey_raster_options options{};
    ckey_logo_image logo{};
    std::vector<uint8_t> logo_pixels;
};

bool read_raster(JNIEnv *env, jint size_px, jint policy, bool gray, jboolean transparent, jint supersample,
                 jdouble min_feature_px, jboolean materials, jbyteArray logo_rgba, jint logo_w, jint logo_h,
                 RasterArg &out) {
    ckey_raster_options_default(&out.options);  // sets abi_version = CKEY_ABI_VERSION
    if (size_px <= 0 || policy < 0 || supersample <= 0) { throw_iae(env, "invalid raster options"); return false; }
    out.options.size_px = static_cast<uint32_t>(size_px);
    out.options.policy = static_cast<uint32_t>(policy);
    out.options.format = gray ? CKEY_PIXELS_GRAY8 : CKEY_PIXELS_RGBA8;
    out.options.transparent = transparent ? 1u : 0u;
    out.options.supersample = static_cast<uint32_t>(supersample);
    out.options.min_feature_px = min_feature_px;
    out.options.materials = materials ? 1u : 0u;
    out.options.logo = nullptr;
    if (logo_rgba) {
        if (logo_w <= 0 || logo_h <= 0 ||
            static_cast<int64_t>(env->GetArrayLength(logo_rgba)) != static_cast<int64_t>(logo_w) * logo_h * 4) {
            throw_iae(env, "logo pixels must be width * height * 4 bytes");
            return false;
        }
        out.logo_pixels.resize(static_cast<size_t>(logo_w) * static_cast<size_t>(logo_h) * 4u);
        env->GetByteArrayRegion(logo_rgba, 0, static_cast<jsize>(out.logo_pixels.size()),
                                reinterpret_cast<jbyte *>(out.logo_pixels.data()));
        out.logo.rgba = out.logo_pixels.data();
        out.logo.width = static_cast<uint32_t>(logo_w);
        out.logo.height = static_cast<uint32_t>(logo_h);
        out.logo.stride = 0;
        out.options.logo = &out.logo;
    }
    return true;
}

bool render_raster(JNIEnv *env, const ckey_scalable_symbol &symbol, const StyleArg &style, const RasterArg &raster,
                   std::vector<uint8_t> &pixels) {
    size_t required = 0;
    ckey_status st = ckey_render_scalable_raster(&symbol, style.get(), &raster.options, nullptr, 0, &required);
    if (st != CKEY_OK && st != CKEY_INSUFFICIENT_BUFFER) {
        throw_ckey(env, st, "render refused (style failed the safety check or invalid options)");
        return false;
    }
    pixels.resize(required);
    st = ckey_render_scalable_raster(&symbol, style.get(), &raster.options, pixels.data(), pixels.size(), &required);
    if (st != CKEY_OK) { throw_ckey(env, st, "render failed"); return false; }
    return true;
}

// Explain a refused style with the core's own diagnostic (the raster API has
// no diagnostics parameter).
void explain_style(JNIEnv *env, const ckey_scalable_symbol &symbol, const StyleArg &style, int status) {
    if (!style.present) { throw_ckey(env, status, "render failed"); return; }
    ckey_diagnostics diag{};
    diag.abi_version = CKEY_ABI_VERSION;
    const ckey_status st = ckey_check_scalable_style(&symbol, style.get(), &diag);
    if (st != CKEY_OK) throw_ckey(env, st, diag, "style refused");
    else throw_ckey(env, status, "render failed");
}
#endif  // CKEY_JNI_RENDER

#if CKEY_JNI_SCAN
// ---- Scanning -----------------------------------------------------------
// The pipeline of ios/CKeyReader/CameraScanner.swift: find border candidates
// (both polarities), then for each candidate, most border-like first, refine
// it to Profile 0's field and decode Profile 0 (identifies version and layout
// from the keys); then refine and decode Profiles 1, 2 and 3, each against its
// own field (never using one profile's fit for another).

struct Decoded {
    std::vector<uint8_t> bytes;
    int profile = -1;
    uint32_t version = 0, layout = 0;
    double quad[8] = {0};
    int last_status = CKEY_UNCORRECTABLE;
};

bool refine(const uint8_t *gray, size_t w, size_t h, size_t stride, const double *border, ckey_field field, double *out) {
    return ckey_refine_quad_gray8(gray, w, h, stride, border, field, out) >= 0.0;
}

size_t decode_frame(const uint8_t *gray, size_t w, size_t h, size_t stride, size_t max_candidates, Decoded &out) {
    double borders[kMaxCandidates * 8];
    const size_t count = std::min(ckey_find_borders_gray8(gray, w, h, stride, borders, max_candidates), max_candidates);
    std::vector<uint8_t> buffer(kScalableOutput);
    for (size_t c = 0; c < count; ++c) {
        const double *border = borders + c * 8;
        double quad[8];
        ckey_diagnostics diag{};
        diag.abi_version = CKEY_ABI_VERSION;
        size_t length = 0;

        if (refine(gray, w, h, stride, border, CKEY_FIELD_SCALABLE, quad)) {
            uint32_t version = 0, layout = 0;
            const ckey_status st = ckey_decode_scalable_gray8(gray, w, h, stride, quad, buffer.data(), buffer.size(),
                                                              &length, &version, &layout, &diag);
            out.last_status = st;
            if (st == CKEY_OK) {
                out.bytes.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(std::min(length, buffer.size())));
                out.profile = layout == CKEY_LAYOUT_PRINT ? 1 : 0;
                out.version = version;
                out.layout = layout;
                std::memcpy(out.quad, quad, sizeof quad);
                return count;
            }
        }
        if (refine(gray, w, h, stride, border, CKEY_FIELD_LOCATOR, quad)) {
            const ckey_status st = ckey_decode_gray8(gray, w, h, stride, quad, buffer.data(), CKEY_LOCATOR_PAYLOAD_BYTES, &diag);
            out.last_status = st;
            if (st == CKEY_OK) {
                out.bytes.assign(buffer.begin(), buffer.begin() + CKEY_LOCATOR_PAYLOAD_BYTES);
                out.profile = 2;
                std::memcpy(out.quad, quad, sizeof quad);
                return count;
            }
        }
        if (refine(gray, w, h, stride, border, CKEY_FIELD_TEXT, quad)) {
            length = 0;
            const ckey_status st = ckey_decode_text_gray8(gray, w, h, stride, quad, buffer.data(),
                                                          CKEY_EXTENDED_PAYLOAD_MAX_BYTES, &length, &diag);
            out.last_status = st;
            if (st == CKEY_OK) {
                out.bytes.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(length, CKEY_EXTENDED_PAYLOAD_MAX_BYTES)));
                out.profile = 3;
                std::memcpy(out.quad, quad, sizeof quad);
                return count;
            }
        }
        if (refine(gray, w, h, stride, border, CKEY_FIELD_EXTENDED, quad)) {
            length = 0;
            const ckey_status st = ckey_decode_extended_gray8(gray, w, h, stride, quad, buffer.data(),
                                                              CKEY_EXTENDED_PAYLOAD_MAX_BYTES, &length, &diag);
            out.last_status = st;
            if (st == CKEY_OK) {
                out.bytes.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(length, CKEY_EXTENDED_PAYLOAD_MAX_BYTES)));
                out.profile = 4;
                std::memcpy(out.quad, quad, sizeof quad);
                return count;
            }
        }
    }
    return count;
}

// Validates a frame description against the bytes available after offset.
bool check_frame(JNIEnv *env, int64_t available, jint offset, jint width, jint height, jint stride, jint max_candidates,
                 jintArray out_info, jdoubleArray out_quad) {
    if (width <= 0 || height <= 0 || stride < width || offset < 0 || max_candidates <= 0) {
        throw_iae(env, "invalid frame geometry (width, height > 0; stride >= width; offset >= 0)");
        return false;
    }
    const int64_t needed = static_cast<int64_t>(offset) + static_cast<int64_t>(height - 1) * stride + width;
    if (needed > available) { throw_iae(env, "frame buffer is smaller than offset + (height - 1) * stride + width"); return false; }
    if (!out_info || env->GetArrayLength(out_info) < kDecodeInfo || !out_quad || env->GetArrayLength(out_quad) < 8) {
        throw_iae(env, "decode output arrays are too small");
        return false;
    }
    return true;
}

jbyteArray finish_decode(JNIEnv *env, size_t candidates, const Decoded &d, jintArray out_info, jdoubleArray out_quad) {
    jint info[kDecodeInfo] = {d.profile, static_cast<jint>(d.version), static_cast<jint>(d.layout),
                              static_cast<jint>(candidates), d.last_status};
    env->SetIntArrayRegion(out_info, 0, kDecodeInfo, info);
    if (d.profile < 0) return nullptr;
    env->SetDoubleArrayRegion(out_quad, 0, 8, d.quad);
    return new_byte_array(env, d.bytes.data(), d.bytes.size());
}
#endif  // CKEY_JNI_SCAN

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass s = env->FindClass("java/lang/String");
    if (!s) return JNI_ERR;
    g_string = static_cast<jclass>(env->NewGlobalRef(s));
    g_string_from_bytes = env->GetMethodID(g_string, "<init>", "([BLjava/lang/String;)V");
    g_string_get_bytes = env->GetMethodID(g_string, "getBytes", "(Ljava/lang/String;)[B");
    jstring utf8 = env->NewStringUTF("UTF-8");
    if (!g_string_from_bytes || !g_string_get_bytes || !utf8) return JNI_ERR;
    g_utf8 = static_cast<jstring>(env->NewGlobalRef(utf8));
    jclass e = env->FindClass("org/openckey/ckey/CKeyException");
    if (e) {
        g_ckey_exception = static_cast<jclass>(env->NewGlobalRef(e));
        g_ckey_exception_ctor = env->GetMethodID(g_ckey_exception, "<init>", "(ILjava/lang/String;)V");
    }
    if (env->ExceptionCheck()) env->ExceptionClear();  // fall back to IllegalStateException
    return JNI_VERSION_1_6;
}

// ---- Library ------------------------------------------------------------

jint CKEY_JNI_FN(nativeFeatures)(JNIEnv *, jclass) {
    jint f = 0;
#if CKEY_JNI_SCAN
    f |= kFeatureScan;
#endif
#if CKEY_JNI_RENDER
    f |= kFeatureRender;
#endif
#if CKEY_JNI_TELEMETRY
    f |= kFeatureTelemetry;
#endif
#if CKEY_JNI_CERTIFY
    f |= kFeatureCertify;
#endif
    return f;
}

jstring CKEY_JNI_FN(nativeSoftwareVersion)(JNIEnv *env, jclass) {
    const char *v = ckey_get_software_version();
    return new_utf8_string(env, v ? v : "", v ? std::strlen(v) : 0);
}

jint CKEY_JNI_FN(nativeAbiVersion)(JNIEnv *, jclass) { return static_cast<jint>(ckey_get_abi_version()); }

// ---- Codec --------------------------------------------------------------

jint CKEY_JNI_FN(nativeCapacity)(JNIEnv *, jclass, jint version, jint level, jint layout) {
    if (version < 1 || level < 0 || layout < 0) return 0;
    const size_t c = ckey_scalable_capacity(static_cast<uint32_t>(version), static_cast<ckey_ecc_level>(level),
                                            static_cast<ckey_scalable_layout>(layout));
    return static_cast<jint>(std::min<size_t>(c, static_cast<size_t>(std::numeric_limits<jint>::max())));
}

jint CKEY_JNI_FN(nativeModulesAcross)(JNIEnv *, jclass, jint version) {
    return version < 1 ? 0 : static_cast<jint>(ckey_scalable_modules_across(static_cast<uint32_t>(version)));
}

jbyteArray CKEY_JNI_FN(nativeEncode)(JNIEnv *env, jclass, jbyteArray payload, jint version, jint level, jint layout) {
    return guarded<jbyteArray>(env, nullptr, [&]() -> jbyteArray {
        if (!payload) { throw_class(env, "java/lang/NullPointerException", "payload"); return nullptr; }
        if (version < 0 || level < 0 || layout < 0) { throw_iae(env, "invalid version, level or layout"); return nullptr; }
        const jsize n = env->GetArrayLength(payload);
        std::vector<uint8_t> bytes(static_cast<size_t>(n));
        if (n) env->GetByteArrayRegion(payload, 0, n, reinterpret_cast<jbyte *>(bytes.data()));
        auto symbol = std::make_unique<ckey_scalable_symbol>();
        std::memset(symbol.get(), 0, sizeof(ckey_scalable_symbol));
        ckey_diagnostics diag{};
        diag.abi_version = CKEY_ABI_VERSION;
        const ckey_status st = ckey_encode_scalable(bytes.data(), bytes.size(), static_cast<uint32_t>(version),
                                                    static_cast<ckey_ecc_level>(level),
                                                    static_cast<ckey_scalable_layout>(layout), symbol.get(), &diag);
        if (st != CKEY_OK) { throw_ckey(env, st, diag, "encode failed"); return nullptr; }
        return new_byte_array(env, reinterpret_cast<const uint8_t *>(symbol.get()), sizeof(ckey_scalable_symbol));
    });
}

// [version, ecc_level, layout, payload_length, codeword_length]
jintArray CKEY_JNI_FN(nativeSymbolInfo)(JNIEnv *env, jclass, jbyteArray blob) {
    return guarded<jintArray>(env, nullptr, [&]() -> jintArray {
        auto symbol = std::make_unique<ckey_scalable_symbol>();
        if (!read_symbol(env, blob, *symbol)) return nullptr;
        const jint info[5] = {static_cast<jint>(symbol->version), static_cast<jint>(symbol->ecc_level),
                              static_cast<jint>(symbol->layout), static_cast<jint>(symbol->payload_length),
                              static_cast<jint>(symbol->codeword_length)};
        jintArray out = env->NewIntArray(5);
        if (out) env->SetIntArrayRegion(out, 0, 5, info);
        return out;
    });
}

// ---- Safe Actions (spec/CKey-Safe-Actions.md) ---------------------------
// A refusal is an ordinary outcome here, not an exception: out_status[0]
// receives the core status and the returned string is the core's diagnostic.
// On CKEY_OK the string is the verified fields, one per line (every field is
// restricted ASCII, params are hex): scope, issuer, app, action, key_id,
// request_id, issued_at, expires_at, params. A null public key parses only;
// CKeySafeActions never treats a parsed-only envelope as verified.

jstring CKEY_JNI_FN(nativeSafeAction)(JNIEnv *env, jclass, jbyteArray payload, jbyteArray public_key, jlong now_seconds,
                                       jint max_lifetime, jintArray out_status) {
    return guarded<jstring>(env, nullptr, [&]() -> jstring {
        if (!payload || !out_status) { throw_class(env, "java/lang/NullPointerException", "payload"); return nullptr; }
        if (env->GetArrayLength(out_status) < 1) { throw_iae(env, "out_status needs one element"); return nullptr; }
        if (public_key && env->GetArrayLength(public_key) != 32) { throw_iae(env, "a public key is 32 bytes"); return nullptr; }
        if (now_seconds < 0 || max_lifetime < 1) { throw_iae(env, "invalid time or lifetime"); return nullptr; }
        const jsize n = env->GetArrayLength(payload);
        std::vector<uint8_t> bytes(static_cast<size_t>(n));
        if (n) env->GetByteArrayRegion(payload, 0, n, reinterpret_cast<jbyte *>(bytes.data()));
        uint8_t key[32];
        if (public_key) env->GetByteArrayRegion(public_key, 0, 32, reinterpret_cast<jbyte *>(key));
        auto sa = std::make_unique<ckey_safe_action>();
        std::memset(sa.get(), 0, sizeof(ckey_safe_action));
        ckey_diagnostics diag{};
        diag.abi_version = CKEY_ABI_VERSION;
        const ckey_status st = public_key
            ? ckey_safe_action_verify(bytes.data(), bytes.size(), key, static_cast<uint64_t>(now_seconds),
                                      static_cast<uint32_t>(max_lifetime), sa.get(), &diag)
            : ckey_safe_action_parse(bytes.data(), bytes.size(), sa.get(), &diag);
        const jint status = static_cast<jint>(st);
        env->SetIntArrayRegion(out_status, 0, 1, &status);
        if (st != CKEY_OK) {
            const size_t len = strnlen(diag.message, sizeof(diag.message));
            return new_utf8_string(env, diag.message, len);
        }
        std::string out;
        out.reserve(1024 + 2 * sa->params_len);
        auto line = [&](const std::string &v) { out += v; out += '\n'; };
        line(std::to_string(sa->scope));
        line(sa->issuer);
        line(sa->app);
        line(sa->action);
        line(sa->key_id);
        line(sa->request_id);
        line(std::to_string(sa->issued_at));
        line(std::to_string(sa->expires_at));
        static const char digits[] = "0123456789abcdef";
        for (size_t i = 0; i < sa->params_len && i < sizeof(sa->params); ++i) {
            out += digits[sa->params[i] >> 4];
            out += digits[sa->params[i] & 15];
        }
        return new_utf8_string(env, out.data(), out.size());
    });
}

// ---- Rendering ----------------------------------------------------------
#if CKEY_JNI_RENDER

void CKEY_JNI_FN(nativeStylePreset)(JNIEnv *env, jclass, jint preset, jintArray out_ints, jdoubleArray out_doubles) {
    if (!check_style_shape(env, out_ints, out_doubles)) return;
    ckey_style s;
    ckey_style_default(&s);
    const ckey_status st = ckey_style_preset_fill(&s, static_cast<ckey_style_preset>(preset));
    if (st != CKEY_OK) { throw_ckey(env, st, "unknown style preset"); return; }
    write_style(env, s, out_ints, out_doubles);
}

jstring CKEY_JNI_FN(nativeCheckStyle)(JNIEnv *env, jclass, jbyteArray blob, jintArray ints, jdoubleArray doubles,
                                      jstring logo_href) {
    return guarded<jstring>(env, nullptr, [&]() -> jstring {
        auto symbol = std::make_unique<ckey_scalable_symbol>();
        StyleArg style;
        if (!read_symbol(env, blob, *symbol) || !read_style(env, ints, doubles, logo_href, style)) return nullptr;
        ckey_style fallback;
        ckey_style_default(&fallback);
        ckey_diagnostics diag{};
        diag.abi_version = CKEY_ABI_VERSION;
        const ckey_status st = ckey_check_scalable_style(symbol.get(), style.present ? &style.style : &fallback, &diag);
        if (st != CKEY_OK) { throw_ckey(env, st, diag, "style refused"); return nullptr; }
        return new_utf8_string(env, diag.message, strnlen(diag.message, sizeof diag.message));
    });
}

// Straight RGBA8 (or GRAY8). premultiply = true converts RGBA to the
// premultiplied form Bitmap.copyPixelsFromBuffer expects for ARGB_8888.
jbyteArray CKEY_JNI_FN(nativeRenderRaster)(JNIEnv *env, jclass, jbyteArray blob, jintArray ints, jdoubleArray doubles,
                                           jstring logo_href, jint size_px, jint policy, jboolean gray,
                                           jboolean transparent, jboolean premultiply, jint supersample,
                                           jdouble min_feature_px, jboolean materials, jbyteArray logo_rgba,
                                           jint logo_w, jint logo_h) {
    return guarded<jbyteArray>(env, nullptr, [&]() -> jbyteArray {
        auto symbol = std::make_unique<ckey_scalable_symbol>();
        StyleArg style;
        RasterArg raster;
        if (!read_symbol(env, blob, *symbol) || !read_style(env, ints, doubles, logo_href, style) ||
            !read_raster(env, size_px, policy, gray, transparent, supersample, min_feature_px, materials, logo_rgba,
                         logo_w, logo_h, raster))
            return nullptr;
        std::vector<uint8_t> pixels;
        size_t required = 0;
        ckey_status st = ckey_render_scalable_raster(symbol.get(), style.get(), &raster.options, nullptr, 0, &required);
        if (st != CKEY_OK && st != CKEY_INSUFFICIENT_BUFFER) { explain_style(env, *symbol, style, st); return nullptr; }
        if (!render_raster(env, *symbol, style, raster, pixels)) return nullptr;
        if (premultiply && !gray) {
            for (size_t p = 0; p + 3 < pixels.size(); p += 4) {
                const uint32_t a = pixels[p + 3];
                if (a == 255) continue;
                for (int c = 0; c < 3; ++c) pixels[p + c] = static_cast<uint8_t>((pixels[p + c] * a + 127) / 255);
            }
        }
        return new_byte_array(env, pixels.data(), pixels.size());
    });
}

jbyteArray CKEY_JNI_FN(nativeRenderPng)(JNIEnv *env, jclass, jbyteArray blob, jintArray ints, jdoubleArray doubles,
                                        jstring logo_href, jint size_px, jint policy, jboolean transparent,
                                        jint supersample, jdouble min_feature_px, jboolean materials,
                                        jbyteArray logo_rgba, jint logo_w, jint logo_h, jdouble dpi) {
    return guarded<jbyteArray>(env, nullptr, [&]() -> jbyteArray {
        auto symbol = std::make_unique<ckey_scalable_symbol>();
        StyleArg style;
        RasterArg raster;
        if (!read_symbol(env, blob, *symbol) || !read_style(env, ints, doubles, logo_href, style) ||
            !read_raster(env, size_px, policy, false, transparent, supersample, min_feature_px, materials, logo_rgba,
                         logo_w, logo_h, raster))
            return nullptr;
        size_t required = 0;
        ckey_status st = ckey_render_scalable_raster(symbol.get(), style.get(), &raster.options, nullptr, 0, &required);
        if (st != CKEY_OK && st != CKEY_INSUFFICIENT_BUFFER) { explain_style(env, *symbol, style, st); return nullptr; }
        std::vector<uint8_t> pixels;
        if (!render_raster(env, *symbol, style, raster, pixels)) return nullptr;
        const uint32_t side = raster.options.size_px;
        size_t png_size = 0;
        st = ckey_encode_png(pixels.data(), side, side, static_cast<size_t>(side) * 4u, CKEY_PNG_RGBA8, dpi, nullptr, 0, &png_size);
        if (st != CKEY_OK && st != CKEY_INSUFFICIENT_BUFFER) { throw_ckey(env, st, "PNG encoding failed"); return nullptr; }
        std::vector<uint8_t> png(png_size);
        st = ckey_encode_png(pixels.data(), side, side, static_cast<size_t>(side) * 4u, CKEY_PNG_RGBA8, dpi, png.data(),
                             png.size(), &png_size);
        if (st != CKEY_OK) { throw_ckey(env, st, "PNG encoding failed"); return nullptr; }
        return new_byte_array(env, png.data(), std::min(png_size, png.size()));
    });
}

jstring CKEY_JNI_FN(nativeRenderSvg)(JNIEnv *env, jclass, jbyteArray blob, jintArray ints, jdoubleArray doubles,
                                     jstring logo_href, jint scale) {
    return guarded<jstring>(env, nullptr, [&]() -> jstring {
        auto symbol = std::make_unique<ckey_scalable_symbol>();
        StyleArg style;
        if (!read_symbol(env, blob, *symbol) || !read_style(env, ints, doubles, logo_href, style)) return nullptr;
        if (scale <= 0) { throw_iae(env, "scale must be positive"); return nullptr; }
        ckey_style fallback;
        ckey_style_default(&fallback);
        const ckey_style *st_ptr = style.present ? &style.style : &fallback;
        size_t required = 0;
        ckey_status st = ckey_render_scalable_styled_svg(symbol.get(), st_ptr, static_cast<uint32_t>(scale), nullptr, 0, &required);
        if (st != CKEY_OK && st != CKEY_INSUFFICIENT_BUFFER) { explain_style(env, *symbol, style, st); return nullptr; }
        std::vector<char> svg(required + 1);
        st = ckey_render_scalable_styled_svg(symbol.get(), st_ptr, static_cast<uint32_t>(scale), svg.data(), svg.size(), &required);
        if (st != CKEY_OK) { throw_ckey(env, st, "SVG rendering failed"); return nullptr; }
        return new_utf8_string(env, svg.data(), strnlen(svg.data(), svg.size()));
    });
}

jbyteArray CKEY_JNI_FN(nativeRenderPdf)(JNIEnv *env, jclass, jbyteArray blob, jintArray ints, jdoubleArray doubles,
                                        jstring logo_href, jdouble width_pt, jboolean transparent) {
    return guarded<jbyteArray>(env, nullptr, [&]() -> jbyteArray {
        auto symbol = std::make_unique<ckey_scalable_symbol>();
        StyleArg style;
        if (!read_symbol(env, blob, *symbol) || !read_style(env, ints, doubles, logo_href, style)) return nullptr;
        size_t required = 0;
        ckey_status st = ckey_render_scalable_pdf(symbol.get(), style.get(), width_pt, transparent ? 1u : 0u, nullptr, 0, &required);
        if (st != CKEY_OK && st != CKEY_INSUFFICIENT_BUFFER) { explain_style(env, *symbol, style, st); return nullptr; }
        std::vector<uint8_t> pdf(required);
        st = ckey_render_scalable_pdf(symbol.get(), style.get(), width_pt, transparent ? 1u : 0u, pdf.data(), pdf.size(), &required);
        if (st != CKEY_OK) { throw_ckey(env, st, "PDF rendering failed"); return nullptr; }
        return new_byte_array(env, pdf.data(), std::min(required, pdf.size()));
    });
}

#endif  // CKEY_JNI_RENDER

// ---- Certification (optional) ------------------------------------------
#if CKEY_JNI_CERTIFY && CKEY_JNI_RENDER && CKEY_JNI_SCAN
// model:    null for the default, else [px_per_module, blur_px, noise, glare, trials]
// out_cert: [status, certified, policy, adjustments, trials, trials_passed,
//            min_contrast, feature_separation, logo_load]
// Returns the certification message; never throws for a refusal (status says).
jstring CKEY_JNI_FN(nativeCertify)(JNIEnv *env, jclass, jbyteArray blob, jintArray ints, jdoubleArray doubles,
                                   jstring logo_href, jboolean transparent, jintArray backdrop_colors,
                                   jbyteArray backdrop_rgba, jint backdrop_w, jint backdrop_h, jdoubleArray model,
                                   jintArray out_style_ints, jdoubleArray out_style_doubles, jdoubleArray out_cert) {
    return guarded<jstring>(env, nullptr, [&]() -> jstring {
        auto symbol = std::make_unique<ckey_scalable_symbol>();
        StyleArg style;
        if (!read_symbol(env, blob, *symbol) || !read_style(env, ints, doubles, logo_href, style)) return nullptr;
        if (!check_style_shape(env, out_style_ints, out_style_doubles)) return nullptr;
        if (!out_cert || env->GetArrayLength(out_cert) < 9) { throw_iae(env, "certification output array too small"); return nullptr; }
        std::vector<ckey_rgb> colours;
        if (backdrop_colors) {
            const jsize n = env->GetArrayLength(backdrop_colors);
            if (n > 16) { throw_iae(env, "at most 16 backdrop colours"); return nullptr; }
            std::vector<jint> raw(static_cast<size_t>(n));
            if (n) env->GetIntArrayRegion(backdrop_colors, 0, n, raw.data());
            for (jint c : raw) colours.push_back(rgb_from_int(c));
        }
        std::vector<uint8_t> pixels;
        if (backdrop_rgba) {
            if (backdrop_w <= 0 || backdrop_h <= 0 ||
                static_cast<int64_t>(env->GetArrayLength(backdrop_rgba)) != static_cast<int64_t>(backdrop_w) * backdrop_h * 4) {
                throw_iae(env, "backdrop pixels must be width * height * 4 bytes");
                return nullptr;
            }
            pixels.resize(static_cast<size_t>(backdrop_w) * static_cast<size_t>(backdrop_h) * 4u);
            env->GetByteArrayRegion(backdrop_rgba, 0, static_cast<jsize>(pixels.size()), reinterpret_cast<jbyte *>(pixels.data()));
        }
        ckey_backdrop backdrop{};
        backdrop.colours = colours.empty() ? nullptr : colours.data();
        backdrop.colour_count = static_cast<uint32_t>(colours.size());
        backdrop.rgba = pixels.empty() ? nullptr : pixels.data();
        backdrop.width = pixels.empty() ? 0u : static_cast<uint32_t>(backdrop_w);
        backdrop.height = pixels.empty() ? 0u : static_cast<uint32_t>(backdrop_h);
        backdrop.stride = 0;
        const bool has_backdrop = !colours.empty() || !pixels.empty();
        ckey_scan_model scan;
        ckey_scan_model_default(&scan);
        if (model) {
            if (env->GetArrayLength(model) != 5) { throw_iae(env, "scan model needs 5 values"); return nullptr; }
            jdouble m[5];
            env->GetDoubleArrayRegion(model, 0, 5, m);
            scan.px_per_module = m[0];
            scan.blur_px = m[1];
            scan.noise = m[2];
            scan.glare = m[3];
            scan.trials = m[4] >= 0 && m[4] <= 1e6 ? static_cast<uint32_t>(m[4]) : 0u;
        }
        ckey_style effective;
        ckey_style_default(&effective);
        ckey_certification cert{};
        cert.abi_version = CKEY_ABI_VERSION;
        const ckey_status st = ckey_certify_scalable_style(symbol.get(), style.get(), transparent ? 1u : 0u,
                                                           has_backdrop ? &backdrop : nullptr, &scan, &effective, &cert);
        // The effective style may point at our logo string; Java gets the URL
        // back from its own input, so only the plain fields are copied.
        write_style(env, effective, out_style_ints, out_style_doubles);
        const jdouble result[9] = {static_cast<jdouble>(st), static_cast<jdouble>(cert.certified),
                                   static_cast<jdouble>(cert.policy), static_cast<jdouble>(cert.adjustments),
                                   static_cast<jdouble>(cert.trials), static_cast<jdouble>(cert.trials_passed),
                                   cert.min_contrast, cert.feature_separation, cert.logo_load};
        env->SetDoubleArrayRegion(out_cert, 0, 9, result);
        return new_utf8_string(env, cert.message, strnlen(cert.message, sizeof cert.message));
    });
}
#endif

// ---- Scanning -----------------------------------------------------------
#if CKEY_JNI_SCAN

jbyteArray CKEY_JNI_FN(nativeDecodeArray)(JNIEnv *env, jclass, jbyteArray gray, jint offset, jint width, jint height,
                                          jint stride, jint max_candidates, jintArray out_info, jdoubleArray out_quad) {
    return guarded<jbyteArray>(env, nullptr, [&]() -> jbyteArray {
        if (!gray) { throw_class(env, "java/lang/NullPointerException", "gray"); return nullptr; }
        if (!check_frame(env, env->GetArrayLength(gray), offset, width, height, stride, max_candidates, out_info, out_quad))
            return nullptr;
        // GetByteArrayElements (not a critical section): decoding can take tens
        // of milliseconds and must not stall the garbage collector.
        jbyte *data = env->GetByteArrayElements(gray, nullptr);
        if (!data) return nullptr;
        Decoded d;
        size_t candidates = 0;
        try {
            candidates = decode_frame(reinterpret_cast<const uint8_t *>(data) + offset, static_cast<size_t>(width),
                                      static_cast<size_t>(height), static_cast<size_t>(stride),
                                      std::min<size_t>(static_cast<size_t>(max_candidates), kMaxCandidates), d);
        } catch (...) {
            env->ReleaseByteArrayElements(gray, data, JNI_ABORT);
            throw;
        }
        env->ReleaseByteArrayElements(gray, data, JNI_ABORT);
        return finish_decode(env, candidates, d, out_info, out_quad);
    });
}

// A direct ByteBuffer (for example android.media.Image's Y plane): no copy.
jbyteArray CKEY_JNI_FN(nativeDecodeBuffer)(JNIEnv *env, jclass, jobject buffer, jint offset, jint width, jint height,
                                           jint stride, jint max_candidates, jintArray out_info, jdoubleArray out_quad) {
    return guarded<jbyteArray>(env, nullptr, [&]() -> jbyteArray {
        if (!buffer) { throw_class(env, "java/lang/NullPointerException", "buffer"); return nullptr; }
        auto *data = static_cast<const uint8_t *>(env->GetDirectBufferAddress(buffer));
        const jlong capacity = env->GetDirectBufferCapacity(buffer);
        if (!data || capacity < 0) { throw_iae(env, "decodeBuffer needs a direct ByteBuffer"); return nullptr; }
        if (!check_frame(env, capacity, offset, width, height, stride, max_candidates, out_info, out_quad)) return nullptr;
        Decoded d;
        const size_t candidates = decode_frame(data + offset, static_cast<size_t>(width), static_cast<size_t>(height),
                                               static_cast<size_t>(stride),
                                               std::min<size_t>(static_cast<size_t>(max_candidates), kMaxCandidates), d);
        return finish_decode(env, candidates, d, out_info, out_quad);
    });
}

#endif  // CKEY_JNI_SCAN

// ---- Telemetry ----------------------------------------------------------
#if CKEY_JNI_TELEMETRY

jint CKEY_JNI_FN(nativeTelemetrySetMode)(JNIEnv *, jclass, jint mode) { return ckey_telemetry_set_mode(mode); }
jint CKEY_JNI_FN(nativeTelemetryGetMode)(JNIEnv *, jclass) { return ckey_telemetry_get_mode(); }

jint CKEY_JNI_FN(nativeTelemetrySetClient)(JNIEnv *env, jclass, jstring sdk, jstring version, jstring platform) {
    return guarded<jint>(env, CKEY_INTERNAL_ERROR, [&]() -> jint {
        std::string s, v, p;
        bool hs = false, hv = false, hp = false;
        if (!utf8_from_jstring(env, sdk, s, hs) || !utf8_from_jstring(env, version, v, hv) ||
            !utf8_from_jstring(env, platform, p, hp))
            return CKEY_INVALID_ARGUMENT;
        if (!hs || !hv || !hp) return CKEY_INVALID_ARGUMENT;
        return ckey_telemetry_set_client(s.c_str(), v.c_str(), p.c_str());
    });
}

static uint64_t ms(jlong v) { return v < 0 ? 0u : static_cast<uint64_t>(v); }

void CKEY_JNI_FN(nativeTelemetrySessionBegin)(JNIEnv *, jclass, jlong now_ms) { ckey_telemetry_session_begin(ms(now_ms)); }

void CKEY_JNI_FN(nativeTelemetryFrame)(JNIEnv *, jclass, jlong now_ms, jint processing_ms) {
    ckey_telemetry_frame(ms(now_ms), processing_ms < 0 ? 0u : static_cast<uint32_t>(processing_ms));
}

jint CKEY_JNI_FN(nativeTelemetrySessionSuccess)(JNIEnv *, jclass, jlong now_ms, jint profile) {
    if (profile < 0) return CKEY_INVALID_ARGUMENT;
    return ckey_telemetry_session_success(ms(now_ms), static_cast<uint32_t>(profile));
}

void CKEY_JNI_FN(nativeTelemetrySessionEnd)(JNIEnv *, jclass, jlong now_ms) { ckey_telemetry_session_end(ms(now_ms)); }

jboolean CKEY_JNI_FN(nativeTelemetryShouldSend)(JNIEnv *, jclass, jlong now_ms) {
    return ckey_telemetry_should_send(ms(now_ms)) ? JNI_TRUE : JNI_FALSE;
}

// Returns the batch JSON and writes its id to out_id[0]; null when there is
// nothing to send.
jstring CKEY_JNI_FN(nativeTelemetryTakeBatch)(JNIEnv *env, jclass, jstring utc_day, jlongArray out_id) {
    return guarded<jstring>(env, nullptr, [&]() -> jstring {
        std::string day;
        bool has_day = false;
        if (!utf8_from_jstring(env, utc_day, day, has_day)) return nullptr;
        if (!has_day || !out_id || env->GetArrayLength(out_id) < 1) { throw_iae(env, "takeBatch needs a day and an id array"); return nullptr; }
        std::vector<char> json(CKEY_TELEMETRY_MAX_BODY_BYTES + 1);
        size_t len = 0;
        uint64_t id = 0;
        ckey_status st = ckey_telemetry_take_batch(day.c_str(), json.data(), json.size(), &len, &id);
        if (st == CKEY_INSUFFICIENT_BUFFER) {
            json.resize(len + 1);
            st = ckey_telemetry_take_batch(day.c_str(), json.data(), json.size(), &len, &id);
        }
        if (st != CKEY_OK) { throw_ckey(env, st, "telemetry batch refused (invalid day?)"); return nullptr; }
        if (len == 0 || id == 0) return nullptr;
        const jlong jid = static_cast<jlong>(id);
        env->SetLongArrayRegion(out_id, 0, 1, &jid);
        return new_utf8_string(env, json.data(), std::min(len, json.size()));
    });
}

jint CKEY_JNI_FN(nativeTelemetryBatchResult)(JNIEnv *, jclass, jlong batch_id, jint http_status, jlong now_ms) {
    return ckey_telemetry_batch_result(static_cast<uint64_t>(batch_id), http_status, ms(now_ms));
}

static jlong clamp64(uint64_t v) {
    return v > static_cast<uint64_t>(std::numeric_limits<jlong>::max()) ? std::numeric_limits<jlong>::max() : static_cast<jlong>(v);
}

// out (33 longs): started, succeeded, failed, frames, decoded[5],
// time_to_decode {count, sum, buckets[10]}, frame_ms {count, sum, buckets[10]}
jint CKEY_JNI_FN(nativeTelemetrySnapshot)(JNIEnv *env, jclass, jlongArray out) {
    constexpr jsize kLen = 4 + CKEY_TELEMETRY_PROFILES + 2 * (2 + CKEY_TELEMETRY_BUCKETS);
    if (!out || env->GetArrayLength(out) < kLen) { throw_iae(env, "snapshot array too small"); return CKEY_INVALID_ARGUMENT; }
    ckey_telemetry_counters c{};
    const ckey_status st = ckey_telemetry_snapshot(&c);
    if (st != CKEY_OK) return st;
    jlong v[kLen];
    jsize k = 0;
    v[k++] = clamp64(c.sessions_started);
    v[k++] = clamp64(c.sessions_succeeded);
    v[k++] = clamp64(c.sessions_failed);
    v[k++] = clamp64(c.frames);
    for (unsigned p = 0; p < CKEY_TELEMETRY_PROFILES; ++p) v[k++] = clamp64(c.decoded[p]);
    for (const ckey_telemetry_histogram *h : {&c.time_to_decode_ms, &c.frame_ms}) {
        v[k++] = clamp64(h->count);
        v[k++] = clamp64(h->sum);
        for (unsigned b = 0; b < CKEY_TELEMETRY_BUCKETS; ++b) v[k++] = clamp64(h->buckets[b]);
    }
    env->SetLongArrayRegion(out, 0, kLen, v);
    return CKEY_OK;
}

jstring CKEY_JNI_FN(nativeTelemetryExportState)(JNIEnv *env, jclass) {
    return guarded<jstring>(env, nullptr, [&]() -> jstring {
        size_t len = 0;
        std::vector<char> out(4096);
        ckey_status st = ckey_telemetry_export_state(out.data(), out.size(), &len);
        if (st == CKEY_INSUFFICIENT_BUFFER) {
            out.resize(len + 1);
            st = ckey_telemetry_export_state(out.data(), out.size(), &len);
        }
        if (st != CKEY_OK) { throw_ckey(env, st, "telemetry export failed"); return nullptr; }
        return new_utf8_string(env, out.data(), std::min(len, out.size()));
    });
}

jint CKEY_JNI_FN(nativeTelemetryImportState)(JNIEnv *env, jclass, jstring json) {
    return guarded<jint>(env, CKEY_INTERNAL_ERROR, [&]() -> jint {
        std::string s;
        bool has = false;
        if (!utf8_from_jstring(env, json, s, has)) return CKEY_INVALID_ARGUMENT;
        if (!has) return CKEY_INVALID_ARGUMENT;
        return ckey_telemetry_import_state(s.c_str());
    });
}

jstring CKEY_JNI_FN(nativeTelemetryEndpoint)(JNIEnv *env, jclass) {
    return new_utf8_string(env, CKEY_TELEMETRY_ENDPOINT, std::strlen(CKEY_TELEMETRY_ENDPOINT));
}

// TEST ONLY (instrumented and host tests): ckey_telemetry_reset_for_testing.
void CKEY_JNI_FN(nativeTelemetryResetForTesting)(JNIEnv *, jclass) { ckey_telemetry_reset_for_testing(); }

#endif  // CKEY_JNI_TELEMETRY

}  // extern "C"
