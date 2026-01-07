
#ifndef __TSCH_CUSTOM_H__
#define __TSCH_CUSTOM_H__


/********** Libraries **********/
#include "contiki.h"
#include "net/netstack.h"

/******** Configuration *******/
// Size of the packet transmissions queue
#ifndef MAX_NUMBER_OF_CUSTOM_QUEUE
#define MAX_NUMBER_OF_CUSTOM_QUEUE 20
#endif

/************ Types ***********/
// structure to store every transmission of TSCH communication
typedef struct {
    uint8_t data_type;
    uint8_t packet_seqno;
    uint8_t transmission_count;
    uint8_t time_slot;
    uint8_t channel_offset;
    uint8_t node_id;
    linkaddr_t trans_addr;
} packet_status;

// structure to store packet_status array as a queue 
typedef struct
{
    int front;
    int rear; 
    int size;
    unsigned capacity;
    packet_status packets[MAX_NUMBER_OF_CUSTOM_QUEUE];
} queue_packet_status;

// packet data types for differentating the flows
enum data_type { UNICAST_DATA, BROADCAST_DATA, EB_DATA };

/********** Functions *********/
// Queue is full when size becomes equal to the capacity
int isFull(queue_packet_status *queue);

// Queue is empty when size is 0
int isEmpty(queue_packet_status *queue);

// Function to add an item to the queue. It changes rear and size
void enqueue(queue_packet_status *queue, packet_status pkt_sts);

// Function to empty the queue
uint8_t emptyQueue(queue_packet_status *queue);


/* ---------------  Additional functions ----------------------*/

// Additional function to remove an item from queue. It changes front and size
packet_status dequeue(queue_packet_status *queue);

// Additional function to get front of queue
packet_status front(queue_packet_status *queue);

// Additional function to get rear of queue
packet_status rear(queue_packet_status *queue);

#endif /* __TSCH_CUSTOM_H__ */