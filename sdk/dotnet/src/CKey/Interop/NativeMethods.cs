// P/Invoke declarations for include/ckey/ckey.h and include/ckey/telemetry.h.
// Every parameter is blittable (pointers, fixed-width integers, double,
// nuint for size_t), so [LibraryImport] (net8.0) generates no marshalling
// code and the [DllImport] fallback (netstandard2.0) marshals nothing either.
// C enums are passed as int; ckey_status is returned as int.
// Each function is declared twice (#if) with an identical signature; keep
// the two halves in sync when the C ABI changes.
using System.Runtime.InteropServices;

namespace OpenCKey.Interop
{
    internal static class NativeLibraryName
    {
        /// <summary>ckeynative.dll / libckeynative.so / libckeynative.dylib (sdk/dotnet/native).</summary>
        internal const string Name = "ckeynative";
    }

    /// <summary>Codec, scan, render and certify functions of the ckey native library.</summary>
    internal static unsafe partial class NativeMethods
    {
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_safe_action_parse(byte* payload, nuint length, NativeSafeAction* output, NativeDiagnostics* diagnostics);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_safe_action_parse(byte* payload, nuint length, NativeSafeAction* output, NativeDiagnostics* diagnostics);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_safe_action_verify(byte* payload, nuint length, byte* publicKey, ulong now, uint maxLifetime, NativeSafeAction* output, NativeDiagnostics* diagnostics);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_safe_action_verify(byte* payload, nuint length, byte* publicKey, ulong now, uint maxLifetime, NativeSafeAction* output, NativeDiagnostics* diagnostics);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial uint ckey_get_abi_version();
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern uint ckey_get_abi_version();
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial byte* ckey_get_software_version();
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern byte* ckey_get_software_version();
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_decode_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, byte* outPayload, nuint outCapacity, NativeDiagnostics* diagnostics);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_decode_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, byte* outPayload, nuint outCapacity, NativeDiagnostics* diagnostics);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_decode_text_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, byte* outPayload, nuint outCapacity, nuint* outLength, NativeDiagnostics* diagnostics);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_decode_text_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, byte* outPayload, nuint outCapacity, nuint* outLength, NativeDiagnostics* diagnostics);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_decode_extended_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, byte* outPayload, nuint outCapacity, nuint* outLength, NativeDiagnostics* diagnostics);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_decode_extended_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, byte* outPayload, nuint outCapacity, nuint* outLength, NativeDiagnostics* diagnostics);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial nuint ckey_find_borders_gray8(byte* gray, nuint width, nuint height, nuint stride, double* outQuads, nuint maxQuads);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern nuint ckey_find_borders_gray8(byte* gray, nuint width, nuint height, nuint stride, double* outQuads, nuint maxQuads);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial double ckey_refine_quad_gray8(byte* gray, nuint width, nuint height, nuint stride, double* borderQuad, int field, double* outQuad);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern double ckey_refine_quad_gray8(byte* gray, nuint width, nuint height, nuint stride, double* borderQuad, int field, double* outQuad);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial uint ckey_scalable_modules_across(uint version);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern uint ckey_scalable_modules_across(uint version);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial nuint ckey_scalable_capacity(uint version, int level, int layout);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern nuint ckey_scalable_capacity(uint version, int level, int layout);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_encode_scalable(byte* payload, nuint payloadLength, uint version, int level, int layout, NativeScalableSymbol* outSymbol, NativeDiagnostics* diagnostics);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_encode_scalable(byte* payload, nuint payloadLength, uint version, int level, int layout, NativeScalableSymbol* outSymbol, NativeDiagnostics* diagnostics);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_decode_scalable_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, byte* outPayload, nuint outCapacity, nuint* outLength, uint* outVersion, uint* outLayout, NativeDiagnostics* diagnostics);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_decode_scalable_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, byte* outPayload, nuint outCapacity, nuint* outLength, uint* outVersion, uint* outLayout, NativeDiagnostics* diagnostics);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial double ckey_calibration_score_scalable_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, uint* outVersion);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern double ckey_calibration_score_scalable_gray8(byte* gray, nuint width, nuint height, nuint stride, double* quad, uint* outVersion);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial void ckey_style_default(NativeStyle* style);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void ckey_style_default(NativeStyle* style);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_style_preset_fill(NativeStyle* style, int preset);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_style_preset_fill(NativeStyle* style, int preset);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial double ckey_scalable_logo_max_radius(uint version, int layout);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern double ckey_scalable_logo_max_radius(uint version, int layout);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial double ckey_scalable_logo_load(NativeScalableSymbol* symbol, double logoRadius);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern double ckey_scalable_logo_load(NativeScalableSymbol* symbol, double logoRadius);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_check_scalable_style(NativeScalableSymbol* symbol, NativeStyle* style, NativeDiagnostics* diagnostics);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_check_scalable_style(NativeScalableSymbol* symbol, NativeStyle* style, NativeDiagnostics* diagnostics);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_render_scalable_styled_svg(NativeScalableSymbol* symbol, NativeStyle* style, uint scale, byte* outUtf8, nuint outCapacity, nuint* outRequired);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_render_scalable_styled_svg(NativeScalableSymbol* symbol, NativeStyle* style, uint scale, byte* outUtf8, nuint outCapacity, nuint* outRequired);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_render_scalable_print(NativeScalableSymbol* symbol, NativeStyle* style, double widthInches, double dpi, byte* outGray, nuint outCapacity, nuint* outSide);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_render_scalable_print(NativeScalableSymbol* symbol, NativeStyle* style, double widthInches, double dpi, byte* outGray, nuint outCapacity, nuint* outSide);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial void ckey_raster_options_default(NativeRasterOptions* options);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void ckey_raster_options_default(NativeRasterOptions* options);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_render_scalable_raster(NativeScalableSymbol* symbol, NativeStyle* style, NativeRasterOptions* options, byte* output, nuint outCapacity, nuint* outRequired);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_render_scalable_raster(NativeScalableSymbol* symbol, NativeStyle* style, NativeRasterOptions* options, byte* output, nuint outCapacity, nuint* outRequired);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_encode_png(byte* pixels, uint width, uint height, nuint stride, int format, double dpi, byte* output, nuint outCapacity, nuint* outRequired);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_encode_png(byte* pixels, uint width, uint height, nuint stride, int format, double dpi, byte* output, nuint outCapacity, nuint* outRequired);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_render_scalable_pdf(NativeScalableSymbol* symbol, NativeStyle* style, double widthPt, uint transparent, byte* output, nuint outCapacity, nuint* outRequired);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_render_scalable_pdf(NativeScalableSymbol* symbol, NativeStyle* style, double widthPt, uint transparent, byte* output, nuint outCapacity, nuint* outRequired);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial void ckey_scan_model_default(NativeScanModel* model);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void ckey_scan_model_default(NativeScanModel* model);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_certify_scalable_style(NativeScalableSymbol* symbol, NativeStyle* style, uint transparent, NativeBackdrop* backdrop, NativeScanModel* model, NativeStyle* outStyle, NativeCertification* output);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_certify_scalable_style(NativeScalableSymbol* symbol, NativeStyle* style, uint transparent, NativeBackdrop* backdrop, NativeScanModel* model, NativeStyle* outStyle, NativeCertification* output);
#endif
    }

    /// <summary>Telemetry functions; absent when the native library was built with CKEY_DOTNET_TELEMETRY=OFF.</summary>
    internal static unsafe partial class NativeTelemetry
    {
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_set_mode(int mode);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_set_mode(int mode);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_get_mode();
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_get_mode();
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_set_client(byte* sdk, byte* sdkVersion, byte* platform);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_set_client(byte* sdk, byte* sdkVersion, byte* platform);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial void ckey_telemetry_session_begin(ulong nowMs);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void ckey_telemetry_session_begin(ulong nowMs);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial void ckey_telemetry_frame(ulong nowMs, uint processingMs);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void ckey_telemetry_frame(ulong nowMs, uint processingMs);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_session_success(ulong nowMs, uint profileCode);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_session_success(ulong nowMs, uint profileCode);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial void ckey_telemetry_session_end(ulong nowMs);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void ckey_telemetry_session_end(ulong nowMs);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_snapshot(NativeTelemetryCounters* output);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_snapshot(NativeTelemetryCounters* output);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_should_send(ulong nowMs);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_should_send(ulong nowMs);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_take_batch(byte* utcDay, byte* outJson, nuint capacity, nuint* outLength, ulong* outBatchId);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_take_batch(byte* utcDay, byte* outJson, nuint capacity, nuint* outLength, ulong* outBatchId);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_batch_result(ulong batchId, int httpStatus, ulong nowMs);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_batch_result(ulong batchId, int httpStatus, ulong nowMs);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_export_state(byte* output, nuint capacity, nuint* length);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_export_state(byte* output, nuint capacity, nuint* length);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial int ckey_telemetry_import_state(byte* json);
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int ckey_telemetry_import_state(byte* json);
#endif
#if NET7_0_OR_GREATER
        [LibraryImport(NativeLibraryName.Name)]
        internal static partial void ckey_telemetry_reset_for_testing();
#else
        [DllImport(NativeLibraryName.Name, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void ckey_telemetry_reset_for_testing();
#endif
    }
}
