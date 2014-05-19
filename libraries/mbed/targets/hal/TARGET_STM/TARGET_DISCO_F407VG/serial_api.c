/* mbed Microcontroller Library
 *******************************************************************************
 * Copyright (c) 2014, STMicroelectronics
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of STMicroelectronics nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************
 */
#include "serial_api.h"
#include "cmsis.h"
#include "pinmap.h"
#include <string.h>
#include "stm32f4xx_hal.h"

static const PinMap PinMap_UART_TX[] = {
    {PA_2,  UART_2, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF7_USART2)},
    {PA_9,  UART_1, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF7_USART1)},
    {PA_11, UART_6, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF8_USART6)},
    {PB_6,  UART_1, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF7_USART1)},
    {PC_6,  UART_6, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF8_USART6)},
    {NC,    NC,     0}
};

static const PinMap PinMap_UART_RX[] = {
    {PA_3,  UART_2, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF7_USART2)},
    {PA_10, UART_1, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF7_USART1)},
    {PA_12, UART_6, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF8_USART6)},
    {PB_7,  UART_1, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF7_USART1)},
    {PC_7,  UART_6, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_PULLUP, GPIO_AF8_USART6)},
    {NC,    NC,     0}
};

#define UART_NUM (3)

static uint32_t serial_irq_ids[UART_NUM] = {0};

static uart_irq_handler irq_handler;

UART_HandleTypeDef UartHandle;
    
int stdio_uart_inited = 0;
serial_t stdio_uart;

static void init_uart(serial_t *obj) {
    UartHandle.Instance = (USART_TypeDef *)(obj->uart);
    
    UartHandle.Init.BaudRate   = obj->baudrate;
    UartHandle.Init.WordLength = obj->databits;
    UartHandle.Init.StopBits   = obj->stopbits;
    UartHandle.Init.Parity     = obj->parity;
    UartHandle.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    UartHandle.Init.Mode       = UART_MODE_TX_RX;
  
    HAL_UART_Init(&UartHandle);
}

void serial_init(serial_t *obj, PinName tx, PinName rx) {
    // Determine the UART to use (UART_1, UART_2, ...)
    UARTName uart_tx = (UARTName)pinmap_peripheral(tx, PinMap_UART_TX);
    UARTName uart_rx = (UARTName)pinmap_peripheral(rx, PinMap_UART_RX);
  
    // Get the peripheral name (UART_1, UART_2, ...) from the pin and assign it to the object
    obj->uart = (UARTName)pinmap_merge(uart_tx, uart_rx);
    assert(obj->uart != (UARTName)NC);

    // Enable USART clock
    if (obj->uart == UART_1) {
        __USART1_CLK_ENABLE();
    }
    if (obj->uart == UART_2) {
        __USART2_CLK_ENABLE();
    }
    if (obj->uart == UART_6) {
        __USART6_CLK_ENABLE();
    }
    
    // Configure the UART pins
    pinmap_pinout(tx, PinMap_UART_TX);
    pinmap_pinout(rx, PinMap_UART_RX);
    pin_mode(tx, PullUp);
    pin_mode(rx, PullUp);
    
    // Configure UART
    obj->baudrate = 9600;
    obj->databits = UART_WORDLENGTH_8B;
    obj->stopbits = UART_STOPBITS_1;
    obj->parity   = UART_PARITY_NONE;

    init_uart(obj);

    // The index is used by irq
    if (obj->uart == UART_1) obj->index = 0;
    if (obj->uart == UART_2) obj->index = 1;
    if (obj->uart == UART_6) obj->index = 2;
    
    // For stdio management
    if (obj->uart == STDIO_UART) {
        stdio_uart_inited = 1;
        memcpy(&stdio_uart, obj, sizeof(serial_t));
    }
    
}

void serial_free(serial_t *obj) {
    serial_irq_ids[obj->index] = 0;
}

void serial_baud(serial_t *obj, int baudrate) {
    obj->baudrate = baudrate;
    init_uart(obj);
}

void serial_format(serial_t *obj, int data_bits, SerialParity parity, int stop_bits) {
    if (data_bits == 8) {
        obj->databits = UART_WORDLENGTH_8B;
    }
    else {
        obj->databits = UART_WORDLENGTH_9B;
    }

    switch (parity) {
      case ParityOdd:
      case ParityForced0:
          obj->parity = UART_PARITY_ODD;
      break;
      case ParityEven:
      case ParityForced1:
          obj->parity = UART_PARITY_EVEN;
      break;
      default: // ParityNone
          obj->parity = UART_PARITY_NONE;
      break;
    }
    
    if (stop_bits == 2) {
        obj->stopbits = UART_STOPBITS_2;
    }
    else {
        obj->stopbits = UART_STOPBITS_1;
    }

    init_uart(obj);
}

