/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *   Adapted for mlvpn by Laurent Coustet (c) 2015
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <inttypes.h>
#include <string.h>
#include <sys/queue.h>

#include "reorder.h"
#include "log.h"

/* A generic circular buffer */
struct cir_buffer {
    unsigned int size;   /**< Number of pkts that can be stored */
    unsigned int mask;   /**< [buffer_size - 1]: used for wrap-around */
    unsigned int head;   /**< insertion point in buffer */
    unsigned int tail;   /**< extraction point in buffer */
    unsigned int count;
    mlvpn_pkt_t **pkts;
};

/* The reorder buffer data structure itself */
struct mlvpn_reorder_buffer {
    uint64_t min_seqn;  /**< Lowest seq. number that can be in the buffer */
    unsigned int memsize; /**< memory area size of reorder buffer */
    struct cir_buffer order_buf; /**< buffer used to reorder pkts */
    int is_initialized;
};

struct mlvpn_reorder_buffer *
mlvpn_reorder_init(struct mlvpn_reorder_buffer *b, unsigned int bufsize,
        unsigned int size)
{
    const unsigned int min_bufsize = sizeof(*b) +
                    (size * sizeof(mlvpn_pkt_t *));
    if (b == NULL) {
        log_crit("reorder", "Invalid reorder buffer parameter: NULL");
        return NULL;
    }
    if (bufsize < min_bufsize) {
        log_crit("reorder", "Invalid reorder buffer memory size: %u, "
            "minimum required: %u", bufsize, min_bufsize);
        return NULL;
    }

    memset(b, 0, bufsize);
    b->memsize = bufsize;
    b->order_buf.size = size;
    b->order_buf.mask = size - 1;
    b->order_buf.pkts = (void *)&b[1];

    return b;
}

struct mlvpn_reorder_buffer*
mlvpn_reorder_create(unsigned int size)
{
    struct mlvpn_reorder_buffer *b = NULL;

    const unsigned int bufsize = sizeof(struct mlvpn_reorder_buffer) +
                    (2 * size * sizeof(mlvpn_pkt_t *));
    /* Allocate memory to store the reorder buffer structure. */
    b = calloc(1, bufsize);
    if (b == NULL) {
        log_crit("reorder", "Memzone allocation failed");
    } else {
        mlvpn_reorder_init(b, bufsize, size);
    }
    return b;
}

void
mlvpn_reorder_reset(struct mlvpn_reorder_buffer *b)
{
    log_info("reorder", "resetting reorder buffer");
    mlvpn_reorder_init(b, b->memsize, b->order_buf.size);
}

void
mlvpn_reorder_free(struct mlvpn_reorder_buffer *b)
{
    /* Check user arguments. */
    if (b == NULL)
        return;
    free(b);
}

inline uint8_t
is_empty(struct cir_buffer *buf)
{
    return buf->tail == buf->head;
}

inline uint8_t
is_full(struct cir_buffer *buf)
{
    return ((buf->tail + 1) & buf->mask) == buf->head;
}

inline mlvpn_pkt_t*
dequeue_from_order_buf(struct mlvpn_reorder_buffer *b)
{
    struct cir_buffer *order_buf = &b->order_buf;
    mlvpn_pkt_t *packet = order_buf->pkts[order_buf->head];
    order_buf->pkts[order_buf->head] = NULL;
    b->min_seqn = packet->seq + 1;          // we expect the next packet to be the first one in our order_buf
    order_buf->head = (order_buf->head + 1) & order_buf->mask;
    if (order_buf->count > 0) {
        order_buf->count--;
    }
    log_debug("reorder", "dequeued packet from order_buf: %lu", packet->seq);
    return packet;
}

