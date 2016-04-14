/*
 * t_debug.h
 *
 *  Created on: 14 gru 2015
 *      Author: mateusz
 */

#ifndef _T_DEBUG_H_
#define _T_DEBUG_H_

#include "board.h"
#include "periph/gpio.h"

extern struct timeval time_ref;
extern char time_stamp[20];

typedef struct {
    gpio_t gpio;
    gpio_pp_t pullup;
    gpio_flank_t flank;
} debug_time_sync_t;

void debug_callback(void *arg);
void debug_timeref_init(void);

void debug_timeref_reset(void);
void debug_timestamp(void);

#endif /* _T_DEBUG_H_ */