/******************************************************************************
 * INTERRUPTS HANDLING
 ******************************************************************************/

// Not part of mbed api
static void uart_irq(UARTName name, int id) {
    UartHandle.Instance = (USART_TypeDef *)name;
      
    if (serial_irq_ids[id] != 0) {
        if (__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_TC) != RESET) {
            irq_handler(serial_irq_ids[id], TxIrq);
            __HAL_UART_CLEAR_FLAG(&UartHandle, UART_FLAG_TC);
        }
        if (__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_RXNE) != RESET) {
            irq_handler(serial_irq_ids[id], RxIrq);
            __HAL_UART_CLEAR_FLAG(&UartHandle, UART_FLAG_RXNE);
        }
    }
}

// Not part of mbed api
static void uart1_irq(void) {uart_irq(UART_1, 0);}
static void uart2_irq(void) {uart_irq(UART_2, 1);}
static void uart6_irq(void) {uart_irq(UART_6, 2);}

void serial_irq_handler(serial_t *obj, uart_irq_handler handler, uint32_t id) {
    irq_handler = handler;
    serial_irq_ids[obj->index] = id;
}

void serial_irq_set(serial_t *obj, SerialIrq irq, uint32_t enable) {
    IRQn_Type irq_n = (IRQn_Type)0;
    uint32_t vector = 0;
    
    UartHandle.Instance = (USART_TypeDef *)(obj->uart);
    
    if (obj->uart == UART_1) {
      irq_n = USART1_IRQn;
      vector = (uint32_t)&uart1_irq;
    }
  
    if (obj->uart == UART_2) {
      irq_n = USART2_IRQn;
      vector = (uint32_t)&uart2_irq;
    }

    if (obj->uart == UART_6) {
      irq_n = USART6_IRQn;
      vector = (uint32_t)&uart6_irq;
    }
    
    if (enable) {
      
        if (irq == RxIrq) {
            __HAL_UART_ENABLE_IT(&UartHandle, UART_IT_RXNE);
        }
        else { // TxIrq
            __HAL_UART_ENABLE_IT(&UartHandle, UART_IT_TC);
        }
        
        NVIC_SetVector(irq_n, vector);
        NVIC_EnableIRQ(irq_n);
        
    } else { // disable
      
        int all_disabled = 0;
        
        if (irq == RxIrq) {
             __HAL_UART_DISABLE_IT(&UartHandle, UART_IT_RXNE);
            // Check if TxIrq is disabled too
            if ((UartHandle.Instance->CR1 & USART_CR1_TXEIE) == 0) all_disabled = 1;
        }
        else { // TxIrq
            __HAL_UART_DISABLE_IT(&UartHandle, UART_IT_TXE);
            // Check if RxIrq is disabled too
            if ((UartHandle.Instance->CR1 & USART_CR1_RXNEIE) == 0) all_disabled = 1;
        }
        
        if (all_disabled) NVIC_DisableIRQ(irq_n);
        
    }
}

/******************************************************************************
 * READ/WRITE
 ******************************************************************************/

int serial_getc(serial_t *obj) {
    USART_TypeDef *uart = (USART_TypeDef *)(obj->uart);
    while (!serial_readable(obj));
    return (int)(uart->DR & 0x1FF);
}

void serial_putc(serial_t *obj, int c) {
    USART_TypeDef *uart = (USART_TypeDef *)(obj->uart);
    while (!serial_writable(obj));
    uart->DR = (uint32_t)(c & 0x1FF);
}

int serial_readable(serial_t *obj) {
    int status;
    UartHandle.Instance = (USART_TypeDef *)(obj->uart);
    // Check if data is received
    status = ((__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_RXNE) != RESET) ? 1 : 0);
    return status;
}

int serial_writable(serial_t *obj) {
    int status;
    UartHandle.Instance = (USART_TypeDef *)(obj->uart);
    // Check if data is transmitted
    status = ((__HAL_UART_GET_FLAG(&UartHandle, UART_FLAG_TXE) != RESET) ? 1 : 0);
    return status;
}

void serial_clear(serial_t *obj) {
    UartHandle.Instance = (USART_TypeDef *)(obj->uart);
    __HAL_UART_CLEAR_FLAG(&UartHandle, UART_FLAG_TXE);
    __HAL_UART_CLEAR_FLAG(&UartHandle, UART_FLAG_RXNE);
}

void serial_pinout_tx(PinName tx) {
    pinmap_pinout(tx, PinMap_UART_TX);
}

void serial_break_set(serial_t *obj) {
    UartHandle.Instance = (USART_TypeDef *)(obj->uart);
    HAL_LIN_SendBreak(&UartHandle);
}

void serial_break_clear(serial_t *obj) {
}
