/********** Libraries ***********/
#include "federated-learning.h"
#include "q-learning.h"
#include "sys/clock.h"
#include <string.h>
#include <stdlib.h>

#include "sys/log.h"
#define LOG_MODULE "FedLearn"
#define LOG_LEVEL LOG_LEVEL_INFO

/********** Global Variables ***********/
static federated_state_t fed_state;

/********** Helper Functions ***********/

/**
 * Compare function for qsort (used in median calculation)
 */
static int compare_float(const void *a, const void *b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

/**
 * Calculate median of an array of floats
 */
static float calculate_median(float *values, uint8_t count) {
    if (count == 0) return 0.0;
    
    // Create temporary array for sorting
    float temp[MAX_FEDERATED_NEIGHBORS + 1];
    memcpy(temp, values, count * sizeof(float));
    
    // Sort array
    qsort(temp, count, sizeof(float), compare_float);
    
    // Return median
    if (count % 2 == 0) {
        return (temp[count/2 - 1] + temp[count/2]) / 2.0;
    } else {
        return temp[count/2];
    }
}

/********** Public Functions ***********/

/**
 * Initialize federated learning system
 */
void federated_learning_init(fed_aggregation_method_t method) {
    // Initialize all neighbors as inactive
    for (int i = 0; i < MAX_FEDERATED_NEIGHBORS; i++) {
        fed_state.neighbors[i].is_active = 0;
        fed_state.neighbors[i].node_id = 0;
        fed_state.neighbors[i].num_samples = 0;
        fed_state.neighbors[i].last_update_time = 0;
    }
    
    fed_state.num_active_neighbors = 0;
    fed_state.local_num_samples = 0;
    fed_state.aggregation_method = method;
    fed_state.aggregation_weight = 0.5;  // Equal weight between local and federated
    
    LOG_INFO("Federated Learning initialized with method=%u\n", method);
}

/**
 * Store or update Q-table from a neighbor node
 */
uint8_t store_neighbor_q_table(uint16_t node_id, float *q_values, uint8_t num_samples) {
    if (q_values == NULL) {
        LOG_WARN("Attempted to store NULL Q-table\n");
        return 0;
    }
    
    // Check if this neighbor already exists
    int existing_idx = -1;
    for (int i = 0; i < MAX_FEDERATED_NEIGHBORS; i++) {
        if (fed_state.neighbors[i].is_active && 
            fed_state.neighbors[i].node_id == node_id) {
            existing_idx = i;
            break;
        }
    }
    
    // Update existing neighbor
    if (existing_idx >= 0) {
        memcpy(fed_state.neighbors[existing_idx].q_values, q_values, 
               Q_VALUE_LIST_SIZE * sizeof(float));
        fed_state.neighbors[existing_idx].num_samples = num_samples;
        fed_state.neighbors[existing_idx].last_update_time = clock_seconds();
        LOG_INFO("Updated Q-table from node %u (samples=%u)\n", node_id, num_samples);
        return 1;
    }
    
    // Find empty slot for new neighbor
    for (int i = 0; i < MAX_FEDERATED_NEIGHBORS; i++) {
        if (!fed_state.neighbors[i].is_active) {
            fed_state.neighbors[i].node_id = node_id;
            memcpy(fed_state.neighbors[i].q_values, q_values, 
                   Q_VALUE_LIST_SIZE * sizeof(float));
            fed_state.neighbors[i].num_samples = num_samples;
            fed_state.neighbors[i].is_active = 1;
            fed_state.neighbors[i].last_update_time = clock_seconds();
            fed_state.num_active_neighbors++;
            LOG_INFO("Added Q-table from new node %u (samples=%u, total_neighbors=%u)\n", 
                     node_id, num_samples, fed_state.num_active_neighbors);
            return 1;
        }
    }
    
    LOG_WARN("No space to store Q-table from node %u (table full)\n", node_id);
    return 0;
}

/**
 * Aggregate Q-tables using FedAvg (simple averaging)
 */
uint8_t federated_aggregate_fedavg(void) {
    if (fed_state.num_active_neighbors == 0) {
        LOG_INFO("No neighbors to aggregate with\n");
        return 0;
    }
    
    float *local_q_table = get_q_table();
    
    // Temporary array for aggregation
    float temp_aggregated[Q_VALUE_LIST_SIZE];
    
    // Initialize with zeros
    for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
        temp_aggregated[j] = 0.0;
    }
    
    // Sum all Q-values (local + neighbors)
    uint8_t count = 0;
    
    // Add local Q-values
    for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
        temp_aggregated[j] += local_q_table[j];
    }
    count++;
    
    // Add neighbor Q-values
    for (int i = 0; i < MAX_FEDERATED_NEIGHBORS; i++) {
        if (fed_state.neighbors[i].is_active) {
            for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
                temp_aggregated[j] += fed_state.neighbors[i].q_values[j];
            }
            count++;
        }
    }
    
    // Average and update local Q-table with aggregation weight
    for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
        float avg = temp_aggregated[j] / count;
        local_q_table[j] = fed_state.aggregation_weight * local_q_table[j] + 
                           (1.0 - fed_state.aggregation_weight) * avg;
    }
    
    LOG_INFO("FedAvg: aggregated %u neighbors\n", count - 1);
    return count - 1;
}

/**
 * Aggregate Q-tables using weighted average based on number of samples
 */
