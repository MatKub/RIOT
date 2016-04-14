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

#include "msg.h"

#include "cpu_conf.h"
#include "xtimer.h"
#include "random.h"
#include "ps.h"
#include "thread.h"
#include "xtimer.h"
#include "shell.h"
#include "shell_commands.h"
#include "net/gnrc.h"

#define LOG_LEVEL LOG_ERROR
#include "log.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

/* Node params */
#define SENDER  1   // 1-sender; 0-receiver
#define SRC_ADDR    5  // source address
#define MAX_DATA_BYTES_COUNT    120

/* stack for rawdump thread */
static char rawdmp_stack[THREAD_STACKSIZE_MAIN];

extern int RADIO_proc;  /* radio process number */
int MAIN_proc;  /* main process number */

/* Radio parameters */
uint8_t src_addr = SRC_ADDR;
uint8_t dst_addr = 0;
uint8_t channel = 25;

/* MSG */
#define MSG_DATA_SND 0x4325

/* statistics structure */
struct {
    int sent;
    int received;
    int lost;
    int with_errors;
    int bytes_per_second;
} RX_TX_stat = {0, 0, 0, 0, 0};

typedef struct {
    uint8_t length;
    char data[MAX_DATA_BYTES_COUNT];
    int nbr;
    uint64_t timestamp;
} TX_packet_t;

/**
 * @brief   Make a raw dump of the given packet contents
 */
void pkt_print(gnrc_pktsnip_t *pkt)
{
    gnrc_pktsnip_t *snip = pkt;

    /* How many packets in snip? */
    int pkt_cnt = gnrc_pkt_count(pkt);
    for(uint8_t cnt = 0; cnt < pkt_cnt; ++cnt) {
        DEBUG("ADGFBSDfd - %d\n", pkt_cnt);
        if(snip != NULL) {
            DEBUG("Layer %d, length %d\n", cnt, snip->size);
            for(size_t i = 0; i < snip->size; i++) {
                DEBUG("%03d ", ((uint8_t * )(snip->data))[i]);
            }
            DEBUG("\n");
        }
        snip = snip->next;
    }
}

/**
 * @brief   Event loop of the RAW dump thread
 *
 * @param[in] arg   unused parameter
 */
void *rawdump(void *arg)
{
    msg_t msg;
    gnrc_pktsnip_t *pkt;
    uint8_t * adr_pointer;
    gnrc_netif_hdr_t * netif_hdr;

#if SENDER == 1
    uint64_t time_diff;
    bool msg_sent = false;
    TX_packet_t* sent_packet = NULL;
#else
    uint32_t cnt = 0;
    gnrc_pktsnip_t *nif;
    uint8_t from = 0;
#endif

    while(1) {
        msg_receive(&msg);

        pkt = (gnrc_pktsnip_t*)msg.content.ptr;

        switch(msg.type) {
        case GNRC_NETAPI_MSG_TYPE_RCV:
#if SENDER == 1
            /* If in sender mode, here the packet will be processed and checked */
            if(pkt->next->type != GNRC_NETTYPE_NETIF) {
                printf("No netif header, possibly foreign message...\n");
            } else {
                netif_hdr = (gnrc_netif_hdr_t *)pkt->next->data;
                /* set up header */
                if(netif_hdr->dst_l2addr_len == 1) {
                    adr_pointer = gnrc_netif_hdr_get_dst_addr(netif_hdr);
                    if(SRC_ADDR == (*adr_pointer)) {
                        /* Calculating time */
                        if(sent_packet != NULL)
                        {
                            time_diff = xtimer_now64() - sent_packet->timestamp;
                            RX_TX_stat.bytes_per_second = sent_packet->length * 2 * 1000000 / time_diff;

                            /* Comparing packets */
                            if(pkt->size != sent_packet->length) {
                                printf("Different messages length...\n");
                            } else {
                                for(int i = 0;; ++i) {
                                    if(i == pkt->size) {
                                        DEBUG("Packet received successfully\n");
                                        printf("%lu - nbr %04d, length %03u, after %lu[ms], speed %d[Bps]\n", (uint32_t)(xtimer_now64()/1000), sent_packet->nbr, sent_packet->length, (uint32_t)time_diff / 1000, RX_TX_stat.bytes_per_second);
                                        ++RX_TX_stat.received;
                                        break;
                                    }
                                    if(((char*)pkt->data)[i] != sent_packet->data[i]) {
                                        printf("Packet errors...\n");
                                        ++RX_TX_stat.with_errors;
                                        break;
                                    }
                                }
                            }
                            msg_sent = false;
                        }
                    } else {
                        printf("Wrong address, foreign message...\n");
                    }
                }
            }
            gnrc_pktbuf_release(pkt);
#else
            /* If receiver mode, printing packet information */
            /* In first pktsnip are data, in the second should be netif header */
            if (pkt->next->type != GNRC_NETTYPE_NETIF) {
                /* Undefined address */
                from = 0;
            } else {
                netif_hdr = (gnrc_netif_hdr_t *)pkt->next->data;
                /* set up header */
                if(netif_hdr->dst_l2addr_len == 1) {
                    adr_pointer = gnrc_netif_hdr_get_src_addr(netif_hdr);
                    from = (*adr_pointer);
                } else {
                    /* Undefined address */
                    from = 0;
                }
                printf("%lu - nbr %lu, from %d, length %u, RSSI %u, LQI %d\n", (uint32_t)(xtimer_now64()/1000), cnt++, from, pkt->size, netif_hdr->rssi, netif_hdr->lqi);
            }
            /* And sending packet back */
            gnrc_pktbuf_release(pkt->next);
            pkt->next = NULL;
            nif = gnrc_netif_hdr_build(&src_addr, 1, &from, 1);
            nif->next = pkt;

            msg.sender_pid = thread_getpid();
            msg.type = GNRC_NETAPI_MSG_TYPE_SND;
            msg.content.ptr = (char*)nif;
            msg_send(&msg, RADIO_proc);

            /* Message space will be freed after the packet will be sent */
#endif
            break;
#if SENDER == 1
        case MSG_DATA_SND:
            if(msg_sent == true) {
                printf("Lost message, ");
                if(sent_packet)
                {
                    printf("length %u\n", sent_packet->length);
                }
                ++RX_TX_stat.lost;
            }
            msg_sent = true;
            sent_packet = (TX_packet_t*)msg.content.ptr;
            break;
#endif
        default:
            /* do nothing */
            break;
        }
    }

    /* never reached */
    return NULL;
}

