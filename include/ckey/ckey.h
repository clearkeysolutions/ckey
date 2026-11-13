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

#define CKEY_ABI_VERSION 4u
#define CKEY_SOFTWARE_VERSION "0.1.0-build.107"
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
    CKEY_INTEGRITY_FAILURE = 6,
    CKEY_EXPIRED = 7
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
 * One-call frame scan: border search, refinement and decoding for every
 * profile, in the order the reference readers use. This is the call camera
 * integrations should make; the three functions above remain for callers that
 * need the stages separately.
 *
 * Region of interest: with roi_width and roi_height set, only that rectangle
 * of the frame is searched, at full resolution and in place (no copy). A
 * region around the expected symbol is faster and removes the rest of the
 * scene from the search; alternate it with whole frames so symbols outside
 * the region still read. Coordinates in the result are always full-frame.
 *
 * Lens distortion: set k1 (Brown radial coefficient on focal-normalised
 * coordinates; focal_px and the principal point describe the camera) to have
 * the region undistorted before the search. Leave k1 at 0 when the platform
 * already corrects distortion or the lens is a normal wide or tele camera.
 */
#define CKEY_SCAN_PROFILE_SCALABLE 1u
#define CKEY_SCAN_PROFILE_LOCATOR 2u
#define CKEY_SCAN_PROFILE_TEXT 4u
#define CKEY_SCAN_PROFILE_EXTENDED 8u
#define CKEY_SCAN_PROFILE_ALL 15u
/* Result flags. */
#define CKEY_SCAN_FLAG_SYMBOL_CUT 1u   /* a symbol-like border runs off the frame or region: back off */
#define CKEY_SCAN_FLAG_BUDGET 2u       /* the scan stopped at its work budget */
#define CKEY_SCAN_FLAG_ML 4u           /* the decoded candidate came from the ML locator */
/* Options flags (ckey_scan_options.flags). */
#define CKEY_SCAN_OPTION_ML 1u         /* try ML locator proposals before the border search */
#define CKEY_SCAN_OPTION_ML_GATE 2u    /* with ML: skip the border search when the locator sees no symbol */
#define CKEY_SCAN_OPTION_ML_AFTER 4u   /* try ML locator proposals only after the border search found nothing that decodes */
/* Live frames: the classical reader only for this call (no ML locator, corner
 * refiner retry, site-model reading or multi-frame averaging). A camera loop
 * can scan most frames with it and the full ladder now and then, so a frame
 * that will not read gives up in milliseconds, not seconds. Acceptance is
 * unchanged. */
#define CKEY_SCAN_OPTION_FAST 8u
/* Bits 8..15 of flags: a Profile 0 version hint (1..31; 0 = none). Versions
 * within one of it are tried first and pass their key fit more easily, which
 * helps on noisy frames whose keys are hard to read. With CKEY_SCAN_OPTION_ML
 * and no hint, the locator's density estimate is used as the hint. */
#define CKEY_SCAN_VERSION_HINT(v) ((uint32_t)((v) & 0xffu) << 8)
#define CKEY_SCAN_VERSION_HINT_OF(flags) (((flags) >> 8) & 0xffu)
typedef struct ckey_scan_options {
    uint32_t abi_version;      /* CKEY_ABI_VERSION */
    uint32_t profiles;         /* CKEY_SCAN_PROFILE_* mask; 0 = all */
    uint32_t max_candidates;   /* border candidates to try; 0 = 6 */
    uint32_t flags;            /* CKEY_SCAN_OPTION_* (was reserved; 0 = classical only) */
    double roi_x, roi_y, roi_width, roi_height;  /* pixels; zero width or height = whole frame */
    double k1;                 /* radial distortion; 0 = none */
    double focal_px;           /* focal length in pixels for k1; 0 = the longer frame side */
    double center_x, center_y; /* principal point in pixels; both 0 = frame centre */
} ckey_scan_options;
typedef struct ckey_scan_result {
    uint32_t abi_version;
    ckey_status status;        /* CKEY_OK when a payload was decoded */
    uint32_t profile;          /* ckey_field of the decoded symbol */
    uint32_t version;          /* Profile 0 version, else 0 */
    uint32_t layout;           /* ckey_scalable_layout for Profile 0 */
    uint32_t corrected_symbols;
    uint32_t erasures_used;
    uint32_t candidates;       /* border candidates found */
    uint32_t flags;            /* CKEY_SCAN_FLAG_* */
    uint32_t located;          /* 1 if quad holds a symbol-like candidate, decoded or not */
    size_t payload_length;
    double quad[8];            /* field corners, full-frame pixels; canonical symbol order when decoded */
    double score;              /* fit score of quad (see ckey_refine_quad_gray8) */
    char message[96];
} ckey_scan_result;
CKEY_API void ckey_scan_options_init(ckey_scan_options *options);
/*
 * ML locator (optional). A small convolutional network proposes symbols in a
 * whole frame: four field corners in symbol corner order, a presence score, a
 * readability estimate (the probability the decoder reads the symbol from its
 * true corners), the version and the layout. Proposals are only proposals:
 * ckey_scan_frame_gray8 refines each one and decodes it, and a payload is
 * accepted only after Reed-Solomon and CRC-32C, exactly as for a classical
 * candidate. No model is active until one is built in (src/ml_builtin_*.inc,
 * generated by tools/embed_ml_model.py; CKEY_ML_NO_BUILTIN leaves them out) or
 * loaded; without one, ckey_ml_locate_gray8 returns 0 and scanning is
 * unchanged.
 * ckey_ml_load_model parses and validates a CKML model (it may come as a data
 * asset, so it is checked completely before use) and makes it the process's
 * model; NULL restores the built-in one. ckey_ml_model_id returns a 64-bit
 * identifier of the active model (0 when none).
 */
