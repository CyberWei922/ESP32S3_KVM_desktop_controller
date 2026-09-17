#include "protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#define MAX_EXACT_JSON_INTEGER 9007199254740991.0

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error == NULL || capacity == 0) return;
    snprintf(error, capacity, "%s", message != NULL ? message : "protocol_error");
}

static bool copy_bounded(char *destination, size_t capacity, const char *source)
{
    if (destination == NULL || capacity == 0 || source == NULL || source[0] == '\0' ||
        strlen(source) >= capacity) {
        return false;
    }
    snprintf(destination, capacity, "%s", source);
    return true;
}

static const cJSON *required_item(const cJSON *object, const char *name)
{
    return cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
}

static bool json_integer(const cJSON *item, double minimum, double maximum, double *value)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        floor(item->valuedouble) != item->valuedouble || item->valuedouble < minimum ||
        item->valuedouble > maximum) {
        return false;
    }
    if (value != NULL) *value = item->valuedouble;
    return true;
}

static protocol_status_t parse_version(const cJSON *root, char *error, size_t error_capacity)
{
    double value = 0;
    const cJSON *version = required_item(root, "protocol_version");
    if (!json_integer(version, 0, INT32_MAX, &value)) {
        set_error(error, error_capacity, "protocol_version_type");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if ((int)value != PROTOCOL_VERSION) {
        set_error(error, error_capacity, "unsupported_protocol_version");
        return PROTOCOL_ERROR_UNSUPPORTED_VERSION;
    }
    return PROTOCOL_OK;
}

static protocol_status_t parse_platform(const cJSON *item, protocol_platform_t *platform,
                                        char *error, size_t error_capacity)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        set_error(error, error_capacity, "platform_type");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if (strcmp(item->valuestring, "macos") == 0) {
        *platform = PROTOCOL_PLATFORM_MACOS;
        return PROTOCOL_OK;
    }
    if (strcmp(item->valuestring, "windows") == 0) {
        *platform = PROTOCOL_PLATFORM_WINDOWS;
        return PROTOCOL_OK;
    }
    set_error(error, error_capacity, "unsupported_platform");
    return PROTOCOL_ERROR_UNSUPPORTED_PLATFORM;
}

