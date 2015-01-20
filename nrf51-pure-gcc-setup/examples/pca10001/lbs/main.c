/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/**
 * This file is the main file for the application described in application note
 * nAN-36 Creating Bluetooth® Low Energy Applications Using nRF51822.
 */

//#include "nrf_delay.h"
#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "app_error.h"
#include "nrf_gpio.h"
#include "nrf51_bitfields.h"
#include "ble.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "ble_eval_board_pins.h"
#include "app_scheduler.h"
#include "ble_stack_handler.h"
#include "app_timer.h"
#include "app_gpiote.h"
#include "app_button.h"
#include "ble_debug_assert_handler.h"
#include "ble_lbs.h"
#include "simple_uart.h"
#include "uart.h"

//#define WAKEUP_BUTTON_PIN EVAL_BOARD_BUTTON_0 /**< Button used to wake up the application. */
//#define LEDBUTTON_LED_PIN_NO  EVAL_BOARD_LED_0
//#define LEDBUTTON_BUTTON_PIN_NO EVAL_BOARD_BUTTON_1
#define WAKEUP_BUTTON_PIN 1 
#define LEDBUTTON_BUTTON_PIN_NO 0

#define ADVERTISING_LED_PIN_N 8
//#define LEDBUTTON_LED_PIN_NO  9
#define CONNECTED_LED_PIN_N 10

#define DEVICE_NAME "LedButtonDemo" /**< Name of device. Will be included in the advertising data. */

//const uint8_t DEVICE_NAME [] = "LedButtonDemo";

#define APP_ADV_INTERVAL 64 /**< The advertising interval (in units of 0.625 ms. This value corresponds to 40 ms). */
#define APP_ADV_TIMEOUT_IN_SECONDS 180 /**< The advertising timeout (in units of seconds). */

// YOUR_JOB: Modify these according to requirements.
#define APP_TIMER_PRESCALER 0 /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_MAX_TIMERS 2 /**< Maximum number of simultaneously created timers. */
#define APP_TIMER_OP_QUEUE_SIZE 4 /**< Size of timer operation queues. */

//#define UNIT_1_25_MS 1250
//#define UNIT_10_MS   10000
// http://blogs.msdn.com/b/doronh/archive/2006/03/27/562502.aspx
// from https://github.com/mbedmicro/nRF51822/blob/master/nordic/nrf-sdk/app_common/app_util.h
enum {
	UNIT_0_625_MS = 625, /**< Number of microseconds in 0.625 milliseconds. */
	UNIT_1_25_MS = 1250, /**< Number of microseconds in 1.25 milliseconds. */
	UNIT_10_MS = 10000 /**< Number of microseconds in 10 milliseconds. */
};

#define 	BLE_GAP_ADDR_CYCLE_MODE_NONE   0x00 
#define 	BLE_GAP_ADDR_CYCLE_MODE_AUTO   0x01
//https://github.com/mbedmicro/BLE_API/blob/master/common/blecommon.h
typedef enum ble_error_e {
	BLE_ERROR_NONE = 0, /**< No error */
	BLE_ERROR_BUFFER_OVERFLOW = 1, /**< The requested action would cause a buffer overflow and has been aborted */
	BLE_ERROR_NOT_IMPLEMENTED = 2, /**< Requested a feature that isn't yet implement or isn't supported by the target HW */
	BLE_ERROR_PARAM_OUT_OF_RANGE = 3, /**< One of the supplied parameters is outside the valid range */
	BLE_STACK_BUSY = 4, /**< The stack is busy */
} ble_error_t;

typedef enum addr_type_e {
	ADDR_TYPE_PUBLIC = 0,
	ADDR_TYPE_RANDOM_STATIC,
	ADDR_TYPE_RANDOM_PRIVATE_RESOLVABLE,
	ADDR_TYPE_RANDOM_PRIVATE_NON_RESOLVABLE
} addr_type_t;

#define	MSEC_TO_UNITS(TIME, RESOLUTION)   (((TIME) * 1000) / (RESOLUTION))
#define MIN_CONN_INTERVAL MSEC_TO_UNITS(7.5, UNIT_1_25_MS) 
#define MAX_CONN_INTERVAL MSEC_TO_UNITS(1000, UNIT_1_25_MS) /**< Maximum acceptable connection interval (1 second). */
#define SLAVE_LATENCY 3 /**< Slave latency. */
#define CONN_SUP_TIMEOUT MSEC_TO_UNITS(4000, UNIT_10_MS) /**< Connection supervisory timeout (4 seconds). */

#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(20000, APP_TIMER_PRESCALER) /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (15 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER) /**< Time between each call to sd_ble_gap_conn_param_update after the first (5 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT 3 /**< Number of attempts before giving up the connection parameter negotiation. */

