// Host test for the blinky node. The node code (blinky.c, persist.c,
// vscp-can.c) and the VSCP firmware are built for the PC with a stand-in
// for the STM32 HAL (stubs/main.h). The test plays the role of the CAN bus,
// the button and the clock.

#include "main.h"
#include "can.h"

#include "blinky.h"
#include "persist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vscp-class.h>
#include <vscp-firmware.h>
#include <vscp-type.h>

// ----------------------------- HAL stand-in ---------------------------------

GPIO_PinState g_test_pins[3] = { GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_SET };
GPIO_TypeDef g_test_ports[3] = { 0, 1, 2 };
uint8_t g_test_flash[1024];
CAN_HandleTypeDef hcan;

static uint32_t s_tick;
static unsigned s_led_writes; // Changes of the LED pin
static unsigned s_erases;

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
  (void) pin;
  if ((0 == *port) && (g_test_pins[0] != state)) s_led_writes++;
  g_test_pins[*port] = state;
}
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
  HAL_GPIO_WritePin(port, pin, (GPIO_PIN_SET == g_test_pins[*port]) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) { (void) pin; return g_test_pins[*port]; }
uint32_t HAL_GetTick(void) { return s_tick; }
uint32_t HAL_GetUIDw0(void) { return 0x11223344u; }
uint32_t HAL_GetUIDw1(void) { return 0x55667788u; }
uint32_t HAL_GetUIDw2(void) { return 0x99AABBCCu; }
void Error_Handler(void) { fprintf(stderr, "Error_Handler called\n"); exit(2); }

HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *perase, uint32_t *perror)
{
  (void) perror;
  memset((void *) perase->PageAddress, 0xff, 1024);
  s_erases++;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uintptr_t address, uint64_t data)
{
  (void) type;
  // Like real flash: bits can only be cleared
  uint32_t word;
  memcpy(&word, (void *) address, 4);
  word &= (uint32_t) data;
  memcpy((void *) address, &word, 4);
  return HAL_OK;
}

// CAN: frames waiting to be read by the node and frames the node has sent
typedef struct { uint32_t id; uint32_t ide; uint8_t dlc; uint8_t data[8]; } frame_t;
#define QSIZE 64
static frame_t s_rx[QSIZE]; static unsigned s_rxn;
static frame_t s_tx[QSIZE]; static unsigned s_txn;

HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, const CAN_FilterTypeDef *f) { (void) h; (void) f; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h) { (void) h; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t its) { (void) h; (void) its; return HAL_OK; }
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *h, uint32_t fifo) { (void) h; (void) fifo; return s_rxn; }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *hdr, uint8_t data[])
{
  (void) h; (void) fifo;
  if (!s_rxn) return HAL_ERROR;
  memset(hdr, 0, sizeof(*hdr));
  hdr->IDE = s_rx[0].ide; hdr->ExtId = s_rx[0].id; hdr->StdId = s_rx[0].id & 0x7ff;
  hdr->RTR = CAN_RTR_DATA; hdr->DLC = s_rx[0].dlc;
  memcpy(data, s_rx[0].data, 8);
  memmove(&s_rx[0], &s_rx[1], sizeof(frame_t) * (--s_rxn));
  return HAL_OK;
}
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *h) { (void) h; return 3; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *hdr, const uint8_t data[], uint32_t *pmailbox)
{
  (void) h; *pmailbox = 0;
  if (s_txn >= QSIZE) { fprintf(stderr, "tx queue full\n"); exit(2); }
  s_tx[s_txn].id = hdr->ExtId; s_tx[s_txn].ide = hdr->IDE; s_tx[s_txn].dlc = (uint8_t) hdr->DLC;
  memcpy(s_tx[s_txn].data, data, 8);
  s_txn++;
  return HAL_OK;
}

// ------------------------------ Test helpers --------------------------------

