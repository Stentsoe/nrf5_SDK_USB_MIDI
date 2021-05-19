/**
 * Copyright (c) 2017 - 2020, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "nrf.h"
#include "app_util.h"
#include "nrf_drv_usbd.h"
#include "nrf_drv_clock.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_drv_power.h"

#include "app_usbd.h"
#include "app_usbd_core.h"
#include "app_usbd_string_desc.h"
#include "app_usbd_midi.h"
#include "app_error.h"
#include "boards.h"
#include "bsp.h"
#include "app_timer.h"


#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

/**@file
 * @defgroup usbd_midi_example main.c
 * @{
 * @ingroup usbd_midi_example
 * @brief USBD Midi class example
 *
 */

#define LED_USB_RESUME (BSP_BOARD_LED_0)
#define LED_MIDI_OPEN  (BSP_BOARD_LED_1)
#define LED_MIDI_RX   (BSP_BOARD_LED_2)
#define LED_MIDI_TX   (BSP_BOARD_LED_3)

#define CODE_IDX_NOTE_ON 0x09
#define CODE_IDX_NOTE_OFF 0x08
#define NOTE_ON 144
#define NOTE_OFF 128
#define NOTE_C 48
#define NOTE_E 52
#define NOTE_G 55
#define NOTE_B 59
#define VELOCITY 100

#define BTN_MIDI_KEY_0_NOTE       NOTE_C
#define BTN_MIDI_KEY_1_NOTE       NOTE_E
#define BTN_MIDI_KEY_2_NOTE       NOTE_G
#define BTN_MIDI_KEY_3_NOTE       NOTE_B

#define BTN_MIDI_KEY_0_RELEASE  (bsp_event_t)(BSP_EVENT_KEY_LAST + 1)
#define BTN_MIDI_KEY_1_RELEASE  (bsp_event_t)(BSP_EVENT_KEY_LAST + 2)
#define BTN_MIDI_KEY_2_RELEASE  (bsp_event_t)(BSP_EVENT_KEY_LAST + 3)
#define BTN_MIDI_KEY_3_RELEASE  (bsp_event_t)(BSP_EVENT_KEY_LAST + 4)

/**
 * @brief USB Midi event size
 */
#define USBD_MIDI_EVENT_SIZE 4

/**
 * @brief USB Midi event size
 */
#define RX_BUFFER_SIZE 256



/**
 * @brief helper macro to put midi event into tx buffer
 */
#define MIDI_EVENT(tx_buffer, code_idx, status_byte, data1, data2) \
        tx_buffer[0] = code_idx;        \
        tx_buffer[1] = status_byte;     \
        tx_buffer[2] = data1;           \
        tx_buffer[3] = data2;


/**
 * @brief Enable power USB detection
 *
 * Configure if example supports USB port connection
 */
#ifndef USBD_POWER_DETECTION
#define USBD_POWER_DETECTION true
#endif

/**
 * @brief Midi class user event handler
 */

static void midi_user_ev_handler(app_usbd_class_inst_t const * p_inst,
                                      app_usbd_midi_user_event_t   event);



/**
 * @brief   Midi class complete interface descriptor
 */
APP_USBD_MIDI_DESCRIPTOR(m_midi_desc,
                         APP_USBD_AUDIO_MIDI_CS_MIDI_STREAMING_INTERFACE_DSC,
                         APP_USBD_AUDIO_MIDI_EMBEDDED_IN_JACK_DSC,
                         APP_USBD_AUDIO_MIDI_EXTERNAL_IN_JACK_DSC,
                         APP_USBD_AUDIO_MIDI_EMBEDDED_OUT_JACK_DSC,
                         APP_USBD_AUDIO_MIDI_EXTERNAL_OUT_JACK_DSC,
                         APP_USBD_AUDIO_MIDI_STANDARD_BULK_OUT_ENDPOINT_DSC,
                         APP_USBD_AUDIO_MIDI_BULK_OUT_ENDPOINT_DSC,
                         APP_USBD_AUDIO_MIDI_STANDARD_BULK_IN_ENDPOINT_DSC,
                         APP_USBD_AUDIO_MIDI_BULK_IN_ENDPOINT_DSC);



/**
 * @brief Interfaces list passed to @ref APP_USBD_MIDI_GLOBAL_DEF
 */
