/**
 * @file test_main.c
 * @brief Platform-agnostic test runner for libsdi12.
 *
 * Compiles together with test_crc.c, test_address.c, test_sensor.c,
 * test_master.c, and test_metamorphic.c into a single test binary.
 *
 * Build with any C compiler:
 *   gcc -std=c11 -I.. -o test_sdi12 *.c ../sdi12_crc.c ../sdi12_sensor.c ../sdi12_master.c -lm
 *   ./test_sdi12
 *
 * Or use the provided Makefile:
 *   make test
 */
#define SDI12_TEST_IMPLEMENTATION
#include "sdi12_test.h"

/* ── setUp / tearDown (Unity hooks) ─────────────────────────────────────── */

void setUp(void)  { /* nothing */ }
void tearDown(void) { /* nothing */ }

/* ── Extern test function declarations ──────────────────────────────────── */

/* test_crc.c */
extern void test_crc16_empty(void);
extern void test_crc16_single_char(void);
extern void test_crc16_known_vector(void);
extern void test_crc16_different_data_differs(void);
extern void test_crc_encode_ascii_zero(void);
extern void test_crc_encode_ascii_all_ones(void);
extern void test_crc_encode_ascii_printable_range(void);
extern void test_crc_append_basic(void);
extern void test_crc_append_with_existing_crlf(void);
extern void test_crc_append_buffer_overflow(void);
extern void test_crc_verify_valid(void);
extern void test_crc_verify_corrupt_data(void);
extern void test_crc_verify_corrupt_crc(void);
extern void test_crc_verify_too_short(void);
extern void test_crc_roundtrip_various(void);

/* test_address.c */
extern void test_valid_digits(void);
extern void test_valid_uppercase(void);
extern void test_valid_lowercase(void);
extern void test_invalid_special_chars(void);
extern void test_invalid_control_chars(void);
extern void test_invalid_boundaries(void);
extern void test_total_valid_count(void);

/* test_sensor.c */
extern void test_sensor_init_ok(void);
extern void test_sensor_init_null_ctx(void);
extern void test_sensor_init_invalid_address(void);
extern void test_sensor_init_missing_send_callback(void);
extern void test_sensor_init_loads_persisted_address(void);
extern void test_sensor_acknowledge(void);
extern void test_sensor_query_address(void);
extern void test_sensor_wrong_address_no_response(void);
extern void test_sensor_identify(void);
extern void test_sensor_measurement_m(void);
extern void test_sensor_measurement_mc_with_crc(void);
extern void test_sensor_measurement_m_empty_group(void);
extern void test_sensor_measurement_c(void);
extern void test_sensor_measurement_cc_with_crc(void);
extern void test_sensor_send_data_after_m(void);
extern void test_sensor_send_data_with_crc(void);
extern void test_sensor_send_data_no_data(void);
extern void test_sensor_continuous_r0(void);
extern void test_sensor_continuous_rc0_with_crc(void);
extern void test_sensor_continuous_empty_group(void);
extern void test_sensor_change_address(void);
extern void test_sensor_change_address_invalid(void);
extern void test_sensor_highvol_stub(void);
extern void test_sensor_break_aborts_measurement(void);
extern void test_sensor_break_null_safe(void);
extern void test_sensor_extended_command(void);
extern void test_sensor_extended_no_handler(void);
extern void test_sensor_identify_measurement(void);
extern void test_sensor_identify_concurrent(void);
extern void test_sensor_identify_param_metadata(void);
extern void test_sensor_identify_param_metadata_second(void);
extern void test_sensor_register_max_params(void);
extern void test_sensor_group_count(void);
extern void test_sensor_measurement_done_service_request(void);
extern void test_sensor_measurement_done_concurrent_no_sr(void);
extern void test_sensor_negative_value_in_data(void);

