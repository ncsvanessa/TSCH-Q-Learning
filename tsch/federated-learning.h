#ifndef FEDERATED_LEARNING_HEADER
#define FEDERATED_LEARNING_HEADER

/********** Libraries **********/
#include "contiki.h"

// Forward declaration to avoid circular dependency
#ifndef Q_VALUE_LIST_SIZE
#define Q_VALUE_LIST_SIZE 101  // Default TSCH slotframe length
#endif

/******** Configuration *******/
// Maximum number of neighbor nodes to store Q-tables from
#ifndef MAX_FEDERATED_NEIGHBORS
#define MAX_FEDERATED_NEIGHBORS 10
#endif

// Federated learning synchronization interval
#ifndef FEDERATED_SYNC_INTERVAL
#define FEDERATED_SYNC_INTERVAL 180
#endif

// Enable/disable federated learning
#ifndef ENABLE_FEDERATED_LEARNING
#define ENABLE_FEDERATED_LEARNING 1
#endif

// Federated aggregation method
typedef enum {
    FEDAVG,           // Federated Averaging (default)
    FEDMEDIAN,        // Federated Median
    WEIGHTED_FEDAVG   // Weighted Federated Averaging (based on node performance)
} fed_aggregation_method_t;

/********** Structures *********/

// Structure to store Q-table from a neighbor node
typedef struct {
    uint16_t node_id;                    // ID of the neighbor node
    float q_values[Q_VALUE_LIST_SIZE];   // Q-table from neighbor
    uint8_t num_samples;                  // Number of learning iterations (for weighting)
    uint8_t is_active;                    // Whether this entry is valid/active
    uint32_t last_update_time;            // Timestamp of last update
} neighbor_q_table_t;

// Global federated learning state
typedef struct {
    neighbor_q_table_t neighbors[MAX_FEDERATED_NEIGHBORS];  // Q-tables from neighbors
    uint8_t num_active_neighbors;                            // Number of active neighbors
    uint8_t local_num_samples;                               // Local learning iterations count
    fed_aggregation_method_t aggregation_method;             // Aggregation method to use
    float aggregation_weight;                                 // Weight for local model (0-1)
} federated_state_t;

/********** Functions *********/

/**
 * Initialize federated learning system
 */
void federated_learning_init(fed_aggregation_method_t method);

/**
 * Store or update Q-table from a neighbor node
 * Returns 1 on success, 0 on failure
 */
uint8_t store_neighbor_q_table(uint16_t node_id, float *q_values, uint8_t num_samples);

/**
 * Aggregate Q-tables from neighbors using FedAvg (Federated Averaging)
 * Updates the local Q-table with the aggregated values
 * Returns the number of neighbors included in aggregation
 */
uint8_t federated_aggregate_fedavg(void);

/**
 * Aggregate Q-tables using weighted average based on performance
 * Nodes with more samples get higher weight
 * Returns the number of neighbors included in aggregation
 */
uint8_t federated_aggregate_weighted(void);

/**
 * Aggregate Q-tables using median value
 * Returns the number of neighbors included in aggregation
 */
uint8_t federated_aggregate_median(void);

/**
 * Main federated aggregation function
 * Returns the number of neighbors included in aggregation
 */
uint8_t federated_aggregate(void);

/**
 * Get the local Q-table to send to neighbors
 * Returns pointer to local Q-table
 */
float* get_local_q_table_for_sharing(void);

/**
 * Increment local sample count
 */
void increment_local_samples(void);

/**
 * Get current local sample count
 */
uint8_t get_local_sample_count(void);

/**
 * Clean up stale neighbor entries
 * Removes entries older than timeout seconds
 */
void cleanup_stale_neighbors(uint32_t timeout_seconds);

/**
 * Get federated learning statistics
 */
void get_federated_stats(uint8_t *num_neighbors, uint8_t *local_samples, 
                         fed_aggregation_method_t *method);

/**
 * Set aggregation method
 */
void set_aggregation_method(fed_aggregation_method_t method);

/**
 * Set local model weight for aggregation (0.0 to 1.0)
 * Higher weight = trust local model more
 */
void set_local_model_weight(float weight);

#endif /* FEDERATED_LEARNING_HEADER */