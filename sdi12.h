/**
 * @file sdi12.h
 * @brief Portable SDI-12 v1.4 Protocol Library — Common Types and Definitions
 *
 * A hardware-independent, pure C implementation of the SDI-12 serial digital
 * interface standard (version 1.4, February 20 2023). Provides both sensor
 * (slave) and data recorder (master) capabilities via callback-based I/O.
 *
 * No hardware dependencies — all I/O is delegated to user-supplied callbacks.
 *
 * @author Phillip Weinstock
 * @copyright 2026 All Rights Reserved
 */
#ifndef SDI12_H
#define SDI12_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ────────────────────────────────────────────────────────────────────────── */
/*  Version                                                                  */
/* ────────────────────────────────────────────────────────────────────────── */

#define SDI12_LIB_VERSION_MAJOR 0
#define SDI12_LIB_VERSION_MINOR 1
#define SDI12_LIB_VERSION_PATCH 0

/** SDI-12 protocol version this library targets. */
#define SDI12_PROTOCOL_VERSION "14"

/* ────────────────────────────────────────────────────────────────────────── */
/*  Protocol Constants                                                       */
/* ────────────────────────────────────────────────────────────────────────── */

/** Baud rate — fixed by the SDI-12 standard. */
#define SDI12_BAUD_RATE 1200

/** Maximum number of measurement parameters per M command (n = 1 digit). */
#define SDI12_M_MAX_VALUES 9

/** Maximum number of measurement parameters per C command (nn = 2 digits). */
#define SDI12_C_MAX_VALUES 99

/** Maximum number of measurement parameters per HA/HB command (nnn = 3 digits). */
#define SDI12_H_MAX_VALUES 999

/** Maximum data pages for standard commands. */
#define SDI12_MAX_DATA_PAGES 10

/** Maximum data pages for high-volume commands. */
#define SDI12_MAX_HV_DATA_PAGES 1000

/** Maximum binary data payload per §5.2 (bytes). */
#define SDI12_BIN_MAX_PAYLOAD 1000

/** Binary packet overhead: addr(1) + pkt_size(2) + type(1) + CRC(2). */
#define SDI12_BIN_PKT_OVERHEAD 6

/** Max chars of <values> in a D response after M/V commands. */
#define SDI12_M_VALUES_MAX_CHARS 35

/** Max chars of <values> in a D response after C/R/HA commands. */
#define SDI12_C_VALUES_MAX_CHARS 75

/** Max chars per value (sign + 7 digits + decimal point). */
#define SDI12_VALUE_MAX_CHARS 9

/** Max total response length (generous — longest is extended multi-line). */
#define SDI12_MAX_RESPONSE_LEN 82

/** Max command length a master would send or sensor would receive. */
#define SDI12_MAX_COMMAND_LEN 20

/** Alias used by master for outgoing command buffer sizing. */
#define SDI12_CMD_MAX_CHARS SDI12_MAX_COMMAND_LEN

/** Max chars in a complete response (address + values + CRC + CRLF). */
#define SDI12_RESP_MAX_CHARS SDI12_MAX_RESPONSE_LEN

/** Max parsed values the master will store from a single D response. */
#define SDI12_MAX_VALUES SDI12_C_MAX_VALUES

/** Max additional measurement/concurrent command indices (M1-M9, C1-C9). */
#define SDI12_MAX_MEAS_GROUPS 10

/** Max number of registerable parameters across all groups. */
#define SDI12_MAX_PARAMS 20

/** Max extended command registrations. */
#define SDI12_MAX_XCMDS 8

/** Identification string field widths per spec. */
#define SDI12_ID_VERSION_LEN  2
#define SDI12_ID_VENDOR_LEN   8
#define SDI12_ID_MODEL_LEN    6
#define SDI12_ID_FWVER_LEN    3
#define SDI12_ID_SERIAL_MAXLEN 13

/* ────────────────────────────────────────────────────────────────────────── */
/*  Timing Constants (milliseconds)                                          */
/* ────────────────────────────────────────────────────────────────────────── */

/** Minimum break duration the recorder must send. */
#define SDI12_BREAK_MS          12

/** Post-break marking duration. */
#define SDI12_MARK_AFTER_BREAK_MS  9  /* 8.33 ms rounded up */

/** Alias for post-break marking. */
#define SDI12_MARKING_MS SDI12_MARK_AFTER_BREAK_MS

/** Max sensor response time after command stop bit. */
#define SDI12_RESPONSE_TIMEOUT_MS  15

/** Max inter-character gap within a message. */
#define SDI12_INTERCHAR_MAX_MS  2  /* 1.66 ms rounded up */

