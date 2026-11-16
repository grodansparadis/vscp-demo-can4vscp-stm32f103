/*
  This file is part of the VSCP Blinky demo for the STM32F103 "Blue Pill"
  with CAN4VSCP (VSCP Level I over CAN).

  The MIT License (MIT)
  Copyright (C) 2000-2026 Ake Hedman, the VSCP project <info@vscp.org>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

/*!
  @file blinky.c
  The VSCP blinky demo for a VSCP Level I node on CAN (CAN4VSCP).

  This file holds the node logic and all the callbacks vscp-firmware.c
  needs, except the two transport callbacks which are in vscp-can.c.

  What the node does
  ------------------
  - Blinks the LED with the interval in registers 4/5 when enabled.
  - Sends events when the button is pressed and released.
  - Runs a millisecond counter that can be read in registers 6-9.
  - Has a decision matrix with four rows that can control the LED.
  - Sends a heartbeat every minute (done by vscp-firmware.c).

  A long press on the button (two seconds) drops the nickname and starts
  a new nickname discovery.
*/

#include "blinky.h"

#include "main.h"
#include "persist.h"
#include "vscp-can.h"

#include "vscp-compiler.h"
#include "vscp-projdefs.h"

#include <string.h>
#include <vscp-class.h>
#include <vscp-firmware.h>
#include <vscp-type.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// The MDF for the node is fetched from this URL (without "http://").
// Max 32 characters.
static const char s_mdf_url[] = "eurosource.se/blinky0.json";

// GUID of the node, MSB first. Built from the unique id of the MCU.
static uint8_t s_guid[16];

// Status register (register 2). Not persistent.
static uint8_t s_status;

// Millisecond counter (registers 6-9). Not persistent.
static volatile uint32_t s_ms_counter;

// Milliseconds since the LED was last toggled by the blink function
static volatile uint16_t s_blink_cnt;

// Counts milliseconds up to one second
static volatile uint16_t s_ms_in_second;

// Seconds that have passed and not yet been handled by the main loop
static volatile uint8_t s_seconds_pending;

// Button handling. The button is sampled every millisecond.
#define BTN_EVENT_PRESSED  0x01u
#define BTN_EVENT_RELEASED 0x02u

static volatile uint8_t s_btn_events;  // BTN_EVENT_xxx waiting for the main loop
static uint8_t s_btn_raw;              // Last sampled state (ISR only)
static uint8_t s_btn_stable;           // Debounced state (ISR only)
static uint8_t s_btn_debounce;         // Debounce counter (ISR only)
static uint8_t s_long_press;           // Set while a long press is in progress

// ---------------------------------------------------------------------------
//                                 Hardware
// ---------------------------------------------------------------------------

// The LED on the Blue Pill (PC13) is on when the pin is low.
#define LED_PIN_ON  GPIO_PIN_RESET
#define LED_PIN_OFF GPIO_PIN_SET

// The status LED (PB12) is on when the pin is high.
#define STATUS_LED_PIN_ON  GPIO_PIN_SET
#define STATUS_LED_PIN_OFF GPIO_PIN_RESET

// The button (PA0) connects the pin to ground when pressed.
#define BTN_PIN_PRESSED GPIO_PIN_RESET

///////////////////////////////////////////////////////////////////////////////
// send_event
//
// Send an event with zone/subzone data from this node.
//

static void
send_event(uint16_t vscp_class, uint8_t vscp_type, uint8_t byte0, uint8_t zone, uint8_t subzone)
{
  vscp_omsg.flags      = VSCP_VALID_MSG + 3;
  vscp_omsg.priority   = VSCP_PRIORITY_NORMAL;
  vscp_omsg.vscp_class = vscp_class;
  vscp_omsg.vscp_type  = vscp_type;
  vscp_omsg.data[0]    = byte0;
  vscp_omsg.data[1]    = zone;
  vscp_omsg.data[2]    = subzone;
  vscp_sendEvent();
}

///////////////////////////////////////////////////////////////////////////////
// led_set
//
// Turn the LED on or off. With bEvent set a CLASS1.INFORMATION ON or OFF
// event is sent if the state changed. The periodic blink does not send
// events, it would flood the bus.
//