/* test_master.c */
extern void test_parse_meas_m_basic(void);
extern void test_parse_meas_m_with_wait(void);
extern void test_parse_meas_m_max_wait(void);
extern void test_parse_meas_c_basic(void);
extern void test_parse_meas_c_two_digit_count(void);
extern void test_parse_meas_h_three_digit_count(void);
extern void test_parse_meas_v_same_as_m(void);
extern void test_parse_meas_too_short(void);
extern void test_parse_meas_null_args(void);
extern void test_parse_meas_different_addresses(void);
extern void test_parse_values_single_positive(void);
extern void test_parse_values_single_negative(void);
extern void test_parse_values_multiple(void);
extern void test_parse_values_integer(void);
extern void test_parse_values_zero(void);
extern void test_parse_values_empty_string(void);
extern void test_parse_values_max_capacity(void);
extern void test_parse_values_with_crc_strip(void);
extern void test_parse_values_large_value(void);
extern void test_parse_values_mixed_signs(void);
extern void test_parse_values_null_args(void);

/* test_metamorphic.c — CRC properties */
extern void test_meta_crc_single_byte_mutation_detected(void);
extern void test_meta_crc_append_verify_roundtrip_universal(void);
extern void test_meta_crc_double_append_not_idempotent(void);
extern void test_meta_crc_encoding_bijective(void);

/* test_metamorphic.c — Address properties */
extern void test_meta_address_idempotent(void);
extern void test_meta_address_partition_complete(void);

/* test_metamorphic.c — Sensor properties */
extern void test_meta_sensor_address_change_reversible(void);
extern void test_meta_sensor_wrong_address_silence_universal(void);
extern void test_meta_sensor_measurement_deterministic(void);
extern void test_meta_sensor_break_returns_ready_from_any_state(void);
extern void test_meta_sensor_crc_variant_adds_three_chars(void);
extern void test_meta_sensor_ha_vs_m_response_format(void);
extern void test_meta_sensor_hb_with_binary_callback(void);
extern void test_meta_sensor_hb_without_callback_uses_ascii(void);

/* test_metamorphic.c — Master parser properties */
extern void test_meta_parse_sign_flip_negates(void);
extern void test_meta_parse_concatenation_additive(void);
extern void test_meta_parse_deterministic(void);
extern void test_meta_parse_decimal_count_matches_input(void);
extern void test_meta_parse_meas_address_passthrough(void);

