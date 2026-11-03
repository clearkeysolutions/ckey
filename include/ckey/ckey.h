#ifndef CKEY_CKEY_H
#define CKEY_CKEY_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(CKEY_SHARED)
#  ifdef CKEY_BUILDING
#    define CKEY_API __declspec(dllexport)
#  else
#    define CKEY_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(CKEY_SHARED)
#  define CKEY_API __attribute__((visibility("default")))
#else
#  define CKEY_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CKEY_ABI_VERSION 2u
#define CKEY_SOFTWARE_VERSION "0.1.0-dev.40"
#define CKEY_FORMAT_VERSION 14u
#define CKEY_TEXT_FORMAT_VERSION 15u
#define CKEY_LOCATOR_PAYLOAD_BYTES 16u
#define CKEY_TEXT_PAYLOAD_MAX_BYTES 200u
#define CKEY_TEXT_CODEWORD_BYTES 255u
#define CKEY_EXTENDED_FORMAT_VERSION 16u
#define CKEY_EXTENDED_PAYLOAD_MAX_BYTES 2048u
#define CKEY_EXTENDED_BLOCK_COUNT 10u
#define CKEY_EXTENDED_DATA_BYTES_PER_BLOCK 223u
#define CKEY_EXTENDED_PARITY_BYTES_PER_BLOCK 32u
#define CKEY_EXTENDED_CODEWORD_BYTES_PER_BLOCK 255u
#define CKEY_EXTENDED_CODEWORD_BYTES (CKEY_EXTENDED_BLOCK_COUNT * CKEY_EXTENDED_CODEWORD_BYTES_PER_BLOCK)
#define CKEY_LOCATOR_SIDE 29u
#define CKEY_MAX_RENDER_SCALE 256u
#define CKEY_MAX_SYMBOL_MODULES (CKEY_LOCATOR_SIDE * CKEY_LOCATOR_SIDE)

typedef enum ckey_status {
    CKEY_OK = 0,
    CKEY_INVALID_ARGUMENT = 1,
    CKEY_UNSUPPORTED_PROFILE = 2,
    CKEY_INSUFFICIENT_BUFFER = 3,
    CKEY_INTERNAL_ERROR = 4,
    CKEY_UNCORRECTABLE = 5,
    CKEY_INTEGRITY_FAILURE = 6
} ckey_status;

typedef struct ckey_symbol {
    uint32_t abi_version;
    uint32_t side;
    /* Systematic RS codeword for the locator profile; geometry is derived. */
    uint8_t codeword[77];
} ckey_symbol;

/* Profile 2: a high-capacity text/binary symbol. Its 255-byte codeword is a
 * full-length RS(255,210) block and carries up to 200 payload bytes. */
typedef struct ckey_text_symbol {
    uint32_t abi_version;
    uint32_t side;
    uint32_t payload_length;
    uint8_t codeword[CKEY_TEXT_CODEWORD_BYTES];
} ckey_text_symbol;

/* Profile 3: ten interleaved systematic RS(255,223) blocks.  The public
 * codeword order is byte-interleaved by block so a local image defect is
 * distributed across all blocks rather than concentrated in one. */
typedef struct ckey_extended_symbol {
    uint32_t abi_version;
    uint32_t side;
    uint32_t payload_length;
    uint8_t codeword[CKEY_EXTENDED_CODEWORD_BYTES];
} ckey_extended_symbol;

typedef struct ckey_diagnostics {
    uint32_t abi_version;
    ckey_status status;
    uint32_t corrected_symbols;
    uint32_t erasures_used;
    char message[96];
} ckey_diagnostics;

CKEY_API uint32_t ckey_get_abi_version(void);
CKEY_API const char *ckey_get_software_version(void);
CKEY_API ckey_status ckey_encode_locator(const uint8_t *payload, size_t payload_len,
                                          ckey_symbol *out_symbol,
                                          ckey_diagnostics *out_diagnostics);
