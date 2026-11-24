/*
 * Split BLE peripheral transport that connects to any bonded central (dongle).
 *
 * Stock ZMK peripherals advertise *directed* at the single central they bonded
 * with first, so a half can only ever talk to one dongle. This transport
 * replaces only the advertising/connection policy: it always advertises
 * undirected with the split service UUID, so whichever dongle is powered and in
 * range connects. Dongles that already bonded with this half reconnect using
 * their stored keys; an unpaired dongle with free peripheral slots pairs.
 *
 * The GATT split service (ZMK's split/bluetooth/service.c) is reused as-is, so
 * dongles run unmodified ZMK. The stock BLE peripheral transport is still
 * compiled but never enabled, because this transport registers with a better
 * (lower) priority and ZMK enables only the first available transport.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/split/bluetooth/uuid.h>
#include <zmk/split/transport/peripheral.h>
#include <zmk/split/transport/types.h>

LOG_MODULE_REGISTER(split_ble_any_central, CONFIG_ZMK_LOG_LEVEL);

/* Defined in ZMK's app/src/split/bluetooth/service.c (private header). */
int zmk_split_transport_peripheral_bt_report_event(
    const struct zmk_split_transport_peripheral_event *ev);

static const struct bt_data adv_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_SOME, 0x0f, 0x18 /* Battery Service */),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, ZMK_SPLIT_BT_SERVICE_UUID),
};

/* Fast advertising for quick (re)connects, then slow to save battery. */
static const struct bt_le_adv_param adv_fast = BT_LE_ADV_PARAM_INIT(
    BT_LE_ADV_OPT_CONN, BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);
static const struct bt_le_adv_param adv_slow = BT_LE_ADV_PARAM_INIT(
    BT_LE_ADV_OPT_CONN, BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL);

static bool enabled;
static bool settings_loaded;
static bool advertising;
static bool advertising_slow;
static struct bt_conn *central_conn;

static zmk_split_transport_peripheral_status_changed_cb_t status_cb;

static void stop_advertising(void) {
    if (advertising) {
        int err = bt_le_adv_stop();
        if (err < 0) {
            LOG_WRN("Failed to stop advertising (%d)", err);
        }
        advertising = false;
    }
}

static void start_advertising(bool slow) {
    if (!enabled || central_conn) {
        return;
    }

    stop_advertising();

    int err = bt_le_adv_start(slow ? &adv_slow : &adv_fast, adv_data, ARRAY_SIZE(adv_data),
                              NULL, 0);
    if (err < 0) {
        LOG_ERR("Failed to start %s advertising (%d)", slow ? "slow" : "fast", err);
        return;
    }

    advertising = true;
    advertising_slow = slow;
    LOG_DBG("Advertising (%s) for any central", slow ? "slow" : "fast");
}

static void adv_fast_work_cb(struct k_work *work);
static void adv_slow_work_cb(struct k_work *work);
static K_WORK_DEFINE(adv_fast_work, adv_fast_work_cb);
static K_WORK_DELAYABLE_DEFINE(adv_slow_work, adv_slow_work_cb);

static void adv_fast_work_cb(struct k_work *work) {
    start_advertising(false);
    if (advertising) {
        k_work_reschedule(&adv_slow_work, K_SECONDS(CONFIG_ZMK_SPLIT_BLE_ANY_CENTRAL_FAST_ADV_S));
    }
}

static void adv_slow_work_cb(struct k_work *work) {
    if (advertising && !advertising_slow) {
        start_advertising(true);
    }
}

static struct zmk_split_transport_status get_status(void) {
    return (struct zmk_split_transport_status){
        .available = settings_loaded,
        .enabled = enabled,
        .connections = central_conn ? ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_ALL_CONNECTED
                                    : ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_DISCONNECTED,
    };
}

static void notify_status_work_cb(struct k_work *work);
static K_WORK_DEFINE(notify_status_work, notify_status_work_cb);

static bool is_peripheral_role(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL;
}

static void connected(struct bt_conn *conn, uint8_t err) {
    if (!is_peripheral_role(conn)) {
        return;
    }

    /* Connectable advertising stops by itself once a connection is created. */
    advertising = false;
    k_work_cancel_delayable(&adv_slow_work);

    if (err) {
        LOG_WRN("Central connection failed (0x%02x)", err);
        k_work_submit(&adv_fast_work);
        return;
    }

    if (!central_conn) {
        central_conn = bt_conn_ref(conn);
    }

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected to central %s", addr);

    k_work_submit(&notify_status_work);
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    if (conn != central_conn) {
        return;
    }

    LOG_INF("Central disconnected (reason 0x%02x)", reason);

    bt_conn_unref(central_conn);
    central_conn = NULL;

    k_work_submit(&notify_status_work);
}

/* Called once the controller has freed the connection; safe to advertise again. */
static void recycled(void) {
    if (enabled && !central_conn) {
        k_work_submit(&adv_fast_work);
    }
}

BT_CONN_CB_DEFINE(any_central_conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = recycled,
};

static int set_enabled(bool en) {
    enabled = en;

    if (en) {
        k_work_submit(&adv_fast_work);
        return 0;
    }

    k_work_cancel_delayable(&adv_slow_work);
    stop_advertising();

    if (central_conn) {
        int err = bt_conn_disconnect(central_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        if (err < 0) {
            LOG_WRN("Failed to disconnect from central (%d)", err);
        }
    }

    return 0;
}

static int set_status_callback(zmk_split_transport_peripheral_status_changed_cb_t cb) {
    status_cb = cb;
    return 0;
}

static const struct zmk_split_transport_peripheral_api any_central_api = {
    .report_event = zmk_split_transport_peripheral_bt_report_event,
    .set_enabled = set_enabled,
    .set_status_callback = set_status_callback,
    .get_status = get_status,
};

ZMK_SPLIT_TRANSPORT_PERIPHERAL_REGISTER(bt_any_central, &any_central_api,
                                        CONFIG_ZMK_SPLIT_BLE_ANY_CENTRAL_PRIORITY);

static void notify_status_work_cb(struct k_work *work) {
    if (status_cb) {
        status_cb(&bt_any_central, get_status());
    }
}

/* Only advertise once bonds/identity are loaded from settings. */
static int any_central_settings_commit(void) {
    settings_loaded = true;
    k_work_submit(&notify_status_work);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(split_ble_any_central, "split_any_central", NULL, NULL,
                               any_central_settings_commit, NULL);
