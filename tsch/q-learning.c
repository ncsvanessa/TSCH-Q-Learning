/********** Libraries ***********/
#include "contiki.h"
#include "net/mac/tsch/tsch.h"
#include "q-learning.h"
#include "lib/random.h"
#include <stdlib.h>
#include <string.h>

/********** global variables ***********/
// parameters to calculate the reward (TSCH-based)
float theta1 = 3.0;           // weight for successful transmissions
float theta2 = 0.5;           // weight for buffer management
float theta3 = 2.0;           // weight for retransmission penalty
float theta4 = 0.5;           // weight for conflicts
float conflict_penalty = 100.0; // penalty per conflict detected

// Maximum buffer difference
#define MAX_BUFFER_PENALTY 20

// Q-value updating paramaters
float learning_rate = 0.1;
float discount_factor = 0.9;

// default state varibale
env_state *current_state;

// Q-table to store q-values, 2 index means -> action is 3, first three slots are active
float q_list[Q_VALUE_LIST_SIZE];

// Structure to track link allocations
typedef struct {
    uint8_t src;
    uint8_t dst;
    uint8_t count;
} link_allocation_t;

/********** TSCH Reward Functions *********/

/**
 * Compute reward for TSCH slotframe schedule based on throughput and retransmissions
 * Adapted from Python notebook implementation with retransmission penalty
 * 
 * Parameters:
 * - n_tx: number of successful transmissions
 * - n_rx: number of successful receptions
 * - n_buff_prev: buffer size before scheduling period
 * - n_buff_new: buffer size after scheduling period
 * - avg_retrans: average number of retransmissions per packet (1.0 = no retrans)
 * 
 * Returns: reward value (throughput - buffer penalties - retransmission cost)
 */
float tsch_reward_function(uint8_t n_tx, uint8_t n_rx, uint8_t n_buff_prev, 
                          uint8_t n_buff_new, float avg_retrans) {
    float throughput = theta1 * (n_tx + n_rx);
    
    // Calculate buffer difference
    int buffer_diff = (int)n_buff_prev - (int)n_buff_new;
    if (buffer_diff < 0) buffer_diff = 0;  // no penalty if buffer increased
    if (buffer_diff > MAX_BUFFER_PENALTY) buffer_diff = MAX_BUFFER_PENALTY;  // cap penalty
    
    float buffer_penalty = theta2 * buffer_diff;
    
    float retrans_penalty = 0.0;
    if (avg_retrans > 1.0) {
        retrans_penalty = theta3 * (avg_retrans - 1.0);
    }
    
    return throughput - buffer_penalty - retrans_penalty;
}

/**
 * Legacy reward function
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

/**
 * Epsilon-greedy action selection strategy
 * Balances exploration (random actions) and exploitation (best known action)
 * 
 * Parameters:
 * - epsilon: probability of random exploration (0.0 to 1.0)
 *   - epsilon = 0.0: pure exploitation (always choose best action)
 *   - epsilon = 1.0: pure exploration (always random)
 *   - typical: 0.1 to 0.3 for good balance
 * 
 * Returns: selected action index
 */
uint8_t get_action_epsilon_greedy(float epsilon) {
    // Generate random number between 0 and 1
    float random_val = (float)random_rand() / RANDOM_RAND_MAX;
    
    if (random_val < epsilon) {
        // Exploration: choose random action
        return random_rand() % Q_VALUE_LIST_SIZE;
    } else {
        // Exploitation: choose best known action
        return get_highest_q_val();
    }
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