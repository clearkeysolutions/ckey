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

#define CKEY_ABI_VERSION 1u
#define CKEY_SOFTWARE_VERSION "0.1.0-dev.4"
#define CKEY_FORMAT_VERSION 3u
#define CKEY_LOCATOR_PAYLOAD_BYTES 16u
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
    uint8_t codeword[42];
} ckey_symbol;

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
/* scale is pixels per module and is constrained to 1..CKEY_MAX_RENDER_SCALE. */
CKEY_API ckey_status ckey_render_svg(const ckey_symbol *symbol, uint32_t scale,
                                      char *out_utf8, size_t out_capacity,
                                      size_t *out_required);

#ifdef __cplusplus
}
#endif
#endif