#define MIDI_INTERFACES_CONFIG() APP_USBD_MIDI_CONFIG_IN_OUT(0, 1)

/*lint -save -e26 -e64 -e123 -e505 -e651*/



/**
 * @brief Midi class instance
 */
APP_USBD_MIDI_GLOBAL_DEF(m_app_midi,
                          MIDI_INTERFACES_CONFIG(),
                          midi_user_ev_handler,
                          &m_midi_desc,
                          RX_BUFFER_SIZE
);


/*lint -restore*/



/**
 * @brief User event handler @ref app_usbd_midi_user_ev_handler_t 
 * */
static void midi_user_ev_handler(app_usbd_class_inst_t const * p_inst,
                                    app_usbd_midi_user_event_t event)
{
    // app_usbd_midi_t const * p_midi = app_usbd_midi_class_get(p_inst);
    switch (event)
    {
        case APP_USBD_MIDI_USER_EVT_CLASS_REQ:
            break;
        case APP_USBD_MIDI_USER_EVT_PORT_OPEN:
        {
            NRF_LOG_INFO("Midi port opened");
            bsp_board_led_on(LED_MIDI_OPEN);
            ret_code_t ret = bsp_buttons_enable();
            APP_ERROR_CHECK(ret);

            break;
        }
        case APP_USBD_MIDI_USER_EVT_PORT_CLOSE:
            break;
        case APP_USBD_MIDI_USER_EVT_TX_DONE:
            bsp_board_led_invert(LED_MIDI_TX);
            break;
        case APP_USBD_MIDI_USER_EVT_RX_DONE:
        {
            uint8_t m_rx_buffer[4];
            ret_code_t ret = app_usbd_midi_get(&m_app_midi,
                                                m_rx_buffer);
            if(ret) {
                NRF_LOG_INFO("ret get %d", ret);
            }
            NRF_LOG_HEXDUMP_INFO(m_rx_buffer, 4);
            // app_usbd_midi_read(&m_app_midi,
            //                     m_rx_buffer);
            
            bsp_board_led_invert(LED_MIDI_RX);
            break;
        }
        default:
            break;
    }
}

/**
 * @brief USBD library specific event handler.
 *
 * @param event     USBD library event.
 */
static void usbd_user_ev_handler(app_usbd_event_type_t event)
{
    switch (event)
    {
        case APP_USBD_EVT_DRV_SOF:
            break;
        case APP_USBD_EVT_DRV_SUSPEND:
            bsp_board_leds_off();
            break;
        case APP_USBD_EVT_DRV_RESUME:
            bsp_board_led_on(LED_USB_RESUME);
            break;
        case APP_USBD_EVT_STARTED:
            break;
        case APP_USBD_EVT_STOPPED:
            app_usbd_disable();
            bsp_board_leds_off();
            break;
        case APP_USBD_EVT_POWER_DETECTED:
            NRF_LOG_INFO("USB power detected");

            if (!nrf_drv_usbd_is_enabled())
            {
                app_usbd_enable();
            }
            break;
        case APP_USBD_EVT_POWER_REMOVED:
            NRF_LOG_INFO("USB power removed");
            app_usbd_stop();
            break;
        case APP_USBD_EVT_POWER_READY:
            NRF_LOG_INFO("USB ready");
            app_usbd_start();
            break;
        default:
            break;
    }
}

