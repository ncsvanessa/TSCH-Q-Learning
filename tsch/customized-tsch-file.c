// #include "dev/radio.h"
// #include "contiki.h"
// #include "net/netstack.h"
// #include "net/packetbuf.h"
// #include "net/queuebuf.h"
// #include "net/mac/framer/framer-802154.h"
// #include "net/mac/tsch/tsch.h"
// #include "sys/critical.h"

// #include "sys/log.h"

#include "customized-tsch-file.h"

//#include <limits.h>
// #include <stdio.h>
// #include <stdlib.h>

// Default status to return if a function fails
packet_status default_pkt;

// Queue is full when size becomes equal to the capacity
int isFull(queue_packet_status *queue)
{
    return (queue->size == queue->capacity);
}

// Queue is empty when size is 0
int isEmpty(queue_packet_status *queue)
{
    return (queue->size == 0);
}

// Function to add an item to the queue. It changes rear and size
void enqueue(queue_packet_status *queue, packet_status item)
{
    if (isFull(queue))
        return;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->packets[queue->rear] = item;
    queue->size = queue->size + 1;
}

// Function to remove an item from queue. It changes front and size
packet_status dequeue(queue_packet_status *queue)
{
    if (isEmpty(queue)){
        return default_pkt;
    }
    packet_status item = queue->packets[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size = queue->size - 1;
    return item;
}

// Empty the queue. It changes the front and rear pointer, makes the size 0
uint8_t emptyQueue(queue_packet_status *queue)
{
    uint8_t size = queue->size;
    queue->size = 0;
    queue->front = 0;
    queue->rear = -1;
    return size;
}

// Function to get the front of queue
packet_status front(queue_packet_status *queue)
{
    if (isEmpty(queue)){
        return default_pkt;
    }
    return queue->packets[queue->front];
}

// Function to get the rear of queue
packet_status rear(queue_packet_status *queue)
{
    if (isEmpty(queue)){
        return default_pkt;
    }
    return queue->packets[queue->rear];
}