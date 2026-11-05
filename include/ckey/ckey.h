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

#define CKEY_ABI_VERSION 3u
#define CKEY_SOFTWARE_VERSION "0.1.0-dev.60"
#define CKEY_FORMAT_VERSION 14u
#define CKEY_TEXT_FORMAT_VERSION 25u
#define CKEY_LOCATOR_PAYLOAD_BYTES 16u
#define CKEY_TEXT_PAYLOAD_MAX_BYTES 200u
#define CKEY_TEXT_CODEWORD_BYTES 255u
#define CKEY_EXTENDED_FORMAT_VERSION 26u
#define CKEY_EXTENDED_PAYLOAD_MAX_BYTES 2048u
#define CKEY_EXTENDED_BLOCK_COUNT 10u
#define CKEY_EXTENDED_DATA_BYTES_PER_BLOCK 223u /* legacy Format 0.10/0.16 */
#define CKEY_EXTENDED_PARITY_BYTES_PER_BLOCK 32u /* legacy Format 0.10/0.16 */
#define CKEY_EXTENDED_CODEWORD_BYTES_PER_BLOCK 255u
#define CKEY_EXTENDED_CODEWORD_BYTES (CKEY_EXTENDED_BLOCK_COUNT * CKEY_EXTENDED_CODEWORD_BYTES_PER_BLOCK)
#define CKEY_EXTENDED_TIER_COUNT 7u

typedef enum ckey_extended_tier {
    CKEY_EXTENDED_TIER_AUTO = 0,
    CKEY_EXTENDED_TIER_256 = 256,
    CKEY_EXTENDED_TIER_384 = 384,
    CKEY_EXTENDED_TIER_512 = 512,
    CKEY_EXTENDED_TIER_768 = 768,
    CKEY_EXTENDED_TIER_1024 = 1024,
    CKEY_EXTENDED_TIER_1536 = 1536,
    CKEY_EXTENDED_TIER_2048 = 2048
} ckey_extended_tier;
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

/* Profile 3: ten interleaved systematic RS(255,k) blocks. Tiered formats
 * (0.17 and later) select k from the declared capacity/ECC tier; legacy
 * Format 0.10/0.16 uses k=223.
 * The public codeword is byte-interleaved by block. */
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
/* Select a minimum payload-capacity tier (256..2048 bytes). AUTO selects the
 * smallest tier that fits. Smaller tiers devote more of the fixed optical
 * codeword to Reed-Solomon parity. */