static void
led_set(uint8_t bOn, uint8_t bEvent)
{
  uint8_t bWasOn = (s_status & BLINKY_STATUS_LED_ON) ? 1 : 0;

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, bOn ? LED_PIN_ON : LED_PIN_OFF);

  if (bOn) {
    s_status |= BLINKY_STATUS_LED_ON;
  }
  else {
    s_status &= (uint8_t) ~BLINKY_STATUS_LED_ON;
  }

  if (bEvent && (bWasOn != (bOn ? 1 : 0)) && (VSCP_STATE_ACTIVE == vscp_node_state)) {
    send_event(VSCP_CLASS1_INFORMATION,
               bOn ? VSCP_TYPE_INFORMATION_ON : VSCP_TYPE_INFORMATION_OFF,
               0,
               g_persist.zone,
               g_persist.subzone);
  }
}

///////////////////////////////////////////////////////////////////////////////
// led_toggle
//

static void
led_toggle(uint8_t bEvent)
{
  led_set((s_status & BLINKY_STATUS_LED_ON) ? 0 : 1, bEvent);
}

///////////////////////////////////////////////////////////////////////////////
// get_blink_interval
//

static uint16_t
get_blink_interval(void)
{
  return (uint16_t) (((uint16_t) g_persist.blink_interval_msb << 8) | g_persist.blink_interval_lsb);
}

///////////////////////////////////////////////////////////////////////////////
// set_defaults
//
// Set all application registers to the defaults in the MDF.
//

static void
set_defaults(void)
{
  g_persist.zone               = BLINKY_DEFAULT_ZONE;
  g_persist.subzone            = BLINKY_DEFAULT_SUBZONE;
  g_persist.control            = BLINKY_DEFAULT_CONTROL;
  g_persist.blink_interval_msb = (uint8_t) (BLINKY_DEFAULT_BLINK_INTERVAL >> 8);
  g_persist.blink_interval_lsb = (uint8_t) (BLINKY_DEFAULT_BLINK_INTERVAL & 0xffu);
  g_persist.button_byte0       = BLINKY_DEFAULT_BUTTON_BYTE0;
  g_persist.button_zone        = BLINKY_DEFAULT_BUTTON_ZONE;
  g_persist.button_subzone     = BLINKY_DEFAULT_BUTTON_SUBZONE;
  memset(g_persist.dm, 0, sizeof(g_persist.dm));
  persist_changed();
}

///////////////////////////////////////////////////////////////////////////////
// build_guid
//
// VSCP reserves a GUID range for MCUs that have a unique id:
// FD:00:02 for ST, followed by the 96-bit unique id. The last byte is zero.
//

static void
build_guid(void)
{
  uint32_t uid[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };

  memset(s_guid, 0, sizeof(s_guid));
  s_guid[0] = 0xFD;
  s_guid[1] = 0x00;
  s_guid[2] = 0x02;

  for (uint8_t i = 0; i < 3; i++) {
    s_guid[3 + (i * 4)] = (uint8_t) (uid[i] >> 24);
    s_guid[4 + (i * 4)] = (uint8_t) (uid[i] >> 16);
    s_guid[5 + (i * 4)] = (uint8_t) (uid[i] >> 8);
    s_guid[6 + (i * 4)] = (uint8_t) (uid[i]);
  }
}

///////////////////////////////////////////////////////////////////////////////
// do_action
//
// Execute a decision matrix action.
//

