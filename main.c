/*
	Copyright 2019 Benjamin Vedder	benjamin@vedder.se

	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "nordic_common.h"
#include "nrf.h"
#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_ble.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "app_timer.h"
#include "ble_nus.h"
#include "app_uart.h"
#include "app_util_platform.h"
#include "nrf_pwr_mgmt.h"
#include "bsp_btn_ble.h"

#if defined (UART_PRESENT)
#include "nrf_uart.h"
#endif
#if defined (UARTE_PRESENT)
#include "nrf_uarte.h"
#endif

#ifdef NRF52840_XXAA
#include "app_usbd_core.h"
#include "app_usbd.h"
#include "app_usbd_string_desc.h"
#include "app_usbd_cdc_acm.h"
#include "app_usbd_serial_num.h"
#include "nrf_drv_power.h"
#include "nrf_drv_usbd.h"
#include "nrf_drv_clock.h"
#include "boards.h"
#endif

#include "packet.h"
#include "buffer.h"
#include "datatypes.h"
#include "esb_timeslot.h"
#include "crc.h"

#ifndef MODULE_BUILTIN
#define MODULE_BUILTIN					0
#endif

#define APP_BLE_CONN_CFG_TAG            1                                           /**< A tag identifying the SoftDevice BLE configuration. */

#ifdef NRF52840_XXAA
#define DEVICE_NAME                     "VESC 52840"
#else
#if MODULE_BUILTIN
#define DEVICE_NAME                     "VESC 52832 BUILTIN"
#else
#define DEVICE_NAME                     "VESC 52832 UART"
#endif
#endif

#define NUS_SERVICE_UUID_TYPE           BLE_UUID_TYPE_VENDOR_BEGIN                  /**< UUID type for the Nordic UART Service (vendor specific). */

#define APP_BLE_OBSERVER_PRIO           3                                           /**< Application's BLE observer priority. You shouldn't need to modify this value. */

#define APP_ADV_INTERVAL                64                                          /**< The advertising interval (in units of 0.625 ms. This value corresponds to 40 ms). */

#define APP_ADV_DURATION                18000                                       /**< The advertising duration (180 seconds) in units of 10 milliseconds. */

#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(7.5, UNIT_1_25_MS)             /**< Minimum acceptable connection interval (20 ms), Connection interval uses 1.25 ms units. */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(20, UNIT_1_25_MS)             /**< Maximum acceptable connection interval (75 ms), Connection interval uses 1.25 ms units. */
#define SLAVE_LATENCY                   0                                           /**< Slave latency. */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)             /**< Connection supervisory timeout (4 seconds), Supervision Timeout uses 10 ms units. */
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000)                       /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000)                      /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. */

#define DEAD_BEEF                       0xDEADBEEF                                  /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#ifdef NRF52840_XXAA
#define UART_TX_BUF_SIZE                2048
#define UART_RX_BUF_SIZE                32768
#else
#define UART_TX_BUF_SIZE                2048
#define UART_RX_BUF_SIZE                8192
#endif

#define PACKET_VESC						0
#define PACKET_BLE						1

#ifdef NRF52840_XXAA																/**< nrf52840 dongle (PCA10059). */
#define UART_RX							31
#define UART_TX							29
#define UART_TX_DISABLED				2
#define ADVERTISING_LED                 BSP_BOARD_LED_1                         /**< Is on when device disconnected. */
#define CONNECTED_LED                   BSP_BOARD_LED_3                         /**< Is on when device has connected. */
#define POWER_LED                   	BSP_BOARD_LED_0                         /**< Is on when device is advertising. */
//#define LED_PIN							6
#else
#if MODULE_BUILTIN
#define UART_RX							6
#define UART_TX							7
#define UART_TX_DISABLED				25
#define EN_DEFAULT						1
#define LED_PIN							8
#else
#define UART_RX							7
#define UART_TX							6
#define UART_TX_DISABLED				25
#define EN_DEFAULT						1
#define LED_PIN							8
#endif
#endif

// Private variables
APP_TIMER_DEF(m_packet_timer);
APP_TIMER_DEF(m_nrf_timer);

BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT);                                   /**< BLE NUS service instance. */
NRF_BLE_GATT_DEF(m_gatt);                                                           /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);                                                             /**< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising);                                                 /**< Advertising module instance. */

