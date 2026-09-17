#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

static const char *HELLO =
    "{\"type\":\"hello\",\"protocol_version\":1,\"device_id\":\"macbook-air-m5\","
    "\"platform\":\"macos\",\"app\":\"StatsForKVM\"}";

static const char *TELEMETRY =
    "{\"type\":\"telemetry\",\"protocol_version\":1,\"device_id\":\"macbook-air-m5\","
    "\"platform\":\"macos\",\"sequence\":7,\"timestamp_milliseconds\":1700000000000,"
    "\"metrics\":{\"cpu.temperature.average\":{\"valid\":true,\"value\":48.5,"
    "\"unit\":\"celsius\",\"source\":\"Average CPU\",\"error\":null,"
    "\"sampled_at_milliseconds\":1700000000000},\"memory.usage\":{\"valid\":true,"
    "\"value\":50.0,\"unit\":\"percent\",\"source\":\"Stats RAM_Usage\","
    "\"error\":null,\"sampled_at_milliseconds\":1700000000000}}}";

static protocol_message_t parse_ok(const char *json)
{
    protocol_message_t message;
    char error[PROTOCOL_ERROR_MAX] = {0};
    protocol_status_t status = protocol_parse_message(json, strlen(json), &message,
                                                       error, sizeof(error));
    if (status != PROTOCOL_OK) fprintf(stderr, "unexpected parse failure: %s\n", error);
    assert(status == PROTOCOL_OK);
    return message;
}

static void expect_parse_status(const char *json, protocol_status_t expected)
{
    protocol_message_t message;
    char error[PROTOCOL_ERROR_MAX] = {0};
    assert(protocol_parse_message(json, strlen(json), &message, error, sizeof(error)) == expected);
}

static char *replace_once(const char *input, const char *needle, const char *replacement)
{
    const char *position = strstr(input, needle);
    assert(position != NULL);
    size_t prefix = (size_t)(position - input);
    size_t length = prefix + strlen(replacement) + strlen(position + strlen(needle)) + 1;
    char *result = malloc(length);
    assert(result != NULL);
    snprintf(result, length, "%.*s%s%s", (int)prefix, input, replacement,
             position + strlen(needle));
    return result;
}

int main(void)
{
    protocol_message_t hello = parse_ok(HELLO);
    assert(hello.type == PROTOCOL_MESSAGE_HELLO);
    protocol_message_t telemetry = parse_ok(TELEMETRY);
    assert(telemetry.data.telemetry.cpu_temperature.valid);
    assert(telemetry.data.telemetry.memory_usage.valid);

    protocol_session_t session = {0};
    char error[PROTOCOL_ERROR_MAX] = {0};
    assert(protocol_session_accept(&session, &hello, error, sizeof(error)) == PROTOCOL_OK);
    assert(protocol_session_accept(&session, &telemetry, error, sizeof(error)) == PROTOCOL_OK);
    assert(protocol_session_accept(&session, &telemetry, error, sizeof(error)) ==
           PROTOCOL_ERROR_SEQUENCE);

    protocol_session_t no_hello = {0};
    assert(protocol_session_accept(&no_hello, &telemetry, error, sizeof(error)) ==
           PROTOCOL_ERROR_HELLO_REQUIRED);

    char *cpu_null = replace_once(TELEMETRY, "\"valid\":true,\"value\":48.5",
                                  "\"valid\":true,\"value\":null");
    protocol_message_t cpu_null_message = parse_ok(cpu_null);
    assert(!cpu_null_message.data.telemetry.cpu_temperature.valid);
    assert(strcmp(cpu_null_message.data.telemetry.cpu_temperature.error, "value_missing") == 0);
    free(cpu_null);

    char *valid_false = replace_once(TELEMETRY,
        "\"valid\":true,\"value\":48.5,\"unit\":\"celsius\",\"source\":\"Average CPU\","
        "\"error\":null",
        "\"valid\":false,\"value\":null,\"unit\":\"celsius\",\"source\":\"Average CPU\","
        "\"error\":\"sensor_unavailable\"");
    protocol_message_t invalid_message = parse_ok(valid_false);
    assert(!invalid_message.data.telemetry.cpu_temperature.valid);
    free(valid_false);

    char *memory_oob = replace_once(TELEMETRY, "\"value\":50.0,\"unit\":\"percent\"",
                                    "\"value\":101,\"unit\":\"percent\"");
    protocol_message_t oob_message = parse_ok(memory_oob);
    assert(!oob_message.data.telemetry.memory_usage.valid);
    assert(strcmp(oob_message.data.telemetry.memory_usage.error, "value_out_of_range") == 0);
    free(memory_oob);

    char *wrong_version = replace_once(HELLO, "\"protocol_version\":1",
                                       "\"protocol_version\":2");
    expect_parse_status(wrong_version, PROTOCOL_ERROR_UNSUPPORTED_VERSION);
    free(wrong_version);

    char oversized[PROTOCOL_MAX_MESSAGE_BYTES + 2];
    memset(oversized, 'x', sizeof(oversized));
    protocol_message_t unused;
    assert(protocol_parse_message(oversized, sizeof(oversized), &unused, error, sizeof(error)) ==
           PROTOCOL_ERROR_TOO_LONG);

    const char *complete_result =
        "{\"type\":\"command_result\",\"protocol_version\":1,"
        "\"device_id\":\"macbook-air-m5\",\"request_id\":\"esp32-1\","
        "\"success\":true,\"write_succeeded\":true,\"confirmed\":true,"
        "\"requested_input\":15,\"read_back_input\":15,\"error\":null}";
    protocol_message_t result = parse_ok(complete_result);
    assert(result.data.command_result.write_succeeded);
    assert(result.data.command_result.confirmed);
    const char *unconfirmed_result =
        "{\"type\":\"command_result\",\"protocol_version\":1,"
        "\"device_id\":\"macbook-air-m5\",\"request_id\":\"esp32-2\","
        "\"success\":true,\"write_succeeded\":true,\"confirmed\":false,"
        "\"requested_input\":15,\"read_back_input\":null,"
        "\"error\":\"input_written_but_unconfirmed\"}";
    protocol_message_t unconfirmed = parse_ok(unconfirmed_result);
    assert(unconfirmed.data.command_result.write_succeeded);
    assert(!unconfirmed.data.command_result.confirmed);
    char *incomplete = replace_once(complete_result, "\"confirmed\":true,", "");
    expect_parse_status(incomplete, PROTOCOL_ERROR_SCHEMA);
    free(incomplete);

    char command[512];
    assert(protocol_build_switch_command(command, sizeof(command), "esp32-1", "windows",
                                         "macbook-air-m5"));
    assert(strstr(command, "\"action\":\"switch_display\"") != NULL);
    puts("protocol fixed-vector tests: PASS");
    return 0;
}