static void
do_action(uint8_t action, uint8_t param)
{
  switch (action) {

    case BLINKY_ACTION_LED_STATE:
      if (BLINKY_ARG_LED_OFF == param) {
        led_set(0, TRUE);
      }
      else if (BLINKY_ARG_LED_ON == param) {
        led_set(1, TRUE);
      }
      else if (BLINKY_ARG_LED_TOGGLE == param) {
        led_toggle(TRUE);
      }
      break;

    case BLINKY_ACTION_LED_ON:
      led_set(1, TRUE);
      break;

    case BLINKY_ACTION_LED_OFF:
      led_set(0, TRUE);
      break;

    case BLINKY_ACTION_LED_TOGGLE:
      led_toggle(TRUE);
      break;

    case BLINKY_ACTION_START_BLINK:
      if (!(g_persist.control & BLINKY_CTRL_ENABLE_LED)) {
        g_persist.control |= BLINKY_CTRL_ENABLE_LED;
        s_blink_cnt = 0;
        persist_changed();
      }
      break;

    case BLINKY_ACTION_STOP_BLINK:
      if (g_persist.control & BLINKY_CTRL_ENABLE_LED) {
        g_persist.control &= (uint8_t) ~BLINKY_CTRL_ENABLE_LED;
        persist_changed();
      }
      break;

    case BLINKY_ACTION_CLR_COUNTER:
      s_ms_counter = 0;
      break;

    case BLINKY_ACTION_NOOP:
    default:
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// do_dm
//
// Feed the incoming event (vscp_imsg) through the decision matrix.
//

static void
do_dm(void)
{
  // Copy the event. An action may send an event, and that must not
  // disturb the matching of the rows that follow.
  const uint16_t vscp_class = vscp_imsg.vscp_class;
  const uint8_t vscp_type   = vscp_imsg.vscp_type;
  const uint8_t oaddr       = vscp_imsg.oaddr;
  const uint8_t size        = vscp_imsg.flags & 0x0f;
  const uint8_t zone        = vscp_imsg.data[1];
  const uint8_t subzone     = vscp_imsg.data[2];

  for (uint8_t row = 0; row < BLINKY_DM_ROWS; row++) {

    const uint8_t *pdm = &g_persist.dm[row * BLINKY_DM_ROW_SIZE];
    uint8_t flags      = pdm[VSCP_DM_POS_FLAGS];

    if (!(flags & VSCP_DM_FLAG_ENABLED)) {
      continue;
    }

    if ((flags & VSCP_DM_FLAG_CHECK_OADDR) && (oaddr != pdm[VSCP_DM_POS_OADDR])) {
      continue;
    }

    // Zone 255 and subzone 255 mean "all"
    if (flags & VSCP_DM_FLAG_CHECK_ZONE) {
      if ((size < 2) || ((255 != zone) && (zone != g_persist.zone))) {
        continue;
      }
    }

    if (flags & VSCP_DM_FLAG_CHECK_SUBZONE) {
      if ((size < 3) || ((255 != subzone) && (subzone != g_persist.subzone))) {
        continue;
      }
    }

    // The class is nine bits. Bit eight of mask and filter is in the flags.
    uint16_t class_mask   = (uint16_t) (((flags & VSCP_DM_FLAG_CLASS_MASK) ? 0x100u : 0u) | pdm[VSCP_DM_POS_CLASSMASK]);
    uint16_t class_filter = (uint16_t) (((flags & VSCP_DM_FLAG_CLASS_FILTER) ? 0x100u : 0u) | pdm[VSCP_DM_POS_CLASSFILTER]);

    if ((class_filter ^ vscp_class) & class_mask) {
      continue;
    }

    if ((pdm[VSCP_DM_POS_TYPEFILTER] ^ vscp_type) & pdm[VSCP_DM_POS_TYPEMASK]) {
      continue;
    }

    do_action(pdm[VSCP_DM_POS_ACTION], pdm[VSCP_DM_POS_ACTIONPARAM]);
  }
}

///////////////////////////////////////////////////////////////////////////////
// do_status_led
//
// The status LED shows the state of the node: blinking while the node
// searches for a nickname, steady when it is active, off on error.
//

static void
do_status_led(void)
{
  switch (vscp_initledfunc) {

    case VSCP_LED_BLINK1:
      if (vscp_statuscnt > 100) {
        HAL_GPIO_TogglePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin);
        vscp_statuscnt = 0;
      }
      break;

    case VSCP_LED_ON:
      HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, STATUS_LED_PIN_ON);
      vscp_statuscnt = 0;
      break;

    case VSCP_LED_OFF:
    default:
      HAL_GPIO_WritePin(STATUS_LED_GPIO_Port, STATUS_LED_Pin, STATUS_LED_PIN_OFF);
      vscp_statuscnt = 0;
      break;
  }
}

///////////////////////////////////////////////////////////////////////////////
// do_button
//
// Handle button presses and releases reported by the millisecond tick.
//

