/*
 * Copyright (c) 2007, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         Testing the broadcast layer in Rime
 * \author
 *         Adam Dunkels <adam@sics.se>
 */

#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>

// My modification:
/* This structure holds information about neighbors. */
struct neighbor {
  /* The ->next pointer is needed since we are placing these on a
     Contiki list. */
  struct neighbor *next;

  /* The ->addr field holds the Rime address of the neighbor. */
  linkaddr_t addr;

  /* The ->last_rssi and ->last_lqi fields hold the Received Signal
     Strength Indicator (RSSI) and CC2420 Link Quality Indicator (LQI)
     values that are received for the incoming broadcast packets. */
  uint16_t last_rssi;
};

/* This #define defines the maximum amount of neighbors we can remember. */
#define MAX_NEIGHBORS 5

/* This MEMB() definition defines a memory pool from which we allocate
   neighbor entries. */
MEMB(neighbors_memb, struct neighbor, MAX_NEIGHBORS);

/* The neighbors_list is a Contiki list that holds the neighbors we
   have seen thus far. */
LIST(neighbors_list);

void sort_list_based_on_rssi() {
  if (neighbors_list == NULL || list_head(neighbors_list) == NULL) {
    return;
  }

  struct neighbor* prev = list_head(neighbors_list);
  if (prev == NULL) {
    return;
  }
  struct neighbor* next = list_item_next(prev);
  int swapped = 0;

  while (next != NULL) {
    if (next->last_rssi > prev->last_rssi) {
      // Move the mote having higher rssi to the top of the list;
      swapped = 1;
      list_remove(neighbors_list, next);
      list_push(neighbors_list, next);
    }

    if (swapped == 1) {
      prev = list_head(neighbors_list);
      next = list_item_next(prev);
      swapped = 0;
    }
    else {
      prev = next;
      next = next->next;
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Broadcast example");
AUTOSTART_PROCESSES(&example_broadcast_process);
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{
  struct neighbor *n;
  struct neighbor *each;
  uint16_t new_mote_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
  
  /* Print out a message. */
  printf("broadcast message received from %d.%d with, '%s', RSSI %d\n",
         from->u8[0], from->u8[1],
         (char *)packetbuf_dataptr(),
         new_mote_rssi);
  
  /* Check if we already know this neighbor. */
  for(n = list_head(neighbors_list); n != NULL; n = list_item_next(n)) {

    /* We break out of the loop if the address of the neighbor matches
       the address of the neighbor from which we received this
       broadcast message. */
    if(linkaddr_cmp(&n->addr, from)) {
      break;
    }
  }

  /* If n is NULL, this neighbor was not found in our list, and we
     allocate a new struct neighbor from the neighbors_memb memory
     pool. */
  if(n == NULL) {
    n = memb_alloc(&neighbors_memb);

    /* If we could not allocate a new neighbor entry, we should remove the mote with lowest rssi in neighbor_list */
    if(n == NULL) {
      struct neighbor *lowest_rssi_neighbor = list_tail(neighbors_list);
      if (lowest_rssi_neighbor->last_rssi < new_mote_rssi) {
        printf("DEBUG: Remove the mote (id=%d.%d) who has the lowest rssi (%d) in neighbor list\n",
                                        lowest_rssi_neighbor->addr.u8[0], 
                                        lowest_rssi_neighbor->addr.u8[1], 
                                        lowest_rssi_neighbor->last_rssi);
        list_remove(neighbors_list, lowest_rssi_neighbor);
        memb_free(&neighbors_memb, lowest_rssi_neighbor);
        // Re-allocate new memory for new mote.
        n = memb_alloc(&neighbors_memb);
      }
      else {
        printf("DEBUG: Don't append the new mote to the neighbor list because its RSSI is even lower than the existing mote having the lowest RSSI\n");
        return;
      }
    }

    /* Initialize the fields. */
    linkaddr_copy(&n->addr, from);
    /* Place the neighbor on the neighbor list, */
    list_add(neighbors_list, n);
  }

  /* We can now fill in the fields in our neighbor entry. */
  n->last_rssi = new_mote_rssi;

  /* Sort the list based on RSSI */
  sort_list_based_on_rssi();

  /* Dump the list */
  printf("[neighbor] [RSSI]\n");
  for(each = list_head(neighbors_list); each != NULL; each = list_item_next(each)) {
    printf("[%d.%d] [%d]\n", each->addr.u8[0], each->addr.u8[1], each->last_rssi);
  }

}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_broadcast_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    packetbuf_copyfrom("Hello", 6);
    broadcast_send(&broadcast);
    printf("broadcast message sent\n");
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
