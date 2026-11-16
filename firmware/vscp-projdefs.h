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
  @file vscp-projdefs.h
  Project configuration for the VSCP firmware code (vscp-firmware.c and
  vscp-firmware-helper.c).
*/

#ifndef _VSCP_PROJDEFS_H_
#define _VSCP_PROJDEFS_H_

#include <stdio.h>

/*
  Firmware version, reported in the standard registers 0x94-0x96.
*/
#define THIS_FIRMWARE_MAJOR_VERSION   (0u)
#define THIS_FIRMWARE_MINOR_VERSION   (1u)
#define THIS_FIRMWARE_RELEASE_VERSION (0u)

/*
  Debug output on USART1 (PA9, 115200 8N1). Comment out to remove all
  printouts and save about 2 KB of flash.
*/
#define BLINKY_DEBUG

#ifdef BLINKY_DEBUG
#define LOGSTR(...) printf(__VA_ARGS__)
#else
#define LOGSTR(...)
#endif

/*
  ----------------------------------------------------------------------------
                        VSCP firmware helper defines
  ----------------------------------------------------------------------------
  The helper is only used for the CAN id conversions here, so the optional
  parts stay disabled. That keeps the helper out of the 64 KB flash.

  #define VSCP_FWHLP_BINARY_FRAME_SUPPORT
  #define VSCP_FWHLP_CRYPTO_SUPPORT
  #define VSCP_FWHLP_JSON_SUPPORT
  #define VSCP_FWHLP_XML_SUPPORT
*/

/*
  ----------------------------------------------------------------------------
                        VSCP Level I firmware defines
  ----------------------------------------------------------------------------
  Optional parts of vscp-firmware.c. None are needed for the blinky demo.

  #define VSCP_FIRMWARE_ENABLE_ERROR_REPORTING
  #define VSCP_FIRMWARE_ENABLE_LOGGING
  #define ENABLE_WRITE_2PROTECTED_LOCATIONS
  #define DROP_NICKNAME_EXTENDED_FEATURES
  #define EMBEDDED_MDF
*/

#endif // _VSCP_PROJDEFS_H_