void bsp_event_callback(bsp_event_t ev)
{

    uint8_t m_tx_buffer[USBD_MIDI_EVENT_SIZE];
    switch ((unsigned int)ev)
    {
        case BSP_EVENT_KEY_0:
            MIDI_EVENT(m_tx_buffer, CODE_IDX_NOTE_ON, NOTE_ON, BTN_MIDI_KEY_0_NOTE, VELOCITY);
            break;
        case BTN_MIDI_KEY_0_RELEASE:
            MIDI_EVENT(m_tx_buffer, CODE_IDX_NOTE_OFF, NOTE_OFF, BTN_MIDI_KEY_0_NOTE, VELOCITY);
            break;
        case BSP_EVENT_KEY_1:
            MIDI_EVENT(m_tx_buffer, CODE_IDX_NOTE_ON, NOTE_ON, BTN_MIDI_KEY_1_NOTE, VELOCITY);
            break;
        case BTN_MIDI_KEY_1_RELEASE:
            MIDI_EVENT(m_tx_buffer, CODE_IDX_NOTE_OFF, NOTE_OFF, BTN_MIDI_KEY_1_NOTE, VELOCITY);
            break;
        case BSP_EVENT_KEY_2:
            MIDI_EVENT(m_tx_buffer, CODE_IDX_NOTE_ON, NOTE_ON, BTN_MIDI_KEY_2_NOTE, VELOCITY);
            break;
        case BTN_MIDI_KEY_2_RELEASE:
            MIDI_EVENT(m_tx_buffer, CODE_IDX_NOTE_OFF, NOTE_OFF, BTN_MIDI_KEY_2_NOTE, VELOCITY);
            break;
        case BSP_EVENT_KEY_3:
            MIDI_EVENT(m_tx_buffer, CODE_IDX_NOTE_ON, NOTE_ON, BTN_MIDI_KEY_3_NOTE, VELOCITY);
            break;
        case BTN_MIDI_KEY_3_RELEASE:
            MIDI_EVENT(m_tx_buffer, CODE_IDX_NOTE_OFF, NOTE_OFF, BTN_MIDI_KEY_3_NOTE, VELOCITY);
            break;
        default:
            return;   
    }
    
    app_usbd_midi_write(&m_app_midi, m_tx_buffer);

}

static void init_bsp(void)
{
    ret_code_t ret;
    ret = bsp_init(BSP_INIT_BUTTONS, bsp_event_callback);

    UNUSED_RETURN_VALUE(bsp_event_to_button_action_assign(BSP_BOARD_BUTTON_0, 
                                                          BSP_BUTTON_ACTION_RELEASE,
                                                          BTN_MIDI_KEY_0_RELEASE));

    UNUSED_RETURN_VALUE(bsp_event_to_button_action_assign(BSP_BOARD_BUTTON_1, 
                                                          BSP_BUTTON_ACTION_RELEASE,
                                                          BTN_MIDI_KEY_1_RELEASE));

    UNUSED_RETURN_VALUE(bsp_event_to_button_action_assign(BSP_BOARD_BUTTON_2, 
                                                          BSP_BUTTON_ACTION_RELEASE,
                                                          BTN_MIDI_KEY_2_RELEASE));

    UNUSED_RETURN_VALUE(bsp_event_to_button_action_assign(BSP_BOARD_BUTTON_3, 
                                                          BSP_BUTTON_ACTION_RELEASE,
                                                          BTN_MIDI_KEY_3_RELEASE));
    APP_ERROR_CHECK(ret);

    /* Configure LEDs */
    bsp_board_init(BSP_INIT_LEDS);
}


int main(void)
{
    ret_code_t ret;
    static const app_usbd_config_t usbd_config = {
        .ev_state_proc = usbd_user_ev_handler,
        .enable_sof = false
    };

    ret = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(ret);
    NRF_LOG_DEFAULT_BACKENDS_INIT();

    ret = nrf_drv_clock_init();
    APP_ERROR_CHECK(ret);

    NRF_LOG_INFO("USBD midi example started.");
    
    nrf_drv_clock_lfclk_request(NULL);

    while(!nrf_drv_clock_lfclk_is_running())
    {
        /* Just waiting */
    }

    ret = app_timer_init();
    APP_ERROR_CHECK(ret);

    // Initialize LEDs and buttons
    init_bsp();

    ret = app_usbd_init(&usbd_config);
    APP_ERROR_CHECK(ret);

    app_usbd_class_inst_t const * class_inst_midi =
        app_usbd_midi_class_inst_get(&m_app_midi);

    ret = app_usbd_class_append(class_inst_midi);
    APP_ERROR_CHECK(ret);

    if (USBD_POWER_DETECTION)
    {
        ret = app_usbd_power_events_enable();
        APP_ERROR_CHECK(ret);
    }
    else
    {
        NRF_LOG_INFO("No USB power detection enabled\r\nStarting USB now");

        app_usbd_enable();
        app_usbd_start();
    }
    while (true)
    {
        while (app_usbd_event_queue_process())
        {
            /* Nothing to do */
        }
        UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());
        /* Sleep CPU only if there was no interrupt since last loop processing */
        __WFE();
    }
}

/** @} */