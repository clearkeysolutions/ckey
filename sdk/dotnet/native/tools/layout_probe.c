/* Prints the size and field offsets of every C struct that the CKey .NET
 * interop mirrors (src/CKey/Interop/NativeStructs.cs). Build it with the
 * native CMake project and diff the output against layout_expected.txt; the
 * xUnit test StructLayoutTests asserts the same numbers on the managed side. */
#include <stddef.h>
#include <stdio.h>

#include "ckey/ckey.h"
#include "ckey/telemetry.h"

#define S(T) printf("%s size %u\n", #T, (unsigned)sizeof(T))
#define O(T, f) printf("%s.%s %u\n", #T, #f, (unsigned)offsetof(T, f))

int main(void) {
    S(ckey_diagnostics);
    O(ckey_diagnostics, status); O(ckey_diagnostics, corrected_symbols);
    O(ckey_diagnostics, erasures_used); O(ckey_diagnostics, message);

    S(ckey_scalable_symbol);
    O(ckey_scalable_symbol, version); O(ckey_scalable_symbol, ecc_level); O(ckey_scalable_symbol, layout);
    O(ckey_scalable_symbol, payload_length); O(ckey_scalable_symbol, codeword_length);
    O(ckey_scalable_symbol, codeword);

    S(ckey_rgb);
    S(ckey_style);
    O(ckey_style, background); O(ckey_style, background_edge); O(ckey_style, ink); O(ckey_style, ink_shade);
    O(ckey_style, accents); O(ckey_style, accent_count); O(ckey_style, accent_percent);
    O(ckey_style, modules); O(ckey_style, rings); O(ckey_style, rays);
    O(ckey_style, bevel); O(ckey_style, shadow); O(ckey_style, texture);
    O(ckey_style, logo_href); O(ckey_style, logo_radius); O(ckey_style, logo_plate); O(ckey_style, policy);

    S(ckey_logo_image);
    O(ckey_logo_image, rgba); O(ckey_logo_image, width); O(ckey_logo_image, height); O(ckey_logo_image, stride);

    S(ckey_raster_options);
    O(ckey_raster_options, size_px); O(ckey_raster_options, policy); O(ckey_raster_options, format);
    O(ckey_raster_options, transparent); O(ckey_raster_options, supersample);
    O(ckey_raster_options, min_feature_px); O(ckey_raster_options, materials); O(ckey_raster_options, logo);

    S(ckey_backdrop);
    O(ckey_backdrop, colours); O(ckey_backdrop, colour_count); O(ckey_backdrop, rgba);
    O(ckey_backdrop, width); O(ckey_backdrop, height); O(ckey_backdrop, stride);

    S(ckey_scan_model);
    O(ckey_scan_model, px_per_module); O(ckey_scan_model, blur_px); O(ckey_scan_model, noise);
    O(ckey_scan_model, glare); O(ckey_scan_model, trials);

    S(ckey_certification);
    O(ckey_certification, certified); O(ckey_certification, policy); O(ckey_certification, adjustments);
    O(ckey_certification, trials); O(ckey_certification, trials_passed);
    O(ckey_certification, min_contrast); O(ckey_certification, feature_separation);
    O(ckey_certification, logo_load); O(ckey_certification, message);

    S(ckey_telemetry_histogram);
    O(ckey_telemetry_histogram, sum); O(ckey_telemetry_histogram, buckets);
    S(ckey_telemetry_counters);
    O(ckey_telemetry_counters, sessions_succeeded); O(ckey_telemetry_counters, sessions_failed);
    O(ckey_telemetry_counters, frames); O(ckey_telemetry_counters, decoded);
    O(ckey_telemetry_counters, time_to_decode_ms); O(ckey_telemetry_counters, frame_ms);
    S(ckey_safe_action);
    O(ckey_safe_action, scope); O(ckey_safe_action, issuer); O(ckey_safe_action, app); O(ckey_safe_action, action);
    O(ckey_safe_action, key_id); O(ckey_safe_action, request_id); O(ckey_safe_action, issued_at);
    O(ckey_safe_action, expires_at); O(ckey_safe_action, params); O(ckey_safe_action, params_len);
    return 0;
}
