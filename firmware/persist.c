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
  @file persist.c
  Persistent storage for the node in the last page of the internal flash.
*/

#include "main.h"

#include "persist.h"

#include <string.h>

_Static_assert((sizeof(persist_t) % 4u) == 0u, "persist_t must be a multiple of four bytes");
_Static_assert(sizeof(persist_t) <= PERSIST_FLASH_SIZE, "persist_t must fit in the storage page");

persist_t g_persist;

static uint8_t s_dirty;        // Set when g_persist differs from flash
static uint32_t s_changed_tick; // Time of the last change

///////////////////////////////////////////////////////////////////////////////
// persist_load
//

void
persist_load(void)
{
  const persist_t *pflash = (const persist_t *) PERSIST_FLASH_ADDR;

  if (PERSIST_MAGIC == pflash->magic) {
    memcpy(&g_persist, pflash, sizeof(g_persist));
  }
  else {
    memset(&g_persist, 0xff, sizeof(g_persist));
    g_persist.magic = PERSIST_MAGIC;
  }

  s_dirty = 0;
}

///////////////////////////////////////////////////////////////////////////////
// persist_changed
//

void
persist_changed(void)
{
  s_dirty        = 1;
  s_changed_tick = HAL_GetTick();
}

///////////////////////////////////////////////////////////////////////////////
// persist_work
//

void
persist_work(void)
{
  if (s_dirty && ((HAL_GetTick() - s_changed_tick) >= PERSIST_COMMIT_DELAY_MS)) {
    persist_commit();
  }
}

///////////////////////////////////////////////////////////////////////////////
// persist_commit
//

int
persist_commit(void)
{
  int rv = 0;

  if (!s_dirty) {
    return 0;
  }

  // Nothing to do if flash already holds the same data
  if (0 == memcmp(&g_persist, (const void *) PERSIST_FLASH_ADDR, sizeof(g_persist))) {
    s_dirty = 0;
    return 0;
  }

  FLASH_EraseInitTypeDef erase = { 0 };
  uint32_t page_error          = 0;

  erase.TypeErase   = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = PERSIST_FLASH_ADDR;
  erase.NbPages     = 1;

  HAL_FLASH_Unlock();

  if (HAL_OK != HAL_FLASHEx_Erase(&erase, &page_error)) {
    rv = -1;
  }
  else {
    const uint32_t *pword = (const uint32_t *) (const void *) &g_persist;
    for (uint32_t i = 0; i < (sizeof(g_persist) / 4u); i++) {
      if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, PERSIST_FLASH_ADDR + (i * 4u), pword[i])) {
        rv = -1;
        break;
      }
    }
  }

  HAL_FLASH_Lock();

  if (0 == rv) {
    s_dirty = 0;
  }
  else {
    // Try again later
    s_changed_tick = HAL_GetTick();
  }

  return rv;
}