typedef struct ckey_ml_detection {
    double quad[8];      /* field corners, full-frame pixels, symbol corner order */
    double score;        /* presence, 0..1 */
    double quality;      /* estimated probability of a read from these corners, 0..1 */
    double version;      /* estimated Profile 0 version (continuous) */
    double print_layout; /* probability of the print layout, 0..1 */
} ckey_ml_detection;
CKEY_API size_t ckey_ml_locate_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                     double min_score, ckey_ml_detection *out, size_t max_out);
CKEY_API ckey_status ckey_ml_load_model(const uint8_t *data, size_t length);
CKEY_API uint64_t ckey_ml_model_id(void);
/*
 * ML site model (optional). With a site model loaded (ckey_ml_load_model
 * recognises the kind from the file), the Profile 0 decoder adds one reading
 * strategy after the plain one: the candidate's field is rectified at two
 * samples per module, the model turns it into an ink logit map, and each
 * site's logit gives its value and confidence for the soft decoder. Reed-
 * Solomon and CRC-32C still decide. ckey_ml_site_model_id is 0 when none.
 *
 * Tooling for training data (used by the key-test harness):
 *   ckey_ml_rectify_gray8 samples the 29-unit field of a Profile 0 symbol
 *   whose field corners are given in canonical order (corner i at canonical
 *   (0,0), (29,0), (29,29), (0,29)), at samples_per_module samples per module
 *   pitch of `version`, padded to a multiple of `align`. It writes side x side
 *   bytes and reports whether ink is darker than paper.
 *   ckey_ml_site_map lists every site of a symbol: its canonical position
 *   (units 0..29), kind (0 grid module, 1 dial ray, 2 ring dot) and whether
 *   it is printed.
 */
CKEY_API uint64_t ckey_ml_site_model_id(void);
/* Corner refiner (optional): with one loaded, a Profile 0 decode that fails
 * from the given corners is retried once from corners the refiner corrected
 * (two rounds on the rectified symbol). 0 when none. */
CKEY_API uint64_t ckey_ml_refiner_model_id(void);
/* Unloads every ML model, built-in ones included, until the next
 * ckey_ml_load_model call (NULL restores the built-ins). Scanning is then
 * exactly classical: for comparisons, and for hosts that want no ML. */
#define CKEY_ML_HAS_CLEAR 1
CKEY_API void ckey_ml_clear_models(void);
/*
 * Multi-frame reading. When a frame does not decode, the field of its most
 * plausible Profile 0 reading is rectified into the symbol's own coordinates,
 * standardised, matched to the running average in each of the four
 * quarter-turn orientations and added; the average is then decoded like a
 * clean frame by the normal decoder (which identifies the version itself, and
 * uses the ML site model when one is loaded). Frames that are each too noisy,
 * glared or partly hidden to decode alone can decode together. The average is
 * kept only while the same symbol stays in view: centre within half its size,
 * size within 25%, and the frame must correlate with the average. A frame
 * that does not match is skipped; three in a row restart it. A decode (alone
 * or from the average) resets it. Region-of-interest scans accumulate too
 * (lens-undistorted scans do not). Acceptance is unchanged: Reed-Solomon and
 * CRC-32C, exactly as on a single frame. CKEY_SCAN_FLAG_ACCUMULATED marks a
 * read from the average. One accumulator per camera stream; not thread-safe.
 */
#define CKEY_SCAN_FLAG_ACCUMULATED 8u
typedef struct ckey_frame_accumulator ckey_frame_accumulator;
CKEY_API ckey_frame_accumulator *ckey_accumulator_create(void);
CKEY_API void ckey_accumulator_destroy(ckey_frame_accumulator *accumulator);
CKEY_API void ckey_accumulator_reset(ckey_frame_accumulator *accumulator);
CKEY_API uint32_t ckey_accumulator_frames(const ckey_frame_accumulator *accumulator);
CKEY_API ckey_status ckey_scan_frame_accumulate_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                                      const ckey_scan_options *options, ckey_frame_accumulator *accumulator,
                                                      uint8_t *out_payload, size_t out_capacity, ckey_scan_result *out_result);

/* Rectifies any canonical region: the square [x0, x0 + extent] x [y0, y0 +
 * extent] (canonical 29-unit field coordinates, corners given in canonical
 * order) into side x side bytes. Used for the corner refiner's input. */
CKEY_API ckey_status ckey_ml_rectify_region_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                                  const double quad_xy[8], double x0, double y0, double extent,
                                                  uint32_t side, uint8_t *out, size_t out_capacity);
