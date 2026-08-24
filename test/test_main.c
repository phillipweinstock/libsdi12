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
extern void test_crc16_update_matches_one_shot(void);
extern void test_crc_spec_ascii_vectors(void);

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
extern void test_sensor_init_needs_only_send_and_read(void);
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
extern void test_sensor_data_pagination_d0(void);
extern void test_sensor_data_pagination_d1(void);
extern void test_sensor_data_pagination_past_end(void);
extern void test_sensor_data_pagination_with_crc(void);
extern void test_sensor_binary_packet_fits_buffer(void);
extern void test_sensor_concurrent_survives_break(void);
extern void test_sensor_concurrent_aborted_by_addressed_command(void);
extern void test_sensor_concurrent_not_aborted_by_other_address(void);
extern void test_sensor_identify_mc_has_crc(void);
extern void test_sensor_identify_cc_has_crc(void);
extern void test_sensor_overlong_value_truncated_safely(void);
extern void test_sensor_measurement_done_null_values(void);
extern void test_sensor_nonfinite_values_emit_valid_sentinels(void);
extern void test_sensor_huge_values_clamped_to_spec_max(void);
extern void test_sensor_decimals_clamped_to_fit_nine_chars(void);
extern void test_sensor_ha_pages_carry_mandatory_crc(void);
extern void test_sensor_ic_param_metadata_no_crc(void);
extern void test_sensor_iha_param_metadata_has_crc(void);
extern void test_sensor_imc_identify_summary_no_crc(void);
extern void test_sensor_metadata_preserves_crc_request(void);
extern void test_sensor_empty_hv_uses_seven_digits(void);
extern void test_sensor_malformed_m_ignored(void);
extern void test_sensor_overlong_address_change_ignored(void);
extern void test_sensor_bare_h_ignored(void);
extern void test_sensor_bare_ir_ignored(void);
extern void test_sensor_decimals0_rounds(void);
extern void test_sensor_identify_meas_reports_ttt(void);

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
extern void test_master_get_data_crc_verified_ok(void);
extern void test_master_get_data_crc_corrupt_detected(void);
extern void test_master_get_data_wrong_address_rejected(void);
extern void test_master_identify_wrong_address_rejected(void);
extern void test_master_hv_binary_roundtrip_ok(void);
extern void test_master_hv_binary_small_buffer_reports_truncation(void);
extern void test_master_hv_binary_wrong_address_rejected(void);
extern void test_master_get_data_rejects_page_over_9(void);
extern void test_master_hv_binary_rejects_page_over_999(void);
extern void test_master_hv_data_rejects_page_over_999(void);
extern void test_master_acknowledge_present(void);
extern void test_master_acknowledge_absent_on_timeout(void);
extern void test_master_query_address_cmd(void);
extern void test_master_change_address_ok(void);
extern void test_master_change_address_refused(void);
extern void test_master_start_measurement_m(void);
extern void test_master_start_measurement_mc_group(void);
extern void test_master_start_measurement_concurrent(void);
extern void test_master_verify_cmd(void);
extern void test_master_continuous_values(void);
extern void test_master_continuous_crc_verified(void);
extern void test_master_wait_service_request_ok(void);
extern void test_master_wait_service_request_wrong_address(void);
extern void test_master_extended_roundtrip(void);
extern void test_master_extended_multiline_two_lines(void);
extern void test_master_identify_measurement_metadata(void);
extern void test_master_identify_param_metadata(void);
extern void test_master_get_hv_data_raw(void);
extern void test_master_get_hv_data_wrong_address_rejected(void);
extern void test_master_send_break_invokes_callback(void);
extern void test_master_bintype_sizes(void);
extern void test_master_hv_commands_are_plain(void);
extern void test_master_get_hv_data_verifies_and_strips_crc(void);
extern void test_master_get_hv_data_missing_crc_rejected(void);
extern void test_master_start_measurement_wrong_address_rejected(void);
extern void test_master_measurement_group_range_rejected(void);
extern void test_master_change_address_allows_persist_time(void);

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
    /* Unbuffered stdout so results survive a crash mid-run */
    setvbuf(stdout, NULL, _IONBF, 0);

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
    RUN_TEST(test_crc16_update_matches_one_shot);
    RUN_TEST(test_crc_spec_ascii_vectors);

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
    RUN_TEST(test_sensor_init_needs_only_send_and_read);
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
    RUN_TEST(test_sensor_data_pagination_d0);
    RUN_TEST(test_sensor_data_pagination_d1);
    RUN_TEST(test_sensor_data_pagination_past_end);
    RUN_TEST(test_sensor_data_pagination_with_crc);
    RUN_TEST(test_sensor_binary_packet_fits_buffer);
    RUN_TEST(test_sensor_concurrent_survives_break);
    RUN_TEST(test_sensor_concurrent_aborted_by_addressed_command);
    RUN_TEST(test_sensor_concurrent_not_aborted_by_other_address);
    RUN_TEST(test_sensor_identify_mc_has_crc);
    RUN_TEST(test_sensor_identify_cc_has_crc);
    RUN_TEST(test_sensor_overlong_value_truncated_safely);

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
    RUN_TEST(test_master_get_data_crc_verified_ok);
    RUN_TEST(test_master_get_data_crc_corrupt_detected);
    RUN_TEST(test_master_get_data_wrong_address_rejected);
    RUN_TEST(test_master_identify_wrong_address_rejected);
    RUN_TEST(test_master_hv_binary_roundtrip_ok);
    RUN_TEST(test_master_hv_binary_small_buffer_reports_truncation);
    RUN_TEST(test_master_hv_binary_wrong_address_rejected);
    RUN_TEST(test_master_get_data_rejects_page_over_9);
    RUN_TEST(test_master_hv_binary_rejects_page_over_999);
    RUN_TEST(test_master_hv_data_rejects_page_over_999);
    RUN_TEST(test_master_acknowledge_present);
    RUN_TEST(test_master_acknowledge_absent_on_timeout);
    RUN_TEST(test_master_query_address_cmd);
    RUN_TEST(test_master_change_address_ok);
    RUN_TEST(test_master_change_address_refused);
    RUN_TEST(test_master_start_measurement_m);
    RUN_TEST(test_master_start_measurement_mc_group);
    RUN_TEST(test_master_start_measurement_concurrent);
    RUN_TEST(test_master_verify_cmd);
    RUN_TEST(test_master_continuous_values);
    RUN_TEST(test_master_continuous_crc_verified);
    RUN_TEST(test_master_wait_service_request_ok);
    RUN_TEST(test_master_wait_service_request_wrong_address);
    RUN_TEST(test_master_extended_roundtrip);
    RUN_TEST(test_master_extended_multiline_two_lines);
    RUN_TEST(test_master_identify_measurement_metadata);
    RUN_TEST(test_master_identify_param_metadata);
    RUN_TEST(test_master_get_hv_data_raw);
    RUN_TEST(test_master_get_hv_data_wrong_address_rejected);
    RUN_TEST(test_master_send_break_invokes_callback);
    RUN_TEST(test_master_bintype_sizes);
    RUN_TEST(test_master_hv_commands_are_plain);
    RUN_TEST(test_master_get_hv_data_verifies_and_strips_crc);
    RUN_TEST(test_master_get_hv_data_missing_crc_rejected);
    RUN_TEST(test_master_start_measurement_wrong_address_rejected);
    RUN_TEST(test_master_measurement_group_range_rejected);
    RUN_TEST(test_master_change_address_allows_persist_time);

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

    RUN_TEST(test_sensor_nonfinite_values_emit_valid_sentinels);
    RUN_TEST(test_sensor_huge_values_clamped_to_spec_max);
    RUN_TEST(test_sensor_decimals_clamped_to_fit_nine_chars);
    RUN_TEST(test_sensor_ha_pages_carry_mandatory_crc);
    RUN_TEST(test_sensor_ic_param_metadata_no_crc);
    RUN_TEST(test_sensor_iha_param_metadata_has_crc);
    RUN_TEST(test_sensor_imc_identify_summary_no_crc);
    RUN_TEST(test_sensor_metadata_preserves_crc_request);
    RUN_TEST(test_sensor_empty_hv_uses_seven_digits);
    RUN_TEST(test_sensor_malformed_m_ignored);
    RUN_TEST(test_sensor_overlong_address_change_ignored);
    RUN_TEST(test_sensor_bare_h_ignored);
    RUN_TEST(test_sensor_bare_ir_ignored);
    RUN_TEST(test_sensor_decimals0_rounds);
    RUN_TEST(test_sensor_identify_meas_reports_ttt);

    /* Runs last: a regression here previously segfaulted */
    RUN_TEST(test_sensor_measurement_done_null_values);

    return UNITY_END();
}
