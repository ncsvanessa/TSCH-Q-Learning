/********** Libraries ***********/
#include "net/mac/tsch/tsch.h"
#include "q-learning.h"
#include "lib/random.h"
#include <stdlib.h>
#include <string.h>

/********** global variables ***********/
// parameters to calculate the reward (TSCH-based)
float theta1 = 3.0;           // weight for successful transmissions
float theta2 = 1.5;           // weight for buffer management
float theta3 = 0.5;           // weight for conflicts
float conflict_penalty = 100.0; // penalty per conflict detected

// Q-value updating paramaters
float learning_rate = 0.1;
float discount_factor = 0.9;

// default state varibale
env_state *current_state;

// Q-table to store q-values, 2 index means -> action is 3, first three slots are active
float q_list[Q_VALUE_LIST_SIZE];

// Structure to track link allocations (for conflict detection)
typedef struct {
    uint8_t src;
    uint8_t dst;
    uint8_t count;
} link_allocation_t;

/********** TSCH Reward Functions *********/

/**
 * Compute reward for TSCH slotframe schedule based on throughput and conflicts
 * Adapted from Python notebook implementation
 * 
 * Parameters:
 * - n_tx: number of successful transmissions
 * - n_rx: number of successful receptions
 * - n_buff_prev: buffer size before scheduling period
 * - n_buff_new: buffer size after scheduling period
 * - n_conflicts: number of detected conflicts in the schedule
 * 
 * Returns: reward value (throughput - conflict penalties - buffer penalties)
 */
float tsch_reward_function(uint8_t n_tx, uint8_t n_rx, uint8_t n_buff_prev, 
                          uint8_t n_buff_new, uint8_t n_conflicts) {
    float throughput = theta1 * (n_tx + n_rx);
    float buffer_penalty = theta2 * (n_buff_prev > n_buff_new ? n_buff_prev - n_buff_new : 0);
    float conflict_cost = conflict_penalty * n_conflicts;
    
    return throughput - buffer_penalty - conflict_cost;
}

/**
 * Legacy reward function (kept for backward compatibility)
 */
float reward(uint8_t n_tx, uint8_t n_rx, uint8_t n_buff_prev, uint8_t n_buff_new) {
    return (theta1 * (n_tx + n_rx) - theta2 * (n_buff_prev - n_buff_new));
}

// Function to find the action with highest q-value, returns the index of max value
uint8_t get_highest_q_val(void) {
    int max_val_index = 0;
    for (int i = 1; i < Q_VALUE_LIST_SIZE; i++) {
        if (q_list[i] > q_list[max_val_index]) {
            max_val_index = i;
        }
    }
    return max_val_index;
}

// Function to get the current state (buffer_size and energy_level)
env_state *get_current_state(void) {
    current_state->buffer_size = 0;
    current_state->energy_level = 0;
    return current_state;
}

// Updating the q-value table with improved formula
void update_q_table(uint8_t action, float got_reward) {
    q_list[action] = (1 - learning_rate) * q_list[action] + 
                     learning_rate * (got_reward + discount_factor * q_list[get_highest_q_val()]);
}

// function to return the main q-list
float * get_q_table(void) {
    return q_list;
}

// generating random q-values
void generate_random_q_values(void) {
    for (int i = 0; i < Q_VALUE_LIST_SIZE; i++) {
        q_list[i] = (float) random_rand() / RANDOM_RAND_MAX;
    }
}