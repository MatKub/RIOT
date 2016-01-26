/*
 * Copyright (C) 2014 Freie Universität Berlin
 * Copyright (C) 2013 INRIA
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     drivers_cc112x
 * @{
 * @file
 * @brief       Functions for packet reception and transmission on cc112x devices
 *
 * @author      Oliver Hahm <oliver.hahm@inria.fr>
 * @author      Fabian Nack <nack@inf.fu-berlin.de>
 * @author      Kaspar Schleiser <kaspar@schleiser.de>
 * @}
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cc112x.h"
#include "cc112x-spi.h"
#include "cc112x-internal.h"
#include "cc112x-interface.h"
#include "cc112x-defines.h"
#include "cc112x-netdev2.h"

#include "periph/gpio.h"
#include "irq.h"

#include "kernel_types.h"
#include "msg.h"

#include "cpu_conf.h"
#include "cpu.h"

#include "log.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

static void _rx_start(cc112x_t *dev)
{
    DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    dev->radio_state = RADIO_RX_BUSY;

    cc112x_pkt_buf_t *pkt_buf = &dev->pkt_buf;
    pkt_buf->pos = 0;

    gpio_irq_disable(dev->params.gpio2);

    /* Asserted when the RX FIFO is filled above
     * FIFO_CFG.FIFO_THR or the end of packet is reached. De-asserted
     * when the RX FIFO is empty
     */
    /*Associated to the RX FIFO. Asserted when the RX FIFO is filled above
    FIFO_CFG.FIFO_THR. De-asserted when the RX FIFO is drained below
    (or is equal) to the same threshold. This signal is also available in the
    MODEM_STATUS1 register*/
    cc112x_write_reg(dev, CC112X_IOCFG2, 0x01);
    gpio_irq_enable(dev->params.gpio2);

}

