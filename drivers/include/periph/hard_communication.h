/**
 * @defgroup    drivers_periph_hard_communication
 * @ingroup     drivers_periph
 * @brief       Low-level communication interface handling for hard fault debug tracing.
 *
 * @{
 * @file
 * @brief       Low-level GPIO peripheral driver interface definitions
 *
 * @author      Kubaszek Mateusz <mathir.km.riot@gmail.com>
 */

#ifndef HARD_COMMUNICATION_H
#define HARD_COMMUNICATION_H

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief   Initialization of communication interface.
     * @note    CPU dependant function for communication after hard fault.
     */
    void hard_com_init(void);

    /**
     * @brief   Puts string on communication port.
     * @note    Argument should be null terminated.
     * @param[in]   Char pointer to null terminated string.
     */
    void hard_com_put(const char* out, int len);

    void h_puts(const char* msg);

    void h_printf(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_H */
/** @} */
