/*
 * Copyright (C) 2015 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       Test application for CC112X network device driver
 *
 * @author      Kubaszek Mateusz <mathir.km.riot@gmail.com>
 *
 * @}
 */

#include <stdio.h>

#include "shell.h"
#include "msg.h"

#include "cpu_conf.h"
#include "xtimer.h"
#include "random.h"

#include "cc112x_params.h"
#include "../../drivers/cc112x/include/gnrc_netdev2_cc112x.h"
#include "./../../drivers/cc112x/include/cc112x-netdev2.h"

#include <time.h>
#include <sys/time.h>

#define ENABLE_DEBUG       1
#include "debug.h"
#include "debug_t.h"

#define SENDER  0
#define SRC_ADDR    11
#define DATA_BYTES_COUNT    230

#define CC112X_MAC_PRIO          (THREAD_PRIORITY_MAIN - 3)
#define CC112X_MAC_STACKSIZE     (THREAD_STACKSIZE_DEFAULT + DEBUG_EXTRA_STACKSIZE + 1024)
static netdev2_cc112x_t cc112x_dev;
static gnrc_netdev2_t gnrc_netdev2_dev;
static char _stack[CC112X_MAC_STACKSIZE];

/**
 * @brief   Maybe you are a golfer?!
 */
int main(void)
{
    genrand_init(1234567);
    debug_timeref_init();

    DEBUG("CC112x device driver test\n");
    msg_t msg;

    msg_t msg2;
    msg_t msg3;

    uint8_t src_addr = SRC_ADDR;
    uint8_t dst_addr = 0;
    uint8_t channel = 25;
    gnrc_netapi_opt_t opt;

    gnrc_pktsnip_t *pkt;
    gnrc_pktsnip_t *nif;
    uint8_t data[DATA_BYTES_COUNT];
    uint8_t count;

    gpio_init(GPIO_T(GPIO_PORT_D, 7), GPIO_DIR_INPUT, GPIO_PULLUP);

    const cc112x_params_t *p = cc112x_params;
    printf("Initializing CC112X radio at SPI_%i\n", p->spi);
    int res = netdev2_cc112x_setup(&cc112x_dev, p);
    if (res < 0) {
        printf("Error initializing CC112X radio device!");
    }
    else {
        gnrc_netdev2_cc112x_init(&gnrc_netdev2_dev, &(cc112x_dev.netdev));
        res = gnrc_netdev2_init(_stack, CC112X_MAC_STACKSIZE, CC112X_MAC_PRIO, "cc112x", &gnrc_netdev2_dev);
        if (res < 0) {
            printf("Error starting gnrc_cc112x thread for CC112X!");
        }
    }
    /* Setting channel */
    //            ++channel;
    if(channel > 30)
        channel = 25;
    opt.data = &channel;
    opt.data_len = 1;
    opt.opt = NETOPT_CHANNEL;

    msg2.sender_pid = thread_getpid();
    msg2.type = GNRC_NETAPI_MSG_TYPE_SET;
    msg2.content.ptr = (char*)&opt;
    msg_send_receive(&msg2, &msg3, res);

    printf("Channel %d\n", channel);
    xtimer_usleep(500000);

    int cnt = 0;

    if(SENDER) {
        opt.data = &src_addr;
        opt.data_len = 1;
        opt.opt = NETOPT_ADDRESS;

        msg2.sender_pid = thread_getpid();
        msg2.type = GNRC_NETAPI_MSG_TYPE_SET;
        msg2.content.ptr = (char*)&opt;
        msg_send_receive(&msg2, &msg, res);

        count = 229;

        while(1) {
            /* Generating message */

            ++cnt;
//            while(count==0 || count > DATA_BYTES_COUNT){
//                count = genrand_uint32() & 0xff;
//            }
            count += 10;
            if(count > DATA_BYTES_COUNT){
                count = 1;
            }

            for(uint32_t a = 0; a <= count; ++a) {
                data[a] = genrand_uint32() & 0xff;
            }

            DEBUG("Sending packet, length - %d, nbr - %d\n", count, cnt);

            /* Preparing packet */
            pkt = gnrc_pktbuf_add(NULL, data, count, GNRC_NETTYPE_UNDEF);
            if(pkt == NULL) {
                puts("Error: unable to copy data to packet buffer");
                return 0;
            }
            pkt->next = NULL;
            nif = gnrc_netif_hdr_build(&src_addr, 1, &dst_addr, 1);
            nif->next = pkt;
            /* set the outgoing message's fields */
            msg.type = GNRC_NETAPI_MSG_TYPE_SND;
            msg.content.ptr = (char*)nif;
            msg.sender_pid = thread_getpid();
            /* send message */
            msg_send(&msg, res);

            xtimer_usleep(10000000/4);

        }
    } else {
//        dump.pid = thread_create(rawdmp_stack, sizeof(rawdmp_stack), RAWDUMP_PRIO,
//        THREAD_CREATE_STACKTEST, rawdump, NULL, "rawdump");
//        dump.demux_ctx = GNRC_NETREG_DEMUX_CTX_ALL;
//        gnrc_netreg_register(GNRC_NETTYPE_UNDEF, &dump);
        while(1);
    }

    return 0;
}