CKEY_API ckey_status ckey_encode_extended_tiered(const uint8_t *payload, size_t payload_len,
                                                  ckey_extended_tier tier,
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
 * same caller-supplied quadrilateral contract as ckey_decode_gray8. They read
 * the current optical layout only (Profile 2 Format 0.25, Profile 3 Format
 * 0.26); the byte-codeword decoders above also accept legacy frames. Payload
 * acceptance is gated by RS correction and CRC-32C. */
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
/* Equivalent continuous key-fit score for Profile 2's 29-unit dense field.
 * It samples Profile 2's own Grid, Ring, and Ray keys and must not be
 * substituted with the Profile 1 score. */
CKEY_API double ckey_calibration_score_text_gray8(const uint8_t *gray, size_t width,
                                                   size_t height, size_t stride,
                                                   const double quad_xy[8]);
/* Equivalent continuous key-fit score for Profile 3's 116-unit field.  The
 * locator uses this during projective geometry refinement; it never accepts a
 * payload and does not depend on a fixed pixel size. */
CKEY_API double ckey_calibration_score_extended_gray8(const uint8_t *gray, size_t width,
                                                       size_t height, size_t stride,
                                                       const double quad_xy[8]);
/*
 * Full-frame localization. ckey_find_borders_gray8 finds candidate CKey
 * border outlines (either polarity) in a whole grayscale frame and writes up
 * to max_quads perspective quadrilaterals (8 doubles each: the frame stroke's
 * outer edge), most border-like first; it returns the count. Pass a border to
 * ckey_refine_quad_gray8 to fit it to one profile's canonical field using
 * that profile's fixed keys; it returns the calibration score (negative for
 * invalid arguments) and writes a quadrilateral suitable for the matching
 * ckey_decode_*_gray8 call. Neither function accepts a payload.
 */
typedef enum ckey_field {
    CKEY_FIELD_SCALABLE = 0,  /* Profile 0: any version, identified by its keys */
    CKEY_FIELD_LOCATOR = 1,
    CKEY_FIELD_TEXT = 2,
    CKEY_FIELD_EXTENDED = 3
} ckey_field;
CKEY_API size_t ckey_find_borders_gray8(const uint8_t *gray, size_t width, size_t height,
                                        size_t stride, double *out_quads, size_t max_quads);
CKEY_API double ckey_refine_quad_gray8(const uint8_t *gray, size_t width, size_t height,
                                       size_t stride, const double border_quad[8],
                                       ckey_field field, double out_quad[8]);
/*
 * Profile 0: the scalable family (Format 0.27). One skeleton (frame, finders,
 * polar centre) at every size; version 1..31 sets the module pitch, from
 * Profile 1-like modules (v1, 29 across) to Profile 3 density (v31, 232
 * across), about 7% finer per step. The Grid, Ring, and Ray Keys are drawn at
 * each version's own module and slot pitch, so a reader identifies the
 * version from them; the error-corrected frame repeats it. Every optical site
 * carries a codeword bit at one of four correction levels.
 */
#define CKEY_SCALABLE_FORMAT_VERSION 27u
#define CKEY_SCALABLE_PRINT_FORMAT_VERSION 28u
/* Profile 0 layout families. STANDARD is Format 0.27. PRINT (Format 0.28,
 * "P0.1") keeps every mark and gap at least about one module (equal-arc ring
 * slots, larger dots, wider and fewer rays) so symbols survive small prints:
 * lower capacity per version, readable at a smaller printed size. */
typedef enum ckey_scalable_layout {
    CKEY_LAYOUT_STANDARD = 0,
    CKEY_LAYOUT_PRINT = 1
} ckey_scalable_layout;
#define CKEY_SCALABLE_VERSION_COUNT 31u
#define CKEY_SCALABLE_MAX_CODEWORD_BYTES 8192u
typedef enum ckey_ecc_level {
    CKEY_ECC_L = 0,  /* ~7% of each block correctable */
    CKEY_ECC_M = 1,  /* ~14% */
    CKEY_ECC_Q = 2,  /* ~22% */
    CKEY_ECC_H = 3   /* ~30% */
} ckey_ecc_level;
typedef struct ckey_scalable_symbol {
    uint32_t abi_version;
    uint32_t version;          /* 1..CKEY_SCALABLE_VERSION_COUNT */
    uint32_t ecc_level;        /* ckey_ecc_level */
    uint32_t layout;           /* ckey_scalable_layout */
    uint32_t payload_length;
    uint32_t codeword_length;  /* bytes used in codeword[] */
    uint8_t codeword[CKEY_SCALABLE_MAX_CODEWORD_BYTES];
} ckey_scalable_symbol;
/* Modules across the field for a version (29..232); 0 if invalid. The
 * module pitch in the 29-unit canonical field is 29 / modules. */
CKEY_API uint32_t ckey_scalable_modules_across(uint32_t version);
/* Maximum payload bytes for a version, level, and layout; 0 if invalid. */
CKEY_API size_t ckey_scalable_capacity(uint32_t version, ckey_ecc_level level, ckey_scalable_layout layout);
/* version 0 selects the smallest version whose capacity fits the payload. */
CKEY_API ckey_status ckey_encode_scalable(const uint8_t *payload, size_t payload_len,
                                           uint32_t version, ckey_ecc_level level,
                                           ckey_scalable_layout layout,
                                           ckey_scalable_symbol *out_symbol,
                                           ckey_diagnostics *out_diagnostics);
CKEY_API ckey_status ckey_decode_scalable(const uint8_t *codeword, size_t codeword_len,
                                           uint32_t version, ckey_scalable_layout layout,
                                           uint8_t *out_payload, size_t out_capacity,
                                           size_t *out_payload_len, ckey_diagnostics *out_diagnostics);
/* Identifies the layout and version from the keys, then decodes.
 * out_version and out_layout may be NULL. */
CKEY_API ckey_status ckey_decode_scalable_gray8(const uint8_t *gray, size_t width,
                                                 size_t height, size_t stride,
                                                 const double quad_xy[8],
                                                 uint8_t *out_payload, size_t out_capacity,
                                                 size_t *out_payload_len, uint32_t *out_version,
                                                 uint32_t *out_layout,
                                                 ckey_diagnostics *out_diagnostics);
/* Best key fit over all versions for a proposed quadrilateral (negative for
 * invalid arguments); out_version (may be NULL) receives that version. */
CKEY_API double ckey_calibration_score_scalable_gray8(const uint8_t *gray, size_t width,
                                                       size_t height, size_t stride,
                                                       const double quad_xy[8],
                                                       uint32_t *out_version);
CKEY_API ckey_status ckey_render_scalable_svg(const ckey_scalable_symbol *symbol, uint32_t scale,
                                               char *out_utf8, size_t out_capacity,
                                               size_t *out_required);
/*
 * Profile 0 design system. Styling changes how a symbol is painted, never
 * where a reader samples: site geometry, the frame, finders, keys, and the
 * quiet margin are fixed. Payload marks may change shape within their own
 * cells; colour, materials, and a logo are free within checked limits.
 * ckey_check_scalable_style validates a style against a symbol (contrast,
 * effect limits, logo error budget) and explains any refusal; the styled
 * renderer refuses any style that fails the check.
 */
typedef struct ckey_rgb { uint8_t r, g, b; } ckey_rgb;
typedef enum ckey_module_shape {
    CKEY_MODULE_ROUNDED = 0,   /* default rounded square */
    CKEY_MODULE_SQUARE = 1,
    CKEY_MODULE_CIRCLE = 2,
    CKEY_MODULE_DIAMOND = 3
} ckey_module_shape;
typedef enum ckey_ring_style {
    CKEY_RING_JOINED = 0,      /* default: adjacent dots join into arcs */
    CKEY_RING_DOTS = 1         /* every ring mark a separate dot */
} ckey_ring_style;
typedef enum ckey_ray_cap {
    CKEY_RAY_ROUND = 0,        /* default */
    CKEY_RAY_FLAT = 1
} ckey_ray_cap;
typedef struct ckey_style {
    ckey_rgb background;       /* centre of the background */
    ckey_rgb background_edge;  /* edge colour (radial vignette); equal = solid */
    ckey_rgb ink;              /* marks; light side of a metallic sheen */
    ckey_rgb ink_shade;        /* sheen shade across the symbol; equal = flat ink */
    ckey_rgb accents[4];       /* optional accent colours for payload marks */
    uint32_t accent_count;     /* 0..4 */
    uint32_t accent_percent;   /* 0..100: share of payload marks drawn in accents */
    ckey_module_shape modules;
    ckey_ring_style rings;
    ckey_ray_cap rays;
    double bevel;              /* 0..1 embossed highlight */
    double shadow;             /* 0..1 drop-shadow depth */
    double texture;            /* 0..1 background grain */
    const char *logo_href;     /* image URL or data: URI; NULL or "" for none */
    double logo_radius;        /* canonical units; 0 = the empty centre's size */
    uint32_t logo_plate;       /* 1: background disc behind the logo */
} ckey_style;
/* Fills the default style (the plain Profile 0 look). */
CKEY_API void ckey_style_default(ckey_style *style);
/* Largest logo radius for a version (keeps the Ring Key and outer ring). */
CKEY_API double ckey_scalable_logo_max_radius(uint32_t version, ckey_scalable_layout layout);
/* Share (0..) of the worst RS block's correction capacity consumed by marks
 * hidden under a logo of this radius; negative for invalid arguments. A
 * style is accepted up to 0.5, leaving half the correction for damage. */
CKEY_API double ckey_scalable_logo_load(const ckey_scalable_symbol *symbol, double logo_radius);
CKEY_API ckey_status ckey_check_scalable_style(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                                ckey_diagnostics *out_diagnostics);
CKEY_API ckey_status ckey_render_scalable_styled_svg(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                                      uint32_t scale, char *out_utf8, size_t out_capacity,
                                                      size_t *out_required);
/*
 * Print rendering: black ink on paper on the printer's dot grid, width_in
 * inches wide (the 31-unit canvas including the quiet margin) at dpi. Each
 * out_gray byte is 0 (ink) or 255 (paper): there is no grey for a printer
 * to halftone into noise. style may be NULL (default mark shapes); colours,
 * materials, and logo images are screen styling and are not printed. Call
 * with out_gray NULL to get *out_side; the buffer needs side * side bytes.
 */
CKEY_API ckey_status ckey_render_scalable_print(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                                 double width_in, double dpi, uint8_t *out_gray,
                                                 size_t out_capacity, size_t *out_side);
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