static void _rx_read_data(netdev2_cc112x_t *cc112x_netdev)//, void (*callback)(void*), void*arg)
{
//    DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    cc112x_t *cc112x = &cc112x_netdev->cc112x;
    int fifo = cc112x_read_reg(cc112x, CC112X_NUM_RXBYTES);
    uint8_t ovf = cc112x_read_reg(cc112x, CC112X_MODEM_STATUS1) & 0x04; /* overflow status bit */

    if(ovf) {
        DEBUG("%s:%s:%u Overflowed\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        cc112x_switch_to_rx(cc112x);
        return;
    }

    if(!fifo) {
        DEBUG("%s:%s:%u FIFO empty \n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        gpio_irq_enable(cc112x->params.gpio2);
        return;
    }

    cc112x_pkt_buf_t *pkt_buf = &cc112x->pkt_buf;
    if(!pkt_buf->pos) {
        /* rewrite the packet length */
        pkt_buf->pos = 1;
        pkt_buf->packet.length = cc112x_read_reg(cc112x, CC112X_SINGLE_RXFIFO);

        DEBUG("%s:%s:%u Started receiving, pkt. length %d \n", RIOT_FILE_RELATIVE, __func__, __LINE__, pkt_buf->packet.length);
        /* Possible packet received, RX -> IDLE (0.1 us) */
        cc112x->cc112x_statistic.packets_in++;
    }

    int left = pkt_buf->packet.length + 1 - pkt_buf->pos;

    /* if the fifo doesn't contain the rest of the packet,
     * leav at least one byte as per spec sheet. */
    int to_read = (fifo < left) ? (fifo - 1) : fifo;
    if(to_read > left) {
        to_read = left;
    }

    if(to_read) {
//        DEBUG("%s:%s:%u Reading data - %d\n", RIOT_FILE_RELATIVE, __func__, __LINE__, to_read);
        cc112x_readburst_reg(cc112x, CC112X_BURST_RXFIFO, ((char *)&pkt_buf->packet) + pkt_buf->pos, to_read);
        pkt_buf->pos += to_read;
    }

    if(to_read == left) {
        /* full packet received. */

        /* Store RSSI value of packet */
        pkt_buf->rssi = cc112x_read_reg(cc112x, CC112X_RSSI1);

        /* Bit 0-6 of LQI indicates the link quality (LQI) */
        pkt_buf->lqi = cc112x_read_reg(cc112x, CC112X_LQI_VAL);

        /* MSB of LQI is the CRC_OK bit */
        int crc_ok = (pkt_buf->lqi & CRC_OK) >> 7;

        if(crc_ok) {
            DEBUG("cc112x: received packet from=%u to=%u payload "
                    "len=%u\n",
                    (unsigned)pkt_buf->packet.phy_src,
                    (unsigned)pkt_buf->packet.address,
                    pkt_buf->packet.length-3);
            /* let someone know that we've got a packet */
            cc112x_netdev->netdev.event_callback(&cc112x_netdev->netdev, NETDEV2_EVENT_RX_COMPLETE, NULL);

            cc112x_switch_to_rx(cc112x);
        } else {
            DEBUG("%s:%s:%u crc-error\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
            cc112x->cc112x_statistic.packets_in_crc_fail++;
            cc112x_switch_to_rx(cc112x);
        }
    }
}

static void _rx_continue(netdev2_cc112x_t *cc112x_netdev)//, void (*callback)(void*), void*arg)
{
    DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    cc112x_t *cc112x = &cc112x_netdev->cc112x;
    if(cc112x->radio_state != RADIO_RX_BUSY) {
        DEBUG("%s:%s:%u _rx_continue in invalid state\n", RIOT_FILE_RELATIVE,
                __func__, __LINE__);
        cc112x_switch_to_rx(cc112x);
        return;
    }

    do {
        _rx_read_data(cc112x_netdev);//, callback, arg);
    } while(gpio_read(cc112x->params.gpio2));
}

static void _tx_abort(cc112x_t *dev)
{
    DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    cc112x_switch_to_rx(dev);
}

static void _tx_continue(cc112x_t *dev)
{
    DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    gpio_irq_disable(dev->params.gpio2);

    cc112x_pkt_t *pkt = &dev->pkt_buf.packet;
    int size = pkt->length + 1;
    int left = size - dev->pkt_buf.pos;

    /* If no more data to send, switch to rx mode */
    if(!left) {
        dev->cc112x_statistic.raw_packets_out++;

        DEBUG("%s:%s:%u packet successfully sent\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        puts("\n\n### packet sent\n\n");

        cc112x_switch_to_rx(dev);
        return;
    }

    /* Check tx fifo status */
    int fifo = cc112x_read_reg(dev, CC112X_MODEM_STATUS0);

    if(fifo & 0x01) {
        DEBUG("%s:%s:%u txfifo underflow!\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        _tx_abort(dev);
        return;
    }

    if(fifo & 0x08) {
        DEBUG("%s:%s:%u txfifo full!?\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        _tx_abort(dev);
        return;
    }

    if(fifo & 0x02) {
        DEBUG("%s:%s:%u txfifo overflowed!?\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        _tx_abort(dev);
        return;
    }

    fifo = 100; /* Maximum length of data to be sent */
    int to_send = left > fifo ? fifo : left;

    /* Write packet into TX FIFO */
    cc112x_writeburst_reg(dev, CC112X_BURST_TXFIFO, ((char *)pkt) + dev->pkt_buf.pos, to_send);
    dev->pkt_buf.pos += to_send;

    if(left == size) {
        /* Packet was successfully written into FIFO butter, switch to TX mode */
        DEBUG("%s:%s:%u strobing TX\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        cc112x_write_reg(dev, CC112X_IOCFG2, 0x02);
        cc112x_strobe(dev, CC112X_STX);
        gpio_irq_enable(dev->params.gpio2);
    }

    if(to_send < left) {
        /* No all has been sent */
        /* set GPIO2 to 0x2 -> will deassert at TX FIFO below threshold */
        DEBUG("%s:%s:%u irq when MCU can write more data\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        cc112x_write_reg(dev, CC112X_IOCFG2, 0x02);
        gpio_irq_enable(dev->params.gpio2);
    } else {
        /* All has been sent */
        /* set GPIO2 to 0x6 -> will deassert at packet end */
        DEBUG("%s:%s:%u irq will deassert at packet end\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        cc112x_write_reg(dev, CC112X_IOCFG2, 0x06);
        gpio_irq_enable(dev->params.gpio2);
    }
}

void cc112x_isr_handler(void* arg)
{
    netdev2_cc112x_t *cc112x_netdev = (netdev2_cc112x_t*)arg;
    cc112x_t *cc112x = &cc112x_netdev->cc112x;
//
//    printf("RADIO STATE - %x\n", cc112x->radio_state);
//    printf("MODEM_STATUS_0 - %x\n", cc112x_read_reg(cc112x, CC112X_MODEM_STATUS0));
//    printf("MODEM_STATUS_1 - %x\n", cc112x_read_reg(cc112x, CC112X_MODEM_STATUS1));
//    printf("MARC_STATUS_1 - %x\n", cc112x_read_reg(cc112x, CC112X_MARC_STATUS1));

    DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    switch(cc112x->radio_state) {
    case RADIO_RX:
        if(gpio_read(cc112x->params.gpio2)) {
            DEBUG("cc112x_isr_handler((): reading started\n");
            _rx_start(cc112x);
        } else {
            DEBUG("cc112x_isr_handler((): isr handled too slow?\n");
            cc112x_switch_to_rx(cc112x);
        }
        break;
    case RADIO_RX_BUSY:
        _rx_continue(cc112x_netdev);
        break;
    case RADIO_TX_BUSY:
        if(!gpio_read(cc112x->params.gpio2)) {
            _tx_continue(cc112x);
        } else {
            DEBUG("cc112x_isr_handler() RADIO_TX_BUSY + GPIO2 asserted\n");
        }
        break;
    default:
        DEBUG("%s:%s:%u: unhandled mode\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    }
}

int cc112x_send(cc112x_t *dev, cc112x_pkt_t *packet)
{
    DEBUG("cc112x: snd pkt to %u payload_length=%u\n",
            (unsigned)packet->address, (unsigned)packet->length+1);
    uint8_t size;

    switch(dev->radio_state) {
    case RADIO_RX_BUSY:
    case RADIO_TX_BUSY:
        puts("invalid state for sending");
        DEBUG("cc112x: invalid state for sending: %x\n",
                (dev->radio_state));
        return -EAGAIN;
    }

    /*
     * Number of bytes to send is:
     * length of phy payload (packet->length)
     * + size of length field (1 byte)
     */
    size = packet->length + 1;

    if(size > CC112X_PACKET_LENGTH) {
        DEBUG("%s:%s:%u trying to send oversized packet\n",
                RIOT_FILE_RELATIVE, __func__, __LINE__);
        return -ENOSPC;
    }

    /* set source address */
    packet->phy_src = dev->radio_address;

    /* Disable RX interrupt */
    gpio_irq_disable(dev->params.gpio2);
    dev->radio_state = RADIO_TX_BUSY;

    /* triggered when FIFIO filled above 64B */
    cc112x_write_reg(dev, CC112X_IOCFG2, 0x02);

    /* Put CC112x in IDLE mode to flush the FIFO */
    cc112x_strobe(dev, CC112X_SIDLE);

    /* Flush TX FIFO to be sure it is empty */
    cc112x_strobe(dev, CC112X_SFTX);

    memcpy((char*)&dev->pkt_buf.packet, packet, size);
    dev->pkt_buf.pos = 0;

    _tx_continue(dev);

    return size;
}