/** Marking duration after which break is required. */
#define SDI12_MARKING_TIMEOUT_MS 87

/** Sensor returns to standby after this much idle marking. */
#define SDI12_STANDBY_TIMEOUT_MS 100

/** Min retry wait (no response). */
#define SDI12_RETRY_MIN_MS  17  /* 16.67 ms rounded up */

/** Max gap between lines in multi-line extended response. */
#define SDI12_MULTILINE_GAP_MS 150

/** Max delay for sensor to persist new address. */
#define SDI12_ADDRESS_CHANGE_DELAY_MS 1000

/* ────────────────────────────────────────────────────────────────────────── */
/*  Enumerations                                                             */
/* ────────────────────────────────────────────────────────────────────────── */

/** SDI-12 bus direction. */
typedef enum {
    SDI12_DIR_RX = 0, /**< Receive mode (listen on bus). */
    SDI12_DIR_TX = 1  /**< Transmit mode (drive bus). */
} sdi12_dir_t;

/** Measurement command type — determines response format and behavior. */
typedef enum {
    SDI12_MEAS_STANDARD = 0,   /**< aM! — standard, n=1 digit, 35-char D limit */
    SDI12_MEAS_CONCURRENT,     /**< aC! — concurrent, nn=2 digits, 75-char D limit */
    SDI12_MEAS_HIGHVOL_ASCII,  /**< aHA! — high-volume ASCII, nnn=3 digits */
    SDI12_MEAS_HIGHVOL_BINARY, /**< aHB! — high-volume binary */
    SDI12_MEAS_VERIFICATION,   /**< aV! — verification */
    SDI12_MEAS_CONTINUOUS      /**< aR! — continuous (no prior M needed) */
} sdi12_meas_type_t;

/** Sensor state machine states (for sensor-side implementation). */
typedef enum {
    SDI12_STATE_STANDBY = 0,   /**< Low-power standby. */
    SDI12_STATE_READY,         /**< Awake, listening for commands. */
    SDI12_STATE_MEASURING,     /**< Standard measurement in progress (M/V). */
    SDI12_STATE_MEASURING_C,   /**< Concurrent measurement in progress. */
    SDI12_STATE_DATA_READY     /**< Measurement complete, data available. */
} sdi12_state_t;

/** Return codes for library functions. */
typedef enum {
    SDI12_OK = 0,
    SDI12_ERR_INVALID_ADDRESS,
    SDI12_ERR_INVALID_COMMAND,
    SDI12_ERR_BUFFER_OVERFLOW,
    SDI12_ERR_NOT_ADDRESSED,
    SDI12_ERR_NO_DATA,
    SDI12_ERR_PARAM_LIMIT,
    SDI12_ERR_CALLBACK_MISSING,
    SDI12_ERR_TIMEOUT,
    SDI12_ERR_CRC_MISMATCH,
    SDI12_ERR_PARSE_FAILED,
    SDI12_ERR_ABORTED
} sdi12_err_t;

/** Binary data types for high-volume binary (aHB!) responses. */
typedef enum {
    SDI12_BINTYPE_INVALID = 0,
    SDI12_BINTYPE_INT8    = 1,
    SDI12_BINTYPE_UINT8   = 2,
    SDI12_BINTYPE_INT16   = 3,
    SDI12_BINTYPE_UINT16  = 4,
    SDI12_BINTYPE_INT32   = 5,
    SDI12_BINTYPE_UINT32  = 6,
    SDI12_BINTYPE_INT64   = 7,
    SDI12_BINTYPE_UINT64  = 8,
    SDI12_BINTYPE_FLOAT32 = 9,
    SDI12_BINTYPE_FLOAT64 = 10
} sdi12_bintype_t;

/* ────────────────────────────────────────────────────────────────────────── */
/*  Data Structures                                                          */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Sensor identification info (aI! response fields).
 *
 * All string fields are null-terminated. Shorter strings should be
 * space-padded to their full width per the SDI-12 spec.
 */
typedef struct {
    char vendor[SDI12_ID_VENDOR_LEN + 1];          /**< 8-char vendor name. */
    char model[SDI12_ID_MODEL_LEN + 1];            /**< 6-char model number. */
    char firmware_version[SDI12_ID_FWVER_LEN + 1]; /**< 3-char firmware version. */
    char serial[SDI12_ID_SERIAL_MAXLEN + 1];       /**< 0–13 char optional serial. */
} sdi12_ident_t;

/**
 * @brief Measurement parameter metadata.
 *
 * Describes a single measurement value: its SHEF code, units, and how
 * to read it from the sensor hardware via a user-supplied callback.
 */