static uint16_t   m_conn_handle          = BLE_CONN_HANDLE_INVALID;                 /**< Handle of the current connection. */
static uint16_t   m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - 3;            /**< Maximum length of data (in bytes) that can be transmitted to the peer by the Nordic UART service module. */
static ble_uuid_t m_adv_uuids[]          =                                          /**< Universally unique service identifier. */
{
		{BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}
};
static bool								m_is_enabled = true;
static bool								m_uart_error = false;
static int								m_other_comm_disable_time = 0;

app_uart_comm_params_t m_uart_comm_params =
{
		.rx_pin_no    = UART_RX,
		.tx_pin_no    = UART_TX,
		.rts_pin_no   = 0,
		.cts_pin_no   = 0,
		.flow_control = APP_UART_FLOW_CONTROL_DISABLED,
		.use_parity   = false,
#if defined (UART_PRESENT)
		.baud_rate    = NRF_UART_BAUDRATE_115200
#else
		.baud_rate    = NRF_UARTE_BAUDRATE_115200
#endif
};

// Functions
void ble_printf(const char* format, ...);
static void set_enabled(bool en);

#ifdef NRF52840_XXAA
static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const * p_inst,
		app_usbd_cdc_acm_user_event_t event);

#define CDC_ACM_COMM_INTERFACE  0
#define CDC_ACM_COMM_EPIN       NRF_DRV_USBD_EPIN2

#define CDC_ACM_DATA_INTERFACE  1
#define CDC_ACM_DATA_EPIN       NRF_DRV_USBD_EPIN1
#define CDC_ACM_DATA_EPOUT      NRF_DRV_USBD_EPOUT1

/**
 * @brief CDC_ACM class instance
 * */
APP_USBD_CDC_ACM_GLOBAL_DEF(m_app_cdc_acm,
		cdc_acm_user_ev_handler,
		CDC_ACM_COMM_INTERFACE,
		CDC_ACM_DATA_INTERFACE,
		CDC_ACM_COMM_EPIN,
		CDC_ACM_DATA_EPIN,
		CDC_ACM_DATA_EPOUT,
		APP_USBD_CDC_COMM_PROTOCOL_NONE
);

static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const * p_inst, app_usbd_cdc_acm_user_event_t event) {
	switch (event) {
	case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN: {
//		nrf_gpio_pin_set(LED_PIN);
		// Setup first transfer
		char rx;
		app_usbd_cdc_acm_read(&m_app_cdc_acm, &rx, 1);
		break;
	}
	case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
//		nrf_gpio_pin_clear(LED_PIN);
		break;
	case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
		break;
	case APP_USBD_CDC_ACM_USER_EVT_RX_DONE: {
		ret_code_t ret;
		char rx;

		do {
			ret = app_usbd_cdc_acm_read(&m_app_cdc_acm, &rx, 1);
		} while (ret == NRF_SUCCESS);
		break;
	}
	default:
		break;
	}
}

static void usbd_user_ev_handler(app_usbd_event_type_t event) {
	switch (event) {
	case APP_USBD_EVT_DRV_SUSPEND:
		break;
	case APP_USBD_EVT_DRV_RESUME:
		break;
	case APP_USBD_EVT_STARTED:
		break;
	case APP_USBD_EVT_STOPPED:
		app_usbd_disable();
		break;
	case APP_USBD_EVT_POWER_DETECTED:
		if (!nrf_drv_usbd_is_enabled()) {
			app_usbd_enable();
		}
		break;
	case APP_USBD_EVT_POWER_REMOVED:
		app_usbd_stop();
		break;
	case APP_USBD_EVT_POWER_READY:
		app_usbd_start();
		break;
	default:
		break;
	}
}
#endif