static int s_failed;
#define CHECK(cond) do { if (!(cond)) { printf("FAILED line %d: %s\n", __LINE__, #cond); s_failed++; } } while (0)

static uint32_t make_id(uint8_t priority, uint16_t vscp_class, uint8_t vscp_type, uint8_t nickname)
{
  return ((uint32_t) priority << 26) | ((uint32_t) vscp_class << 16) | ((uint32_t) vscp_type << 8) | nickname;
}

static void run_ms(unsigned ms)
{
  while (ms--) {
    s_tick++;
    blinky_tick_1ms();
    blinky_work();
  }
}

// Put a frame on the bus for the node
static void inject(uint32_t ide, uint16_t vscp_class, uint8_t vscp_type, uint8_t from, uint8_t dlc, const uint8_t *data)
{
  frame_t *p = &s_rx[s_rxn++];
  memset(p, 0, sizeof(*p));
  p->ide = ide; p->id = make_id(3, vscp_class, vscp_type, from); p->dlc = dlc;
  if (dlc) memcpy(p->data, data, dlc);
  HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
  run_ms(2);
}

// Find and remove the first sent frame with this class and type
static int sent(uint16_t vscp_class, uint8_t vscp_type, frame_t *pout)
{
  for (unsigned i = 0; i < s_txn; i++) {
    if ((((s_tx[i].id >> 16) & 0x1ff) == vscp_class) && (((s_tx[i].id >> 8) & 0xff) == vscp_type)) {
      if (pout) *pout = s_tx[i];
      memmove(&s_tx[i], &s_tx[i + 1], sizeof(frame_t) * (s_txn - i - 1));
      s_txn--;
      return 1;
    }
  }
  return 0;
}

static int read_reg(uint8_t nickname, uint8_t reg)
{
  uint8_t d[2] = { nickname, reg };
  frame_t f;
  inject(CAN_ID_EXT, VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_READ_REGISTER, 0, 2, d);
  if (!sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_RW_RESPONSE, &f) || (f.data[0] != reg)) return -1;
  return f.data[1];
}

static int write_reg(uint8_t nickname, uint8_t reg, uint8_t value)
{
  uint8_t d[3] = { nickname, reg, value };
  frame_t f;
  inject(CAN_ID_EXT, VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_WRITE_REGISTER, 0, 3, d);
  if (!sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_RW_RESPONSE, &f) || (f.data[0] != reg)) return -1;
  return f.data[1];
}

#define LED_IS_ON() (GPIO_PIN_RESET == g_test_pins[0])

// --------------------------------- Tests ------------------------------------