typedef struct {
    char shef[4];    /**< SHEF code, e.g. "TA", "PA", "RP". Null-terminated. */
    char units[21];  /**< Units string, e.g. "C", "Kpa". Null-terminated. */
} sdi12_param_meta_t;

/**
 * @brief A single measurement value with sign-prefixed formatting.
 */
typedef struct {
    float value;     /**< The measurement value. */
    uint8_t decimals; /**< Number of decimal places to format (0–7). */
} sdi12_value_t;

/**
 * @brief Parsed measurement response from a sensor (master-side use).
 *
 * Populated by the master after receiving atttn / atttnn / atttnnn responses.
 */
typedef struct {
    char     address;
    uint16_t wait_seconds;  /**< ttt field (0–999). */
    uint16_t value_count;   /**< n / nn / nnn field (0–999). */
    sdi12_meas_type_t type;
} sdi12_meas_response_t;

/**
 * @brief Parsed data values from a D command response (master-side use).
 */
typedef struct {
    char          address;
    sdi12_value_t values[SDI12_C_MAX_VALUES]; /**< Parsed numeric values. */
    uint8_t       value_count;                /**< Number of values parsed. */
    bool          crc_valid;                  /**< True if CRC was present and valid. */
} sdi12_data_response_t;

/**
 * @brief Parsed parameter metadata from aIM_nnn! / aIC_nnn! response.
 *
 * Response format: "a,SHEF,units;\r\n"
 */
typedef struct {
    char address;
    char shef[8];    /**< SHEF code (e.g. "TA", "PA"). Null-terminated. */
    char units[24];  /**< Units string (e.g. "C", "kPa"). Null-terminated. */
} sdi12_param_meta_response_t;

/**
 * @brief Parsed identification response (master-side use).
 */
typedef struct {
    char address;
    char sdi12_version[3];  /**< e.g. "14" */
    sdi12_ident_t info;
} sdi12_ident_response_t;

/* ────────────────────────────────────────────────────────────────────────── */
/*  Address Validation                                                       */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Check if a character is a valid SDI-12 sensor address.
 *
 * Valid: '0'–'9', 'A'–'Z', 'a'–'z' (62 total).
 *
 * @param c Character to validate.
 * @return true if valid SDI-12 address.
 */
static inline bool sdi12_valid_address(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

/* ────────────────────────────────────────────────────────────────────────── */
/*  CRC API (implemented in sdi12_crc.c)                                     */
/* ────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Compute SDI-12 CRC-16-IBM over a buffer.
 *
 * Polynomial 0xA001 (reflected CRC-16-IBM), initial value 0x0000.
 *
 * @param data Pointer to data buffer.
 * @param len  Number of bytes to process.
 * @return 16-bit CRC value.
 */
uint16_t sdi12_crc16(const void *data, size_t len);

/**
 * @brief Encode a 16-bit CRC into 3 ASCII characters per SDI-12 spec.
 *
 * Each 6-bit group is OR'd with 0x40 to produce a printable character.
 *
 * @param crc The 16-bit CRC value.
 * @param out Output buffer — must hold at least 4 bytes (3 chars + null).
 */
void sdi12_crc_encode_ascii(uint16_t crc, char out[4]);

/**
 * @brief Compute and append CRC to a response string.
 *
 * Calculates CRC over the string, encodes as 3 ASCII chars, and appends
 * them before the existing CR/LF terminator (or at end if unterminated).
 *
 * @param buf    Response buffer (must have room for 3 extra chars).
 * @param buflen Total buffer capacity.
 * @return SDI12_OK on success, SDI12_ERR_BUFFER_OVERFLOW if insufficient room.
 */
sdi12_err_t sdi12_crc_append(char *buf, size_t buflen);

/**
 * @brief Compute and append CRC to a response buffer with explicit data length.
 *
 * Like sdi12_crc_append() but uses an explicit data length instead of strlen(),
 * making it safe for binary payloads that may contain null bytes.
 * The CRC is computed over buf[0..data_len-1] and inserted before CR/LF.
 *
 * @param buf       Response buffer.
 * @param data_len  Number of data bytes (payload before CR/LF, or total if no CR/LF).
 * @param buflen    Total buffer capacity.
 * @return SDI12_OK on success, SDI12_ERR_BUFFER_OVERFLOW if insufficient room.
 */
sdi12_err_t sdi12_crc_append_n(char *buf, size_t data_len, size_t buflen);

/**
 * @brief Verify CRC on a received response string.
 *
 * Expects the 3 CRC chars immediately before the CR/LF terminator.
 *
 * @param buf    Response string (with CRC and CR/LF).
 * @param len    Length of the response string.
 * @return true if CRC matches.
 */
bool sdi12_crc_verify(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SDI12_H */
