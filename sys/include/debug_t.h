/*
 * t_debug.h
 *
 *  Created on: 14 gru 2015
 *      Author: mateusz
 */

#ifndef TESTS_GNRC_NETWORKING_CC112X_T_DEBUG_H_
#define TESTS_GNRC_NETWORKING_CC112X_T_DEBUG_H_

#include "board.h"
#include "periph/gpio.h"

extern struct timeval time_ref;
extern char time_stamp[20];

typedef struct {
    gpio_t gpio;
    gpio_pp_t pullup;
    gpio_flank_t flank;
} debug_time_sync_t;

#ifndef DEBUG_SYNC_ENABLE
void debug_callback(void *arg);
void debug_timeref_init(void);
#endif

void debug_timeref_reset(void);
void debug_timestamp(void);

#endif /* TESTS_GNRC_NETWORKING_CC112X_T_DEBUG_H_ */
