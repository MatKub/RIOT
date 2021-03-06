/*
 * Copyright (C) 2015 Kaspar Schleiser <kaspar@schleiser.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <assert.h>

#include <sys/uio.h>

#include "net/netdev2.h"
#include "net/gnrc.h"
#include "cc112x.h"
#include "cc112x-netdev2.h"
#include "net/gnrc/netdev2.h"
#include "od.h"

#define LOG_LEVEL LOG_DEBUG
#include "log.h"

static int _send(gnrc_netdev2_t *gnrc_netdev2, gnrc_pktsnip_t *pkt)
{
    LOG_DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    cc112x_pkt_t cc112x_pkt;
    netdev2_t *dev = gnrc_netdev2->dev;
    netdev2_cc112x_t *netdev_cc112x = (netdev2_cc112x_t *) dev;
    cc112x_t *cc112x = &netdev_cc112x->cc112x;

    assert(pkt != NULL);
    assert(dev->driver == &netdev2_cc112x_driver);

    gnrc_netif_hdr_t *netif_hdr;
    gnrc_pktsnip_t *payload;

    if(!(cc112x->radio_state == RADIO_RX_BUSY ||
            cc112x->radio_state == RADIO_IDLE ||
            cc112x->radio_state == RADIO_RX ||
            cc112x->radio_state == RADIO_PWD)){
        LOG_INFO("%s:%s:%u: Radio is busy\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
        return -EBUSY;
    }

    payload = pkt->next;

    if (pkt->type != GNRC_NETTYPE_NETIF) {
        LOG_WARNING("gnrc_netdev2_cc112x: First header was not generic netif header\n");
        gnrc_pktbuf_release(pkt);
        return -EBADMSG;
    }

    netif_hdr = (gnrc_netif_hdr_t *) pkt->data;

    /* set up header */
    if (netif_hdr->src_l2addr_len == 1) {
        uint8_t *_src_addr = gnrc_netif_hdr_get_src_addr(netif_hdr);
        cc112x_pkt.phy_src = *_src_addr;
    }
    else {
        cc112x_pkt.phy_src = cc112x->radio_address;
    }

    if (netif_hdr->flags & (GNRC_NETIF_HDR_FLAGS_BROADCAST |
                GNRC_NETIF_HDR_FLAGS_MULTICAST)) {
        cc112x_pkt.address = 0;
    }
    else {
        uint8_t *_dst_addr = gnrc_netif_hdr_get_dst_addr(netif_hdr);
        cc112x_pkt.address = _dst_addr[netif_hdr->dst_l2addr_len-1];
    }

    switch (payload->type) {
#ifdef MODULE_GNRC_SIXLOWPAN
        case GNRC_NETTYPE_SIXLOWPAN:
            cc112x_pkt.flags = 1;
            break;
#endif
        default:
            cc112x_pkt.flags = 0;
    }

    struct iovec vector;
    vector.iov_base = (char*)&cc112x_pkt;
    vector.iov_len = sizeof(cc112x_pkt_t);

    unsigned payload_len = 0;
    uint8_t *pos = cc112x_pkt.data;

//    /* Printing packets: */
//    gnrc_pktsnip_t *next = payload;
//    DEBUG("SENDING snips - %d\n",gnrc_pkt_count(next));
//    for(uint8_t a = 0 ; a < gnrc_pkt_count(pkt) ; ++a){
//        DEBUG("%d type - %d ; size - %d\n", a, next->type, next->size);
//        next = next->next;
//    }

//    DEBUG("cc112x_pkt - data");

    while (payload) {
        payload_len += payload->size;

//        next = payload;
//        DEBUG("Type - %d ; size - %d\n", next->type, next->size);

        if (payload_len > CC112X_MAX_DATA_LENGTH) {
            LOG_WARNING("gnrc_netdev2_cc112x: payload length exceeds maximum"
                    "(%u>%u)\n", payload_len, CC112X_MAX_DATA_LENGTH);
            gnrc_pktbuf_release(pkt);
            return -EBADMSG;
        }

        memcpy(pos, payload->data, payload->size);
        pos += payload->size;
        payload = payload->next;
    }

    /* pkt has been copied into iovec, we're done with it. */
    gnrc_pktbuf_release(pkt);

    cc112x_pkt.length = (uint8_t) payload_len + CC112X_HEADER_LENGTH;

