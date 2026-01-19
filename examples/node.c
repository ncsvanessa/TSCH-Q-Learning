/********** Libraries ***********/
#include "contiki.h"
#include "net/ipv6/simple-udp.h"
#include "net/mac/tsch/tsch.h"
#include "lib/random.h"
#include "sys/node-id.h"
#include "net/mac/tsch/tsch-slot-operation.h"
#include "net/queuebuf.h"
#include "federated-learning.h"

#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

/********** Global variables ***********/
#define UDP_PORT 8765
#define UDP_FEDERATED_PORT 8766  // Port for Q-table sharing

// period to send a packet to the udp server
#define SEND_INTERVAL (60 * CLOCK_SECOND)

// period to update Q-values
#define Q_TABLE_INTERVAL (120 * CLOCK_SECOND)

// period for federated synchronization
// FEDERATED_SYNC_INTERVAL is 180 seconds by default

// period to finish setting up Minimal Scheduling 
#define SET_UP_MINIMAL_SCHEDULE (120 * CLOCK_SECOND)

// UDP communication process
PROCESS(node_udp_process, "UDP communicatio process");
// Q-Learning and scheduling process
PROCESS(scheduler_process, "RL-TSCH Scheduler Process");
// Federated learning synchronization process
PROCESS(federated_sync_process, "Federated Learning Sync Process");

AUTOSTART_PROCESSES(&node_udp_process, &scheduler_process, &federated_sync_process);

// variable to stop the udp comm for a while
uint8_t udp_com_stop = 1;

// data to send to the server
char custom_payload[PACKETBUF_CONF_SIZE];

// single slotframe for all communications 
struct tsch_slotframe *sf_min;
// struct tsch_link *link_list[TSCH_SCHEDULE_DEFAULT_LENGTH];
struct tsch_link *custom_links[TSCH_SCHEDULE_DEFAULT_LENGTH];

/********** Scheduler Setup ***********/
// Function starts Minimal Shceduler
static void init_tsch_schedule(void)
{
  // create a single slotframe
  //struct tsch_slotframe *sf_min;
  tsch_schedule_remove_all_slotframes();
  sf_min = tsch_schedule_add_slotframe(0, TSCH_SCHEDULE_DEFAULT_LENGTH);

  // shared/advertising cell at (0, 0)
  custom_links[0] = tsch_schedule_add_link(sf_min, LINK_OPTION_TX | LINK_OPTION_RX | LINK_OPTION_SHARED,
                         LINK_TYPE_ADVERTISING, &tsch_broadcast_address, 0, 0, 1);

  // all other cell are initialized as shared/dedicated 
  for (int i = 1; i < TSCH_SCHEDULE_DEFAULT_LENGTH; i++)
  {
    custom_links[i] = tsch_schedule_add_link(sf_min, LINK_OPTION_TX | LINK_OPTION_RX | LINK_OPTION_SHARED,
                           LINK_TYPE_NORMAL, &tsch_broadcast_address, i, 0, 1);
  }
}

// set up new schedule based on the chosen action
void set_up_new_schedule(uint8_t action) {
  // Action represents number of active slots (besides slot 0 which is always advertising)
  // Keep slots 0 to action active, disable the rest
  
  // Note: slot 0 is always kept as advertising slot
  // Slots 1 to action: keep active
  // Slots action+1 to end: could be disabled
  
  LOG_INFO("Schedule update: action=%u (keeping %u active slots)\n", action, action + 1);
}

// function to receive udp packets
static void rx_packet(struct simple_udp_connection *c,
                      const uip_ipaddr_t *sender_addr,
                      uint16_t sender_port,
                      const uip_ipaddr_t *receiver_addr,
                      uint16_t receiver_port,
                      const uint8_t *data,
                      uint16_t datalen)
{
  uint32_t seqnum;

  if (datalen >= sizeof(seqnum))
  {
    memcpy(&seqnum, data, sizeof(seqnum));

    LOG_INFO("Received from ");
    LOG_INFO_6ADDR(sender_addr);
    LOG_INFO_(", seqnum %" PRIu32, seqnum);
    LOG_INFO_("  data: %s\n", data);
  }
}

// function to populate the payload
void create_payload() {
  for (int i = 0; i < PACKETBUF_CONF_SIZE; i++){
    custom_payload[i] = i % 26 + 'a';
  }
}

// function to empty the queue and/or print the statistics
uint8_t empty_schedule_records(uint8_t tx_rx) {
  queue_packet_status *queue;
  if (tx_rx == 0) {
    queue = func_custom_queue_tx();
    LOG_INFO(" Transmission Operations in %lu seconds\n", (unsigned long)Q_TABLE_INTERVAL);
  } else {
    queue = func_custom_queue_rx();
    LOG_INFO(" Receiving Operations in %lu seconds\n", (unsigned long)Q_TABLE_INTERVAL);
  }
  #if PRINT_TRANSMISSION_RECORDS
  for(int i=0; i < queue->size; i++){
      LOG_INFO("seqnum:%u trans_count:%u timeslot:%u channel_off:%u\n", 
      queue->packets[i].packet_seqno,  
      queue->packets[i].transmission_count, 
      queue->packets[i].time_slot,
      queue->packets[i].channel_offset);
    }
  #endif
  return emptyQueue(queue);
}

