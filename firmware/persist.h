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
  @file persist.h
  Persistent storage for the node in the last page of the internal flash.

  The STM32F103 has no EEPROM. All persistent values are kept in a RAM copy
  that is written to one flash page a short while after the last change, so
  a burst of register writes costs one erase cycle instead of one per byte.
*/

#ifndef PERSIST_H
#define PERSIST_H

#include <stdint.h>

#include "blinky.h"

#ifdef __cplusplus
extern "C" {
#endif

// Storage page: the last 1 KB page of a 64 KB device. The linker script
// keeps the firmware out of this page.
#ifndef PERSIST_FLASH_ADDR
#define PERSIST_FLASH_ADDR 0x0800FC00u
#endif
#define PERSIST_FLASH_SIZE 0x400u

// Time from the last change until the RAM copy is written to flash (ms)
#define PERSIST_COMMIT_DELAY_MS 1000u

#define PERSIST_MAGIC 0x56534331u // "VSC1"

/*!
  The persistent data. The size must be a multiple of four bytes.
*/
typedef struct {
  uint32_t magic;             // PERSIST_MAGIC when the page holds valid data
  uint8_t ctrl[2];            // VSCP control bytes (0x55/0xAA when initialized)
  uint8_t nickname;           // Node nickname (0xFF = none)
  uint8_t zone;               // Register 0
  uint8_t subzone;            // Register 1
  uint8_t control;            // Register 3
  uint8_t blink_interval_msb; // Register 4
  uint8_t blink_interval_lsb; // Register 5
  uint8_t button_byte0;       // Register 10
  uint8_t button_zone;        // Register 11
  uint8_t button_subzone;     // Register 12
  uint8_t userid[5];          // Standard registers 0x84-0x88
  uint8_t dm[BLINKY_DM_SIZE]; // Decision matrix, registers 0x60-0x7F
} persist_t;

/// The RAM copy. Change it, then call persist_changed().
extern persist_t g_persist;

/*!
  Read the stored data into g_persist. If the flash page holds no valid data
  the RAM copy is filled with 0xFF, which makes the VSCP firmware initialize
  it (see vscp_init_pstorage()).
*/
void
persist_load(void);

/*!
  Tell that g_persist has been changed and needs to be saved.
*/
void
persist_changed(void);

/*!
  Save to flash if there are unsaved changes old enough. Call from the main
  loop.
*/
void
persist_work(void);

/*!
  Save to flash now if there are unsaved changes.
  @return 0 on success.
*/
int
persist_commit(void);

#ifdef __cplusplus
}
#endif

#endif // PERSIST_H