//    DEBUG("Flags - %d, Address - %d , PHY src - %d\n", cc112x_pkt.flags, cc112x_pkt.address, cc112x_pkt.phy_src);
//    DEBUG("Printing the buffer in HEX (%dB)\n", cc112x_pkt.length);
//    for(uint8_t i = 0 ; i < cc112x_pkt.length ; ++i){
//        printf(" %x\n", cc112x_pkt.data[i]);
//    }
//    DEBUG("\n");

    LOG_INFO("gnrc_netdev2_cc112x: sending packet from %u to %u with payload "
            "length %u\n",
            (unsigned)cc112x_pkt.phy_src,
            (unsigned)cc112x_pkt.address,
            (unsigned)cc112x_pkt.length);

    return dev->driver->send(dev, &vector, 1);
}

static gnrc_pktsnip_t *_recv(gnrc_netdev2_t *gnrc_netdev2)
{
    LOG_DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    netdev2_t *dev = gnrc_netdev2->dev;
    cc112x_t *cc112x = &((netdev2_cc112x_t*) dev)->cc112x;

    cc112x_pkt_t *cc112x_pkt = &cc112x->rx_pkt_buf.packet;

    int payload_length = cc112x_pkt->length - CC112X_HEADER_LENGTH;

    int nettype;

//    DEBUG("Flags - %d, Address - %d , PHY src - %d\n", cc112x_pkt->flags, cc112x_pkt->address, cc112x_pkt->phy_src);
//    DEBUG("Printing the buffer in HEX (%dB)\n", cc112x_pkt->length);
//    for(uint8_t i = 0 ; i < cc112x_pkt->length ; ++i){
//        printf(" %x\n", cc112x_pkt->data[i]);
//    }
//    DEBUG("\n");

    int addr_len;
    switch (cc112x_pkt->flags) {
#ifdef MODULE_GNRC_SIXLOWPAN
        case 1:
            addr_len = 8;
            nettype = GNRC_NETTYPE_SIXLOWPAN;
            break;
#endif
        default:
            addr_len = 1;
            nettype = GNRC_NETTYPE_UNDEF;
    }

    /* copy packet payload into pktbuf */
    gnrc_pktsnip_t *pkt = gnrc_pktbuf_add(NULL, cc112x_pkt->data, payload_length, nettype);

    if(!pkt) {
        LOG_ERROR("cc112x: _recv: cannot allocate pktsnip.\n");
        return NULL;
    }

    gnrc_pktsnip_t *netif_hdr;
    netif_hdr = gnrc_pktbuf_add(NULL, NULL, sizeof(gnrc_netif_hdr_t) + 2*addr_len, GNRC_NETTYPE_NETIF);

    if (netif_hdr == NULL) {
        LOG_WARNING("gnrc_netdev2_cc112x: no space left in packet buffer\n");
        gnrc_pktbuf_release(pkt);
        return NULL;
    }

    gnrc_netif_hdr_init(netif_hdr->data, addr_len, addr_len);
    if (addr_len == 8) {
        uint64_t src_addr = cc112x_pkt->phy_src;
        uint64_t dst_addr = cc112x_pkt->address;
        gnrc_netif_hdr_set_src_addr(netif_hdr->data, (uint8_t*)&src_addr, addr_len);
        gnrc_netif_hdr_set_dst_addr(netif_hdr->data, (uint8_t*)&dst_addr, addr_len);
    }
    else {
        gnrc_netif_hdr_set_src_addr(netif_hdr->data, (uint8_t*)&cc112x_pkt->phy_src, addr_len);
        gnrc_netif_hdr_set_dst_addr(netif_hdr->data, (uint8_t*)&cc112x_pkt->address, addr_len);
    }

    ((gnrc_netif_hdr_t *)netif_hdr->data)->if_pid = thread_getpid();
    ((gnrc_netif_hdr_t *)netif_hdr->data)->lqi = cc112x->rx_pkt_buf.lqi;
    ((gnrc_netif_hdr_t *)netif_hdr->data)->rssi = cc112x->rx_pkt_buf.rssi;

    LOG_INFO("gnrc_netdev2_cc112x: received packet from %02x of length %u\n",
            (unsigned)cc112x_pkt->phy_src,
            (unsigned)cc112x_pkt->length-CC112X_HEADER_LENGTH);
//#if defined(MODULE_OD) && ENABLE_DEBUG
    od_hex_dump(cc112x_pkt->data, payload_length, OD_WIDTH_DEFAULT);
//#endif

    pkt->next = netif_hdr;

    return pkt;
}

int gnrc_netdev2_cc112x_init(gnrc_netdev2_t *gnrc_netdev2, netdev2_t *dev)
{
    LOG_DEBUG("%s:%s:%u\n", RIOT_FILE_RELATIVE, __func__, __LINE__);
    gnrc_netdev2->send = _send;
    gnrc_netdev2->recv = _recv;
    gnrc_netdev2->dev = dev;

    return 0;
}