/********** UDP Communication Process - Start **********/
PROCESS_THREAD(node_udp_process, ev, data)
{
  static struct simple_udp_connection udp_conn;
  static struct etimer periodic_timer;
  // timer to check if the minimal schedule finished setting-up
  static struct etimer minimal_schedule_setup_timer; 
  static uint32_t seqnum;
  uip_ipaddr_t dst;

  PROCESS_BEGIN();
  
  // creating the payload
  create_payload();
  LOG_INFO("Payload created: %d bytes\n", (int)sizeof(custom_payload));
  
  // generate random q-values
  generate_random_q_values();
  LOG_INFO("Q-values initialized\n");
  
  // Initialize federated learning
  federated_learning_init(WEIGHTED_FEDAVG);  // Use weighted averaging
  LOG_INFO("Federated learning initialized\n");

  /* Initialization; `rx_packet` is the function for packet reception */
  simple_udp_register(&udp_conn, UDP_PORT, NULL, UDP_PORT, rx_packet);

  // Start TSCH with custom schedule
  init_tsch_schedule();
  LOG_INFO("Custom TSCH schedule initialized\n");
  
  if (node_id == 1)
  { /* node_id is 1, then start as root*/
    NETSTACK_ROUTING.root_start();
    LOG_INFO("Started as TSCH coordinator/root\n");
  } else {
    LOG_INFO("Started as TSCH node, scanning for network\n");
  }

  // setting up the timer to create network with Minimal Scheduling
  etimer_set(&minimal_schedule_setup_timer, SET_UP_MINIMAL_SCHEDULE);

  // check if the Minimal Scheduling finished
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&minimal_schedule_setup_timer));
  udp_com_stop = 0;
  LOG_INFO("Finished setting up Minimal Scheduling\n");
  
  // if this is a simple node, start sending upd packets
  if (node_id != 1)
  { LOG_INFO("Started UDP communication\n");
    // start the timer for periodic udp packet sending
    etimer_set(&periodic_timer, SEND_INTERVAL);
    /* Main UDP comm Loop */
    while (udp_com_stop == 0)
    {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      if (NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dst))
      {
        /* Send custom payload to the network root node and increase the packet count number*/
        seqnum++;
        LOG_INFO("Send to ");
        LOG_INFO_6ADDR(&dst);
        LOG_INFO_(", application packet number %" PRIu32 "\n", seqnum);
        simple_udp_sendto(&udp_conn, &custom_payload, sizeof(custom_payload), &dst);
      }
      etimer_set(&periodic_timer, SEND_INTERVAL);
    }
  }
  PROCESS_END();
}
/********** UDP Communication Process - End ***********/

/********** RL-TSCH Scheduler Process - Start ***********/
PROCESS_THREAD(scheduler_process, ev, data)
{
  // timer to update Q-table
  static struct etimer q_table_update_timer;
  // timer to check if the minimal schedule finished setting-up
  static struct etimer minimal_schedule_setup_timer; 

  PROCESS_BEGIN();
  
  /* ************  Finish Minimal Scheduling First ******************/
  etimer_set(&minimal_schedule_setup_timer, SET_UP_MINIMAL_SCHEDULE);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&minimal_schedule_setup_timer));
  /* ************  Finish Minimal Scheduling   ******************/
  
  uint8_t *queue_length;
  uint8_t buffer_len_before = 0;
  uint8_t buffer_len_after = 0;
  uint8_t n_conflicts = 0;  // conflict counter

  /* Main Scheduler Loop */
  while (1)
  {
    // getting the action with the highest q-value and setting up the schedule
    uint8_t action = get_highest_q_val();
    set_up_new_schedule(action);

    // record the buffer size and reset conflict counter
    buffer_len_before = getCustomBuffLen();
    n_conflicts = 0;
    udp_com_stop = 0;

    // set the timer to update Q-table
    etimer_set(&q_table_update_timer, Q_TABLE_INTERVAL);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&q_table_update_timer));

    buffer_len_after = getCustomBuffLen();
    queue_length = getCurrentQueueLen();
    LOG_INFO("Buffer Size: before=%u after=%u current=%u\n", 
             buffer_len_before, buffer_len_after, *queue_length);
    LOG_INFO("Chosen Action: %u\n", action);

    // stopping the slot operations
    udp_com_stop = 1;
    
    // calculating the number of trans/receptions
    uint8_t n_tx_count = empty_schedule_records(0);    
    uint8_t n_rx_count = empty_schedule_records(1);

    // calculate the reward using the new TSCH reward function with conflict detection
    float new_reward = tsch_reward_function(n_tx_count, n_rx_count, buffer_len_before, 
                                           buffer_len_after, n_conflicts);
    
    LOG_INFO("Reward: tx=%u rx=%u conflicts=%u reward=%.2f\n", 
             n_tx_count, n_rx_count, n_conflicts, (double)new_reward);
    
    update_q_table(action, new_reward);
    
    // Increment local sample count for federated learning
    increment_local_samples();
  }
  PROCESS_END();
}
/********** RL-TSCH Scheduler Process - End ***********/

