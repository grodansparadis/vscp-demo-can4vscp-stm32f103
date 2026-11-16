// Stand-in for the STM32 HAL so the node logic can be built and tested on
// a PC. Only what blinky.c, persist.c and vscp-can.c use is here.
#ifndef TEST_MAIN_H
#define TEST_MAIN_H

#include <stddef.h>
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;

// GPIO: the "port" is the index of the pin in g_test_pins
typedef int GPIO_TypeDef;
extern GPIO_PinState g_test_pins[3];
extern GPIO_TypeDef g_test_ports[3];
#define LED_GPIO_Port        (&g_test_ports[0])
#define LED_Pin              0
#define STATUS_LED_GPIO_Port (&g_test_ports[1])
#define STATUS_LED_Pin       0
#define BTN_GPIO_Port        (&g_test_ports[2])
#define BTN_Pin              0

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);

uint32_t HAL_GetTick(void);
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
uint32_t HAL_GetUIDw2(void);
void Error_Handler(void);

#define __disable_irq()
#define __enable_irq()

// Flash
#define FLASH_TYPEERASE_PAGES  0u
#define FLASH_TYPEPROGRAM_WORD 2u
typedef struct {
  uint32_t TypeErase;
  uintptr_t PageAddress;
  uint32_t NbPages;
} FLASH_EraseInitTypeDef;
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *perase, uint32_t *perror);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uintptr_t address, uint64_t data);

extern uint8_t g_test_flash[1024];
#define PERSIST_FLASH_ADDR ((uintptr_t) g_test_flash)

// CAN
#define CAN_ID_STD                  0u
#define CAN_ID_EXT                  4u
#define CAN_RTR_DATA                0u
#define CAN_RTR_REMOTE              2u
#define CAN_RX_FIFO0                0u
#define CAN_FILTERMODE_IDMASK       0u
#define CAN_FILTERSCALE_32BIT       1u
#define CAN_IT_RX_FIFO0_MSG_PENDING 2u

typedef struct { int dummy; } CAN_HandleTypeDef;
typedef struct {
  uint32_t StdId, ExtId, IDE, RTR, DLC;
  FunctionalState TransmitGlobalTime;
} CAN_TxHeaderTypeDef;
typedef struct {
  uint32_t StdId, ExtId, IDE, RTR, DLC, Timestamp, FilterMatchIndex;
} CAN_RxHeaderTypeDef;
typedef struct {
  uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow;
  uint32_t FilterFIFOAssignment, FilterBank, FilterMode, FilterScale;
  uint32_t FilterActivation, SlaveStartFilterBank;
} CAN_FilterTypeDef;

HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, const CAN_FilterTypeDef *f);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t its);
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *h, uint32_t fifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *hdr, uint8_t data[]);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *hdr, const uint8_t data[], uint32_t *pmailbox);
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h);

#endif
