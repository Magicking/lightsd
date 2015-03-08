// Copyright (c) 2014, Louis Opter <kalessin@kalessin.fr>
//
// This file is part of lighstd.
//
// lighstd is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// lighstd is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with lighstd.  If not, see <http://www.gnu.org/licenses/>.

#include <sys/queue.h>
#include <sys/tree.h>
#include <arpa/inet.h>
#include <assert.h>
#include <endian.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <event2/util.h>

#include "wire_proto.h"
#include "core/time_monotonic.h"
#include "bulb.h"
#include "gateway.h"
#include "core/lightsd.h"

union lgtd_lifx_target LGTD_LIFX_UNSPEC_TARGET = { .tags = 0 };

static struct lgtd_lifx_packet_infos_map lgtd_lifx_packet_infos =
    RB_INITIALIZER(&lgtd_lifx_packets_infos);

RB_GENERATE_STATIC(
    lgtd_lifx_packet_infos_map,
    lgtd_lifx_packet_infos,
    link,
    lgtd_lifx_packet_infos_cmp
);

static void
lgtd_lifx_wire_null_packet_encoder_decoder(void *pkt)
{
    (void)pkt;
}

static void
lgtd_lifx_wire_null_packet_handler(struct lgtd_lifx_gateway *gw,
                                   const struct lgtd_lifx_packet_header *hdr,
                                   const void *pkt)
{
    (void)gw;
    (void)hdr;
    (void)pkt;
}

void
lgtd_lifx_wire_load_packet_infos_map(void)
{
#define DECODER(x)  ((void (*)(void *))(x))
#define ENCODER(x)  ((void (*)(void *))(x))
#define HANDLER(x)                                  \
    ((void (*)(struct lgtd_lifx_gateway *,              \
               const struct lgtd_lifx_packet_header *,  \
               const void *))(x))
#define NO_PAYLOAD                                          \
    .encode = lgtd_lifx_wire_null_packet_encoder_decoder
#define RESPONSE_ONLY                                       \
    .encode = lgtd_lifx_wire_null_packet_encoder_decoder
#define REQUEST_ONLY                                        \
    .decode = lgtd_lifx_wire_null_packet_encoder_decoder,   \
    .handle = lgtd_lifx_wire_null_packet_handler

    static struct lgtd_lifx_packet_infos packet_table[] = {
        {
            REQUEST_ONLY,
            NO_PAYLOAD,
            .name = "GET_PAN_GATEWAY",
            .type = LGTD_LIFX_GET_PAN_GATEWAY
        },
        {
            .name = "PAN_GATEWAY",
            .type = LGTD_LIFX_PAN_GATEWAY,
            .size = sizeof(struct lgtd_lifx_packet_pan_gateway),
            .decode = DECODER(lgtd_lifx_wire_decode_pan_gateway),
            .encode = ENCODER(lgtd_lifx_wire_encode_pan_gateway),
            .handle = HANDLER(lgtd_lifx_gateway_handle_pan_gateway)
        },
        {
            REQUEST_ONLY,
            NO_PAYLOAD,
            .name = "GET_LIGHT_STATUS",
            .type = LGTD_LIFX_GET_LIGHT_STATE
        },
        {
            RESPONSE_ONLY,
            .name = "LIGHT_STATUS",
            .type = LGTD_LIFX_LIGHT_STATUS,
            .size = sizeof(struct lgtd_lifx_packet_light_status),
            .decode = DECODER(lgtd_lifx_wire_decode_light_status),
            .handle = HANDLER(lgtd_lifx_gateway_handle_light_status)
        },
        {
            REQUEST_ONLY,
            NO_PAYLOAD, // well it has a payload, but it's just 0 or 1...
            .size = sizeof(struct lgtd_lifx_packet_power_state),
            .name = "SET_POWER_STATE",
            .type = LGTD_LIFX_SET_POWER_STATE,
        },
        {
            .name = "POWER_STATE",
            .type = LGTD_LIFX_POWER_STATE,
            .size = sizeof(struct lgtd_lifx_packet_power_state),
            .decode = DECODER(lgtd_lifx_wire_decode_power_state),
            .handle = HANDLER(lgtd_lifx_gateway_handle_power_state)
        },
        {
            REQUEST_ONLY,
            .name = "SET_LIGHT_COLOR",
            .type = LGTD_LIFX_SET_LIGHT_COLOR,
            .size = sizeof(struct lgtd_lifx_packet_light_color),
            .encode = ENCODER(lgtd_lifx_wire_encode_light_color)
        }
    };

    for (int i = 0; i != LGTD_ARRAY_SIZE(packet_table); ++i) {
        RB_INSERT(
            lgtd_lifx_packet_infos_map,
            &lgtd_lifx_packet_infos,
            &packet_table[i]
        );
    }
}