CKEY_API ckey_status ckey_ml_rectify_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                           const double quad_xy[8], uint32_t version, uint32_t samples_per_module,
                                           uint32_t align, uint8_t *out, size_t out_capacity, uint32_t *out_side,
                                           int *out_dark);
CKEY_API ckey_status ckey_scan_frame_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                            const ckey_scan_options *options,
                                            uint8_t *out_payload, size_t out_capacity,
                                            ckey_scan_result *out_result);
/*
 * Tracking: the four corners of a symbol on every frame, for live brackets or
 * any overlay. It is separate from decoding and keeps going after a read.
 *   - With no track, ckey_track_gray8 searches the frame for a symbol-like
 *     candidate. With a track, it re-detects in a crop around the last
 *     corners (cheap enough for every frame), so the corners are measured,
 *     not extrapolated, and do not drift.
 *   - A candidate must fit the CKey skeleton and keys (score >= gate). A
 *     strong fit or a decode locks at once; a weak one needs a second
 *     agreeing sighting. A track survives up to 8 missed frames.
 *   - Continuity: a re-detection is accepted only if its centre moved less
 *     than half the symbol's size and its size changed by less than 25%.
 *   - Corners keep a stable order from frame to frame (the order that is
 *     closest to the previous frame's), so an overlay never twists.
 * Call ckey_track_seed after a scan that located or decoded a symbol; it
 * re-seeds the track on that symbol's corners.
 */
#define CKEY_TRACK_NONE 0u
#define CKEY_TRACK_TENTATIVE 1u  /* one weak sighting; do not draw yet */
#define CKEY_TRACK_LOCKED 2u
typedef struct ckey_track_state {
    uint32_t abi_version;
    uint32_t status;           /* CKEY_TRACK_* */
    uint32_t missed_frames;    /* consecutive frames without a re-detection */
    uint32_t sightings;
    uint32_t decoded;          /* 1 once this symbol has decoded */
    uint32_t fresh;            /* 1 if quad was measured on the last frame given */
    double quad[8];            /* field corners, full-frame pixels, stable order */
    double score;              /* fit score of the last measurement */
    double gate;               /* minimum score; 0 = default (15) */
    double lock_score;         /* score that locks at once; 0 = default (150) */
} ckey_track_state;
CKEY_API void ckey_track_init(ckey_track_state *track);
CKEY_API void ckey_track_seed(ckey_track_state *track, const double quad_xy[8], double score, int decoded);
/* Returns the track status after this frame. */
CKEY_API uint32_t ckey_track_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                   ckey_track_state *track);
/* Reorders quad_xy (cyclically, either winding) to the order closest to
 * previous_xy. Use it before comparing or smoothing corner sets. */
CKEY_API void ckey_match_corner_order(const double previous_xy[8], double quad_xy[8]);
/*
 * Camera advice. The capture policy lives in the core so every platform
 * behaves the same: an SDK owns the camera, calls ckey_camera_advise_gray8
 * once per analysed frame, and applies what it returns.
 *   exposure_bias_ev  exposure target bias to set, in EV. Negative when the
 *                     symbol clips. While nothing decodes for a while it
 *                     cycles 0, -1, -2 (a screen too bright to detect at all
 *                     comes into range on one of them); it resets on a
 *                     decode or when the candidate is lost.
 *   zoom_factor       multiply the current zoom by this (1 = keep). Above 1
 *                     when the symbol is small in the frame. Apply it only up
 *                     to the device's real-detail limit; zoom that merely
 *                     enlarges pixels does not help.
 *   point_x, point_y  where to meter and focus (0..1 of the frame): the
 *                     candidate's centre, or the frame centre without one.
 *   flags             what to tell the user, if anything.
 */
#define CKEY_ADVICE_MOVE_CLOSER 1u   /* too few pixels per module */
#define CKEY_ADVICE_MOVE_BACK 2u     /* the symbol is cut by the frame edge */
#define CKEY_ADVICE_HOLD_STEADY 4u   /* blurred */
#define CKEY_ADVICE_REFOCUS 8u       /* out of focus: re-aim focus at point_x, point_y */
#define CKEY_ADVICE_GLARE 16u        /* part of the symbol is washed out: tilt the phone or the surface */
#define CKEY_ADVICE_REAIM 32u        /* the metering point moved enough to re-aim exposure and focus */
typedef struct ckey_camera_policy {
    uint32_t abi_version;
    uint32_t failed_frames;    /* analysed frames since the last decode */
    uint32_t bias_step;        /* position in the 0 / -1 / -2 EV cycle */
    uint32_t lost_frames;      /* frames since a candidate was last seen */
    double last_point_x, last_point_y;
    double reserved[4];
} ckey_camera_policy;
typedef struct ckey_camera_advice {
    uint32_t abi_version;
    uint32_t has_candidate;
    uint32_t flags;            /* CKEY_ADVICE_* */
    uint32_t reserved;
    double point_x, point_y;   /* metering and focus point, 0..1 */
    double size;               /* candidate side / shorter frame side; 0 without one */
    double clipped_fraction;   /* share of the candidate at or near white clipping */
    double contrast;           /* Michelson contrast inside the candidate, 0..1 */
    double sharpness;          /* 0 (soft) .. 1 (sharp) at the frame stroke */
    double exposure_bias_ev;
    double zoom_factor;
} ckey_camera_advice;
CKEY_API void ckey_camera_policy_init(ckey_camera_policy *policy);
/* quad_xy: the located or tracked symbol (NULL when none). decoded: nonzero
 * if this frame decoded. */