uint8_t federated_aggregate_weighted(void) {
    if (fed_state.num_active_neighbors == 0) {
        LOG_INFO("No neighbors to aggregate with\n");
        return 0;
    }
    
    float *local_q_table = get_q_table();
    float temp_weighted[Q_VALUE_LIST_SIZE];
    
    // Initialize with zeros
    for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
        temp_weighted[j] = 0.0;
    }
    
    // Calculate total samples (for weight normalization)
    uint32_t total_samples = fed_state.local_num_samples;
    for (int i = 0; i < MAX_FEDERATED_NEIGHBORS; i++) {
        if (fed_state.neighbors[i].is_active) {
            total_samples += fed_state.neighbors[i].num_samples;
        }
    }
    
    if (total_samples == 0) {
        LOG_WARN("Total samples is 0, cannot weight\n");
        return federated_aggregate_fedavg();  // Fallback to simple average
    }
    
    // Weighted sum: local Q-table
    float local_weight = (float)fed_state.local_num_samples / total_samples;
    for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
        temp_weighted[j] += local_weight * local_q_table[j];
    }
    
    // Weighted sum: neighbor Q-tables
    uint8_t neighbor_count = 0;
    for (int i = 0; i < MAX_FEDERATED_NEIGHBORS; i++) {
        if (fed_state.neighbors[i].is_active) {
            float weight = (float)fed_state.neighbors[i].num_samples / total_samples;
            for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
                temp_weighted[j] += weight * fed_state.neighbors[i].q_values[j];
            }
            neighbor_count++;
        }
    }
    
    // Update local Q-table with weighted aggregation
    for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
        local_q_table[j] = temp_weighted[j];
    }
    
    LOG_INFO("Weighted FedAvg: local_weight=%.2f, neighbors=%u\n", 
             (double)local_weight, neighbor_count);
    return neighbor_count;
}

/**
 * Aggregate Q-tables using median (robust to outliers)
 */
uint8_t federated_aggregate_median(void) {
    if (fed_state.num_active_neighbors == 0) {
        LOG_INFO("No neighbors to aggregate with\n");
        return 0;
    }
    
    float *local_q_table = get_q_table();
    float temp_median[Q_VALUE_LIST_SIZE];
    
    // For each Q-value position, collect values from all nodes and take median
    for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
        float values[MAX_FEDERATED_NEIGHBORS + 1];
        values[0] = local_q_table[j];
        uint8_t count = 1;
        
        for (int i = 0; i < MAX_FEDERATED_NEIGHBORS; i++) {
            if (fed_state.neighbors[i].is_active) {
                values[count++] = fed_state.neighbors[i].q_values[j];
            }
        }
        
        temp_median[j] = calculate_median(values, count);
    }
    
    // Update local Q-table with median values and apply aggregation weight
    for (int j = 0; j < Q_VALUE_LIST_SIZE; j++) {
        local_q_table[j] = fed_state.aggregation_weight * local_q_table[j] + 
                           (1.0 - fed_state.aggregation_weight) * temp_median[j];
    }
    
    LOG_INFO("FedMedian: aggregated %u neighbors\n", fed_state.num_active_neighbors);
    return fed_state.num_active_neighbors;
}

/**
 * Main federated aggregation function
 */
uint8_t federated_aggregate(void) {
    #if !ENABLE_FEDERATED_LEARNING
    return 0;
    #endif
    
    switch (fed_state.aggregation_method) {
        case FEDAVG:
            return federated_aggregate_fedavg();
        case WEIGHTED_FEDAVG:
            return federated_aggregate_weighted();
        case FEDMEDIAN:
            return federated_aggregate_median();
        default:
            LOG_WARN("Unknown aggregation method %u\n", fed_state.aggregation_method);
            return federated_aggregate_fedavg();
    }
}

/**
 * Get local Q-table for sharing
 */
float* get_local_q_table_for_sharing(void) {
    return get_q_table();
}

/**
 * Increment local sample count
 */
void increment_local_samples(void) {
    if (fed_state.local_num_samples < 255) {
        fed_state.local_num_samples++;
    }
}

/**
 * Get local sample count
 */
uint8_t get_local_sample_count(void) {
    return fed_state.local_num_samples;
}

/**
 * Clean up stale neighbor entries
 */
void cleanup_stale_neighbors(uint32_t timeout_seconds) {
    uint32_t current_time = clock_seconds();
    uint8_t removed = 0;
    
    for (int i = 0; i < MAX_FEDERATED_NEIGHBORS; i++) {
        if (fed_state.neighbors[i].is_active) {
            if (current_time - fed_state.neighbors[i].last_update_time > timeout_seconds) {
                LOG_INFO("Removing stale neighbor node %u\n", 
                         fed_state.neighbors[i].node_id);
                fed_state.neighbors[i].is_active = 0;
                fed_state.num_active_neighbors--;
                removed++;
            }
        }
    }
    
    if (removed > 0) {
        LOG_INFO("Cleaned up %u stale neighbors\n", removed);
    }
}

/**
 * Get federated learning statistics
 */
void get_federated_stats(uint8_t *num_neighbors, uint8_t *local_samples, 
                         fed_aggregation_method_t *method) {
    if (num_neighbors) *num_neighbors = fed_state.num_active_neighbors;
    if (local_samples) *local_samples = fed_state.local_num_samples;
    if (method) *method = fed_state.aggregation_method;
}

/**
 * Set aggregation method
 */
void set_aggregation_method(fed_aggregation_method_t method) {
    fed_state.aggregation_method = method;
    LOG_INFO("Aggregation method changed to %u\n", method);
}

/**
 * Set local model weight
 */
void set_local_model_weight(float weight) {
    if (weight < 0.0) weight = 0.0;
    if (weight > 1.0) weight = 1.0;
    fed_state.aggregation_weight = weight;
    LOG_INFO("Local model weight set to %.2f\n", (double)weight);
}