#define APP_GPIOTE_MAX_USERS 1 /**< Maximum number of users of the GPIOTE handler. */

#define BUTTON_DETECTION_DELAY APP_TIMER_TICKS(50, APP_TIMER_PRESCALER) /**< Delay from a GPIOTE event until a button is reported as pushed (in number of timer ticks). */

#define SEC_PARAM_TIMEOUT 30 /**< Timeout for Pairing Request or Security Request (in seconds). */
#define SEC_PARAM_BOND 1 /**< Perform bonding. */
#define SEC_PARAM_MITM 0 /**< Man In The Middle protection not required. */
#define SEC_PARAM_IO_CAPABILITIES BLE_GAP_IO_CAPS_NONE /**< No I/O capabilities. */
#define SEC_PARAM_OOB 0 /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE 7 /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE 16 /**< Maximum encryption key size. */

#define DEAD_BEEF 0xDEADBEEF /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

static ble_gap_sec_params_t m_sec_params; /**< Security requirements for this application. */
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID; /**< Handle of the current connection. */
static ble_lbs_t m_lbs;

#define SCHED_MAX_EVENT_DATA_SIZE sizeof(app_timer_event_t) /**< Maximum size of scheduler events. Note that scheduler BLE stack events do not contain any data, as the events are being pulled from the stack in the event handler. */
#define SCHED_QUEUE_SIZE 10 /**< Maximum number of events in the scheduler queue. */

/**@brief Function for error handling, which is called when an error has occurred.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 * how your product is supposed to react in case of error.
 *
 * @param[in] error_code Error code supplied to the handler.
 * @param[in] line_num Line number where the handler is called.
 * @param[in] p_file_name Pointer to the file name.
 */
void app_error_handler(uint32_t error_code, uint32_t line_num,
		const uint8_t * p_file_name) {
// This call can be used for debug purposes during development of an application.
// @note CAUTION: Activating this code will write the stack to flash on an error.
// This function should NOT be used in a final product.
// It is intended STRICTLY for development/debugging purposes.
// The flash write will happen EVEN if the radio is active, thus interrupting
// any communication.
// Use with care. Un-comment the line below to use.
	ble_debug_assert_handler(error_code, line_num, p_file_name);

// On assert, the system can only recover with a reset.
//NVIC_SystemReset();
}

/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 * how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num Line number of the failing ASSERT call.
 * @param[in] file_name File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
	app_error_handler(DEAD_BEEF, line_num, p_file_name);
	uart_tx_str("error happens");
}

/**@brief Function for the LEDs initialization.
 *
 * @details Initializes all LEDs used by the application.
 */
static void leds_init(void) {
	GPIO_LED_CONFIG(ADVERTISING_LED_PIN_N);
	GPIO_LED_CONFIG(CONNECTED_LED_PIN_N);
	GPIO_LED_CONFIG (LEDBUTTON_LED_PIN_NO);
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module.
 */
static void timers_init(void) {
// Initialize timer module, making it use the scheduler
	APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_MAX_TIMERS,
			APP_TIMER_OP_QUEUE_SIZE, true);
}

/**@brief Function for the GAP initialization.
 *
 * @details This function shall be used to setup all the necessary GAP (Generic Access Profile)
 * parameters of the device. It also sets the permissions and appearance.
 */