CKEY_API ckey_status ckey_camera_advise_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                               const double *quad_xy, int decoded,
                                               ckey_camera_policy *policy, ckey_camera_advice *out_advice);
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
#define CKEY_SCALABLE_GROUPED_FORMAT_VERSION 29u
#define CKEY_SCALABLE_PRINT_GROUPED_FORMAT_VERSION 30u
#define CKEY_SCALABLE_PIXEL_FORMAT_VERSION 31u
#define CKEY_HAS_PIXEL_LAYOUT 1
/* Profile 0 layout families. STANDARD is Format 0.27. PRINT (Format 0.28,
 * "P0.1") keeps every mark and gap at least about one module (equal-arc ring
 * slots, larger dots, wider and fewer rays) so symbols survive small prints:
 * lower capacity per version, readable at a smaller printed size. */
/* Byte-grouped placement (Formats 0.29 and 0.30). The symbol's geometry,
 * marks, keys and capacity are those of STANDARD or PRINT; only the rule that
 * assigns codeword bits to sites differs. Each byte's eight bits sit on eight
 * neighbouring sites, so a blot, glare spot or logo costs only the bytes under
 * it, where the scattered placement of 0.27/0.28 lets it touch a few bits of
 * many bytes. Readers before 0.1.0-build.86 cannot read grouped symbols, so
 * the encoder emits them only when asked. Image decoders identify the
 * placement themselves and report it in out_layout. */
typedef enum ckey_scalable_layout {
    CKEY_LAYOUT_STANDARD = 0,          /* Format 0.27 */
    CKEY_LAYOUT_PRINT = 1,             /* Format 0.28 */
    CKEY_LAYOUT_STANDARD_GROUPED = 2,  /* Format 0.29: STANDARD geometry, byte-grouped */
    CKEY_LAYOUT_PRINT_GROUPED = 3,     /* Format 0.30: PRINT geometry, byte-grouped */
    CKEY_LAYOUT_PIXEL = 4              /* Format 0.31 (experimental): PIXEL geometry. The ring zone's
                                          data sites are grid modules, so every data site is a distinct
                                          cell at one cell per module (see ckey_render_scalable_lattice). */
} ckey_scalable_layout;
#define CKEY_SCALABLE_VERSION_COUNT 31u
#define CKEY_SCALABLE_MAX_CODEWORD_BYTES 8192u
/* Parity is 14/28/44/60/80/95% of each Reed-Solomon block. A level corrects
 * half that share in unknown bad bytes, or all of it in bytes the reader
 * knows are missing (erasures: glare, occlusion, sites outside the image).
 * U and X spend most of the symbol on parity: U carries about half of H's
 * payload, X about 5% of L's, and the smallest versions carry nothing at
 * all (ckey_scalable_capacity returns 0). Readers before 0.1.0-build.86
 * reject U and X symbols. */
typedef enum ckey_ecc_level {
    CKEY_ECC_L = 0,  /* ~7% of each block correctable, 14% as erasures */
    CKEY_ECC_M = 1,  /* ~14%, 28% */
    CKEY_ECC_Q = 2,  /* ~22%, 44% */
    CKEY_ECC_H = 3,  /* ~30%, 60% */
    CKEY_ECC_U = 4,  /* Ultra: ~40%, 80% */
    CKEY_ECC_X = 5   /* Extreme: ~47.5%, 95% */
} ckey_ecc_level;
#define CKEY_ECC_LEVEL_COUNT 6u
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
/* ML site map: see ckey_ml_rectify_gray8 above. */
/* Diagnostics: reads every site of a known symbol (`truth`, e.g. one just
 * decoded) from an image at the given field corners (any corner order: the
 * orientation and reading mode that agree best with the truth are kept).
 * out_correct[i] is 1 when site i (ckey_ml_site_map's order) read as printed,
 * 0 when wrong, 255 when unknown (outside the image or washed out);
 * out_confidence[i] the reader's confidence (noise deviations). *out_errors
 * counts wrong sites. CKEY_UNCORRECTABLE when no orientation could be sampled;
 * CKEY_INSUFFICIENT_BUFFER (with *out_count set) when out is NULL or short.
 * Call with out NULL for *out_count. */
#define CKEY_HAS_READ_SITES 1
CKEY_API ckey_status ckey_read_sites_gray8(const uint8_t *gray, size_t width, size_t height, size_t stride,
                                          const double quad_xy[8], const ckey_scalable_symbol *truth,
                                          uint8_t *out_correct, float *out_confidence, size_t capacity,
                                          size_t *out_count, uint32_t *out_errors);
CKEY_API ckey_status ckey_ml_site_map(const ckey_scalable_symbol *symbol, float *out_xy, uint8_t *out_kind,
                                      uint8_t *out_printed, size_t capacity, size_t *out_count);
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
/* Soft-decision decode. reliability (may be NULL: all bytes trusted) holds one
 * value per codeword byte, 0 = known bad (an erasure) up to 255 = certain.
 * When plain correction fails, the least reliable bytes are erased in a few
 * capped steps and decoding is retried (generalised minimum distance). An
 * erased byte costs one parity byte, an unknown bad byte two. Acceptance is
 * unchanged: every block must pass Reed-Solomon with spare syndromes left as
 * a check, and the frame must pass its header and CRC-32C.
 * out_diagnostics->erasures_used counts the bytes erased in the decode that
 * succeeded. */