int main(void)
{
  frame_t f;
  memset(g_test_flash, 0xff, sizeof(g_test_flash));

  // --- First start: no nickname, the node probes for one ---
  blinky_init();
  run_ms(10);
  CHECK(VSCP_STATE_INIT == vscp_node_state);
  CHECK(sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_NEW_NODE_ONLINE, &f));
  CHECK(f.id == make_id(0, 0, VSCP_TYPE_PROTOCOL_NEW_NODE_ONLINE, 0xff)); // probe for the segment controller
  CHECK((CAN_ID_EXT == f.ide) && (1 == f.dlc) && (0 == f.data[0]));

  // No segment controller answers. After three timeouts nickname 1 is probed.
  run_ms(3100);
  s_txn = 0;
  run_ms(1100);
  CHECK(sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_NEW_NODE_ONLINE, &f));
  CHECK(1 == f.data[0]);

  // Another node owns nickname 1 and says so. The node tries 2 and gets it.
  inject(CAN_ID_EXT, VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_PROBE_ACK, 1, 0, NULL);
  s_txn = 0;
  run_ms(3200);
  CHECK(VSCP_STATE_ACTIVE == vscp_node_state);
  CHECK(2 == vscp_nickname);
  f.id = 0;
  while (sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_NEW_NODE_ONLINE, &f)) { }
  CHECK(f.id == make_id(0, 0, VSCP_TYPE_PROTOCOL_NEW_NODE_ONLINE, 2)); // "I am here" from nickname 2
  CHECK(2 == f.data[0]);

  // The nickname and the defaults reach flash
  run_ms(1200);
  CHECK(PERSIST_MAGIC == ((persist_t *) g_test_flash)->magic);
  CHECK(2 == ((persist_t *) g_test_flash)->nickname);
  CHECK(0x55 == ((persist_t *) g_test_flash)->ctrl[0] && 0xAA == ((persist_t *) g_test_flash)->ctrl[1]);

  // --- Registers ---
  s_txn = 0;
  CHECK(BLINKY_DEFAULT_CONTROL == read_reg(2, BLINKY_REG_CONTROL));
  CHECK(0x01 == read_reg(2, BLINKY_REG_BLINK_INTERVAL_MSB));
  CHECK(0xF4 == read_reg(2, BLINKY_REG_BLINK_INTERVAL_LSB));
  CHECK(0xFD == read_reg(2, 0xD0)); // GUID MSB
  CHECK(0x02 == read_reg(2, 0xD2));
  CHECK(0x11 == read_reg(2, 0xD3)); // First byte of the unique id
  CHECK(0xCC == read_reg(2, 0xDE));
  CHECK(0x00 == read_reg(2, 0xDF));
  CHECK('e' == read_reg(2, 0xE0)); // MDF URL
  CHECK(2 == read_reg(2, 0x91));   // Nickname
  CHECK(-1 == read_reg(7, BLINKY_REG_CONTROL)); // Another node is asked: no answer

  // A standard (11-bit) frame is not VSCP and is ignored
  { uint8_t d[2] = { 2, BLINKY_REG_CONTROL };
    inject(CAN_ID_STD, VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_READ_REGISTER, 0, 2, d);
    CHECK(!sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_RW_RESPONSE, NULL)); }

  // The millisecond counter is off by default (control bit 0)
  CHECK(0 == read_reg(2, BLINKY_REG_COUNTER_3));

  // --- Blink: default 500 ms, then 100 ms ---
  s_led_writes = 0;
  run_ms(2050);
  CHECK(4 == s_led_writes);
  CHECK(0 == write_reg(2, BLINKY_REG_BLINK_INTERVAL_MSB, 0));
  CHECK(100 == write_reg(2, BLINKY_REG_BLINK_INTERVAL_LSB, 100));
  s_led_writes = 0;
  run_ms(1000);
  CHECK((s_led_writes >= 9) && (s_led_writes <= 10));
  CHECK(!sent(VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_ON, NULL)); // Blinking sends no events

  // --- Restart: the settings survive ---
  run_ms(1200);
  unsigned erases = s_erases;
  s_txn = 0;
  blinky_init();
  run_ms(10);
  CHECK(VSCP_STATE_ACTIVE == vscp_node_state);
  CHECK(2 == vscp_nickname);
  CHECK(sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_NEW_NODE_ONLINE, &f) && (2 == f.data[0]));
  CHECK(100 == read_reg(2, BLINKY_REG_BLINK_INTERVAL_LSB));
  run_ms(1500);
  CHECK(erases == s_erases); // Nothing changed, so nothing was written

  // --- Stop blinking, enable the counter ---
  CHECK(0x4F == write_reg(2, BLINKY_REG_CONTROL, 0x4F));
  s_led_writes = 0;
  run_ms(500);
  CHECK(0 == s_led_writes);
  { int c = (read_reg(2, BLINKY_REG_COUNTER_2) << 8) | read_reg(2, BLINKY_REG_COUNTER_3);
    CHECK((c >= 500) && (c <= 520)); }
  CHECK(0 == write_reg(2, BLINKY_REG_COUNTER_0, 0x12)); // Any write clears it
  CHECK(read_reg(2, BLINKY_REG_COUNTER_3) < 10);

  // --- LED through the status register ---
  write_reg(2, BLINKY_REG_STATUS, 0); // Blinking may have left the LED on
  s_txn = 0;
  CHECK(BLINKY_STATUS_LED_ON == write_reg(2, BLINKY_REG_STATUS, BLINKY_STATUS_LED_ON));
  CHECK(LED_IS_ON());
  CHECK(sent(VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_ON, &f) && (3 == f.dlc));
  CHECK(0 == write_reg(2, BLINKY_REG_STATUS, 0));
  CHECK(!LED_IS_ON());
  CHECK(sent(VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_OFF, NULL));

  // --- Button ---
  CHECK(7 == write_reg(2, BLINKY_REG_BUTTON_BYTE0, 7));
  CHECK(8 == write_reg(2, BLINKY_REG_BUTTON_ZONE, 8));
  CHECK(9 == write_reg(2, BLINKY_REG_BUTTON_SUBZONE, 9));
  s_txn = 0;
  g_test_pins[2] = GPIO_PIN_RESET; // Pressed
  run_ms(10);
  CHECK(0 == s_txn);               // Still bouncing
  run_ms(30);
  CHECK(sent(VSCP_CLASS1_CONTROL, VSCP_TYPE_CONTROL_TURNON, &f));
  CHECK(f.id == make_id(3, VSCP_CLASS1_CONTROL, VSCP_TYPE_CONTROL_TURNON, 2));
  CHECK((3 == f.dlc) && (7 == f.data[0]) && (8 == f.data[1]) && (9 == f.data[2]));
  // START is a CLASS1.INFORMATION event (class 20, type 25).
  // Its first data byte is reserved and must be zero; zone/subzone follow.
  CHECK(sent(VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_START, &f));
  CHECK(f.id == make_id(3, VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_START, 2));
  CHECK((CAN_ID_EXT == f.ide) && (3 == f.dlc));
  CHECK((0 == f.data[0]) && (8 == f.data[1]) && (9 == f.data[2]));
  CHECK(!sent(VSCP_CLASS1_CONTROL, VSCP_TYPE_CONTROL_START, NULL));
  CHECK(BLINKY_STATUS_BTN_ON == read_reg(2, BLINKY_REG_STATUS));

  g_test_pins[2] = GPIO_PIN_SET;   // Released
  run_ms(40);
  // STOP is CLASS1.INFORMATION (class 20, type 24), with the same payload.
  CHECK(sent(VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_STOP, &f));
  CHECK(f.id == make_id(3, VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_STOP, 2));
  CHECK((CAN_ID_EXT == f.ide) && (3 == f.dlc));
  CHECK((0 == f.data[0]) && (8 == f.data[1]) && (9 == f.data[2]));
  CHECK(!sent(VSCP_CLASS1_CONTROL, VSCP_TYPE_CONTROL_STOP, NULL));
  CHECK(0 == s_txn);

  // --- Decision matrix ---
  // Row 0: TURN-ON from any node turns the LED on.
  // Row 1: TURN-OFF from node 9 to our zone turns it off.
  CHECK(5 == write_reg(2, BLINKY_REG_ZONE, 5));
  { const uint8_t dm[16] = { 0, 0x80, 0xff, VSCP_CLASS1_CONTROL, 0xff, VSCP_TYPE_CONTROL_TURNON, BLINKY_ACTION_LED_ON, 0,
                             9, 0x80 | 0x40 | 0x10, 0xff, VSCP_CLASS1_CONTROL, 0xff, VSCP_TYPE_CONTROL_TURNOFF, BLINKY_ACTION_LED_OFF, 0 };
    for (uint8_t i = 0; i < 16; i++) CHECK(dm[i] == write_reg(2, (uint8_t) (BLINKY_REG_DM_START + i), dm[i])); }
  { uint8_t d[1] = { 2 };
    inject(CAN_ID_EXT, VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_GET_MATRIX_INFO, 0, 1, d);
    CHECK(sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_GET_MATRIX_INFO_RESPONSE, &f));
    CHECK((BLINKY_DM_ROWS == f.data[0]) && (BLINKY_REG_DM_START == f.data[1])); }
  s_txn = 0;
  { uint8_t d[3] = { 0, 1, 1 };
    inject(CAN_ID_EXT, VSCP_CLASS1_CONTROL, VSCP_TYPE_CONTROL_TURNON, 33, 3, d);
    CHECK(LED_IS_ON());
    CHECK(sent(VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_ON, NULL));
    d[1] = 5; // Our zone, but the wrong sender
    inject(CAN_ID_EXT, VSCP_CLASS1_CONTROL, VSCP_TYPE_CONTROL_TURNOFF, 33, 3, d);
    CHECK(LED_IS_ON());
    d[1] = 6; // The right sender, another zone
    inject(CAN_ID_EXT, VSCP_CLASS1_CONTROL, VSCP_TYPE_CONTROL_TURNOFF, 9, 3, d);
    CHECK(LED_IS_ON());
    d[1] = 5; // The right sender and zone
    inject(CAN_ID_EXT, VSCP_CLASS1_CONTROL, VSCP_TYPE_CONTROL_TURNOFF, 9, 3, d);
    CHECK(!LED_IS_ON());
    CHECK(sent(VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_OFF, NULL)); }

  // --- Restore defaults: 0x55 then 0xAA to register 0xA2 ---
  CHECK(0x55 == write_reg(2, 0xA2, 0x55));
  CHECK(0xAA == write_reg(2, 0xA2, 0xAA));
  CHECK(0xF4 == read_reg(2, BLINKY_REG_BLINK_INTERVAL_LSB));
  CHECK(0 == read_reg(2, BLINKY_REG_DM_START + 1));
  CHECK(2 == vscp_nickname); // The nickname is kept

  // --- A flood of frames does not break the node ---
  { uint8_t d[2] = { 2, BLINKY_REG_ZONE };
    for (int i = 0; i < 40; i++) {
      frame_t *p = &s_rx[s_rxn++];
      memset(p, 0, sizeof(*p));
      p->ide = CAN_ID_EXT; p->id = make_id(3, VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_READ_REGISTER, 0); p->dlc = 2;
      memcpy(p->data, d, 2);
    }
    s_txn = 0;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
    run_ms(50);
    CHECK((s_txn > 0) && (s_txn < 40)); // Some were dropped, the rest answered
    s_txn = 0;
    CHECK(0 == read_reg(2, BLINKY_REG_ZONE)); }

  // --- Long press: drop the nickname and search again ---
  s_txn = 0;
  g_test_pins[2] = GPIO_PIN_RESET;
  run_ms(1900);
  CHECK(VSCP_STATE_ACTIVE == vscp_node_state);
  s_txn = 0;
  run_ms(200);
  CHECK(VSCP_STATE_INIT == vscp_node_state);
  CHECK(VSCP_ADDRESS_FREE == vscp_nickname);
  CHECK(sent(VSCP_CLASS1_PROTOCOL, VSCP_TYPE_PROTOCOL_NEW_NODE_ONLINE, &f) && (0 == f.data[0]));
  run_ms(3000); // Still held: must not restart the search again and again
  g_test_pins[2] = GPIO_PIN_SET;
  run_ms(100);
  CHECK(!sent(VSCP_CLASS1_INFORMATION, VSCP_TYPE_INFORMATION_STOP, NULL));
  run_ms(4000);
  CHECK(VSCP_STATE_ACTIVE == vscp_node_state);
  CHECK(1 == vscp_nickname); // Nobody answered for 1 this time

  if (s_failed) {
    printf("%d check(s) failed\n", s_failed);
    return 1;
  }
  printf("All checks passed\n");
  return 0;
}
