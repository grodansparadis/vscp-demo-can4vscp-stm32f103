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
  @file vscp-can.h
  CAN transport for VSCP Level I (CAN4VSCP) on the STM32F103 bxCAN.
*/

#ifndef VSCP_CAN_H
#define VSCP_CAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Number of received frames that can wait for the main loop
#define VSCP_CAN_RX_FIFO_SIZE 16u

/*!
  Set an accept all filter for extended frames, enable the receive
  interrupt and start the CAN controller.
  @return 0 on success.
*/
int
vscp_can_init(void);

/*!
  Number of frames lost because the receive FIFO was full.
*/
uint32_t
vscp_can_get_overruns(void);

/*
  The two transport callbacks of vscp-firmware.c (declared in
  vscp-firmware.h) are implemented in vscp-can.c:

    int8_t getVSCPFrame(uint16_t *pvscpclass, uint8_t *pvscptype,
                        uint8_t *pNodeId, uint8_t *pPriority,
                        uint8_t *pSize, uint8_t *pData);

    int8_t sendVSCPFrame(uint16_t vscpclass, uint8_t vscptype,
                         uint8_t nodeid, uint8_t priority,
                         uint8_t size, uint8_t *pData);
*/
int8_t
getVSCPFrame(uint16_t *pvscpclass,
             uint8_t *pvscptype,
             uint8_t *pNodeId,
             uint8_t *pPriority,
             uint8_t *pSize,
             uint8_t *pData);

int8_t
sendVSCPFrame(uint16_t vscpclass,
              uint8_t vscptype,
              uint8_t nodeid,
              uint8_t priority,
              uint8_t size,
              uint8_t *pData);

#ifdef __cplusplus
}
#endif

#endif // VSCP_CAN_H