CKEY_API ckey_status ckey_decode_scalable_soft(const uint8_t *codeword, const uint8_t *reliability,
                                                size_t codeword_len, uint32_t version,
                                                ckey_scalable_layout layout,
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
/*
 * Optical-safety policy (specification §4), carried by the style:
 *   STRICT    (default) renderers refuse a style that fails the safety rules.
 *   ADAPTIVE  renderers repair the style by fixed rules first (ink pushed to
 *             black or white, failing accents dropped, depth limited, logo
 *             plated or shrunk), then apply the strict rules.
 *             ckey_certify_scalable_style reports every change.
 *   OVERRIDE  renderers draw the style as given, skipping the safety rules.
 *             The caller accepts an uncertified symbol; the style check and
 *             certification still report what fails.
 * Values a renderer cannot draw (unknown shapes, NaN, out-of-range effects)
 * are refused under every policy.
 */
typedef enum ckey_style_policy {
    CKEY_POLICY_STRICT = 0,
    CKEY_POLICY_ADAPTIVE = 1,
    CKEY_POLICY_OVERRIDE = 2
} ckey_style_policy;
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
    uint32_t policy;           /* ckey_style_policy; 0 = strict (ABI 4) */
} ckey_style;
/* Fills the default style: black ink on a white background (spec §4). */
CKEY_API void ckey_style_default(ckey_style *style);
typedef enum ckey_style_preset {
    CKEY_PRESET_PLAIN = 0,     /* the default style */
    CKEY_PRESET_CONCEPT = 1    /* cream metallic ink on a dark vignette, accents, depth, grain */
} ckey_style_preset;
/* Fills a named preset; CKEY_INVALID_ARGUMENT for an unknown preset. */
CKEY_API ckey_status ckey_style_preset_fill(ckey_style *style, ckey_style_preset preset);
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
/*
 * Canonical geometry (specification §2.1). A Profile 0 symbol, with a
 * style's mark shapes, is a list of primitives in canonical units: the
 * symbol field spans 0..29 on both axes and the full canvas, including the
 * quiet margin, spans -1..30 (31 units). x grows right and y grows down.
 * Every backend (raster, PDF, SVG) draws exactly these shapes, in this order
 * (painter's order: a LOGO_PLATE shape erases the ink drawn before it).
 *
 *   kind               p[0..4]                         width
 *   RECT               x, y, w, h, corner radius        -
 *   RECT_OUTLINE       x, y, w, h, corner radius        stroke width (centred on the outline)
 *   CIRCLE             cx, cy, r                        -
 *   DIAMOND            cx, cy, d (vertices at ±d)       -
 *   LINE               x0, y0, x1, y1                   stroke width; round_cap
 *   ARC                cx, cy, r, start, sweep          stroke width; round_cap
 *   LOGO               cx, cy, r (circular clip)        -
 *
 * Arc angles are radians in canvas coordinates: 0 points along +x, and a
 * positive sweep turns clockwise on screen (toward +y).
 */
typedef enum ckey_shape_kind {
    CKEY_SHAPE_RECT = 0, CKEY_SHAPE_RECT_OUTLINE = 1, CKEY_SHAPE_CIRCLE = 2, CKEY_SHAPE_DIAMOND = 3,
    CKEY_SHAPE_LINE = 4, CKEY_SHAPE_ARC = 5, CKEY_SHAPE_LOGO = 6
} ckey_shape_kind;
typedef enum ckey_layer {
    CKEY_LAYER_FRAME = 0, CKEY_LAYER_FINDERS = 1, CKEY_LAYER_GRID_KEY = 2, CKEY_LAYER_POLAR_KEYS = 3,
    CKEY_LAYER_DATA_POLAR = 4, CKEY_LAYER_DATA_CARTESIAN = 5, CKEY_LAYER_LOGO_PLATE = 6, CKEY_LAYER_LOGO = 7
} ckey_layer;
#define CKEY_PAINT_INK (-1)          /* the style's ink (or its sheen) */
#define CKEY_PAINT_BACKGROUND (-2)   /* erases ink: the logo plate */
typedef struct ckey_shape {
    uint32_t kind;       /* ckey_shape_kind */
    uint32_t layer;      /* ckey_layer */
    int32_t paint;       /* CKEY_PAINT_INK, CKEY_PAINT_BACKGROUND, or accent index 0..3 */
    uint32_t round_cap;  /* LINE and ARC: 1 round caps, 0 flat (butt) caps */
    double p[5];
    double width;
} ckey_shape;
/* Writes the symbol's primitives (style may be NULL for the default shapes).
 * Call with out NULL to get *out_count. The style must pass the style check. */
CKEY_API ckey_status ckey_scalable_geometry(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                            ckey_shape *out, size_t out_capacity, size_t *out_count);

