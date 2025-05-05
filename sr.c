#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sr.h"

#define WINDOW_SIZE 8
#define MAX_SEQ 16  // Example: 2 * WINDOW_SIZE

// Global variables for sender (A)
struct pkt buffer[WINDOW_SIZE];
int base = 0;  // Base of the sending window
int next_seq = 0;  // Next sequence number to send
int window_size = WINDOW_SIZE;
float timeout = 20.0;  // Timer duration

// Global variables for receiver (B)
int expected_seq = 0;  // Next expected sequence number

void A_init() {
    // Move all declarations to the top to fix C90 warning
    int i;
    // Initialize buffer and other state
    for (i = 0; i < WINDOW_SIZE; i++) {
        buffer[i].seqnum = -1;  // Mark as unused
    }
    base = 0;
    next_seq = 0;
}

void A_output(struct msg message) {
    if (next_seq < base + window_size) {
        // Buffer the message and send a packet
        struct pkt packet;
        packet.seqnum = next_seq;
        packet.acknum = -1;  // Not used for data packets
        memcpy(packet.payload, message.data, 20);
        packet.checksum = compute_checksum(&packet);
        
        buffer[next_seq % window_size] = packet;
        
        // Print exact string as expected
        printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
        tolayer3(0, packet);
        
        // Start timer if this is the first packet in the window
        if (base == next_seq) {
            printf("          START TIMER: starting timer at %f\n", get_sim_time());
            starttimer(0, timeout);
        }
        
        next_seq = (next_seq + 1) % MAX_SEQ;
    } else {
        // Window is full, drop message (or buffer it in a queue)
        printf("----A: Window is full, dropping message\n");
    }
}

void A_input(struct pkt packet) {
    if (packet.checksum == compute_checksum(&packet)) {
        // Valid ACK
        printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
        if (packet.acknum >= base && packet.acknum < base + window_size) {
            printf("----A: ACK %d is not a duplicate\n", packet.acknum);
            // Mark packet as ACKed
            buffer[packet.acknum % window_size].seqnum = -1;  // Clear slot
            // Slide window if possible
            while (base < MAX_SEQ && buffer[base % window_size].seqnum == -1) {
                base = (base + 1) % MAX_SEQ;
            }
            // Stop timer if window is empty
            if (base == next_seq) {
                printf("          STOP TIMER: stopping timer at %f\n", get_sim_time());
                stoptimer(0);
            }
        } else {
            printf("----A: duplicate ACK %d received, ignoring\n", packet.acknum);
        }
    } else {
        printf("----A: corrupted ACK received, ignoring\n");
    }
}

void A_timerinterrupt() {
    printf("----A: time out, resend unacked packets!\n");
    int i;
    for (i = base; i < next_seq; i++) {
        if (buffer[i % window_size].seqnum != -1) {
            printf("---A: resending packet %d\n", buffer[i % window_size].seqnum);
            tolayer3(0, buffer[i % window_size]);
        }
    }
    // Restart timer
    printf("          START TIMER: starting timer at %f\n", get_sim_time());
    starttimer(0, timeout);
}

void B_input(struct pkt packet) {
    if (packet.checksum == compute_checksum(&packet) && packet.seqnum == expected_seq) {
        // Correct packet received
        printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
        // Deliver to layer 5
        tolayer5(1, packet.payload);
        // Send ACK
        struct pkt ack;
        ack.seqnum = -1;
        ack.acknum = packet.seqnum;
        memset(ack.payload, 0, 20);
        ack.checksum = compute_checksum(&ack);
        tolayer3(1, ack);
        expected_seq = (expected_seq + 1) % MAX_SEQ;
    } else {
        // Corrupted or out-of-order packet
        printf("----B: packet %d is corrupted or out of order, sending ACK for last correct packet %d\n", 
               packet.seqnum, (expected_seq - 1 + MAX_SEQ) % MAX_SEQ);
        struct pkt ack;
        ack.seqnum = -1;
        ack.acknum = (expected_seq - 1 + MAX_SEQ) % MAX_SEQ;
        memset(ack.payload, 0, 20);
        ack.checksum = compute_checksum(&ack);
        tolayer3(1, ack);
    }
}

int compute_checksum(struct pkt *packet) {
    int checksum = 0;
    int i;
    checksum += packet->seqnum + packet->acknum;
    for (i = 0; i < 20; i++) {
        checksum += packet->payload[i];
    }
    return checksum;
}