static protocol_status_t parse_identity(const cJSON *root, protocol_platform_t *platform,
                                        char *device_id, size_t device_id_capacity,
                                        char *error, size_t error_capacity)
{
    protocol_status_t status = parse_platform(required_item(root, "platform"), platform,
                                              error, error_capacity);
    if (status != PROTOCOL_OK) return status;

    const cJSON *device = required_item(root, "device_id");
    if (!cJSON_IsString(device) ||
        !copy_bounded(device_id, device_id_capacity, device->valuestring)) {
        set_error(error, error_capacity, "device_id_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    return PROTOCOL_OK;
}

static protocol_status_t parse_hello(const cJSON *root, protocol_hello_t *hello,
                                     char *error, size_t error_capacity)
{
    protocol_status_t status = parse_identity(root, &hello->platform, hello->device_id,
                                              sizeof(hello->device_id), error, error_capacity);
    if (status != PROTOCOL_OK) return status;

    const cJSON *app = required_item(root, "app");
    if (!cJSON_IsString(app) || !copy_bounded(hello->app, sizeof(hello->app), app->valuestring)) {
        set_error(error, error_capacity, "app_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if (hello->platform == PROTOCOL_PLATFORM_MACOS && strcmp(hello->app, "StatsForKVM") != 0) {
        set_error(error, error_capacity, "macos_app_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    return PROTOCOL_OK;
}

static protocol_status_t parse_metric(const cJSON *metric, const char *expected_unit,
                                      const char *expected_source, double minimum,
                                      double maximum, bool exclusive_bounds,
                                      protocol_metric_t *output,
                                      char *error, size_t error_capacity)
{
    if (!cJSON_IsObject(metric)) {
        set_error(error, error_capacity, "metric_missing");
        return PROTOCOL_ERROR_SCHEMA;
    }

    const cJSON *valid = required_item(metric, "valid");
    const cJSON *value = required_item(metric, "value");
    const cJSON *unit = required_item(metric, "unit");
    const cJSON *source = required_item(metric, "source");
    const cJSON *metric_error = required_item(metric, "error");
    const cJSON *sampled = required_item(metric, "sampled_at_milliseconds");

    if (!cJSON_IsBool(valid) || (!cJSON_IsNumber(value) && !cJSON_IsNull(value)) ||
        !cJSON_IsString(unit) || unit->valuestring == NULL ||
        !cJSON_IsString(source) || source->valuestring == NULL ||
        (!cJSON_IsNull(metric_error) && !cJSON_IsString(metric_error)) ||
        (!cJSON_IsNull(sampled) && !cJSON_IsNumber(sampled))) {
        set_error(error, error_capacity, "metric_field_type");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if (strcmp(unit->valuestring, expected_unit) != 0 ||
        strcmp(source->valuestring, expected_source) != 0) {
        set_error(error, error_capacity, "metric_unit_or_source");
        return PROTOCOL_ERROR_SCHEMA;
    }

    double sampled_value = 0;
    if (!cJSON_IsNull(sampled) &&
        !json_integer(sampled, 0, MAX_EXACT_JSON_INTEGER, &sampled_value)) {
        set_error(error, error_capacity, "sampled_at_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    output->sampled_at_milliseconds = cJSON_IsNull(sampled) ? 0 : (int64_t)sampled_value;
    output->valid = false;
    output->value = 0;
    output->error[0] = '\0';

    if (cJSON_IsFalse(valid)) {
        if (!cJSON_IsNull(value) || !cJSON_IsString(metric_error) ||
            !copy_bounded(output->error, sizeof(output->error), metric_error->valuestring)) {
            set_error(error, error_capacity, "invalid_metric_encoding");
            return PROTOCOL_ERROR_SCHEMA;
        }
        return PROTOCOL_OK;
    }

    if (!cJSON_IsNull(metric_error)) {
        set_error(error, error_capacity, "valid_metric_has_error");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if (cJSON_IsNull(value)) {
        snprintf(output->error, sizeof(output->error), "value_missing");
        return PROTOCOL_OK;
    }
    const bool outside = exclusive_bounds
        ? value->valuedouble <= minimum || value->valuedouble >= maximum
        : value->valuedouble < minimum || value->valuedouble > maximum;
    if (!isfinite(value->valuedouble) || outside) {
        snprintf(output->error, sizeof(output->error), "value_out_of_range");
        return PROTOCOL_OK;
    }
    if (cJSON_IsNull(sampled)) {
        snprintf(output->error, sizeof(output->error), "sample_time_missing");
        return PROTOCOL_OK;
    }
    output->valid = true;
    output->value = value->valuedouble;
    return PROTOCOL_OK;
}

static protocol_status_t parse_telemetry(const cJSON *root, protocol_telemetry_t *telemetry,
                                         char *error, size_t error_capacity)
{
    protocol_status_t status = parse_identity(root, &telemetry->platform, telemetry->device_id,
                                              sizeof(telemetry->device_id), error, error_capacity);
    if (status != PROTOCOL_OK) return status;

    double sequence = 0;
    if (!json_integer(required_item(root, "sequence"), 0, MAX_EXACT_JSON_INTEGER, &sequence)) {
        set_error(error, error_capacity, "sequence_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    telemetry->sequence = (uint64_t)sequence;

    double timestamp = 0;
    if (!json_integer(required_item(root, "timestamp_milliseconds"), 0,
                      MAX_EXACT_JSON_INTEGER, &timestamp)) {
        set_error(error, error_capacity, "timestamp_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    telemetry->timestamp_milliseconds = (int64_t)timestamp;

    const cJSON *metrics = required_item(root, "metrics");
    if (!cJSON_IsObject(metrics)) {
        set_error(error, error_capacity, "metrics_missing");
        return PROTOCOL_ERROR_SCHEMA;
    }
    status = parse_metric(required_item(metrics, "cpu.temperature.average"), "celsius",
                          "Average CPU", 0, 110, true,
                          &telemetry->cpu_temperature, error, error_capacity);
    if (status != PROTOCOL_OK) return status;
    return parse_metric(required_item(metrics, "memory.usage"), "percent",
                        "Stats RAM_Usage", 0, 100, false, &telemetry->memory_usage,
                        error, error_capacity);
}

static protocol_status_t parse_command_result(const cJSON *root,
                                              protocol_command_result_t *result,
                                              char *error, size_t error_capacity)
{
    const cJSON *device = required_item(root, "device_id");
    const cJSON *request = required_item(root, "request_id");
    const cJSON *success = required_item(root, "success");
    const cJSON *write = required_item(root, "write_succeeded");
    const cJSON *confirmed = required_item(root, "confirmed");
    const cJSON *requested = required_item(root, "requested_input");
    const cJSON *read_back = required_item(root, "read_back_input");
    const cJSON *result_error = required_item(root, "error");

    if (!cJSON_IsString(device) ||
        !copy_bounded(result->device_id, sizeof(result->device_id), device->valuestring) ||
        !cJSON_IsString(request) ||
        !copy_bounded(result->request_id, sizeof(result->request_id), request->valuestring) ||
        !cJSON_IsBool(success) || !cJSON_IsBool(write) || !cJSON_IsBool(confirmed) ||
        (!cJSON_IsNull(read_back) && !cJSON_IsNumber(read_back)) ||
        (!cJSON_IsNull(result_error) && !cJSON_IsString(result_error))) {
        set_error(error, error_capacity, "command_result_field_type");
        return PROTOCOL_ERROR_SCHEMA;
    }

    double requested_value = 0;
    if (!json_integer(requested, INT32_MIN, INT32_MAX, &requested_value)) {
        set_error(error, error_capacity, "requested_input_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    result->requested_input = (int)requested_value;
    result->read_back_input_valid = !cJSON_IsNull(read_back);
    if (result->read_back_input_valid) {
        double read_value = 0;
        if (!json_integer(read_back, INT32_MIN, INT32_MAX, &read_value)) {
            set_error(error, error_capacity, "read_back_input_invalid");
            return PROTOCOL_ERROR_SCHEMA;
        }
        result->read_back_input = (int)read_value;
    }
    result->success = cJSON_IsTrue(success);
    result->write_succeeded = cJSON_IsTrue(write);
    result->confirmed = cJSON_IsTrue(confirmed);
    result->error[0] = '\0';
    if (cJSON_IsString(result_error) &&
        !copy_bounded(result->error, sizeof(result->error), result_error->valuestring)) {
        set_error(error, error_capacity, "command_result_error_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if (result->success != result->write_succeeded) {
        set_error(error, error_capacity, "command_result_success_mismatch");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if (result->confirmed) {
        if (!result->write_succeeded || !result->read_back_input_valid ||
            result->read_back_input != result->requested_input || !cJSON_IsNull(result_error)) {
            set_error(error, error_capacity, "command_result_confirmation_mismatch");
            return PROTOCOL_ERROR_SCHEMA;
        }
    } else if (!cJSON_IsString(result_error) || result->error[0] == '\0') {
        set_error(error, error_capacity, "command_result_error_required");
        return PROTOCOL_ERROR_SCHEMA;
    }
    return PROTOCOL_OK;
}

bool protocol_is_valid_utf8(const uint8_t *data, size_t length)
{
    if (data == NULL && length != 0) return false;
    for (size_t i = 0; i < length;) {
        uint8_t byte = data[i];
        if (byte <= 0x7F) {
            ++i;
            continue;
        }
        size_t extra = 0;
        uint32_t codepoint = 0;
        if ((byte & 0xE0) == 0xC0) { extra = 1; codepoint = byte & 0x1F; }
        else if ((byte & 0xF0) == 0xE0) { extra = 2; codepoint = byte & 0x0F; }
        else if ((byte & 0xF8) == 0xF0) { extra = 3; codepoint = byte & 0x07; }
        else return false;
        if (i + extra >= length) return false;
        for (size_t j = 1; j <= extra; ++j) {
            if ((data[i + j] & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (data[i + j] & 0x3F);
        }
        if ((extra == 1 && codepoint < 0x80) || (extra == 2 && codepoint < 0x800) ||
            (extra == 3 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return false;
        i += extra + 1;
    }
    return true;
}

protocol_status_t protocol_parse_message(const char *payload, size_t length,
                                         protocol_message_t *message,
                                         char *error, size_t error_capacity)
{
    if (payload == NULL || message == NULL || length == 0) {
        set_error(error, error_capacity, "empty_message");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if (length > PROTOCOL_MAX_MESSAGE_BYTES) {
        set_error(error, error_capacity, "message_too_long");
        return PROTOCOL_ERROR_TOO_LONG;
    }
    if (!protocol_is_valid_utf8((const uint8_t *)payload, length)) {
        set_error(error, error_capacity, "invalid_utf8");
        return PROTOCOL_ERROR_INVALID_UTF8;
    }

    memset(message, 0, sizeof(*message));
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(payload, length, &parse_end, false);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        set_error(error, error_capacity, "invalid_json");
        return PROTOCOL_ERROR_INVALID_JSON;
    }
    while (parse_end < payload + length &&
           (*parse_end == ' ' || *parse_end == '\t' || *parse_end == '\r' || *parse_end == '\n')) {
        ++parse_end;
    }
    if (parse_end != payload + length) {
        cJSON_Delete(root);
        set_error(error, error_capacity, "trailing_json_data");
        return PROTOCOL_ERROR_INVALID_JSON;
    }

    protocol_status_t status = parse_version(root, error, error_capacity);
    if (status != PROTOCOL_OK) {
        cJSON_Delete(root);
        return status;
    }
    const cJSON *type = required_item(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == NULL) {
        cJSON_Delete(root);
        set_error(error, error_capacity, "type_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }

    if (strcmp(type->valuestring, "hello") == 0) {
        message->type = PROTOCOL_MESSAGE_HELLO;
        status = parse_hello(root, &message->data.hello, error, error_capacity);
    } else if (strcmp(type->valuestring, "telemetry") == 0) {
        message->type = PROTOCOL_MESSAGE_TELEMETRY;
        status = parse_telemetry(root, &message->data.telemetry, error, error_capacity);
    } else if (strcmp(type->valuestring, "command_result") == 0) {
        message->type = PROTOCOL_MESSAGE_COMMAND_RESULT;
        status = parse_command_result(root, &message->data.command_result, error, error_capacity);
    } else {
        set_error(error, error_capacity, "unsupported_message_type");
        status = PROTOCOL_ERROR_SCHEMA;
    }
    cJSON_Delete(root);
    return status;
}

protocol_status_t protocol_session_accept(protocol_session_t *session,
                                          const protocol_message_t *message,
                                          char *error, size_t error_capacity)
{
    if (session == NULL || message == NULL) {
        set_error(error, error_capacity, "session_invalid");
        return PROTOCOL_ERROR_SCHEMA;
    }
    if (message->type == PROTOCOL_MESSAGE_HELLO) {
        memset(session, 0, sizeof(*session));
        session->hello_received = true;
        session->platform = message->data.hello.platform;
        snprintf(session->device_id, sizeof(session->device_id), "%s",
                 message->data.hello.device_id);
        return PROTOCOL_OK;
    }
    if (!session->hello_received) {
        set_error(error, error_capacity, "hello_required");
        return PROTOCOL_ERROR_HELLO_REQUIRED;
    }
    if (message->type == PROTOCOL_MESSAGE_TELEMETRY) {
        const protocol_telemetry_t *telemetry = &message->data.telemetry;
        if (telemetry->platform != session->platform ||
            strcmp(telemetry->device_id, session->device_id) != 0) {
            set_error(error, error_capacity, "identity_mismatch");
            return PROTOCOL_ERROR_IDENTITY_MISMATCH;
        }
        if (session->has_sequence && telemetry->sequence <= session->last_sequence) {
            set_error(error, error_capacity, "sequence_not_increasing");
            return PROTOCOL_ERROR_SEQUENCE;
        }
        session->has_sequence = true;
        session->last_sequence = telemetry->sequence;
        return PROTOCOL_OK;
    }
    if (strcmp(message->data.command_result.device_id, session->device_id) != 0) {
        set_error(error, error_capacity, "identity_mismatch");
        return PROTOCOL_ERROR_IDENTITY_MISMATCH;
    }
    return PROTOCOL_OK;
}

bool protocol_build_switch_command(char *output, size_t output_capacity,
                                   const char *request_id, const char *target,
                                   const char *target_device_id)
{
    if (output == NULL || output_capacity == 0 || request_id == NULL || target == NULL ||
        target_device_id == NULL || request_id[0] == '\0' || target_device_id[0] == '\0' ||
        strlen(request_id) > PROTOCOL_REQUEST_ID_MAX ||
        strlen(target_device_id) > PROTOCOL_DEVICE_ID_MAX ||
        (strcmp(target, "mac") != 0 && strcmp(target, "windows") != 0)) return false;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return false;
    bool ok = cJSON_AddStringToObject(root, "action", "switch_display") != NULL &&
              cJSON_AddNumberToObject(root, "protocol_version", PROTOCOL_VERSION) != NULL &&
              cJSON_AddStringToObject(root, "request_id", request_id) != NULL &&
              cJSON_AddStringToObject(root, "target", target) != NULL &&
              cJSON_AddStringToObject(root, "target_device_id", target_device_id) != NULL &&
              cJSON_AddStringToObject(root, "type", "command") != NULL &&
              cJSON_PrintPreallocated(root, output, (int)output_capacity, false);
    cJSON_Delete(root);
    return ok;
}

const char *protocol_status_name(protocol_status_t status)
{
    switch (status) {
        case PROTOCOL_OK: return "ok";
        case PROTOCOL_ERROR_TOO_LONG: return "too_long";
        case PROTOCOL_ERROR_INVALID_UTF8: return "invalid_utf8";
        case PROTOCOL_ERROR_INVALID_JSON: return "invalid_json";
        case PROTOCOL_ERROR_SCHEMA: return "schema";
        case PROTOCOL_ERROR_UNSUPPORTED_VERSION: return "unsupported_version";
        case PROTOCOL_ERROR_UNSUPPORTED_PLATFORM: return "unsupported_platform";
        case PROTOCOL_ERROR_HELLO_REQUIRED: return "hello_required";
        case PROTOCOL_ERROR_IDENTITY_MISMATCH: return "identity_mismatch";
        case PROTOCOL_ERROR_SEQUENCE: return "sequence";
        default: return "unknown";
    }
}
