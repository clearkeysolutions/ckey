package org.openckey.ckey

import java.nio.ByteBuffer

/**
 * JNI declarations for `libckey_jni.so` (src/main/cpp/ckey_jni.cpp).
 *
 * `@JvmStatic external` inside an `object` compiles to `static native` methods
 * of class `org.openckey.ckey.CKeyNative`, so the C symbols are
 * `Java_org_openckey_ckey_CKeyNative_<name>`. Keep the functions public (an
 * `internal` member would get a mangled JVM name). The consumer ProGuard rules
 * keep this class and its native methods unobfuscated.
 *
 * Not part of the public API: use [CKeySymbol], [CKeyDecoder] and [CKey].
 */
internal object CKeyNative {
    const val FEATURE_SCAN = 1
    const val FEATURE_RENDER = 2
    const val FEATURE_TELEMETRY = 4
    const val FEATURE_CERTIFY = 8

    /** Style marshalling sizes; see read_style() in ckey_jni.cpp. */
    const val STYLE_INTS = 15
    const val STYLE_DOUBLES = 4
    const val DECODE_INFO = 5
    const val SNAPSHOT_LONGS = 33

    /** Bitmask of the modules compiled into the native library. */
    val features: Int

    init {
        // Host-JVM tests (sdk/android/jni-host-test) load an absolute path.
        val explicit = System.getProperty("org.openckey.ckey.library")
        if (explicit.isNullOrEmpty()) System.loadLibrary("ckey_jni") else System.load(explicit)
        features = nativeFeatures()
    }

    fun has(feature: Int): Boolean = features and feature == feature

    fun require(feature: Int, what: String) {
        if (!has(feature)) {
            throw UnsupportedOperationException(
                "this CKey native library was built without $what (see the ckey.native.* Gradle properties)"
            )
        }
    }

    // ---- Library
    @JvmStatic external fun nativeFeatures(): Int
    @JvmStatic external fun nativeSoftwareVersion(): String
    @JvmStatic external fun nativeAbiVersion(): Int

    // ---- Codec (always present)
    @JvmStatic external fun nativeCapacity(version: Int, level: Int, layout: Int): Int
    @JvmStatic external fun nativeModulesAcross(version: Int): Int
    @JvmStatic external fun nativeEncode(payload: ByteArray, version: Int, level: Int, layout: Int): ByteArray
    @JvmStatic external fun nativeSymbolInfo(symbol: ByteArray): IntArray

    // ---- Safe Actions (always present; see CKeySafeActions)
    @JvmStatic external fun nativeSafeAction(
        payload: ByteArray, publicKey: ByteArray?, nowSeconds: Long, maxLifetime: Int, outStatus: IntArray,
    ): String

    // ---- Rendering (FEATURE_RENDER)
    @JvmStatic external fun nativeStylePreset(preset: Int, outInts: IntArray, outDoubles: DoubleArray)
    @JvmStatic external fun nativeCheckStyle(symbol: ByteArray, ints: IntArray?, doubles: DoubleArray?, logoHref: String?): String
    @JvmStatic external fun nativeRenderRaster(
        symbol: ByteArray, ints: IntArray?, doubles: DoubleArray?, logoHref: String?,
        sizePx: Int, policy: Int, gray: Boolean, transparent: Boolean, premultiply: Boolean,
        supersample: Int, minFeaturePx: Double, materials: Boolean,
        logoRgba: ByteArray?, logoWidth: Int, logoHeight: Int,
    ): ByteArray
    @JvmStatic external fun nativeRenderPng(
        symbol: ByteArray, ints: IntArray?, doubles: DoubleArray?, logoHref: String?,
        sizePx: Int, policy: Int, transparent: Boolean,
        supersample: Int, minFeaturePx: Double, materials: Boolean,
        logoRgba: ByteArray?, logoWidth: Int, logoHeight: Int, dpi: Double,
    ): ByteArray
    @JvmStatic external fun nativeRenderSvg(symbol: ByteArray, ints: IntArray?, doubles: DoubleArray?, logoHref: String?, scale: Int): String
    @JvmStatic external fun nativeRenderPdf(
        symbol: ByteArray, ints: IntArray?, doubles: DoubleArray?, logoHref: String?, widthPt: Double, transparent: Boolean,
    ): ByteArray

    // ---- Certification (FEATURE_CERTIFY)
    @JvmStatic external fun nativeCertify(
        symbol: ByteArray, ints: IntArray?, doubles: DoubleArray?, logoHref: String?, transparent: Boolean,
        backdropColors: IntArray?, backdropRgba: ByteArray?, backdropWidth: Int, backdropHeight: Int,
        model: DoubleArray?, outStyleInts: IntArray, outStyleDoubles: DoubleArray, outCert: DoubleArray,
    ): String

    // ---- Scanning (FEATURE_SCAN)
    @JvmStatic external fun nativeDecodeArray(
        gray: ByteArray, offset: Int, width: Int, height: Int, stride: Int, maxCandidates: Int,
        outInfo: IntArray, outQuad: DoubleArray,
    ): ByteArray?
    @JvmStatic external fun nativeDecodeBuffer(
        gray: ByteBuffer, offset: Int, width: Int, height: Int, stride: Int, maxCandidates: Int,
        outInfo: IntArray, outQuad: DoubleArray,
    ): ByteArray?

    // ---- Telemetry (FEATURE_TELEMETRY)
    @JvmStatic external fun nativeTelemetrySetMode(mode: Int): Int
    @JvmStatic external fun nativeTelemetryGetMode(): Int
    @JvmStatic external fun nativeTelemetrySetClient(sdk: String, version: String, platform: String): Int
    @JvmStatic external fun nativeTelemetrySessionBegin(nowMs: Long)
    @JvmStatic external fun nativeTelemetryFrame(nowMs: Long, processingMs: Int)
    @JvmStatic external fun nativeTelemetrySessionSuccess(nowMs: Long, profile: Int): Int
    @JvmStatic external fun nativeTelemetrySessionEnd(nowMs: Long)
    @JvmStatic external fun nativeTelemetryShouldSend(nowMs: Long): Boolean
    @JvmStatic external fun nativeTelemetryTakeBatch(utcDay: String, outId: LongArray): String?
    @JvmStatic external fun nativeTelemetryBatchResult(batchId: Long, httpStatus: Int, nowMs: Long): Int
    @JvmStatic external fun nativeTelemetrySnapshot(out: LongArray): Int
    @JvmStatic external fun nativeTelemetryExportState(): String
    @JvmStatic external fun nativeTelemetryImportState(json: String): Int
    @JvmStatic external fun nativeTelemetryEndpoint(): String
    @JvmStatic external fun nativeTelemetryResetForTesting()
}
