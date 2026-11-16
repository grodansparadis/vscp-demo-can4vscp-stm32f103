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
  @file vscp-can.c
  CAN transport for VSCP Level I (CAN4VSCP) on the STM32F103 bxCAN.

  A VSCP Level I event is carried in one extended CAN frame:

    bit 28-26  priority (0 is highest)
    bit 25     hard coded node id
    bit 24-16  VSCP class (9 bits)
    bit 15-8   VSCP type
    bit 7-0    nickname of the sending node

  Implements getVSCPFrame() and sendVSCPFrame(), the two transport callbacks
  that vscp-firmware.c needs.
*/

#include "vscp-can.h"

#include "can.h"
#include "main.h"

#include "vscp-compiler.h"
#include "vscp-projdefs.h"

#include <string.h>
#include <vscp-firmware-helper.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// How long to wait for a free transmit mailbox (ms)
#define VSCP_CAN_TX_TIMEOUT_MS 20u

typedef struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} can_frame_t;

// Receive FIFO. Written by the receive interrupt, read by the main loop.
static can_frame_t s_rxfifo[VSCP_CAN_RX_FIFO_SIZE];
static volatile uint8_t s_rxhead; // Next position to write
static volatile uint8_t s_rxtail; // Next position to read
static volatile uint32_t s_overruns;

///////////////////////////////////////////////////////////////////////////////
// vscp_can_init
//

int
vscp_can_init(void)
{
  CAN_FilterTypeDef filter = { 0 };

  s_rxhead   = 0;
  s_rxtail   = 0;
  s_overruns = 0;

  // Accept all frames. Standard and remote frames are dropped in the
  // receive interrupt.
  filter.FilterBank           = 0;
  filter.FilterMode           = CAN_FILTERMODE_IDMASK;
  filter.FilterScale          = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh         = 0;
  filter.FilterIdLow          = 0;
  filter.FilterMaskIdHigh     = 0;
  filter.FilterMaskIdLow      = 0;
  filter.FilterFIFOAssignment = CAN_RX_FIFO0;
  filter.FilterActivation     = ENABLE;

  if (HAL_OK != HAL_CAN_ConfigFilter(&hcan, &filter)) {
    return -1;
  }

  if (HAL_OK != HAL_CAN_Start(&hcan)) {
    return -1;
  }

  if (HAL_OK != HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING)) {
    return -1;
  }

  return 0;
}

///////////////////////////////////////////////////////////////////////////////
// vscp_can_get_overruns
//

uint32_t
vscp_can_get_overruns(void)
{
  return s_overruns;
}

///////////////////////////////////////////////////////////////////////////////
// HAL_CAN_RxFifo0MsgPendingCallback
//
// Called from the CAN receive interrupt.
//

void
HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *phcan)
{
  CAN_RxHeaderTypeDef hdr;
  uint8_t data[8];

  while (HAL_CAN_GetRxFifoFillLevel(phcan, CAN_RX_FIFO0) > 0) {

    if (HAL_OK != HAL_CAN_GetRxMessage(phcan, CAN_RX_FIFO0, &hdr, data)) {
      break;
    }

    // VSCP only uses extended data frames
    if ((CAN_ID_EXT != hdr.IDE) || (CAN_RTR_DATA != hdr.RTR)) {
      continue;
    }

    uint8_t next = (uint8_t) ((s_rxhead + 1u) % VSCP_CAN_RX_FIFO_SIZE);
    if (next == s_rxtail) {
      s_overruns++; // FIFO full, the frame is lost
      continue;
    }

    s_rxfifo[s_rxhead].id  = hdr.ExtId;
    s_rxfifo[s_rxhead].dlc = (uint8_t) ((hdr.DLC > 8u) ? 8u : hdr.DLC);
    memcpy(s_rxfifo[s_rxhead].data, data, 8);
    s_rxhead = next;
  }
}

///////////////////////////////////////////////////////////////////////////////
// getVSCPFrame
//
// Callback for vscp-firmware.c. Fetch the next received event, if any.
//

int8_t
getVSCPFrame(uint16_t *pvscpclass,
             uint8_t *pvscptype,
             uint8_t *pNodeId,
             uint8_t *pPriority,
             uint8_t *pSize,
             uint8_t *pData)
{
  if (s_rxtail == s_rxhead) {
    return FALSE; // Nothing received
  }

  const can_frame_t *pframe = &s_rxfifo[s_rxtail];

  *pvscpclass = vscp_fwhlp_getVscpClassFromCANALid(pframe->id);
  *pvscptype  = (uint8_t) vscp_fwhlp_getVscpTypeFromCANALid(pframe->id);
  *pNodeId    = vscp_fwhlp_getNicknameFromCANALid(pframe->id);
  *pPriority  = (uint8_t) ((pframe->id >> 26) & 0x07u);
  *pSize      = pframe->dlc;
  memcpy(pData, pframe->data, pframe->dlc);

  s_rxtail = (uint8_t) ((s_rxtail + 1u) % VSCP_CAN_RX_FIFO_SIZE);

  return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// sendVSCPFrame
//
// Callback for vscp-firmware.c. Send one event.
//

int8_t
sendVSCPFrame(uint16_t vscpclass,
              uint8_t vscptype,
              uint8_t nodeid,
              uint8_t priority,
              uint8_t size,
              uint8_t *pData)
{
  CAN_TxHeaderTypeDef hdr = { 0 };
  uint32_t mailbox;
  uint8_t data[8]         = { 0 };

  if (size > 8u) {
    return FALSE;
  }

  hdr.ExtId = vscp_fwhlp_getCANALidFromData(priority, vscpclass, vscptype) | nodeid;
  hdr.IDE   = CAN_ID_EXT;
  hdr.RTR   = CAN_RTR_DATA;
  hdr.DLC   = size;

  if ((size > 0u) && (NULL != pData)) {
    memcpy(data, pData, size);
  }

  // Wait a short while for a free transmit mailbox
  uint32_t start = HAL_GetTick();
  while (0u == HAL_CAN_GetTxMailboxesFreeLevel(&hcan)) {
    if ((HAL_GetTick() - start) > VSCP_CAN_TX_TIMEOUT_MS) {
      return FALSE;
    }
  }

  if (HAL_OK != HAL_CAN_AddTxMessage(&hcan, &hdr, data, &mailbox)) {
    return FALSE;
  }

  return TRUE;
}