/**
 * @brief   Maybe you are a golfer?!
 */
int main(void)
{
    /* initializing random generator */
    genrand_init(1234567);
    debug_timeref_init();

    MAIN_proc = thread_getpid();
    msg_t msg;

    /* network api options message */
    gnrc_netapi_opt_t opt;

    /* packet snips to send */
    gnrc_pktsnip_t *pkt;
    gnrc_pktsnip_t *nif;

    /* packet to send */
    TX_packet_t TX_packet;
    TX_packet.nbr = 0;

    /* Setting channel */
    opt.data = &channel;
    opt.data_len = 1;
    opt.opt = NETOPT_CHANNEL;

    msg.sender_pid = thread_getpid();
    msg.type = GNRC_NETAPI_MSG_TYPE_SET;
    msg.content.ptr = (char*)&opt;
    msg_send_receive(&msg, &msg, RADIO_proc);

    DEBUG("Radio channel %d\n", channel);

    /* Creating reading thread */
    DEBUG("Creating reading thread %d\n", channel);
    gnrc_netreg_entry_t dump;
    dump.pid = thread_create(rawdmp_stack, sizeof(rawdmp_stack), THREAD_PRIORITY_MAIN - 3, THREAD_CREATE_STACKTEST, rawdump, NULL, "rawdump");
    dump.demux_ctx = GNRC_NETREG_DEMUX_CTX_ALL;
    gnrc_netreg_register(GNRC_NETTYPE_UNDEF, &dump);

    xtimer_sleep(1);

    if(SENDER) {
        opt.data = &src_addr;
        opt.data_len = 1;
        opt.opt = NETOPT_ADDRESS;

        msg.sender_pid = thread_getpid();
        msg.type = GNRC_NETAPI_MSG_TYPE_SET;
        msg.content.ptr = (char*)&opt;
        msg_send_receive(&msg, &msg, RADIO_proc);

        while(1) {
            /* Generating message */
            ++TX_packet.nbr;
            TX_packet.length = 0;
            /* Random packet length (minimum 0, maximum  */
            while(TX_packet.length == 0 || TX_packet.length > MAX_DATA_BYTES_COUNT) {
                TX_packet.length = genrand_uint32() & 0xff;
            }
            /* Generating random data */
            for(uint32_t a = 0; a <= TX_packet.length; ++a) {
                TX_packet.data[a] = genrand_uint32() & 0xff;
            }
            DEBUG("Sending packet, length - %d, nbr - %d\n", TX_packet.length, TX_packet.nbr);

            /* Preparing data frame */
            pkt = gnrc_pktbuf_add(NULL, TX_packet.data, TX_packet.length, GNRC_NETTYPE_UNDEF);
            if(pkt == NULL) {
                DEBUG("Error: unable to copy data to packet buffer");
                return 0;
            }
            pkt->next = NULL;
            /* Preparing network interface frame */
            nif = gnrc_netif_hdr_build(&src_addr, 1, &dst_addr, 1);
            nif->next = pkt;
            /* Set the outgoing message's fields */
            msg.type = GNRC_NETAPI_MSG_TYPE_SND;
            msg.content.ptr = (char*)nif;
            msg.sender_pid = thread_getpid();
            /* Sending message */
            msg_send(&msg, RADIO_proc);
            ++RX_TX_stat.sent;
            /* Sending the data to pkt_dump thread */
            msg.type = MSG_DATA_SND;
            msg.sender_pid = thread_getpid();
            TX_packet.timestamp = xtimer_now64();
            msg.content.ptr = (char*)&TX_packet;
            msg_send(&msg, dump.pid);

            uint8_t packets_per_10sec;
            packets_per_10sec = 0;
            while((packets_per_10sec > 200) || (packets_per_10sec == 0)) {
              packets_per_10sec = genrand_uint32() & 0xff;
            }
            xtimer_usleep(10000000/packets_per_10sec);
        }
    } else {
        while(1) {
            thread_sleep();
        }
    }

    return 0;
}
