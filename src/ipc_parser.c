/*
 * IPC parser - Industrial-grade implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <netinet/in.h>
#include "ipc_parser.h"

/* ------------------ Helper Functions ------------------ */

static bool validate_and_get_total_length(const uint8_t *buf, size_t buf_size, size_t *out_total_len)
{
    if (buf_size < IPC_HDR_LENGTH) {
        return false;
    }

    ipc_header_t hdr;
    memcpy(&hdr, buf, sizeof(hdr));

    if (hdr.magic != IPC_MAGIC || hdr.version != IPC_VERSION) {
        return false;
    }

    uint16_t url_len = ntohs(hdr.url_len);
    uint32_t data_len = ntohl(hdr.data_len);

    // Prevent overflow
    if ((uint64_t)url_len + data_len > IPC_MAX_DATA_LENGTH) {
        return false;
    }

    size_t total = IPC_HDR_LENGTH + url_len + data_len;
    if (total > buf_size) {
        return false; // Truncated packet
    }

    if (out_total_len) {
        *out_total_len = total;
    }
    return true;
}

static void ipc_parser_print_error(const uint8_t *buffer, const char *info, size_t offset, size_t len)
{
    if (len > IPC_HDR_LENGTH) len = IPC_HDR_LENGTH;
    fprintf(stderr, "IPC input header error: %s at offset %zu\n", info, offset);
    fprintf(stderr, "Header bytes: ");
    for (size_t i = 0; i < len; i++) {
        fprintf(stderr, "%02x ", buffer[i]);
    }
    fprintf(stderr, "\n");
}

/* ------------------ Public API ------------------ */

ipc_header_t *ipc_parser_init_header(void *outb, uint8_t type, uint8_t status, uint16_t seqno)
{
    if (!outb) return NULL;

    ipc_header_t *hdr = (ipc_header_t *)outb;
    hdr->magic = IPC_MAGIC;
    hdr->version = IPC_VERSION;
    hdr->type = type;
    hdr->status = status;
    hdr->url_len = 0;
    hdr->seqno = htons(seqno);
    hdr->data_len = 0;
    return hdr;
}

void ipc_parser_init_recv(ipc_recv_t *recv)
{
    if (!recv) return;
    recv->cur_len = 0;
    recv->total_len = 0;
}

bool ipc_parser_set_url(ipc_header_t *ipc_hdr, const ipc_url_t *url)
{
    if (!ipc_hdr || !url) return false;
    if (ipc_hdr->magic != IPC_MAGIC || ipc_hdr->version != IPC_VERSION) return false;
    if (ntohl(ipc_hdr->data_len) != 0) return false; // payload already set

    if (url->url_len > IPC_MAX_DATA_LENGTH) return false;

    ipc_hdr->url_len = htons((uint16_t)url->url_len);
    if (url->url_len > 0) {
        memcpy((char*)(ipc_hdr + 1), url->url, url->url_len);
    }
    return true;
}

bool ipc_parser_set_payload(ipc_header_t *ipc_hdr, const ipc_payload_t *payload)
{
    if (!ipc_hdr || !payload) return false;
    if (ipc_hdr->magic != IPC_MAGIC || ipc_hdr->version != IPC_VERSION) return false;

    size_t url_len = ntohs(ipc_hdr->url_len);
    if (payload->data_len == 0) {
        ipc_hdr->data_len = 0;
        return true;
    }

    if (payload->data_len > IPC_MAX_DATA_LENGTH || 
        url_len > IPC_MAX_DATA_LENGTH ||
        url_len + payload->data_len > IPC_MAX_DATA_LENGTH) {
        return false;
    }

    ipc_hdr->data_len = htonl((uint32_t)payload->data_len);
    memcpy((char*)(ipc_hdr + 1) + url_len, payload->data, payload->data_len);
    return true;
}

bool ipc_parser_get_url(const ipc_header_t *ipc_hdr, ipc_url_t *url)
{
    if (!ipc_hdr || !url) return false;
    if (ipc_hdr->magic != IPC_MAGIC || ipc_hdr->version != IPC_VERSION) return false;

    url->url_len = ntohs(ipc_hdr->url_len);
    url->url = (url->url_len > 0) ? (char*)(ipc_hdr + 1) : NULL;
    return true;
}

bool ipc_parser_get_payload(const ipc_header_t *ipc_hdr, ipc_payload_t *payload)
{
    if (!ipc_hdr || !payload) return false;
    if (ipc_hdr->magic != IPC_MAGIC || ipc_hdr->version != IPC_VERSION) return false;

    payload->data_len = ntohl(ipc_hdr->data_len);
    if (payload->data_len == 0) {
        payload->data = NULL;
    } else {
        payload->data = (char*)(ipc_hdr + 1) + ntohs(ipc_hdr->url_len);
    }
    return true;
}

ipc_header_t *ipc_parser_packet_input(void *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return NULL;
    size_t total_len;
    if (!validate_and_get_total_length((const uint8_t*)buf, buf_len, &total_len)) {
        ipc_parser_print_error((const uint8_t*)buf, "Invalid header or truncated", 0, buf_len);
        return NULL;
    }
    return (ipc_header_t*)buf;
}

/* ------------------ Stream Parser (Sticky Packet Handling) ------------------ */

bool ipc_parser_input(ipc_recv_t *recv, void *buf, size_t buf_len,
                      ipc_input_callback_t callback, void *arg)
{
    if (!recv || !callback) return false;
    if (!buf || buf_len == 0) return true; // nothing to do

    const uint8_t *input = (const uint8_t *)buf;
    size_t consumed = 0;

    while (consumed < buf_len) {
        size_t space_left = IPC_MAX_PACKET_LENGTH - recv->cur_len;
        if (space_left == 0) {
            // Buffer full but no complete packet? Malformed stream.
            recv->cur_len = 0;
            recv->total_len = 0;
            return false;
        }

        size_t to_copy = (buf_len - consumed) < space_left ? (buf_len - consumed) : space_left;
        memcpy(recv->buffer + recv->cur_len, input + consumed, to_copy);
        recv->cur_len += to_copy;
        consumed += to_copy;

        // Try to extract as many complete packets as possible
        while (1) {
            if (recv->cur_len < IPC_HDR_LENGTH) break;

            size_t total_len;
            if (!validate_and_get_total_length(recv->buffer, recv->cur_len, &total_len)) {
                // Header invalid or truncated
                ipc_parser_print_error(recv->buffer, "Invalid packet in stream", 0, recv->cur_len);
                recv->cur_len = 0;
                recv->total_len = 0;
                return false;
            }

            if (recv->cur_len < total_len) break; // Not enough data yet

            // We have a complete packet
            ipc_header_t *hdr = (ipc_header_t *)recv->buffer;
            if (!callback(arg, hdr)) {
                // Callback requests stop
                recv->cur_len = 0;
                recv->total_len = 0;
                return true;
            }

            // Remove processed packet
            memmove(recv->buffer, recv->buffer + total_len, recv->cur_len - total_len);
            recv->cur_len -= total_len;
        }
    }

    return true;
}