/********** Federated Learning Synchronization Process - Start ***********/

// Structure to encapsulate Q-table message
typedef struct {
    uint16_t node_id;
    uint8_t num_samples;
    uint16_t q_table_size;
    float q_values[Q_VALUE_LIST_SIZE];
} q_table_message_t;

// Callback for receiving Q-table messages
static void rx_qtable_packet(struct simple_udp_connection *c,
                              const uip_ipaddr_t *sender_addr,
                              uint16_t sender_port,
                              const uip_ipaddr_t *receiver_addr,
                              uint16_t receiver_port,
                              const uint8_t *data,
                              uint16_t datalen)
{
    if (datalen == sizeof(q_table_message_t)) {
        q_table_message_t *msg = (q_table_message_t *)data;
        
        LOG_INFO("Received Q-table from node %u (samples=%u)\n", 
                 msg->node_id, msg->num_samples);
        
        // Store neighbor's Q-table
        if (store_neighbor_q_table(msg->node_id, msg->q_values, msg->num_samples)) {
            LOG_INFO("Successfully stored Q-table from node %u\n", msg->node_id);
        } else {
            LOG_WARN("Failed to store Q-table from node %u\n", msg->node_id);
        }
    } else {
        LOG_WARN("Received malformed Q-table message (size=%u, expected=%lu)\n", 
                 datalen, (unsigned long)sizeof(q_table_message_t));
    }
}

PROCESS_THREAD(federated_sync_process, ev, data)
{
    static struct simple_udp_connection federated_conn;
    static struct etimer sync_timer;
    static struct etimer minimal_schedule_setup_timer;
    static q_table_message_t q_msg;
    
    PROCESS_BEGIN();
    
    /* Wait for minimal scheduling to finish */
    etimer_set(&minimal_schedule_setup_timer, SET_UP_MINIMAL_SCHEDULE);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&minimal_schedule_setup_timer));
    
    LOG_INFO("Starting Federated Learning Sync Process\n");
    
    // Register UDP connection for Q-table sharing
    simple_udp_register(&federated_conn, UDP_FEDERATED_PORT, NULL, 
                       UDP_FEDERATED_PORT, rx_qtable_packet);
    
    // Set initial sync timer with small random delay to avoid synchronization
    etimer_set(&sync_timer, (FEDERATED_SYNC_INTERVAL * CLOCK_SECOND) + 
               (random_rand() % (30 * CLOCK_SECOND)));
    
    /* Main Federated Sync Loop */
    while (1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&sync_timer));
        
        // Clean up stale neighbors (timeout: 2x sync interval)
        cleanup_stale_neighbors(FEDERATED_SYNC_INTERVAL * 2);
        
        // Broadcast local Q-table to neighbors
        if (NETSTACK_ROUTING.node_is_reachable()) {
            // Prepare Q-table message
            q_msg.node_id = node_id;
            q_msg.num_samples = get_local_sample_count();
            q_msg.q_table_size = Q_VALUE_LIST_SIZE;
            
            float *local_q = get_local_q_table_for_sharing();
            memcpy(q_msg.q_values, local_q, Q_VALUE_LIST_SIZE * sizeof(float));
            
            // Broadcast to all nodes (use broadcast address)
            uip_ipaddr_t broadcast_addr;
            uip_create_linklocal_allnodes_mcast(&broadcast_addr);
            
            LOG_INFO("Broadcasting Q-table (samples=%u)\n", q_msg.num_samples);
            simple_udp_sendto(&federated_conn, &q_msg, sizeof(q_msg), &broadcast_addr);
            
            // Perform federated aggregation
            uint8_t num_aggregated = federated_aggregate();
            
            if (num_aggregated > 0) {
                uint8_t neighbors, samples;
                fed_aggregation_method_t method;
                get_federated_stats(&neighbors, &samples, &method);
                
                LOG_INFO("Federated aggregation complete: neighbors=%u, method=%u, local_samples=%u\n",
                         neighbors, method, samples);
            } else {
                LOG_INFO("No neighbors to aggregate with\n");
            }
        }
        
        // Reset timer for next sync cycle
        etimer_set(&sync_timer, FEDERATED_SYNC_INTERVAL * CLOCK_SECOND);
    }
    
    PROCESS_END();
}
/********** Federated Learning Synchronization Process - End **********/