/*
 * Direct raster backend (specification §2.2): geometry to pixels, with no
 * SVG, no file system, no network, and no graphics library. The output is
 * size_px x size_px pixels covering the full 31-unit canvas, rows top to
 * bottom with no padding.
 *
 * Policies (benchmark them; none is universally best):
 *   COVERAGE  antialiased: each pixel's ink is its supersampled area coverage.
 *   BINARY    each pixel is ink when at least half its samples are covered.
 *   HYBRID    coverage, but marks thinner than min_feature_px (default 1 px)
 *             are widened to it so thin rays and dots stay continuous.
 *             HYBRID changes drawn widths, never the encoded topology.
 *
 * Formats: RGBA8 (straight, non-premultiplied sRGB alpha) or GRAY8 (Rec. 709
 * luma of the opaque result). transparent = 1 (RGBA8 only) paints no
 * background, vignette or texture: unpainted pixels have alpha 0. materials
 * = 0 draws flat ink (no sheen, depth, or grain). logo (optional) is drawn
 * into the style's logo circle; without it the logo area shows only the
 * plate, if any.
 */
typedef enum ckey_raster_policy { CKEY_RASTER_COVERAGE = 0, CKEY_RASTER_BINARY = 1, CKEY_RASTER_HYBRID = 2 } ckey_raster_policy;
typedef enum ckey_pixel_format { CKEY_PIXELS_RGBA8 = 0, CKEY_PIXELS_GRAY8 = 1 } ckey_pixel_format;
typedef struct ckey_logo_image {
    const uint8_t *rgba;       /* straight-alpha RGBA8 */
    uint32_t width, height;
    size_t stride;             /* bytes per row; 0 = width * 4 */
} ckey_logo_image;
typedef struct ckey_raster_options {
    uint32_t abi_version;      /* CKEY_ABI_VERSION */
    uint32_t size_px;          /* 16..16384 */
    uint32_t policy;           /* ckey_raster_policy */
    uint32_t format;           /* ckey_pixel_format */
    uint32_t transparent;
    uint32_t supersample;      /* samples per pixel axis, 1..8 */
    double min_feature_px;     /* HYBRID only; > 0 */
    uint32_t materials;
    const ckey_logo_image *logo;
} ckey_raster_options;
/* Defaults: 512 px, COVERAGE, RGBA8, opaque, 4x4 samples, 1 px, materials on. */
CKEY_API void ckey_raster_options_default(ckey_raster_options *options);
/* style may be NULL (default style). Call with out NULL to get *out_required
 * (size_px * size_px * 4 for RGBA8, size_px * size_px for GRAY8). */
CKEY_API ckey_status ckey_render_scalable_raster(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                                 const ckey_raster_options *options, uint8_t *out,
                                                 size_t out_capacity, size_t *out_required);

/*
 * Pixel-perfect lattice: the symbol on a grid of whole cells, for displays and
 * exports that must never be resampled. Every Profile 0 grid module is
 * exactly cells_per_module x cells_per_module cells (1..32; 0 = the smallest
 * size that decodes at every version: 3 for the Print family, 6 for Standard,
 * 2 for the experimental Pixel family)
 * and is drawn as a centred dot about 0.68 of the module (c - 2 * round(0.16 c)
 * cells; Print and Pixel 2 of 3 and 3 of 4; the whole block at 1 and 2); every
 * other mark (frame,
 * finders, rings, rays, Ring Key) is snapped to the cells it covers by at
 * least 10% (half from 12 cells per module). For display, use the most cells
 * per module that fit the available pixels: finer lattices draw thin rays
 * and round ring marks; whole pixels per cell beyond 32. Smaller sizes than the default do not decode reliably (the
 * Standard family's ring dots are 0.2 of a module). Each out byte is one
 * cell: 0 background, 1 ink, 2 + i accent i. Display it only at whole-number
 * scales without smoothing (one cell = N device pixels); export PNG by
 * repeating each cell N x N and SVG as one square per cell, so every copy is
 * the same image. Flat colours only: materials and logos are not drawn.
 * *out_side is the side in cells (the field plus a quiet margin of at least
 * one canonical unit). Call with out NULL for the size.
 */
/*
 * Pixel-aligned raster: the full renderer (antialiased rings, rays, frame and
 * finders; colours, materials and logo) drawn on the lattice's pixel grid, so
 * every grid module is an exact whole-pixel dot (the lattice's dot rule) and
 * nothing is resampled, while curves stay smooth. One pixel per cell:
 * *out_side = cells_per_module * modules + 2 * ceil(that / 29) pixels (the
 * lattice's side). cells_per_module 1..64; options->size_px is ignored; other
 * options as for ckey_render_scalable_raster. Call with out NULL for the size.
 * This is what the SDKs show on screen and write to PNG.
 */
#define CKEY_HAS_ALIGNED_RASTER 1
CKEY_API ckey_status ckey_render_scalable_aligned_raster(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                                         const ckey_raster_options *options, uint32_t cells_per_module,
                                                         uint8_t *out, size_t out_capacity, uint32_t *out_side,
                                                         size_t *out_required);
#define CKEY_HAS_LATTICE 1
CKEY_API ckey_status ckey_render_scalable_lattice(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                                  uint32_t cells_per_module, uint8_t *out, size_t out_capacity,
                                                  uint32_t *out_side, size_t *out_required);