static void
do_button(void)
{
  uint8_t events;

  __disable_irq();
  events       = s_btn_events;
  s_btn_events = 0;
  __enable_irq();

  if (events & BTN_EVENT_PRESSED) {

    s_status |= BLINKY_STATUS_BTN_ON;

    if ((VSCP_STATE_ACTIVE == vscp_node_state) && (g_persist.control & BLINKY_CTRL_ENABLE_BTN)) {

      if (g_persist.control & BLINKY_CTRL_ENABLE_BTN_TURNON) {
        send_event(VSCP_CLASS1_CONTROL,
                   VSCP_TYPE_CONTROL_TURNON,
                   g_persist.button_byte0,
                   g_persist.button_zone,
                   g_persist.button_subzone);
      }

      if (g_persist.control & BLINKY_CTRL_ENABLE_BTN_START) {
        send_event(VSCP_CLASS1_INFORMATION,
                   VSCP_TYPE_INFORMATION_START,
                   g_persist.button_byte0,
                   g_persist.button_zone,
                   g_persist.button_subzone);
      }
    }
  }

  if (events & BTN_EVENT_RELEASED) {

    s_status &= (uint8_t) ~BLINKY_STATUS_BTN_ON;

    if (s_long_press) {
      // The release that ends a long press is not reported
      s_long_press = 0;
    }
    else if ((VSCP_STATE_ACTIVE == vscp_node_state) && (g_persist.control & BLINKY_CTRL_ENABLE_BTN) &&
             (g_persist.control & BLINKY_CTRL_ENABLE_BTN_STOP)) {
      send_event(VSCP_CLASS1_INFORMATION,
                 VSCP_TYPE_INFORMATION_STOP,
                 g_persist.button_byte0,
                 g_persist.button_zone,
                 g_persist.button_subzone);
    }
  }

  // Long press: forget the nickname and search for a new one
  if (!s_long_press && (vscp_initbtncnt > BLINKY_INIT_BUTTON_HOLD_MS)) {
    s_long_press = 1;
    LOGSTR("Long press: nickname discovery\r\n");
    vscp_nickname = VSCP_ADDRESS_FREE;
    vscp_writeNicknamePermanent(VSCP_ADDRESS_FREE);
    vscp_init();
  }
}

///////////////////////////////////////////////////////////////////////////////
// blinky_init
//

void
blinky_init(void)
{
  build_guid();

  s_status = 0;
  led_set(0, FALSE);

  persist_load();

  if (0 != vscp_can_init()) {
    LOGSTR("Failed to start CAN\r\n");
    Error_Handler();
  }

  // Initializes the storage with defaults on first start
  vscp_check_pstorage();
  vscp_init();

  LOGSTR("\r\nVSCP blinky, CAN4VSCP, STM32F103 - version %u.%u.%u\r\n",
         THIS_FIRMWARE_MAJOR_VERSION,
         THIS_FIRMWARE_MINOR_VERSION,
         THIS_FIRMWARE_RELEASE_VERSION);
  LOGSTR("GUID: ");
  for (uint8_t i = 0; i < 16; i++) {
    LOGSTR("%02X%s", s_guid[i], (i < 15) ? ":" : "\r\n");
  }
  LOGSTR("Nickname: 0x%02X\r\n", vscp_nickname);
}

///////////////////////////////////////////////////////////////////////////////
// blinky_work
//