/**@brief Function for assert macro callback.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyse
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num    Line number of the failing ASSERT call.
 * @param[in] p_file_name File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
	app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

static void gap_params_init(void)
{
	uint32_t                err_code;
	ble_gap_conn_params_t   gap_conn_params;
	ble_gap_conn_sec_mode_t sec_mode;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

	err_code = sd_ble_gap_device_name_set(&sec_mode,
			(const uint8_t *) DEVICE_NAME,
			strlen(DEVICE_NAME));
	APP_ERROR_CHECK(err_code);

	memset(&gap_conn_params, 0, sizeof(gap_conn_params));

	gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
	gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
	gap_conn_params.slave_latency     = SLAVE_LATENCY;
	gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
	APP_ERROR_CHECK(err_code);
}

static void start_advertising(void) {
	ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
	sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_ADV, m_advertising.adv_handle, 8);
	bsp_board_led_on(POWER_LED);
}


/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error)
{
	APP_ERROR_HANDLER(nrf_error);
}

static void nus_data_handler(ble_nus_evt_t * p_evt) {
	if (p_evt->type == BLE_NUS_EVT_RX_DATA) {
		for (uint32_t i = 0; i < p_evt->params.rx_data.length; i++) {
			packet_process_byte(p_evt->params.rx_data.p_data[i], PACKET_BLE);
		}
	}

}

static void services_init(void) {
	uint32_t           err_code;
	ble_nus_init_t     nus_init;
	nrf_ble_qwr_init_t qwr_init = {0};

	// Initialize Queued Write Module.
	qwr_init.error_handler = nrf_qwr_error_handler;

	err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
	APP_ERROR_CHECK(err_code);

	// Initialize NUS.
	memset(&nus_init, 0, sizeof(nus_init));

	nus_init.data_handler = nus_data_handler;

	err_code = ble_nus_init(&m_nus, &nus_init);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling errors from the Connection Parameters module.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error) {
	APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void) {
	uint32_t               err_code;
	ble_conn_params_init_t cp_init;

	memset(&cp_init, 0, sizeof(cp_init));

	cp_init.p_conn_params                  = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
	cp_init.disconnect_on_fail             = true;
	cp_init.evt_handler                    = NULL;
	cp_init.error_handler                  = conn_params_error_handler;

	err_code = ble_conn_params_init(&cp_init);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
	switch (ble_adv_evt) {
	case BLE_ADV_EVT_FAST:
//		bsp_indication_set(BSP_INDICATE_ADVERTISING);
		break;
	case BLE_ADV_EVT_IDLE:
//		sleep_mode_enter();
		start_advertising();
		break;
	default:
		break;
	}
}


/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context) {
	switch (p_ble_evt->header.evt_id) {
	case BLE_GAP_EVT_CONNECTED:
		//nrf_gpio_pin_set(LED_PIN);
		bsp_board_led_on(CONNECTED_LED);
        bsp_board_led_off(ADVERTISING_LED);
		m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
		nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
		sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_CONN, m_conn_handle, 8);
		break;

	case BLE_GAP_EVT_DISCONNECTED:
		//nrf_gpio_pin_clear(LED_PIN);
		bsp_board_led_off(CONNECTED_LED);
		bsp_board_led_on(ADVERTISING_LED);
		m_conn_handle = BLE_CONN_HANDLE_INVALID;
		break;

	case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
		ble_gap_phys_t const phys =
		{
				.rx_phys = BLE_GAP_PHY_AUTO,
				.tx_phys = BLE_GAP_PHY_AUTO,
		};
		sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
	} break;

	case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
		// Pairing not supported
		sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
		break;

	case BLE_GATTS_EVT_SYS_ATTR_MISSING:
		// No system attributes have been stored.
		sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
		break;

	case BLE_GATTC_EVT_TIMEOUT:
		// Disconnect on GATT Client timeout event.
		sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		break;

	case BLE_GATTS_EVT_TIMEOUT:
		// Disconnect on GATT Server timeout event.
		sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		break;

	default:
		// No implementation needed.
		break;
	}
}


/**@brief Function for the SoftDevice initialization.
 *
 * @details This function initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void) {
	nrf_sdh_enable_request();

	// Configure the BLE stack using the default settings.
	// Fetch the start address of the application RAM.
	uint32_t ram_start = 0;
	nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);

	// Enable BLE stack.
	nrf_sdh_ble_enable(&ram_start);

	// Register a handler for BLE events.
	NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

void gatt_evt_handler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt) {
	if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)) {
		m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
//		ble_printf("Data len is set to 0x%X(%d)", m_ble_nus_max_data_len, m_ble_nus_max_data_len);
	}
}

void gatt_init(void) {
	ret_code_t err_code;

	err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
	APP_ERROR_CHECK(err_code);

	err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
	APP_ERROR_CHECK(err_code);
}

void uart_event_handle(app_uart_evt_t * p_event) {
	switch (p_event->evt_type) {
	case APP_UART_DATA_READY: {
//		uint8_t byte;
//		while (app_uart_get(&byte) == NRF_SUCCESS) {
//			packet_process_byte(byte, PACKET_VESC);
//		}
	} break;

	case APP_UART_COMMUNICATION_ERROR:
//		APP_ERROR_HANDLER(p_event->data.error_communication);
		m_uart_error = true;
		break;

	case APP_UART_FIFO_ERROR:
//		APP_ERROR_HANDLER(p_event->data.error_code);
		app_uart_flush();
		packet_reset(PACKET_VESC);
		break;

	default:
		break;
	}
}

static void uart_init(void) {
	uint32_t err_code;
	APP_UART_FIFO_INIT(&m_uart_comm_params,
			UART_RX_BUF_SIZE,
			UART_TX_BUF_SIZE,
			uart_event_handle,
			APP_IRQ_PRIORITY_LOW,
			err_code);
	APP_ERROR_CHECK(err_code);
}

static void advertising_init(void) {
	uint32_t err_code;
	ble_advertising_init_t init;

	memset(&init, 0, sizeof(init));

	init.advdata.name_type          = BLE_ADVDATA_FULL_NAME;
	init.advdata.include_appearance = false;
	init.advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

	init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
	init.srdata.uuids_complete.p_uuids  = m_adv_uuids;

	init.config.ble_adv_fast_enabled  = true;
	init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
	init.config.ble_adv_fast_timeout  = APP_ADV_DURATION;
	init.evt_handler = on_adv_evt;

	err_code = ble_advertising_init(&m_advertising, &init);
	APP_ERROR_CHECK(err_code);

	ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

static void set_enabled(bool en) {
	m_is_enabled = en;

	if (m_is_enabled) {
		app_uart_close();
		m_uart_comm_params.tx_pin_no = UART_TX;
		uart_init();
		nrf_gpio_cfg_default(UART_TX_DISABLED);
	} else {
		app_uart_close();
		m_uart_comm_params.tx_pin_no = UART_TX_DISABLED;
		uart_init();
		nrf_gpio_cfg_default(UART_TX);
	}
}

static void uart_send_buffer(unsigned char *data, unsigned int len) {
	for (int i = 0;i < len;i++) {
		app_uart_put(data[i]);
	}
}

void rfhelp_send_data_crc(uint8_t *data, unsigned int len) {
	uint8_t buffer[len + 2];
	unsigned short crc = crc16((unsigned char*)data, len);
	memcpy(buffer, data, len);
	buffer[len] = (char)(crc >> 8);
	buffer[len + 1] = (char)(crc & 0xFF);
	esb_timeslot_set_next_packet(buffer, len + 2);
}

static void ble_send_buffer(unsigned char *data, unsigned int len) {
	if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
		uint32_t err_code = NRF_SUCCESS;
		int ind = 0;

		while (len > 0) {
			if (m_conn_handle == BLE_CONN_HANDLE_INVALID ||
					(err_code != NRF_ERROR_BUSY && err_code != NRF_SUCCESS && err_code != NRF_ERROR_RESOURCES)) {
				break;
			}

			uint16_t max_len = BLE_NUS_MAX_DATA_LEN;
			uint16_t tmp_len = len > max_len ? max_len : len;
			err_code = ble_nus_data_send(&m_nus, data + ind, &tmp_len, m_conn_handle);

			if (err_code != NRF_ERROR_RESOURCES) {
				len -= tmp_len;
				ind += tmp_len;
			}
		}
	}
}

static void process_packet_ble(unsigned char *data, unsigned int len) {
	if (data[0] == COMM_ERASE_NEW_APP ||
			data[0] == COMM_WRITE_NEW_APP_DATA ||
			data[0] == COMM_ERASE_NEW_APP_ALL_CAN ||
			data[0] == COMM_WRITE_NEW_APP_DATA_ALL_CAN) {
		m_other_comm_disable_time = 5000;
	}

	CRITICAL_REGION_ENTER();
	packet_send_packet(data, len, PACKET_VESC);
	CRITICAL_REGION_EXIT();
}

static void process_packet_vesc(unsigned char *data, unsigned int len) {
	if (data[0] == COMM_EXT_NRF_ESB_SET_CH_ADDR) {
		esb_timeslot_set_ch_addr(data[1], data[2], data[3], data[4]);
	} else if (data[0] == COMM_EXT_NRF_ESB_SEND_DATA) {
		rfhelp_send_data_crc(data + 1, len - 1);
	} else if (data[0] == COMM_EXT_NRF_SET_ENABLED) {
		set_enabled(data[1]);
	} else {
		if (m_is_enabled) {
			packet_send_packet(data, len, PACKET_BLE);
		}
	}
}

void ble_printf(const char* format, ...) {
	va_list arg;
	va_start (arg, format);
	int len;
	static char print_buffer[255];

	print_buffer[0] = COMM_PRINT;
	len = vsnprintf(print_buffer + 1, 254, format, arg);
	va_end (arg);

	if(len > 0) {
		packet_send_packet((unsigned char*)print_buffer, (len < 254) ? len + 1 : 255, PACKET_BLE);
	}
}

void cdc_printf(const char* format, ...) {
#ifdef NRF52840_XXAA
	va_list arg;
	va_start (arg, format);
	int len;
	static char print_buffer[255];

	len = vsnprintf(print_buffer, sizeof(print_buffer), format, arg);
	va_end (arg);

	if(len > 0) {
		app_usbd_cdc_acm_write(&m_app_cdc_acm, print_buffer,
				len < sizeof(print_buffer) ? len : sizeof(print_buffer));
	}
#else
	(void)format;
#endif
}

static void esb_timeslot_data_handler(void *p_data, uint16_t length) {
	if (m_other_comm_disable_time == 0) {
		uint8_t buffer[length + 1];
		buffer[0] = COMM_EXT_NRF_ESB_RX_DATA;
		memcpy(buffer + 1, p_data, length);
		CRITICAL_REGION_ENTER();
		packet_send_packet(buffer, length + 1, PACKET_VESC);
		CRITICAL_REGION_EXIT();
	}
}

static void packet_timer_handler(void *p_context) {
	(void)p_context;
	packet_timerfunc();

	CRITICAL_REGION_ENTER();
	if (m_other_comm_disable_time > 0) {
		m_other_comm_disable_time--;
	}
	CRITICAL_REGION_EXIT();
}

static void nrf_timer_handler(void *p_context) {
	(void)p_context;

	if (m_other_comm_disable_time == 0) {
		uint8_t buffer[1];
		buffer[0] = COMM_EXT_NRF_PRESENT;
		CRITICAL_REGION_ENTER();
		packet_send_packet(buffer, 1, PACKET_VESC);
		CRITICAL_REGION_EXIT();
	}

	cdc_printf("Test\r\n");
}

int main(void) {
	//nrf_gpio_cfg_output(LED_PIN);
	
 /*Initialize LEDs */
    bsp_board_init(BSP_INIT_LEDS);