/*
 * PNG export (specification §2.5), from any pixel buffer. Compressed with
 * DEFLATE (fixed Huffman codes and LZ77); no zlib dependency. GRAY1 writes a
 * 1-bit image from GRAY8 input (< 128 is black) for print bitmaps. dpi > 0
 * records the physical resolution (pHYs). Call with out NULL for the size.
 */
typedef enum ckey_png_format { CKEY_PNG_RGBA8 = 0, CKEY_PNG_GRAY8 = 1, CKEY_PNG_GRAY1 = 2 } ckey_png_format;
CKEY_API ckey_status ckey_encode_png(const uint8_t *pixels, uint32_t width, uint32_t height, size_t stride,
                                     ckey_png_format format, double dpi, uint8_t *out, size_t out_capacity,
                                     size_t *out_required);

/*
 * PDF vector export (specification §2.3-2.4): a one-page PDF 1.4 whose page
 * is width_pt points wide (72 points per inch) and holds the exact geometry
 * as filled and stroked paths (arcs as cubic Béziers). Flat colours only:
 * ink, accents and the background (omitted when transparent = 1). Screen
 * materials (sheen, depth, grain) and logos are not exported. Call with out
 * NULL for the size.
 */
CKEY_API ckey_status ckey_render_scalable_pdf(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                              double width_pt, uint32_t transparent, uint8_t *out,
                                              size_t out_capacity, size_t *out_required);

/*
 * Optical certification (specification §4, acceptance criteria 5 and 6).
 * A style is certified only by decoding it through modelled photographs,
 * never from a contrast ratio alone, and it is judged against plain black on
 * white for the same symbol.
 *
 * The sweep renders the symbol with the native raster, composites it over
 * every backdrop, places it in a cluttered frame tilted by rotation and
 * keystone, adds glare, defocus and sensor noise, and runs the full reader
 * pipeline (ckey_find_borders_gray8, ckey_refine_quad_gray8, decode). It
 * walks a ladder of camera resolutions (9, 7, 5.5, 4.5, 3.75, 3 pixels per
 * module) to find the smallest one at which the style still decodes. The
 * grade compares that with plain black on white:
 *   excellent  decodes as small as plain does
 *   good       needs one ladder step more than plain
 *   marginal   needs two steps more
 *   fails      three or more, never decodes, or needs more than
 *              model.px_per_module
 * certified = 1 needs the safety rules to pass and a grade of good or
 * better. trials and trials_passed count the modelled photos.
 *
 * Transparent symbols: the renderer cannot see what lies behind them, so
 * transparent = 1 needs a backdrop (solid colours and/or the host's pixels
 * behind the symbol). Without one, certification reports certified = 0
 * ("background unknown") and never claims readability.
 *
 * The model is a model, not a measurement. Real-camera tests remain the
 * evidence (spec §11.6).
 */
typedef struct ckey_backdrop {
    const ckey_rgb *colours;   /* solid backgrounds the symbol may sit on */
    uint32_t colour_count;     /* 0..16 */
    const uint8_t *rgba;       /* optional host pixels behind the symbol (straight RGBA8), */
    uint32_t width, height;    /* stretched over the symbol's full 31-unit canvas */
    size_t stride;             /* 0 = width * 4 */
} ckey_backdrop;
typedef struct ckey_scan_model {
    uint32_t abi_version;      /* CKEY_ABI_VERSION */
    double px_per_module;      /* the style must decode at this resolution or finer; default 8 */
    double blur_px;            /* Gaussian defocus sigma in camera pixels; default 0.9 */
    double noise;              /* sensor noise sigma, fraction of full scale; default 0.03 */
    double glare;              /* contrast compression 0..0.9: luma -> luma * (1 - g) + g; default 0.2 */
    uint32_t trials;           /* accepted for compatibility (1..16); the sweep sets its own scenes */
} ckey_scan_model;
#define CKEY_ADJUST_INK 1u         /* ink replaced by black or white */
#define CKEY_ADJUST_ACCENTS 2u     /* accents removed */
#define CKEY_ADJUST_EFFECTS 4u     /* bevel or shadow limited */
#define CKEY_ADJUST_LOGO 8u        /* logo plated or shrunk */
#define CKEY_ADJUST_BACKGROUND 16u /* the symbol was given its own background */
#define CKEY_ADJUST_MATERIALS 32u  /* sheen, vignette, depth and grain removed after a failed model test */
typedef struct ckey_certification {
    uint32_t abi_version;
    uint32_t certified;        /* 1: decoded in every modelled trial */
    uint32_t policy;           /* the policy that was applied */
    uint32_t adjustments;      /* CKEY_ADJUST_* bits (adaptive policy) */
    uint32_t trials, trials_passed;
    double min_contrast;       /* informative WCAG-style ratio over all ink/background pairs */
    double feature_separation; /* luma separation of mark vs gap pixels after compositing: 5th/95th percentile gap, 0..1; <= 0 means they overlap */
    double logo_load;          /* share of the worst block's correction hidden by the logo */
    char message[256];         /* the reason for refusal, or the list of changes and warnings */
} ckey_certification;
CKEY_API void ckey_scan_model_default(ckey_scan_model *model);
/*
 * Certifies style (NULL = default) on symbol under style->policy:
 *   STRICT    returns CKEY_INVALID_ARGUMENT unless the style is certified.
 *   ADAPTIVE  repairs the style; if it still fails the model, removes
 *             materials and retries. On success writes the style to render
 *             to out_style (may be NULL) and returns CKEY_OK.
 *   OVERRIDE  returns CKEY_OK whatever happens; out->certified says whether
 *             the symbol would scan under the model.
 * backdrop may be NULL for an opaque symbol (its own background is used).
 * model may be NULL for the default. transparent selects transparent output.
 */
