/*
 * t_debug.c
 *
 *  Created on: 14 gru 2015
 *      Author: mateusz
 */

/* My own implementation of timestamps */
#include "debug_t.h"

#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include "periph/gpio.h"
#include "board.h"

struct timeval time_ref;
char time_stamp[20];

debug_time_sync_t debug_time_sync;

#ifndef DEBUG_SYNC_ENABLE
static void debug_callback(void *arg)
{
    gpio_irq_disable(debug_time_sync.gpio);
    debug_timeref_reset();
    gpio_irq_enable(debug_time_sync.gpio);
}

void debug_timeref_init(void)
{
    gpio_init_int(debug_time_sync.gpio, debug_time_sync.pullup, debug_time_sync.flank, &debug_callback, 0);
    gpio_irq_enable(debug_time_sync.gpio);
}
#endif

void debug_timeref_reset(void)
{
    gettimeofday(&time_ref, 0);
}



void debug_timestamp(void)
{
    struct timeval time_tmp;
    gettimeofday(&time_tmp, 0);
    timersub(&time_tmp, &time_ref, &time_tmp);
    snprintf(time_stamp, 20, "%4d.%06d\t>", (int)time_tmp.tv_sec,
            (int)time_tmp.tv_usec);
}

