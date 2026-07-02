#define DT_DRV_COMPAT keyball_input_processor_trackball_arrows

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <drivers/input_processor.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/virtual_key_position.h>

struct trackball_arrows_config {
    int16_t tick;
    uint16_t tap_ms;
    uint16_t wait_ms;
    struct zmk_behavior_binding bindings[4];
};

struct trackball_arrows_data {
    int16_t x;
    int16_t y;
    int64_t next_trigger_at;
};

static int trackball_arrows_handle_event(const struct device *dev, struct input_event *event,
                                         uint32_t param1, uint32_t param2,
                                         struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const struct trackball_arrows_config *config = dev->config;
    struct trackball_arrows_data *data = dev->data;

    if (event->code == INPUT_REL_X) {
        data->x += event->value;
    } else {
        data->y += event->value;
    }

    int64_t now = k_uptime_get();
    if (now < data->next_trigger_at) {
        return ZMK_INPUT_PROC_STOP;
    }

    int binding_index = -1;
    if (abs(data->x) > config->tick) {
        binding_index = data->x > 0 ? 0 : 1;
    } else if (abs(data->y) > config->tick) {
        binding_index = data->y > 0 ? 3 : 2;
    }

    if (binding_index >= 0) {
        struct zmk_behavior_binding_event behavior_event = {
            .position = ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
                state->input_device_index, binding_index),
            .timestamp = now,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
        };

        int ret = zmk_behavior_queue_add(&behavior_event, config->bindings[binding_index], true,
                                         config->tap_ms);
        if (ret >= 0) {
            ret = zmk_behavior_queue_add(&behavior_event, config->bindings[binding_index], false,
                                         config->wait_ms);
        }

        data->x = 0;
        data->y = 0;
        data->next_trigger_at = now + config->tap_ms + config->wait_ms;
    }

    return ZMK_INPUT_PROC_STOP;
}

static struct zmk_input_processor_driver_api trackball_arrows_api = {
    .handle_event = trackball_arrows_handle_event,
};

static int trackball_arrows_init(const struct device *dev) { return 0; }

#define TRACKBALL_ARROWS_BINDING(i, n)                                                            \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, i)),                   \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, i, param1), (0),           \
                              (DT_INST_PHA_BY_IDX(n, bindings, i, param1))),                      \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, i, param2), (0),           \
                              (DT_INST_PHA_BY_IDX(n, bindings, i, param2))),                      \
    }

#define TRACKBALL_ARROWS_INST(n)                                                                  \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == 4,                                              \
                 "trackball arrows requires four bindings: right, left, up, down");                \
    static const struct trackball_arrows_config trackball_arrows_config_##n = {                    \
        .tick = DT_INST_PROP(n, tick),                                                            \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                        \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                      \
        .bindings = {LISTIFY(4, TRACKBALL_ARROWS_BINDING, (, ), n)},                              \
    };                                                                                             \
    static struct trackball_arrows_data trackball_arrows_data_##n;                                 \
    DEVICE_DT_INST_DEFINE(n, &trackball_arrows_init, NULL, &trackball_arrows_data_##n,             \
                          &trackball_arrows_config_##n, POST_KERNEL,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &trackball_arrows_api);

DT_INST_FOREACH_STATUS_OKAY(TRACKBALL_ARROWS_INST)