void
blinky_work(void)
{
  uint8_t state_before = vscp_node_state;

  persist_work();
  do_button();
  do_status_led();

  // Fetch an incoming event if there is one
  vscp_getEvent();

  switch (vscp_node_state) {

    case VSCP_STATE_STARTUP:
      if (VSCP_ADDRESS_FREE == vscp_nickname) {
        // No nickname: go and find one
        vscp_node_state = VSCP_STATE_INIT;
      }
      else {
        vscp_node_state = VSCP_STATE_ACTIVE;
        vscp_goActiveState();
      }
      break;

    case VSCP_STATE_INIT:
      vscp_handleProbeState();
      break;

    case VSCP_STATE_PREACTIVE:
      // A segment controller answered the probe and will give us a nickname
      vscp_handlePreActiveState();
      vscp_imsg.flags = 0;
      if (VSCP_STATE_ACTIVE == vscp_node_state) {
        vscp_goActiveState();
      }
      break;

    case VSCP_STATE_ACTIVE:
      if (vscp_imsg.flags & VSCP_VALID_MSG) {
        if (VSCP_CLASS1_PROTOCOL == vscp_imsg.vscp_class) {
          vscp_handleProtocolEvent();
        }
        do_dm();
        vscp_imsg.flags = 0;
      }
      break;

    case VSCP_STATE_ERROR:
    default:
      vscp_error();
      vscp_imsg.flags = 0;
      break;
  }

  if ((state_before != vscp_node_state) && (VSCP_STATE_ACTIVE == vscp_node_state)) {
    LOGSTR("Active, nickname: 0x%02X\r\n", vscp_nickname);
  }

  // Once a second
  while (s_seconds_pending) {
    __disable_irq();
    s_seconds_pending--;
    __enable_irq();
    vscp_doOneSecondWork();
  }

  // Blink
  if (VSCP_STATE_ACTIVE == vscp_node_state) {
    uint16_t interval = get_blink_interval();
    if ((g_persist.control & BLINKY_CTRL_ENABLE_LED) && interval && (s_blink_cnt >= interval)) {
      s_blink_cnt = 0;
      led_toggle(FALSE);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// blinky_tick_1ms
//
// Called from the SysTick interrupt.
//

void
blinky_tick_1ms(void)
{
  // Timers used by vscp-firmware.c
  vscp_timer++;
  vscp_configtimer++;
  vscp_statuscnt++;

  if (g_persist.control & BLINKY_CTRL_ENABLE_MS_COUNTER) {
    s_ms_counter++;
  }

  if (s_blink_cnt < 0xffffu) {
    s_blink_cnt++;
  }

  if (++s_ms_in_second >= 1000u) {
    s_ms_in_second = 0;
    if (s_seconds_pending < 0xffu) {
      s_seconds_pending++;
    }
  }

  // Debounce the button
  uint8_t raw = (BTN_PIN_PRESSED == HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_Pin)) ? 1 : 0;

  if (raw != s_btn_raw) {
    s_btn_raw      = raw;
    s_btn_debounce = 0;
  }
  else if (s_btn_debounce < BLINKY_DEBOUNCE_MS) {
    s_btn_debounce++;
  }
  else if (raw != s_btn_stable) {
    s_btn_stable = raw;
    s_btn_events |= raw ? BTN_EVENT_PRESSED : BTN_EVENT_RELEASED;
  }

  // vscp_initbtncnt is the time the button has been held
  if (s_btn_stable) {
    if (vscp_initbtncnt < 0xffffu) {
      vscp_initbtncnt++;
    }
  }
  else {
    vscp_initbtncnt = 0;
  }
}

// ---------------------------------------------------------------------------
//                     Callbacks for vscp-firmware.c
// ---------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
// Firmware version
//

uint8_t
vscp_getMajorVersion(void)
{
  return THIS_FIRMWARE_MAJOR_VERSION;
}

uint8_t
vscp_getMinorVersion(void)
{
  return THIS_FIRMWARE_MINOR_VERSION;
}

uint8_t
vscp_getSubMinorVersion(void)
{
  return THIS_FIRMWARE_RELEASE_VERSION;
}

///////////////////////////////////////////////////////////////////////////////
// GUID. Index 0 is the most significant byte (register 0xD0).
//

uint8_t
vscp_getGUID(uint8_t idx)
{
  return (idx < 16) ? s_guid[idx] : 0;
}

void
vscp_setGUID(uint8_t idx, uint8_t data)
{
  // The GUID comes from the unique id of the MCU and can not be changed
  (void) idx;
  (void) data;
}

///////////////////////////////////////////////////////////////////////////////
// User id (standard registers 0x84-0x88)
//

uint8_t
vscp_getUserID(uint8_t idx)
{
  return (idx < sizeof(g_persist.userid)) ? g_persist.userid[idx] : 0;
}

void
vscp_setUserID(uint8_t idx, uint8_t data)
{
  if ((idx < sizeof(g_persist.userid)) && (g_persist.userid[idx] != data)) {
    g_persist.userid[idx] = data;
    persist_changed();
  }
}

///////////////////////////////////////////////////////////////////////////////
// Manufacturer id (standard registers 0x89-0x90). Not used by this demo.
//

uint8_t
vscp_getManufacturerId(uint8_t idx)
{
  (void) idx;
  return 0;
}

void
vscp_setManufacturerId(uint8_t idx, uint8_t data)
{
  (void) idx;
  (void) data;
}

///////////////////////////////////////////////////////////////////////////////
// Misc. standard register content
//

uint8_t
vscp_getBootLoaderAlgorithm(void)
{
  return VSCP_BOOTLOADER_NONE;
}

uint8_t
vscp_getBufferSize(void)
{
  return 8; // A CAN frame holds eight data bytes
}

uint8_t
vscp_getRegisterPagesUsed(void)
{
  return 1;
}

uint8_t
vscp_getMDF_URL(uint8_t idx)
{
  return (idx < (sizeof(s_mdf_url) - 1)) ? (uint8_t) s_mdf_url[idx] : 0;
}

uint32_t
vscp_getFamilyCode(void)
{
  return 0;
}

uint32_t
vscp_getFamilyType(void)
{
  return 0;
}

uint16_t
vscp_getFirmwareCode(void)
{
  return 0;
}

uint8_t
vscp_getZone(void)
{
  return g_persist.zone;
}

uint8_t
vscp_getSubzone(void)
{
  return g_persist.subzone;
}

///////////////////////////////////////////////////////////////////////////////
// Nickname
//

uint8_t
vscp_readNicknamePermanent(void)
{
  return g_persist.nickname;
}

void
vscp_writeNicknamePermanent(uint8_t nickname)
{
  if (g_persist.nickname != nickname) {
    g_persist.nickname = nickname;
    persist_changed();
  }
}

///////////////////////////////////////////////////////////////////////////////
// Control bytes. They tell if the persistent storage has been initialized.
//

uint8_t
vscp_getControlByte(uint8_t idx)
{
  return (idx < sizeof(g_persist.ctrl)) ? g_persist.ctrl[idx] : 0;
}

void
vscp_setControlByte(uint8_t idx, uint8_t ctrl)
{
  if ((idx < sizeof(g_persist.ctrl)) && (g_persist.ctrl[idx] != ctrl)) {
    g_persist.ctrl[idx] = ctrl;
    persist_changed();
  }
}

///////////////////////////////////////////////////////////////////////////////
// vscp_init_pstorage
//
// Called when the storage is used for the first time.
//

void
vscp_init_pstorage(void)
{
  memset(g_persist.userid, 0, sizeof(g_persist.userid));
  g_persist.nickname = VSCP_ADDRESS_FREE;
  set_defaults();
}

///////////////////////////////////////////////////////////////////////////////
// vscp_restoreDefaults
//
// Called when 0x55 and then 0xAA is written to register 0xA2 within one
// second.
//

void
vscp_restoreDefaults(void)
{
  set_defaults();
}

///////////////////////////////////////////////////////////////////////////////
// Page select
//

uint8_t
vscp_getPageSelect(uint8_t idx)
{
  return (0 == idx) ? (uint8_t) (vscp_page_select >> 8) : (uint8_t) (vscp_page_select & 0xff);
}

void
vscp_setPageSelect(uint8_t idx, uint8_t data)
{
  if (0 == idx) {
    vscp_page_select = (uint16_t) ((vscp_page_select & 0x00ffu) | ((uint16_t) data << 8));
  }
  else {
    vscp_page_select = (uint16_t) ((vscp_page_select & 0xff00u) | data);
  }
}

///////////////////////////////////////////////////////////////////////////////
// vscp_readAppReg
//
// Read an application register (0x00-0x7F). All registers are on page 0.
//

uint8_t
vscp_readAppReg(uint8_t reg)
{
  uint32_t counter;

  if (0 != vscp_page_select) {
    return 0;
  }

  if ((reg >= BLINKY_REG_DM_START) && (reg < (BLINKY_REG_DM_START + BLINKY_DM_SIZE))) {
    return g_persist.dm[reg - BLINKY_REG_DM_START];
  }

  switch (reg) {

    case BLINKY_REG_ZONE:
      return g_persist.zone;

    case BLINKY_REG_SUBZONE:
      return g_persist.subzone;

    case BLINKY_REG_STATUS:
      return s_status;

    case BLINKY_REG_CONTROL:
      return g_persist.control;

    case BLINKY_REG_BLINK_INTERVAL_MSB:
      return g_persist.blink_interval_msb;

    case BLINKY_REG_BLINK_INTERVAL_LSB:
      return g_persist.blink_interval_lsb;

    case BLINKY_REG_COUNTER_0:
    case BLINKY_REG_COUNTER_1:
    case BLINKY_REG_COUNTER_2:
    case BLINKY_REG_COUNTER_3:
      counter = s_ms_counter;
      return (uint8_t) (counter >> (8u * (uint8_t) (BLINKY_REG_COUNTER_3 - reg)));

    case BLINKY_REG_BUTTON_BYTE0:
      return g_persist.button_byte0;

    case BLINKY_REG_BUTTON_ZONE:
      return g_persist.button_zone;

    case BLINKY_REG_BUTTON_SUBZONE:
      return g_persist.button_subzone;

    default:
      return 0;
  }
}

///////////////////////////////////////////////////////////////////////////////
// vscp_writeAppReg
//
// Write an application register (0x00-0x7F). Returns the content of the
// register after the write, which the sender compares with what it wrote.
//

uint8_t
vscp_writeAppReg(uint8_t reg, uint8_t value)
{
  uint8_t *ppersistent = NULL;

  if (0 != vscp_page_select) {
    return (uint8_t) ~value;
  }

  if ((reg >= BLINKY_REG_DM_START) && (reg < (BLINKY_REG_DM_START + BLINKY_DM_SIZE))) {
    ppersistent = &g_persist.dm[reg - BLINKY_REG_DM_START];
  }
  else {
    switch (reg) {

      case BLINKY_REG_ZONE:
        ppersistent = &g_persist.zone;
        break;

      case BLINKY_REG_SUBZONE:
        ppersistent = &g_persist.subzone;
        break;

      case BLINKY_REG_STATUS:
        // Only the LED bit can be written. It turns the LED on or off.
        led_set((value & BLINKY_STATUS_LED_ON) ? 1 : 0, TRUE);
        return s_status;

      case BLINKY_REG_CONTROL:
        ppersistent = &g_persist.control;
        s_blink_cnt = 0;
        break;

      case BLINKY_REG_BLINK_INTERVAL_MSB:
        ppersistent = &g_persist.blink_interval_msb;
        break;

      case BLINKY_REG_BLINK_INTERVAL_LSB:
        ppersistent = &g_persist.blink_interval_lsb;
        break;

      case BLINKY_REG_COUNTER_0:
      case BLINKY_REG_COUNTER_1:
      case BLINKY_REG_COUNTER_2:
      case BLINKY_REG_COUNTER_3:
        // Any write clears the counter
        s_ms_counter = 0;
        return 0;

      case BLINKY_REG_BUTTON_BYTE0:
        ppersistent = &g_persist.button_byte0;
        break;

      case BLINKY_REG_BUTTON_ZONE:
        ppersistent = &g_persist.button_zone;
        break;

      case BLINKY_REG_BUTTON_SUBZONE:
        ppersistent = &g_persist.button_subzone;
        break;

      default:
        // Not a register of this node
        return (uint8_t) ~value;
    }
  }

  if (*ppersistent != value) {
    *ppersistent = value;
    persist_changed();
  }

  return *ppersistent;
}

///////////////////////////////////////////////////////////////////////////////
// vscp_getMatrixInfo
//
// Fill in the response to CLASS1.PROTOCOL, "get decision matrix info".
//

void
vscp_getMatrixInfo(char *pData)
{
  pData[0] = BLINKY_DM_ROWS;      // Number of rows
  pData[1] = BLINKY_REG_DM_START; // Register offset
  pData[2] = 0;                   // Start page MSB
  pData[3] = 0;                   // Start page LSB
  pData[4] = 0;                   // End page MSB
  pData[5] = 0;                   // End page LSB
  pData[6] = 0;                   // Level II row size, not used
}

///////////////////////////////////////////////////////////////////////////////
// vscp_getEmbeddedMdfInfo
//
// The MDF is on a server (see s_mdf_url), not in the node.
//

void
vscp_getEmbeddedMdfInfo(void)
{
}

///////////////////////////////////////////////////////////////////////////////
// vscp_goBootloaderMode
//
// The demo has no bootloader.
//

void
vscp_goBootloaderMode(uint8_t algorithm)
{
  (void) algorithm;
}
