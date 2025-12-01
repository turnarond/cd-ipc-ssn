/*
 * IPC parser
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <netinet/in.h>
#include "ipc_parser.h"

/*
 * Initialize IPC header (`outb` must have at least IPC_MAX_PACKET_LENGTH bytes)
 */
ipc_header_t *ipc_parser_init_header (void *outb, uint8_t type, uint8_t status, uint16_t seqno)
{
    ipc_header_t *hdr = (ipc_header_t *)outb;
    hdr->magic = IPC_MAGIC;
    hdr->version = IPC_VERSION;
    hdr->type = type;
    hdr->status = status;
    hdr->url_len = 0;
    hdr->seqno = htons(seqno);
    hdr->data_len = 0;

    return  (hdr);
}

void ipc_parser_init_recv(ipc_recv_t *recv)
{
    recv->cur_len = recv->total_len = 0;
}

/*
 * Get IPC url
 */
bool ipc_parser_get_url (const ipc_header_t *ipc_hdr, ipc_url_t *url)
{
    if (ipc_hdr->magic != IPC_MAGIC ||
        ipc_hdr->version != IPC_VERSION) {
        return  (false);
    }

    url->url_len = ntohs(ipc_hdr->url_len);
    url->url = url->url_len ? (char *)(ipc_hdr + 1) : NULL;

    return  (true);
}

/*
 * Get IPC payload
 */
bool ipc_parser_get_payload (const ipc_header_t *ipc_hdr, ipc_payload_t *payload)
{
    if (ipc_hdr->magic != IPC_MAGIC ||
        ipc_hdr->version != IPC_VERSION) {
        return  (false);
    }

    payload->data_len = ntohl(ipc_hdr->data_len);
    if (payload->data_len == 0) {
        payload->data = NULL;
    } else {
        payload->data = (char*)(ipc_hdr + 1) + ntohs(ipc_hdr->url_len);
    }

    return  (true);
}

/*
 * IPC print header error info
 */
static void ipc_parser_print_error (const uint8_t *buffer, const char *info, size_t offset, size_t len)
{
    size_t i;

    len = len > IPC_HDR_LENGTH ? IPC_HDR_LENGTH : len;
    fprintf(stderr, "IPC input header error: %s buffer offset: %zu\n", info, offset);
    fprintf(stderr, "Details : ");
    for (i = 0; i < len; i++) {
        fprintf(stderr, "%02x", *buffer);
        buffer++;
    }
    fprintf(stderr, "\n");
}

static bool parse_header_length(const uint8_t *buf, size_t *out_length)
{
    ipc_header_t hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.magic != IPC_MAGIC || hdr.version != IPC_VERSION) {
        return false;
    }
    uint16_t url_len = ntohs(hdr.url_len);
    uint32_t data_len = ntohl(hdr.data_len);
    if ((uint64_t)url_len + data_len > IPC_MAX_DATA_LENGTH) {
        return false;
    }
    *out_length = (size_t)(url_len + data_len);
    return true;
}

/*
 * IPC input
 * (When there is an unaligned sticky packet,
 *  the processor must support unaligned memory access)
 */
bool ipc_parser_input (ipc_recv_t *recv, void *buf, size_t buf_len,
                        ipc_input_callback_t callback, void *arg)
{
    uint8_t *buffer = (uint8_t *)buf;
    size_t offset, length, left;
    ipc_header_t *ipc_hdr;

    do {
        if (recv->cur_len == 0) {
            if (buf_len < IPC_HDR_LENGTH) {
                memcpy(recv->buffer, buffer, buf_len);
                recv->cur_len   = buf_len;
                recv->total_len = 0;
                break;

            } else {
                parse_header_length(buffer, &length);
            }

            if (buf_len == length + IPC_HDR_LENGTH) {
                buffer  = NULL;
                buf_len = 0;

            } else if (buf_len >= length + IPC_HDR_LENGTH) {
                buffer  += length + IPC_HDR_LENGTH;
                buf_len -= length + IPC_HDR_LENGTH;

            } else {
                // 半个包
                memcpy(recv->buffer, buffer, buf_len);
                recv->cur_len   = buf_len;
                recv->total_len = (uint32_t)length + IPC_HDR_LENGTH;
                break;
            }

        } else {
            if (recv->cur_len < IPC_HDR_LENGTH) {
                /* copy header. */
                if (recv->cur_len + buf_len >= IPC_HDR_LENGTH) {
                    left = IPC_HDR_LENGTH - recv->cur_len;
                    memcpy(&recv->buffer[recv->cur_len], buffer, left);
                    parse_header_length(recv->buffer, &length);
                    recv->cur_len   = IPC_HDR_LENGTH;
                    recv->total_len = (uint32_t)length + IPC_HDR_LENGTH;
                    buffer  += left;
                    buf_len -= left;
                } else {
                    memcpy(&recv->buffer[recv->cur_len], buffer, buf_len);
                    recv->cur_len += buf_len;
                    buffer  = NULL;
                    buf_len = 0;
                    break;
                }
            }

            left = recv->total_len - recv->cur_len;
            if (buf_len == left) {
                memcpy(&recv->buffer[recv->cur_len], buffer, buf_len);
                recv->cur_len = 0;
                buffer  = NULL;
                buf_len = 0;

            } else if (buf_len > left) {
                memcpy(&recv->buffer[recv->cur_len], buffer, left);
                recv->cur_len = 0;
                buffer  += left;
                buf_len -= left;

            } else {
                memcpy(&recv->buffer[recv->cur_len], buffer, buf_len);
                recv->cur_len += buf_len;
                break;
            }

            ipc_hdr = (ipc_header_t *)recv->buffer;
        }

        if (!callback(arg, ipc_hdr)) {
            break;
        }

    } while (buf_len);

    return  (true);
}

/*
 * IPC packet input
 */
ipc_header_t *ipc_parser_packet_input (void *buf, size_t buf_len)
{
    uint8_t *buffer = (uint8_t *)buf;
    size_t length;
    ipc_header_t *ipc_hdr;

    if (buf_len < sizeof(ipc_header_t)) {
        return NULL;
    }

    ipc_hdr = (ipc_header_t *)(buffer);
    if (ipc_hdr->magic != IPC_MAGIC ||
        ipc_hdr->version != IPC_VERSION) {
        ipc_parser_print_error(buffer, "Magic & Version", 0, buf_len);
        return  (NULL);
    }

    length = ntohs(ipc_hdr->url_len) + ntohl(ipc_hdr->data_len);
    if (length > buf_len - IPC_HDR_LENGTH) {
        ipc_parser_print_error(buffer, "Length out of bounds", 0, buf_len);
        return  (NULL);
    }

    return  (ipc_hdr);
}

/*
 * end
 */