static void gap_params_init(void) {
	uint32_t err_code;
	ble_gap_conn_params_t gap_conn_params;
	ble_gap_conn_sec_mode_t sec_mode;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
	err_code = sd_ble_gap_device_name_set(&sec_mode, DEVICE_NAME,
			strlen(DEVICE_NAME));
	APP_ERROR_CHECK(err_code);

	static const unsigned ADDR_LEN = 6;
	const uint8_t address[] = { 0xe7, 0xAA, 0xAA, 0xAA, 0xAA, 0xAB };
	ble_gap_addr_t dev_addr;
	dev_addr.addr_type = ADDR_TYPE_PUBLIC;
	memcpy(dev_addr.addr, address, ADDR_LEN);
	err_code = sd_ble_gap_address_set(&dev_addr);
	APP_ERROR_CHECK(err_code);

	memset(&gap_conn_params, 0, sizeof(gap_conn_params));

	gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
	gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
	gap_conn_params.slave_latency = SLAVE_LATENCY;
	gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the Advertising functionality.
 *
 * @details Encodes the required advertising data and passes it to the stack.
 * Also builds a structure to be passed to the stack when starting advertising.
 */
static void advertising_init(void) {
	uint32_t err_code;
	ble_advdata_t advdata;
	ble_advdata_t scanrsp;
	uint8_t flags = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;
	ble_uuid_t adv_uuids[] = { { LBS_UUID_SERVICE, m_lbs.uuid_type } };

// Build and set advertising data
	memset(&advdata, 0, sizeof(advdata));
	advdata.name_type = BLE_ADVDATA_FULL_NAME;
	advdata.include_appearance = true;
	advdata.flags.size = sizeof(flags);
	advdata.flags.p_data = &flags;
	memset(&scanrsp, 0, sizeof(scanrsp));
	scanrsp.uuids_complete.uuid_cnt = sizeof(adv_uuids) / sizeof(adv_uuids[0]);
	scanrsp.uuids_complete.p_uuids = adv_uuids;
	err_code = ble_advdata_set(&advdata, &scanrsp);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling a write to the LED characteristic of the LED Button service.
 * @detail A pointer to this function is passed to the service in its init structure.
 */
static void led_write_handler(ble_lbs_t * p_lbs, uint8_t led_state) {
	uart_tx_status_code("led_write_handler:", led_state, 0);
// inverted logic: 1 to LED off, 0 to LEd on  
	if (led_state) {
		nrf_gpio_pin_clear (LEDBUTTON_LED_PIN_NO);
	} else {
		nrf_gpio_pin_set (LEDBUTTON_LED_PIN_NO);
	}
}

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void) {
	uint32_t err_code;
	ble_lbs_init_t init;
	init.led_write_handler = led_write_handler;
	err_code = ble_lbs_init(&m_lbs, &init);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing security parameters.
 */
static void sec_params_init(void) {
	m_sec_params.timeout = SEC_PARAM_TIMEOUT;
	m_sec_params.bond = SEC_PARAM_BOND;
	m_sec_params.mitm = SEC_PARAM_MITM;
	m_sec_params.io_caps = SEC_PARAM_IO_CAPABILITIES;
	m_sec_params.oob = SEC_PARAM_OOB;
	m_sec_params.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
	m_sec_params.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
}

/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module which
 * are passed to the application.
 * @note All this function does is to disconnect. This could have been done by simply
 * setting the disconnect_on_fail config parameter, but instead we use the event
 * handler mechanism to demonstrate its use.
 *
 * @param[in] p_evt Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt) {
	uint32_t err_code;
        uart_tx_status_code("on_conn_params_evt() evt_type:", p_evt->evt_type, 0);
	if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
		err_code = sd_ble_gap_disconnect(m_conn_handle,
				BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
		APP_ERROR_CHECK(err_code);
	}
}

/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in] nrf_error Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error) {
	APP_ERROR_HANDLER(nrf_error);
	uart_tx_status_code("nrf error code:", nrf_error, 0);
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void) {
	uint32_t err_code;
	ble_conn_params_init_t cp_init;
	memset(&cp_init, 0, sizeof(cp_init));

	cp_init.p_conn_params = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
	cp_init.disconnect_on_fail = false;
	cp_init.evt_handler = on_conn_params_evt;
	cp_init.error_handler = conn_params_error_handler;
	err_code = ble_conn_params_init(&cp_init);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for starting advertising.
 */
static void advertising_start(void) {
	uint32_t err_code;
	ble_gap_adv_params_t adv_params;
// Start advertising
	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.type = BLE_GAP_ADV_TYPE_ADV_IND;
	adv_params.p_peer_addr = NULL;
	adv_params.fp = BLE_GAP_ADV_FP_ANY;
	adv_params.interval = APP_ADV_INTERVAL;
	adv_params.timeout = APP_ADV_TIMEOUT_IN_SECONDS;

	err_code = sd_ble_gap_adv_start(&adv_params);
	APP_ERROR_CHECK(err_code);
	nrf_gpio_pin_set(ADVERTISING_LED_PIN_N);
}

/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param[in] p_ble_evt Bluetooth stack event.
 */
static void on_ble_evt(ble_evt_t * p_ble_evt) {
	uint32_t err_code = NRF_SUCCESS;
	static ble_gap_evt_auth_status_t m_auth_status;
	ble_gap_enc_info_t * p_enc_info;
	switch (p_ble_evt->header.evt_id) {
	case BLE_GAP_EVT_CONNECTED:
		nrf_gpio_pin_set(CONNECTED_LED_PIN_N);
		nrf_gpio_pin_clear(ADVERTISING_LED_PIN_N);
		m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;

		err_code = app_button_enable();
		break;
	case BLE_GAP_EVT_DISCONNECTED:
		nrf_gpio_pin_clear(CONNECTED_LED_PIN_N);
		m_conn_handle = BLE_CONN_HANDLE_INVALID;

		err_code = app_button_disable();
		if (err_code == NRF_SUCCESS) {
			advertising_start();
		}
		break;
	case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
		err_code = sd_ble_gap_sec_params_reply(m_conn_handle,
				BLE_GAP_SEC_STATUS_SUCCESS, &m_sec_params);
		break;
	case BLE_GATTS_EVT_SYS_ATTR_MISSING:
		err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0);
		break;

	case BLE_GAP_EVT_AUTH_STATUS:
		m_auth_status = p_ble_evt->evt.gap_evt.params.auth_status;
		break;
	case BLE_GAP_EVT_SEC_INFO_REQUEST:
		p_enc_info = &m_auth_status.periph_keys.enc_info;
		if (p_enc_info->div
				== p_ble_evt->evt.gap_evt.params.sec_info_request.div) {
			err_code = sd_ble_gap_sec_info_reply(m_conn_handle, p_enc_info,
					NULL);
		} else {
// No keys found for this device
			err_code = sd_ble_gap_sec_info_reply(m_conn_handle, NULL, NULL);
		}
		break;

	case BLE_GAP_EVT_TIMEOUT:
		if (p_ble_evt->evt.gap_evt.params.timeout.src
				== BLE_GAP_TIMEOUT_SRC_ADVERTISEMENT) {
			nrf_gpio_pin_clear(ADVERTISING_LED_PIN_N);

// Go to system-off mode (this function will not return; wakeup will cause a reset)
			GPIO_WAKEUP_BUTTON_CONFIG(WAKEUP_BUTTON_PIN);
			uart_tx_str("reset by timeout");
			err_code = sd_power_system_off();
		}
		break;

	case BLE_GATTS_EVT_WRITE:
		break;

	case BLE_GAP_EVT_CONN_PARAM_UPDATE:
		nrf_gpio_pin_toggle (LEDBUTTON_LED_PIN_NO);
		break;

	default:
		break;
	}

	APP_ERROR_CHECK(err_code);
}