CKEY_API ckey_status ckey_decode_locator(const uint8_t *codeword, size_t codeword_len,
                                          uint8_t *out_payload, size_t out_capacity,
                                          ckey_diagnostics *out_diagnostics);
CKEY_API ckey_status ckey_encode_text(const uint8_t *payload, size_t payload_len,
                                       ckey_text_symbol *out_symbol,
                                       ckey_diagnostics *out_diagnostics);
CKEY_API ckey_status ckey_decode_text(const uint8_t *codeword, size_t codeword_len,
                                       uint8_t *out_payload, size_t out_capacity,
                                       size_t *out_payload_len,
                                       ckey_diagnostics *out_diagnostics);
CKEY_API ckey_status ckey_encode_extended(const uint8_t *payload, size_t payload_len,
                                           ckey_extended_symbol *out_symbol,
                                           ckey_diagnostics *out_diagnostics);
CKEY_API ckey_status ckey_decode_extended(const uint8_t *codeword, size_t codeword_len,
                                           uint8_t *out_payload, size_t out_capacity,
                                           size_t *out_payload_len,
                                           ckey_diagnostics *out_diagnostics);
/*
 * Decode an 8-bit grayscale image of one planar CKey locator. quad_xy contains
 * four perimeter points (x0,y0,...,x3,y3) in cyclic order; either winding and
 * any starting corner are accepted. They map to canonical (0,0), (29,0),
 * (29,29), (0,29) coordinates. The caller supplies the detected symbol
 * quadrilateral. This function rectifies perspective but does not locate a
 * symbol in a full frame or correct lens distortion. The image is one-channel
 * 8-bit grayscale; both contrast polarities are supported.
 */
CKEY_API ckey_status ckey_decode_gray8(const uint8_t *gray, size_t width,
                                        size_t height, size_t stride,
                                        const double quad_xy[8],
                                        uint8_t *out_payload,
                                        size_t out_capacity,
                                        ckey_diagnostics *out_diagnostics);
/* Projective grayscale decoders for the two dense profiles. These use the
 * same caller-supplied quadrilateral contract as ckey_decode_gray8. They are
 * intended for high-resolution captures where the documented feature floor is
 * met; payload acceptance is still gated by RS correction and CRC-32C. */
CKEY_API ckey_status ckey_decode_text_gray8(const uint8_t *gray, size_t width,
                                             size_t height, size_t stride,
                                             const double quad_xy[8],
                                             uint8_t *out_payload,
                                             size_t out_capacity,
                                             size_t *out_payload_len,
                                             ckey_diagnostics *out_diagnostics);
CKEY_API ckey_status ckey_decode_extended_gray8(const uint8_t *gray, size_t width,
                                                 size_t height, size_t stride,
                                                 const double quad_xy[8],
                                                 uint8_t *out_payload,
                                                 size_t out_capacity,
                                                 size_t *out_payload_len,
                                                 ckey_diagnostics *out_diagnostics);
/*
 * Returns a continuous positive fit score for the Grid, Ring, and Ray Keys
 * under a proposed quadrilateral. Zero means no usable calibration evidence;
 * a negative value means invalid arguments. This never accepts a payload and
 * is intended for callers that refine localization before decoding.
 */
CKEY_API double ckey_calibration_score_gray8(const uint8_t *gray, size_t width,
                                              size_t height, size_t stride,
                                              const double quad_xy[8]);
/* scale is pixels per module and is constrained to 1..CKEY_MAX_RENDER_SCALE. */
CKEY_API ckey_status ckey_render_svg(const ckey_symbol *symbol, uint32_t scale,
                                      char *out_utf8, size_t out_capacity,
                                      size_t *out_required);
CKEY_API ckey_status ckey_render_text_svg(const ckey_text_symbol *symbol, uint32_t scale,
                                           char *out_utf8, size_t out_capacity,
                                           size_t *out_required);
CKEY_API ckey_status ckey_render_extended_svg(const ckey_extended_symbol *symbol, uint32_t scale,
                                               char *out_utf8, size_t out_capacity,
                                               size_t *out_required);

#ifdef __cplusplus
}
#endif
#endif