int
mlvpn_reorder_insert(struct mlvpn_reorder_buffer *b, mlvpn_pkt_t *pkt)
{
    int64_t offset;
    uint32_t position;
    struct cir_buffer *order_buf = &b->order_buf;

    if (!b->is_initialized) {
        b->min_seqn = pkt->seq;
        b->is_initialized = 1;
        log_info("reorder", "initial sequence: %"PRIu64"", pkt->seq);
    }

    /*
     * calculate the offset from the head pointer we need to go.
     * The subtraction takes care of the sequence number wrapping.
     * For example (using 16-bit for brevity):
     *  min_seqn  = 0xFFFD
     *  pkt_seq   = 0x0010
     *  offset    = 0x0010 - 0xFFFD = 0x13
     */
    offset = pkt->seq - b->min_seqn;

    if (offset >= 0 && offset < b->order_buf.size) {
        position = (order_buf->head + offset) & order_buf->mask;
        log_debug("reorder", "inserting packet %lu at position %u with offset %ld and min_seqn %lu", pkt->seq, position, offset, b->min_seqn);
        order_buf->pkts[position] = pkt;
        order_buf->count++;
    } else if (offset < 0) {
        log_info("reorder", "packet %lu out of range, offset %ld and min_seqn %lu", pkt->seq, offset, b->min_seqn);
        return -2;
    } else {
        log_info("reorder", "packet %lu out of range, offset %ld and min_seqn %lu", pkt->seq, offset, b->min_seqn);
        return -1;
    }
    return 0;
}

unsigned int
mlvpn_reorder_drain(struct mlvpn_reorder_buffer *b, mlvpn_pkt_t **pkts,
        unsigned max_pkts)
{
    unsigned int drain_cnt = 0;

    struct cir_buffer *order_buf = &b->order_buf;

    /*
     * fetch packets from order_buf until encountering our first hole
     * (don't "fetch" the hole als well, leaving it to be filled later)
     */
    while ((drain_cnt < max_pkts) &&
            (order_buf->pkts[order_buf->head] != NULL)) {
        pkts[drain_cnt++] = dequeue_from_order_buf(b);
        log_debug("reorder", "added packet from order_buf to drain output: %lu", pkts[drain_cnt-1]->seq);
    }
    return drain_cnt;
}

unsigned int
mlvpn_reorder_force_drain(struct mlvpn_reorder_buffer *b, mlvpn_pkt_t **pkts,
        unsigned max_pkts)
{
	uint64_t min_seqn = b->min_seqn;
	uint64_t first_drained = 0;
    uint64_t drain_cnt = 0;
    uint64_t skipped_holes = 0;
    struct cir_buffer *order_buf = &b->order_buf;
	uint64_t last_packet = order_buf->size;
	
	char drain_log[max_pkts];
	memset(drain_log, 0, max_pkts);

    /*
     * fetch packets from order_buf skipping the first hole
     */
    for(uint64_t i=0; i < order_buf->size && drain_cnt < max_pkts; i++) {
        if (order_buf->pkts[order_buf->head] != NULL) {
            pkts[drain_cnt] = dequeue_from_order_buf(b);
			if(!first_drained)
				first_drained = pkts[drain_cnt]->seq;
            log_debug("reorder", "%lu: force added packet %lu from order_buf to drain output: %lu", i, drain_cnt, pkts[drain_cnt]->seq);
            drain_cnt++;
			drain_log[i]='.';
			last_packet = i;
        } else {
        //} else if (skipped_holes < 1) {
            skipped_holes++;
            order_buf->head = (order_buf->head + 1) & order_buf->mask;
            log_debug("reorder", "%lu: skipping missing packet at drain count %lu, skipped holes: %lu", i, drain_cnt, skipped_holes);
			drain_log[i]='E';
        //} else {
        //    log_debug("reorder", "%lu: already skipped %lu missing packets, stopping force drain at drain count %lu", i, skipped_holes, drain_cnt);
        }
    }
    drain_log[last_packet+1]='\0';
    log_info("reorder", "Buffer start %lu, first drained: %lu: Drained %lu packets encountering %lu holes: %s", min_seqn, first_drained, drain_cnt, skipped_holes-(order_buf->size-last_packet)+1, drain_log);
    return drain_cnt;
}
