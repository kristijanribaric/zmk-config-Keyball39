#define DT_DRV_COMPAT keyball_input_processor_trackball_arrows

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <drivers/input_processor.h>
#include <zmk/events/keycode_state_changed.h>

struct trackball_arrows_config {
    int16_t tick;
    uint16_t tap_ms;
    uint16_t wait_ms;
    zmk_key_t key_codes[4];
};

struct trackball_arrows_data {
    int16_t x;
    int16_t y;
    const struct device *dev;
    struct k_work tap_work;
    struct k_work_delayable release_work;
    uint8_t key_index;
    bool active;
};

static void trackball_arrows_release_work(struct k_work *work) {
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct trackball_arrows_data *data =
        CONTAINER_OF(delayable, struct trackball_arrows_data, release_work);
    const struct trackball_arrows_config *config = data->dev->config;

    raise_zmk_keycode_state_changed_from_encoded(config->key_codes[data->key_index], false,
                                                 k_uptime_get());
    data->active = false;
}

static void trackball_arrows_tap_work(struct k_work *work) {
    struct trackball_arrows_data *data = CONTAINER_OF(work, struct trackball_arrows_data, tap_work);
    const struct trackball_arrows_config *config = data->dev->config;

    raise_zmk_keycode_state_changed_from_encoded(config->key_codes[data->key_index], true,
                                                 k_uptime_get());
    k_work_schedule(&data->release_work, K_MSEC(config->tap_ms + config->wait_ms));
}

static int trackball_arrows_handle_event(const struct device *dev, struct input_event *event,
                                         uint32_t param1, uint32_t param2,
                                         struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const struct trackball_arrows_config *config = dev->config;
    struct trackball_arrows_data *data = dev->data;

    if (data->active) {
        data->x = 0;
        data->y = 0;
        event->value = 0;
        return ZMK_INPUT_PROC_STOP;
    }

    if (event->code == INPUT_REL_X) {
        data->x += event->value;
    } else {
        data->y += event->value;
    }

    event->value = 0;

    int binding_index = -1;
    if (abs(data->x) > config->tick) {
        binding_index = data->x > 0 ? 0 : 1;
    } else if (abs(data->y) > config->tick) {
        binding_index = data->y > 0 ? 3 : 2;
    }

    if (binding_index >= 0) {
        data->key_index = binding_index;
        data->active = true;
        k_work_submit(&data->tap_work);

        data->x = 0;
        data->y = 0;
    }

    return ZMK_INPUT_PROC_STOP;
}

static struct zmk_input_processor_driver_api trackball_arrows_api = {
    .handle_event = trackball_arrows_handle_event,
};

static int trackball_arrows_init(const struct device *dev) {
    struct trackball_arrows_data *data = dev->data;

    data->dev = dev;
    k_work_init(&data->tap_work, trackball_arrows_tap_work);
    k_work_init_delayable(&data->release_work, trackball_arrows_release_work);

    return 0;
}

#define TRACKBALL_ARROWS_KEY_CODE(i, n) DT_INST_PROP_BY_IDX(n, key_codes, i)

#define TRACKBALL_ARROWS_INST(n)                                                                  \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, key_codes) == 4,                                             \
                 "trackball arrows requires four key codes: right, left, up, down");               \
    static const struct trackball_arrows_config trackball_arrows_config_##n = {                    \
        .tick = DT_INST_PROP(n, tick),                                                            \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                        \
        .wait_ms = DT_INST_PROP(n, wait_ms),                                                      \
        .key_codes = {LISTIFY(4, TRACKBALL_ARROWS_KEY_CODE, (, ), n)},                            \
    };                                                                                             \
    static struct trackball_arrows_data trackball_arrows_data_##n;                                 \
    DEVICE_DT_INST_DEFINE(n, &trackball_arrows_init, NULL, &trackball_arrows_data_##n,             \
                          &trackball_arrows_config_##n, POST_KERNEL,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &trackball_arrows_api);

DT_INST_FOREACH_STATUS_OKAY(TRACKBALL_ARROWS_INST)
