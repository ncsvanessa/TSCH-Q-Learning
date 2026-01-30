#ifndef SLOT_CONFIGURATION_HEADER
#define SLOT_CONFIGURATION_HEADER

/********** Libraries **********/
#include "contiki.h"
#include "net/mac/tsch/tsch.h"

/******** Configuration *******/
// Maximum slots to track
#ifndef MAX_TRACKED_SLOTS
#define MAX_TRACKED_SLOTS 101
#endif

// Minimum usage threshold to keep slot active (%)
#ifndef SLOT_USAGE_THRESHOLD
#define SLOT_USAGE_THRESHOLD 3  // Deactivate slots used less than 3% of the time
#endif

// Threshold to convert shared slot to dedicated (successful tx count)
#ifndef DEDICATED_THRESHOLD
#define DEDICATED_THRESHOLD 5  // Convert to dedicated after 5+ successful transmissions
#endif

// Number of learning cycles before reconfiguring slots
#ifndef SLOT_RECONFIG_INTERVAL
#define SLOT_RECONFIG_INTERVAL 3  // Reconfigure every 3 Q-learning cycles
#endif

/******** Slot Configuration Types *******/
typedef enum {
    SLOT_CONFIG_INACTIVE,      // Slot is disabled/not used
    SLOT_CONFIG_SHARED,        // Slot is shared (TX+RX, broadcast)
    SLOT_CONFIG_DEDICATED_TX,  // Dedicated transmit slot (unicast)
    SLOT_CONFIG_DEDICATED_RX,  // Dedicated receive slot
    SLOT_CONFIG_ADVERTISING    // Advertising slot (always slot 0)
} slot_config_type_t;

/******** Slot Statistics Structure *******/
typedef struct {
    uint16_t successful_tx;       // Successful transmissions in this slot
    uint16_t successful_rx;       // Successful receptions in this slot
    uint16_t collisions;          // Detected collisions
    uint16_t total_attempts;      // Total transmission attempts
    uint16_t retransmissions;     // Number of retransmissions
    uint8_t current_config;       // Current configuration (slot_config_type_t)
    uint8_t channel_offset;       // Current channel offset
    linkaddr_t primary_neighbor;  // Primary neighbor for this slot (if dedicated)
    float slot_reward;            // Computed reward for this slot
    uint8_t usage_count;          // Number of times slot was used (for percentages)
} slot_statistics_t;

/******** Global Slot Management *******/
typedef struct {
    slot_statistics_t slots[MAX_TRACKED_SLOTS];  // Statistics per slot
    uint8_t num_active_slots;                     // Number of active slots
    uint8_t num_dedicated_slots;                  // Number of dedicated slots
    uint8_t num_shared_slots;                     // Number of shared slots
    uint8_t learning_cycle_count;                 // Cycle counter for reconfiguration
    uint8_t slotframe_size;                       // Current slotframe size
} slot_manager_t;

/********** Functions *********/

/**
 * Initialize slot configuration manager
 */
void slot_config_init(uint8_t initial_slotframe_size);

/**
 * Record a successful transmission in a slot
 */
void slot_record_tx(uint8_t slot_id, linkaddr_t *dest, uint8_t retrans_count);

/**
 * Record a successful reception in a slot
 */
void slot_record_rx(uint8_t slot_id, linkaddr_t *src);

/**
 * Record a collision in a slot
 */
void slot_record_collision(uint8_t slot_id);

/**
 * Analyze slot statistics and compute rewards per slot
 * Returns average slot reward
 */
float analyze_slot_performance(void);

/**
 * Reconfigure slots based on learned statistics
 * - Deactivates underutilized slots
 * - Converts high-traffic shared slots to dedicated
 * - Optimizes channel offsets to reduce interference
 * 
 * Should be called periodically (e.g., every N Q-learning cycles)
 */
void reconfigure_slots_adaptive(struct tsch_slotframe *sf, struct tsch_link **links);

/**
 * Reset slot statistics for new learning cycle
 */
void reset_slot_statistics(void);

/**
 * Get slot configuration recommendation for a specific slot
 * Returns recommended configuration type
 */
slot_config_type_t get_slot_recommendation(uint8_t slot_id);

/**
 * Get statistics for a specific slot
 */
slot_statistics_t* get_slot_statistics(uint8_t slot_id);

/**
 * Get overall slot manager state
 */
slot_manager_t* get_slot_manager(void);

/**
 * Update slotframe size (called when Q-learning resizes)
 */
void update_slotframe_size(uint8_t new_size);

/**
 * Print slot configuration summary (for debugging)
 */
void print_slot_summary(void);

/**
 * Compute slot-level reward for Q-learning integration
 * Returns bonus/penalty based on slot configuration efficiency
 */
float compute_slot_efficiency_reward(void);

/**
 * Identify best channel offset for a slot based on collision history
 * Returns recommended channel offset (0-15)
 */
uint8_t recommend_channel_offset(uint8_t slot_id);

/**
 * Check if enough data collected to reconfigure
 */
uint8_t should_reconfigure_slots(void);

#endif /* SLOT_CONFIGURATION_HEADER */
