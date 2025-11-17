#include "bluetooth.hpp"

#include <nvs_flash.h>
#include <esp_log.h>

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

#include <esp_bt.h>
#include "host/ble_sm.h"

#define TAG "BLE"
#define DEVICE_NAME "G-Watch"
#define DEVICE_APPEARANCE 0xC0 // Generic Watch Appearance

#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

static const ble_uuid128_t gadgetbridge_service_uuid =
    BLE_UUID128_INIT(
        0x6E, 0x40, 0x00, 0x01,
        0xB5, 0xA3, 0xF3, 0x93,
        0xE0, 0xA9, 0xE5, 0x0E,
        0x24, 0xDC, 0xCA, 0x9E);

static const ble_uuid128_t gadgetbridge_char_uuid_rx =
    BLE_UUID128_INIT(
        0x6E, 0x40, 0x00, 0x02,
        0xB5, 0xA3, 0xF3, 0x93,
        0xE0, 0xA9, 0xE5, 0x0E,
        0x24, 0xDC, 0xCA, 0x9E);

static const ble_uuid128_t gadgetbridge_char_uuid_tx =
    BLE_UUID128_INIT(
        0x6E, 0x40, 0x00, 0x03,
        0xB5, 0xA3, 0xF3, 0x93,
        0xE0, 0xA9, 0xE5, 0x0E,
        0x24, 0xDC, 0xCA, 0x9E);

static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};

static uint8_t custom_value[20] = "Hello BLE";

inline static void format_addr(char *addr_str, uint8_t addr[])
{
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static void print_conn_desc(struct ble_gap_conn_desc *desc)
{
    char addr_str[18] = {0};
    ESP_LOGI(TAG, "connection handle: %d", desc->conn_handle);

    format_addr(addr_str, desc->our_id_addr.val);
    ESP_LOGI(TAG, "device id address: type=%d, value=%s",
             desc->our_id_addr.type, addr_str);

    format_addr(addr_str, desc->peer_id_addr.val);
    ESP_LOGI(TAG, "peer id address: type=%d, value=%s",
             desc->peer_id_addr.type, addr_str);

    ESP_LOGI(TAG,
             "conn_itvl=%d, conn_latency=%d, supervision_timeout=%d, "
             "encrypted=%d, authenticated=%d, bonded=%d",
             desc->conn_itvl, desc->conn_latency, desc->supervision_timeout,
             desc->sec_state.encrypted, desc->sec_state.authenticated,
             desc->sec_state.bonded);
}

/* Forward declaration */
static void start_advertising(void);

/* Called on authentication complete */
static void authentication_complete(struct ble_gap_conn_desc *desc)
{
    ESP_LOGI(TAG, "Authentication complete for conn_handle=%d", desc->conn_handle);
    print_conn_desc(desc);
}

/* Gap event handler with passkey, repeat pairing, and encryption handling */
static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    int rc = 0;
    struct ble_gap_conn_desc desc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0)
        {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "failed to find connection by handle, error code: %d", rc);
                return rc;
            }

            print_conn_desc(&desc);

            struct ble_gap_upd_params params = {
                .itvl_min = desc.conn_itvl,
                .itvl_max = desc.conn_itvl,
                .latency = 3,
                .supervision_timeout = desc.supervision_timeout};
            rc = ble_gap_update_params(event->connect.conn_handle, &params);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "failed to update connection parameters, error code: %d", rc);
                return rc;
            }
        }
        else
        {
            start_advertising();
        }
        return rc;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected from peer; reason=%d", event->disconnect.reason);
        start_advertising();
        return rc;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "connection updated; status=%d", event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc == 0)
            print_conn_desc(&desc);
        return rc;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
    {
        struct ble_sm_io io = {0};
        uint32_t passkey = 123456; // default or generate dynamically

        ESP_LOGI(TAG, "PASSKEY_ACTION: action=%d", event->passkey.params.action);

        switch (event->passkey.params.action)
        {
        case BLE_SM_IOACT_DISP:
            io.action = BLE_SM_IOACT_DISP;
            io.passkey = passkey;
            ESP_LOGI(TAG, "Display passkey: %06u", io.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &io);
            break;

        case BLE_SM_IOACT_NUMCMP:
            io.action = BLE_SM_IOACT_NUMCMP;
            io.numcmp_accept = 1; // accept automatically
            ESP_LOGI(TAG, "Numeric compare: accepting");
            ble_sm_inject_io(event->passkey.conn_handle, &io);
            break;

        case BLE_SM_IOACT_INPUT:
            io.action = BLE_SM_IOACT_INPUT;
            io.passkey = passkey; // in real use, get user input
            ESP_LOGI(TAG, "Input passkey: %06u", io.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &io);
            break;

        default:
            ESP_LOGE(TAG, "Unhandled passkey action");
            break;
        }
        return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        if (rc == 0 && desc.sec_state.authenticated)
            authentication_complete(&desc);
        return rc;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0)
        {
            ble_store_util_delete_peer(&desc.peer_id_addr);
            ESP_LOGI(TAG, "Deleted old pairing info for peer, retrying pairing");
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        return BLE_GAP_REPEAT_PAIRING_IGNORE;

    default:
        break;
    }

    return rc;
}