/* ── Main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    /* ── CRC-16 ─────────────────────────────────────────────────────────── */
    RUN_TEST(test_crc16_empty);
    RUN_TEST(test_crc16_single_char);
    RUN_TEST(test_crc16_known_vector);
    RUN_TEST(test_crc16_different_data_differs);
    RUN_TEST(test_crc_encode_ascii_zero);
    RUN_TEST(test_crc_encode_ascii_all_ones);
    RUN_TEST(test_crc_encode_ascii_printable_range);
    RUN_TEST(test_crc_append_basic);
    RUN_TEST(test_crc_append_with_existing_crlf);
    RUN_TEST(test_crc_append_buffer_overflow);
    RUN_TEST(test_crc_verify_valid);
    RUN_TEST(test_crc_verify_corrupt_data);
    RUN_TEST(test_crc_verify_corrupt_crc);
    RUN_TEST(test_crc_verify_too_short);
    RUN_TEST(test_crc_roundtrip_various);

    /* ── Address Validation ─────────────────────────────────────────────── */
    RUN_TEST(test_valid_digits);
    RUN_TEST(test_valid_uppercase);
    RUN_TEST(test_valid_lowercase);
    RUN_TEST(test_invalid_special_chars);
    RUN_TEST(test_invalid_control_chars);
    RUN_TEST(test_invalid_boundaries);
    RUN_TEST(test_total_valid_count);

    /* ── Sensor (Slave) ─────────────────────────────────────────────────── */
    RUN_TEST(test_sensor_init_ok);
    RUN_TEST(test_sensor_init_null_ctx);
    RUN_TEST(test_sensor_init_invalid_address);
    RUN_TEST(test_sensor_init_missing_send_callback);
    RUN_TEST(test_sensor_init_loads_persisted_address);
    RUN_TEST(test_sensor_acknowledge);
    RUN_TEST(test_sensor_query_address);
    RUN_TEST(test_sensor_wrong_address_no_response);
    RUN_TEST(test_sensor_identify);
    RUN_TEST(test_sensor_measurement_m);
    RUN_TEST(test_sensor_measurement_mc_with_crc);
    RUN_TEST(test_sensor_measurement_m_empty_group);
    RUN_TEST(test_sensor_measurement_c);
    RUN_TEST(test_sensor_measurement_cc_with_crc);
    RUN_TEST(test_sensor_send_data_after_m);
    RUN_TEST(test_sensor_send_data_with_crc);
    RUN_TEST(test_sensor_send_data_no_data);
    RUN_TEST(test_sensor_continuous_r0);
    RUN_TEST(test_sensor_continuous_rc0_with_crc);
    RUN_TEST(test_sensor_continuous_empty_group);
    RUN_TEST(test_sensor_change_address);
    RUN_TEST(test_sensor_change_address_invalid);
    RUN_TEST(test_sensor_highvol_stub);
    RUN_TEST(test_sensor_break_aborts_measurement);
    RUN_TEST(test_sensor_break_null_safe);
    RUN_TEST(test_sensor_extended_command);
    RUN_TEST(test_sensor_extended_no_handler);
    RUN_TEST(test_sensor_identify_measurement);
    RUN_TEST(test_sensor_identify_concurrent);
    RUN_TEST(test_sensor_identify_param_metadata);
    RUN_TEST(test_sensor_identify_param_metadata_second);
    RUN_TEST(test_sensor_register_max_params);
    RUN_TEST(test_sensor_group_count);
    RUN_TEST(test_sensor_measurement_done_service_request);
    RUN_TEST(test_sensor_measurement_done_concurrent_no_sr);
    RUN_TEST(test_sensor_negative_value_in_data);

    /* ── Master (Data Recorder) ─────────────────────────────────────────── */
    RUN_TEST(test_parse_meas_m_basic);
    RUN_TEST(test_parse_meas_m_with_wait);
    RUN_TEST(test_parse_meas_m_max_wait);
    RUN_TEST(test_parse_meas_c_basic);
    RUN_TEST(test_parse_meas_c_two_digit_count);
    RUN_TEST(test_parse_meas_h_three_digit_count);
    RUN_TEST(test_parse_meas_v_same_as_m);
    RUN_TEST(test_parse_meas_too_short);
    RUN_TEST(test_parse_meas_null_args);
    RUN_TEST(test_parse_meas_different_addresses);
    RUN_TEST(test_parse_values_single_positive);
    RUN_TEST(test_parse_values_single_negative);
    RUN_TEST(test_parse_values_multiple);
    RUN_TEST(test_parse_values_integer);
    RUN_TEST(test_parse_values_zero);
    RUN_TEST(test_parse_values_empty_string);
    RUN_TEST(test_parse_values_max_capacity);
    RUN_TEST(test_parse_values_with_crc_strip);
    RUN_TEST(test_parse_values_large_value);
    RUN_TEST(test_parse_values_mixed_signs);
    RUN_TEST(test_parse_values_null_args);

    /* ── Metamorphic: CRC Properties ────────────────────────────────────── */
    RUN_TEST(test_meta_crc_single_byte_mutation_detected);
    RUN_TEST(test_meta_crc_append_verify_roundtrip_universal);
    RUN_TEST(test_meta_crc_double_append_not_idempotent);
    RUN_TEST(test_meta_crc_encoding_bijective);

    /* ── Metamorphic: Address Properties ────────────────────────────────── */
    RUN_TEST(test_meta_address_idempotent);
    RUN_TEST(test_meta_address_partition_complete);

    /* ── Metamorphic: Sensor Properties ─────────────────────────────────── */
    RUN_TEST(test_meta_sensor_address_change_reversible);
    RUN_TEST(test_meta_sensor_wrong_address_silence_universal);
    RUN_TEST(test_meta_sensor_measurement_deterministic);
    RUN_TEST(test_meta_sensor_break_returns_ready_from_any_state);
    RUN_TEST(test_meta_sensor_crc_variant_adds_three_chars);
    RUN_TEST(test_meta_sensor_ha_vs_m_response_format);
    RUN_TEST(test_meta_sensor_hb_with_binary_callback);
    RUN_TEST(test_meta_sensor_hb_without_callback_uses_ascii);

    /* ── Metamorphic: Master Parser Properties ──────────────────────────── */
    RUN_TEST(test_meta_parse_sign_flip_negates);
    RUN_TEST(test_meta_parse_concatenation_additive);
    RUN_TEST(test_meta_parse_deterministic);
    RUN_TEST(test_meta_parse_decimal_count_matches_input);
    RUN_TEST(test_meta_parse_meas_address_passthrough);

    return UNITY_END();
}