const struct lgtd_lifx_packet_infos *
lgtd_lifx_wire_get_packet_infos(enum lgtd_lifx_packet_type packet_type)
{
    struct lgtd_lifx_packet_infos pkt_infos = { .type = packet_type };
    return RB_FIND(lgtd_lifx_packet_infos_map, &lgtd_lifx_packet_infos, &pkt_infos);
}

// Convert all the fields in the header to the host endianness.
//
// \return The payload size or -1 if the header is invalid.
void
lgtd_lifx_wire_decode_header(struct lgtd_lifx_packet_header *hdr)
{
    assert(hdr);

    hdr->size = le16toh(hdr->size);
    hdr->protocol.version = le16toh(hdr->protocol.version);
    if (hdr->protocol.tagged) {
        le64toh(hdr->target.tags);
    }
    hdr->timestamp = le64toh(hdr->timestamp);
    hdr->packet_type = le16toh(hdr->packet_type);
}

const struct lgtd_lifx_packet_infos *
lgtd_lifx_wire_setup_header(struct lgtd_lifx_packet_header *hdr,
                            enum lgtd_lifx_target_type target_type,
                            union lgtd_lifx_target target,
                            const uint8_t *site,
                            enum lgtd_lifx_packet_type packet_type)
{
    assert(hdr);

    const struct lgtd_lifx_packet_infos *pkt_infos =
        lgtd_lifx_wire_get_packet_infos(packet_type);

    memset(hdr, 0, sizeof(*hdr));
    hdr->size = pkt_infos->size + sizeof(*hdr);
    hdr->protocol.version = LGTD_LIFX_PROTOCOL_V1;
    hdr->packet_type = packet_type;
    if (site) {
        memcpy(hdr->site, site, sizeof(hdr->site));
    } else {
        assert(target_type == LGTD_LIFX_TARGET_ALL_DEVICES);
    }

    switch (target_type) {
    case LGTD_LIFX_TARGET_SITE:
        hdr->protocol.tagged = true;
        break;
    case LGTD_LIFX_TARGET_TAGS:
        hdr->protocol.tagged = true;
        hdr->target.tags = target.tags;
        break;
    case LGTD_LIFX_TARGET_DEVICE:
        hdr->protocol.addressable = false;
        memcpy(hdr->target.device_addr, target.addr, LGTD_LIFX_ADDR_LENGTH);
        break;
    case LGTD_LIFX_TARGET_ALL_DEVICES:
        hdr->protocol.tagged = true;
        break;
    }

    lgtd_lifx_wire_encode_header(hdr);

    return pkt_infos;
}

void
lgtd_lifx_wire_encode_header(struct lgtd_lifx_packet_header *hdr)
{
    assert(hdr);

    hdr->size = htole16(hdr->size);
    hdr->protocol.version = htole16(hdr->protocol.version);
    if (hdr->protocol.tagged) {
        le64toh(hdr->target.tags);
    }
    hdr->timestamp = htole64(hdr->timestamp);
    hdr->packet_type = htole16(hdr->packet_type);
}

void
lgtd_lifx_wire_decode_pan_gateway(struct lgtd_lifx_packet_pan_gateway *pkt)
{
    assert(pkt);

    pkt->port = le32toh(pkt->port);
}

void
lgtd_lifx_wire_encode_pan_gateway(struct lgtd_lifx_packet_pan_gateway *pkt)
{
    assert(pkt);

    pkt->port = htole32(pkt->port);
}

void
lgtd_lifx_wire_decode_light_status(struct lgtd_lifx_packet_light_status *pkt)
{
    assert(pkt);

    pkt->hue = le16toh(pkt->hue);
    pkt->saturation = le16toh(pkt->saturation);
    pkt->brightness = le16toh(pkt->brightness);
    pkt->kelvin = le16toh(pkt->kelvin);
    pkt->dim = le16toh(pkt->dim);
    pkt->power = le16toh(pkt->power);
    pkt->tags = le64toh(pkt->tags);
}

void
lgtd_lifx_wire_decode_power_state(struct lgtd_lifx_packet_power_state *pkt)
{
    assert(pkt);
}

void
lgtd_lifx_wire_encode_light_color(struct lgtd_lifx_packet_light_color *pkt)
{
    assert(pkt);

    pkt->hue = htole16(pkt->hue);
    pkt->saturation = htole16(pkt->saturation);
    pkt->brightness = htole16(pkt->brightness);
    pkt->kelvin = htole16(pkt->kelvin);
    pkt->transition = htole32(pkt->transition);
}