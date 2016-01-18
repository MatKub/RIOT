/**
 * @addtogroup  driver_periph
 * @{
 *
 * @file
 * @brief       Low-level hard fault communication implementation.
 *
 * @author      Kubaszek Mateusz <mathir.km.riot@gmail.com>
 */

#include "board.h"
#include "periph/uart.h"

extern int uart_init_blocking(uart_t _uart, uint32_t baudrate);

/**
 * @brief   Initialization of communication interface.
 * @note    CPU dependant function for communication after hard fault.
 */
void hard_com_init(void)
{
    /* STDIO definitions are needed. The default debug and communication USART is reconfigured to be functional inside hard fault ISR. Standard pooling configuration. */
    uart_init_blocking(STDIO, STDIO_BAUDRATE);
    /* Assuming that hard fault handler function has the greatest priority, there is no need to manage NVIC or INTERRUPT registers.*/

}

/**
 * @brief   Puts string on communication port.
 * @note    Argument should be null terminated.
 * @param[in]   Char pointer to null terminated string.
 * @param[in]   String length, in case there will be no null termination or to shorten the output message.
 */
void hard_com_put(const char* out, int len)
{
    switch(STDIO) {
#if UART_0_EN
    case UART_0:
    for(size_t i = 0; i < len; i++) {
        /* Check that transmit buffer is empty */
        while(!(UART_0_DEV->STATUS & USART_STATUS_TXBL))
        ;
        if(!out[i])
        break;
        /* Write data to buffer */
        UART_0_DEV->TXDATA = (uint32_t)out[i];
    }
    break;
#endif
#if UART_1_EN
    case UART_1:
    for(size_t i = 0; i < len; i++)
    {
        /* Check that transmit buffer is empty */
        while(!(UART_1_DEV->STATUS & USART_STATUS_TXBL))
        ;
        if(!out[i])
        break;
        /* Write data to buffer */
        UART_1_DEV->TXDATA = (uint32_t)out[i];
    }
    break;
#endif
#if UART_2_EN
    case UART_2:
        for(size_t i = 0; i < len; i++) {
            /* Check that transmit buffer is empty */
            while(!(UART_2_DEV->STATUS & LEUART_STATUS_TXBL))
                ;
            /* Avoid deadlock if modifying the same register twice when freeze mode is */
            /* activated. */
            if(!(UART_2_DEV->FREEZE & LEUART_FREEZE_REGFREEZE)) {
                /* Wait for any pending previous write operation to have been completed */
                /* in low frequency domain */
                while(UART_2_DEV->SYNCBUSY & LEUART_SYNCBUSY_TXDATA)
                    ;;
                if(!out[i])
                    break;
                UART_2_DEV->TXDATA = (uint32_t)out[i];
            }

        }
        break;
#endif
    }
}

/** @} */