CKEY_API ckey_status ckey_certify_scalable_style(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                                 uint32_t transparent, const ckey_backdrop *backdrop,
                                                 const ckey_scan_model *model, ckey_style *out_style,
                                                 ckey_certification *out);

/*
 * .ckey interchange (specification §2.5; spec/CKey-Interchange-v1.md). A
 * compact, optional container for a Profile 0 symbol and its style; it is
 * never needed to render or display one. The codeword carries the payload
 * in clear, so a .ckey file is exactly as sensitive as its payload.
 *
 * ckey_write_interchange: style may be NULL. Call with out NULL for the size.
 * ckey_read_interchange: strict, bounded parser. It rejects anything
 * malformed, oversized (CKEY_INTERCHANGE_MAX_BYTES), failing its CRC-32C,
 * whose codeword does not decode, or with an unknown required record.
 * out_style may be NULL. A logo reference is copied to logo_href (may be
 * NULL; NUL-terminated, at most logo_cap bytes) and out_style->logo_href
 * points at it; without a buffer the logo is dropped.
 */
#define CKEY_INTERCHANGE_VERSION 1u
#define CKEY_INTERCHANGE_MAX_BYTES 65536u
CKEY_API ckey_status ckey_write_interchange(const ckey_scalable_symbol *symbol, const ckey_style *style,
                                            uint8_t *out, size_t out_capacity, size_t *out_required);
CKEY_API ckey_status ckey_read_interchange(const uint8_t *data, size_t length, ckey_scalable_symbol *out_symbol,
                                           ckey_style *out_style, char *logo_href, size_t logo_cap,
                                           ckey_diagnostics *out_diagnostics);

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

/*
 * Safe Actions (spec/CKey-Safe-Actions.md): a signed request, carried as the
 * text payload "ckey-action:1:<base64url envelope>", for an action the
 * receiving app has registered. The core parses, signs and verifies it; the
 * SDKs decide whether it runs. A payload that fails any check is ordinary text
 * and falls back to the show-and-approve rule.
 */
typedef enum ckey_safe_action_scope {
    CKEY_SCOPE_SAME_DEVICE = 0,   /* the result stays in the app on the scanning device */
    CKEY_SCOPE_CROSS_DEVICE = 1   /* approves something elsewhere, e.g. signs in a browser */
} ckey_safe_action_scope;

typedef struct ckey_safe_action {
    uint32_t abi_version;
    uint32_t scope;              /* ckey_safe_action_scope */
    char issuer[254];            /* issuing site's DNS name, lower case */
    char app[256];               /* receiving app ID, e.g. com.viadere */
    char action[65];             /* action name registered by the receiving app */
    char key_id[33];             /* which issuer key signed it */
    char request_id[65];         /* single-use request ID, 8..64 characters */
    uint64_t issued_at;          /* Unix seconds */
    uint64_t expires_at;         /* Unix seconds */
    uint8_t params[1024];        /* action parameters; UTF-8 JSON recommended */
    size_t params_len;
} ckey_safe_action;

/* 1 when the payload starts with "ckey-action:1:", else 0. Says nothing about validity. */
CKEY_API int ckey_is_safe_action(const uint8_t *payload, size_t len);
/* Reads the fields without checking the signature, e.g. to look up the issuer's key. */
CKEY_API ckey_status ckey_safe_action_parse(const uint8_t *payload, size_t len, ckey_safe_action *out,
                                            ckey_diagnostics *diag);
/*
 * CKEY_OK only when the Ed25519 signature matches public_key, the lifetime is at
 * most max_lifetime_seconds (0 = 600) and now (Unix seconds) is within it, with
 * 120 s of clock tolerance. CKEY_INTEGRITY_FAILURE for a bad signature,
 * CKEY_EXPIRED for a valid but out-of-time envelope, CKEY_INVALID_ARGUMENT if
 * malformed. Single use (request_id) is enforced by the issuer's server.
 */
CKEY_API ckey_status ckey_safe_action_verify(const uint8_t *payload, size_t len, const uint8_t public_key[32],
                                             uint64_t now, uint32_t max_lifetime_seconds,
                                             ckey_safe_action *out, ckey_diagnostics *diag);
/* Issuer side: signs fields with a 32-byte Ed25519 seed into NUL-terminated text. */
CKEY_API ckey_status ckey_safe_action_sign(const ckey_safe_action *fields, const uint8_t seed[32],
                                           char *out, size_t out_capacity, size_t *out_len,
                                           ckey_diagnostics *diag);
CKEY_API void ckey_ed25519_public_key(const uint8_t seed[32], uint8_t out_public_key[32]);

#ifdef __cplusplus
}
#endif
#endif