/**@brief Function for dispatching a BLE stack event to all modules with a BLE stack event handler.
 *
 * @details This function is called from the scheduler in the main loop after a BLE stack
 * event has been received.
 *
 * @param[in] p_ble_evt Bluetooth stack event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt) {
	on_ble_evt(p_ble_evt);
	ble_conn_params_on_ble_evt(p_ble_evt);
	ble_lbs_on_ble_evt(&m_lbs, p_ble_evt);
}

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void) {
	BLE_STACK_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_XTAL_20_PPM, BLE_L2CAP_MTU_DEF,
			ble_evt_dispatch, true);
}

/**@brief Function for the Event Scheduler initialization.
 */
static void scheduler_init(void) {
	APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
}

/**@brief Function for initializing the GPIOTE handler module.
 */
static void gpiote_init(void) {
	APP_GPIOTE_INIT(APP_GPIOTE_MAX_USERS);
}

/**@brief Function for handling button presses.
 */
static void button_event_handler(uint8_t pin_no) {
	static uint8_t send_push = 1;
	uint32_t err_code;
	uart_tx_status_code("event pin #", pin_no, 0);
	switch (pin_no) {
	case LEDBUTTON_BUTTON_PIN_NO:
		err_code = ble_lbs_on_button_change(&m_lbs, send_push);
//err_code = NRF_SUCCESS;
		if (err_code != NRF_SUCCESS && err_code != BLE_ERROR_INVALID_CONN_HANDLE
				&& err_code != NRF_ERROR_INVALID_STATE) {
                        uart_tx_status_code("reset @ err. code #:", err_code, 0);
                        err_code = sd_power_system_off();
			APP_ERROR_CHECK(err_code);
		}
		send_push = !send_push;
		break;
	default:
		break;
	}
}

/**@brief Function for initializing the button handler module.
 */
static void buttons_init(void) {
// Note: Array must be static because a pointer to it will be saved in the Button handler
// module.
	static app_button_cfg_t buttons[] = { { WAKEUP_BUTTON_PIN, false,
			NRF_GPIO_PIN_PULLUP, NULL }, { LEDBUTTON_BUTTON_PIN_NO, false,
			NRF_GPIO_PIN_PULLUP, button_event_handler } };
	APP_BUTTON_INIT(buttons, sizeof(buttons) / sizeof(buttons[0]),
			BUTTON_DETECTION_DELAY, true);
}

/**@brief Function for entering sleep.
 */
static void power_manage(void) {
	uint32_t err_code = sd_app_event_wait();
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for application main entry.
 */
int main(void) {
// Initialize
	leds_init();
	timers_init();
	gpiote_init();
	buttons_init();
	simple_uart_config(UART_RTS, UART_TX, UART_CTS, UART_RX, 0);
	ble_stack_init();
	scheduler_init();
	gap_params_init();
	services_init();
	advertising_init();
	conn_params_init();
	sec_params_init();
// Start execution
	//app_button_enable();
	advertising_start();
	uart_tx_str("nRF51822 run");

// Enter main loop

	for (;;) {
		app_sched_execute();
		power_manage();
	}
}

/**
 * @}
 */
