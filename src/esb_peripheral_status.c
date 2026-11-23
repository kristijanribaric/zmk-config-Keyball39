/*
 * ESB stand-in for ZMK's BLE peripheral connection API.
 *
 * The stock nice!view peripheral widget calls zmk_split_bt_peripheral_is_connected()
 * and listens for zmk_split_peripheral_status_changed, both of which come from the
 * BLE split transport. With BLE disabled, provide them from the ESB link status.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/event_manager.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk_split_esb.h>

#define ESB_STATUS_POLL_MS 500

static bool last_connected;

bool zmk_split_bt_peripheral_is_connected(void) {
    struct zmk_split_esb_status status;
    zmk_split_esb_get_status(&status);
    return !status.searching && status.rssi_dbm != 0;
}

bool zmk_split_bt_peripheral_is_bonded(void) { return true; }

static void esb_status_poll(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(esb_status_work, esb_status_poll);

static void esb_status_poll(struct k_work *work) {
    bool connected = zmk_split_bt_peripheral_is_connected();

    if (connected != last_connected) {
        last_connected = connected;
        raise_zmk_split_peripheral_status_changed(
            (struct zmk_split_peripheral_status_changed){.connected = connected});
    }

    k_work_schedule(&esb_status_work, K_MSEC(ESB_STATUS_POLL_MS));
}

static int esb_status_init(void) {
    k_work_schedule(&esb_status_work, K_MSEC(ESB_STATUS_POLL_MS));
    return 0;
}

SYS_INIT(esb_status_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