static void start_advertising(void)
{
    int rc = 0;
    const char *name;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params adv_params = {0};

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    name = ble_svc_gap_device_name();
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    adv_fields.tx_pwr_lvl_is_present = 1;

    adv_fields.appearance = DEVICE_APPEARANCE;
    adv_fields.appearance_is_present = 1;

    adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
    adv_fields.le_role_is_present = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
        return;
    }

    rsp_fields.device_addr = addr_val;
    rsp_fields.device_addr_type = own_addr_type;
    rsp_fields.device_addr_is_present = 1;

    rsp_fields.adv_itvl = BLE_GAP_ADV_ITVL_MS(500);
    rsp_fields.adv_itvl_is_present = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set scan response data, error code: %d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(500);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(510);

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to start advertising, error code: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising started!");
}

static void on_stack_reset(int reason)
{
    ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void)
{
    int rc = 0;
    char addr_str[18] = {0};

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "device does not have any available bt address!");
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to infer address type, error code: %d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to copy device address, error code: %d", rc);
        return;
    }
    format_addr(addr_str, addr_val);

    start_advertising();
}

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "nimble host task has been started!");
    nimble_port_run();
    vTaskDelete(NULL);
}

static int gadgetbridge_tx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        os_mbuf_append(ctxt->om, custom_value, strlen((char *)custom_value));
        return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ble_hs_mbuf_to_flat(ctxt->om, custom_value, sizeof(custom_value), NULL);
        ESP_LOGI(TAG, "Gadgetbridge TX updated: %s", custom_value);
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static int gadgetbridge_rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        os_mbuf_append(ctxt->om, custom_value, strlen((char *)custom_value));
        return 0;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ble_hs_mbuf_to_flat(ctxt->om, custom_value, sizeof(custom_value), NULL);
        ESP_LOGI(TAG, "Gadgetbridge RX updated: %s", custom_value);
        return 0;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def gadgetbridge_tx_gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &gadgetbridge_service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {
             .uuid = &gadgetbridge_char_uuid_tx.u,
             .access_cb = gadgetbridge_tx_access_cb,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
         },
         {0}}},
    {0}};

static const struct ble_gatt_svc_def gadgetbridge_rx_gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &gadgetbridge_service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {
             .uuid = &gadgetbridge_char_uuid_rx.u,
             .access_cb = gadgetbridge_rx_access_cb,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
         },
         {0}}},
    {0}};

void ble_init(void)
{
    int rc = 0;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ", ret);
        return;
    }

    esp_err_t power_rc = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_N12);
    if (power_rc != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to set TX power, rc=%d", power_rc);
    }

    ble_svc_gap_init();
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set device name to %s, error code: %d",
                 DEVICE_NAME, rc);
        return;
    }
    rc = ble_svc_gap_device_appearance_set(DEVICE_APPEARANCE);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set device appearance, error code: %d", rc);
        return;
    }

    /* Security settings */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    rc = ble_gatts_count_cfg(gadgetbridge_tx_gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_count_cfg(gadgetbridge_rx_gatt_svcs);
    assert(rc == 0);

    rc = ble_gatts_add_svcs(gadgetbridge_tx_gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gadgetbridge_rx_gatt_svcs);
    assert(rc == 0);

    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);
}