#ifdef NRF52840_XXAA
	nrf_drv_clock_init();

	static const app_usbd_config_t usbd_config = {
			.ev_state_proc = usbd_user_ev_handler
	};

	app_usbd_serial_num_generate();
	app_usbd_init(&usbd_config);
	app_usbd_class_inst_t const * class_cdc_acm = app_usbd_cdc_acm_class_inst_get(&m_app_cdc_acm);
	app_usbd_class_append(class_cdc_acm);
#endif

	uart_init();
	app_timer_init();
	nrf_pwr_mgmt_init();
	ble_stack_init();
	gap_params_init();
	gatt_init();
	services_init();
	advertising_init();
	conn_params_init();

	(void)set_enabled;

	packet_init(uart_send_buffer, process_packet_vesc, PACKET_VESC);
	packet_init(ble_send_buffer, process_packet_ble, PACKET_BLE);

	app_timer_create(&m_packet_timer, APP_TIMER_MODE_REPEATED, packet_timer_handler);
	app_timer_start(m_packet_timer, APP_TIMER_TICKS(1), NULL);

	app_timer_create(&m_nrf_timer, APP_TIMER_MODE_REPEATED, nrf_timer_handler);
	app_timer_start(m_nrf_timer, APP_TIMER_TICKS(1000), NULL);

	esb_timeslot_init(esb_timeslot_data_handler);
	esb_timeslot_sd_start();

#ifdef NRF52840_XXAA
	app_usbd_power_events_enable();
#endif

	start_advertising();

	for (;;) {
#ifdef NRF52840_XXAA
		while (app_usbd_event_queue_process()){}
#endif

		if (m_uart_error) {
			app_uart_close();
			uart_init();
			packet_reset(PACKET_VESC);
			m_uart_error = false;
		}

		uint8_t byte;
		while (app_uart_get(&byte) == NRF_SUCCESS) {
			packet_process_byte(byte, PACKET_VESC);
		}

		sd_app_evt_wait();
	}
}
