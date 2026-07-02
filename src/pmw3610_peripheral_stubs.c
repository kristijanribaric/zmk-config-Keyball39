#include <errno.h>

#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>

zmk_keymap_layer_index_t zmk_keymap_highest_layer_active(void) { return 0; }

int zmk_behavior_queue_add(const struct zmk_behavior_binding_event *event,
                           const struct zmk_behavior_binding behavior, bool press, uint32_t wait) {
    return -ENOTSUP;
}
