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
#include "cc112x-internal.h"
#include "periph/gpio.h"
#include "xtimer.h"
#include "irq.h"

#include "kernel_types.h"
#include "msg.h"

#include "cpu_conf.h"
#include "cpu.h"

#include "../cc112x/include/cc112x-defines.h"
#include "../cc112x/include/cc112x-interface.h"
#include "../cc112x/include/cc112x-internal.h"
#include "../cc112x/include/cc112x-netdev2.h"
#include "../cc112x/include/cc112x-spi.h"

#define LOG_LEVEL LOG_ERROR
#include "log.h"

int _tx_frame(cc112x_t *dev)
{
    LOG_DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    gpio_irq_disable(dev->params.gpio2);

    cc112x_pkt_t *pkt = &dev->pkt_buf.packet;
    int pkt_length = pkt->length+1;

    /* Write packet into TX FIFO */
    cc112x_writeburst_reg(dev, CC112X_BURST_TXFIFO, ((char *)pkt), pkt_length);
    /* Read FIFO buffer to check whether data was correctly saved */
    if(cc112x_read_reg(dev, CC112X_NUM_TXBYTES) == pkt_length) {
        LOG_INFO("%s:%s:%u Correnctly written %d bytes.\n", RIOT_FILE_RELATIVE, __func__, __LINE__, pkt_length);
    } else {
        LOG_WARNING("%s:%s:%u Error while writing FIFO, try again...\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        /* Emptying FIFO */
        cc112x_strobe(dev, CC112X_SFTX);
        cc112x_writeburst_reg(dev, CC112X_BURST_TXFIFO, ((char *)pkt), pkt_length);
        if(cc112x_read_reg(dev, CC112X_NUM_TXBYTES) == pkt_length) {
            LOG_INFO("%s:%s:%u Correnctly written %d bytes.\n", RIOT_FILE_RELATIVE, __func__, __LINE__, pkt_length);
        } else {
            LOG_ERROR("%s:%s:%u Error while writing %d bytes to FIFO.\n", RIOT_FILE_RELATIVE, __func__, __LINE__, pkt_length);
            dev->radio_channel = RADIO_IDLE;
            return -1;
        }
    }

    LOG_DEBUG("%s:%s:%u strobing TX\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    dev->radio_state = RADIO_TX_BUSY;
    cc112x_strobe(dev, CC112X_STX);
    gpio_irq_enable(dev->params.gpio2);

    return pkt_length;
}

int _rx_frame(netdev2_cc112x_t *cc112x_netdev)
{
    LOG_DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);

    cc112x_t *cc112x = (cc112x_t*)&cc112x_netdev->cc112x;
    cc112x_pkt_t *pkt = &cc112x->pkt_buf.packet;

    if(cc112x->radio_state != RADIO_RX_BUSY) {
        LOG_ERROR("%s:%s:%u _rx_frame in invalid state\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        return -1;
    }

    /* Getting information about packet */
    int length = cc112x_read_reg(cc112x, CC112X_NUM_RXBYTES);
    int status = cc112x_read_reg(cc112x, CC112X_MARC_STATUS1);
    if(status & 0x09){
        LOG_WARNING("%s:%s:%u FIFO overflow\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        return -1;
    }
    /* Store RSSI value of packet */
    cc112x->pkt_buf.rssi = cc112x_read_reg(cc112x, CC112X_RSSI1);
    /* Bit 0-6 of LQI indicates the link quality (LQI) */
    cc112x->pkt_buf.lqi = cc112x_read_reg(cc112x, CC112X_LQI_VAL);
    /* MSB of LQI is the CRC_OK bit */
    if(cc112x->pkt_buf.lqi & CRC_OK) {
        cc112x->pkt_buf.lqi &= 0x7f;
        /* Packet reading */
        cc112x_readburst_reg(cc112x, CC112X_BURST_RXFIFO, (char *)pkt, length);
        LOG_INFO("cc112x: received packet from=%u to=%u payload len=%u\n",
                (unsigned )cc112x->pkt_buf.packet.phy_src,
                (unsigned )cc112x->pkt_buf.packet.address,
                cc112x->pkt_buf.packet.length - 3);
        /* let someone know that we've got a packet */
        cc112x_netdev->netdev.event_callback(&cc112x_netdev->netdev, NETDEV2_EVENT_RX_COMPLETE, NULL);
    } else {
        LOG_INFO("%s:%s:%u crc-error\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        return 0;
    }
    return length;
}

void cc112x_isr_handler(void* arg)
{
    netdev2_cc112x_t *cc112x_netdev = (netdev2_cc112x_t*)arg;
    cc112x_t *cc112x = &cc112x_netdev->cc112x;

    LOG_DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    switch(cc112x->radio_state) {
    case RADIO_RX:
        if(gpio_read(cc112x->params.gpio2)) {
            LOG_INFO("cc112x_isr_handler((): frame appeared\n");
            cc112x->radio_state = RADIO_RX_BUSY;
        } else {
            LOG_WARNING("cc112x_isr_handler((): isr handled too slow?\n");
            cc112x_switch_to_rx(cc112x);
        }
        break;
    case RADIO_RX_BUSY:
        if(!gpio_read(cc112x->params.gpio2)) {
            LOG_INFO("cc112x_isr_handler((): whole received frame in buffer\n");
            _rx_frame(cc112x_netdev);
            cc112x_switch_to_rx(cc112x);
        } else {
            LOG_WARNING("cc112x_isr_handler((): interrupt lost?\n");
        }
        break;
    case RADIO_TX_BUSY:
        if(!gpio_read(cc112x->params.gpio2)) {
            LOG_INFO("cc112x_isr_handler((): frame sent, going to receive state\n");
            cc112x_switch_to_rx(cc112x);
        }
        break;
    default:
        LOG_WARNING("%s:%s:%u: unhandled mode\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    }
}

int cc112x_send(cc112x_t *dev, cc112x_pkt_t *packet)
{
    LOG_DEBUG("cc112x: snd pkt to %u payload_length=%u\n",
            (unsigned )packet->address, (unsigned )packet->length + 1);
    uint8_t size;
    int cnt = 0;

    if(!(RADIO_IDLE == dev->radio_state || RADIO_RX == dev->radio_state)){
        LOG_WARNING("%s:%s:%u: Invalid state for sending: %x\n", RIOT_FILE_RELATIVE, __func__, __LINE__, (dev->radio_state));
        while(RADIO_IDLE != dev->radio_state){
            xtimer_usleep(1000);
            ++cnt;
            if(100 == cnt){
                LOG_ERROR("%s:%s:%u: Too long in a inapropriate state: %x\n", RIOT_FILE_RELATIVE, __func__, __LINE__, (dev->radio_state));
                return -EAGAIN;
            }
        }
    }

    /*
     * Number of bytes to send is:
     * length of phy payload (packet->length)
     * + size of length field (1 byte)
     */
    size = packet->length + 1;

    if(size > CC112X_PACKET_LENGTH) {
        LOG_WARNING("%s:%s:%u trying to send oversized packet\n",
                RIOT_FILE_RELATIVE, __func__, __LINE__);
        return -ENOSPC;
    }

    /* set source address */
    packet->phy_src = dev->radio_address;

    /* Disable RX interrupt */
    gpio_irq_disable(dev->params.gpio2);
    dev->radio_state = RADIO_TX_BUSY;

    /* Put CC112x in IDLE mode to flush the FIFO */
    cc112x_strobe(dev, CC112X_SIDLE);
    /* Flush TX FIFO to be sure it is empty */
    cc112x_strobe(dev, CC112X_SFTX);

    memcpy((char*)&dev->pkt_buf.packet, packet, size);

    _tx_frame(dev);

    return size;
}
