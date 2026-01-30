/********** Libraries ***********/
#include "slot-configuration.h"
#include "q-learning.h"
#include "net/linkaddr.h"
#include <string.h>
#include <stdlib.h>

#include "sys/log.h"
#define LOG_MODULE "SlotConfig"
#define LOG_LEVEL LOG_LEVEL_INFO

/********** Global Variables ***********/
static slot_manager_t slot_manager;

// Channel offset diversity to reduce interference
static const uint8_t channel_offsets[] __attribute__((unused)) = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
#define NUM_CHANNEL_OFFSETS 16

/********** Private Helper Functions ***********/

/**
 * Compare function for sorting slots by usage
 * Reserved for future use (advanced slot prioritization)
 */
static int compare_slot_usage(const void *a, const void *b) __attribute__((unused));
static int compare_slot_usage(const void *a, const void *b) {
    slot_statistics_t *slot_a = (slot_statistics_t *)a;
    slot_statistics_t *slot_b = (slot_statistics_t *)b;
    return (slot_b->usage_count - slot_a->usage_count);
}

/**
 * Calculate slot utilization percentage
 */
static float calculate_slot_utilization(slot_statistics_t *slot) {
    if (slot->total_attempts == 0) {
        return 0.0;
    }
    return (float)(slot->successful_tx + slot->successful_rx) / slot->total_attempts * 100.0;
}

/**
 * Calculate collision rate for a slot
 */
static float calculate_collision_rate(slot_statistics_t *slot) {
    if (slot->total_attempts == 0) {
        return 0.0;
    }
    return (float)slot->collisions / slot->total_attempts * 100.0;
}

/********** Public Functions ***********/

/**
 * Initialize slot configuration manager
 */
void slot_config_init(uint8_t initial_slotframe_size) {
    memset(&slot_manager, 0, sizeof(slot_manager_t));
    
    slot_manager.slotframe_size = initial_slotframe_size;
    slot_manager.num_active_slots = initial_slotframe_size;
    slot_manager.num_shared_slots = initial_slotframe_size - 1;  // All except slot 0
    slot_manager.num_dedicated_slots = 0;
    slot_manager.learning_cycle_count = 0;
    
    // Initialize slot 0 as advertising
    slot_manager.slots[0].current_config = SLOT_CONFIG_ADVERTISING;
    slot_manager.slots[0].channel_offset = 0;
    
    // Initialize other slots as shared
    for (int i = 1; i < initial_slotframe_size; i++) {
        slot_manager.slots[i].current_config = SLOT_CONFIG_SHARED;
        slot_manager.slots[i].channel_offset = 0;
        linkaddr_copy(&slot_manager.slots[i].primary_neighbor, &linkaddr_null);
    }
    
    LOG_INFO("Slot configuration manager initialized: size=%u\n", initial_slotframe_size);
}

/**
 * Record a successful transmission in a slot
 */
void slot_record_tx(uint8_t slot_id, linkaddr_t *dest, uint8_t retrans_count) {
    if (slot_id >= MAX_TRACKED_SLOTS) return;
    
    slot_statistics_t *slot = &slot_manager.slots[slot_id];
    slot->successful_tx++;
    slot->total_attempts++;
    slot->usage_count++;
    slot->retransmissions += retrans_count;
    
    // Track primary neighbor for potential dedicated slot
    if (dest != NULL && !linkaddr_cmp(dest, &linkaddr_null)) {
        if (linkaddr_cmp(&slot->primary_neighbor, &linkaddr_null)) {
            linkaddr_copy(&slot->primary_neighbor, dest);
        }
    }
}

/**
 * Record a successful reception in a slot
 */
void slot_record_rx(uint8_t slot_id, linkaddr_t *src) {
    if (slot_id >= MAX_TRACKED_SLOTS) return;
    
    slot_statistics_t *slot = &slot_manager.slots[slot_id];
    slot->successful_rx++;
    slot->total_attempts++;
    slot->usage_count++;
    
    // Track primary neighbor
    if (src != NULL && !linkaddr_cmp(src, &linkaddr_null)) {
        if (linkaddr_cmp(&slot->primary_neighbor, &linkaddr_null)) {
            linkaddr_copy(&slot->primary_neighbor, src);
        }
    }
}

/**
 * Record a collision in a slot
 */
void slot_record_collision(uint8_t slot_id) {
    if (slot_id >= MAX_TRACKED_SLOTS) return;
    
    slot_statistics_t *slot = &slot_manager.slots[slot_id];
    slot->collisions++;
    slot->total_attempts++;
}

/**
 * Analyze slot statistics and compute rewards per slot
 */
float analyze_slot_performance(void) {
    float total_reward = 0.0;
    uint8_t active_slots = 0;
    
    for (int i = 0; i < slot_manager.slotframe_size; i++) {
        slot_statistics_t *slot = &slot_manager.slots[i];
        
        if (slot->usage_count > 0 || slot->current_config != SLOT_CONFIG_INACTIVE) {
            // Compute slot reward: throughput - collisions - retransmissions
            float throughput = (float)(slot->successful_tx + slot->successful_rx);
            float collision_penalty = (float)slot->collisions * 2.0;
            float retrans_penalty = (float)slot->retransmissions * 0.5;
            
            slot->slot_reward = throughput - collision_penalty - retrans_penalty;
            total_reward += slot->slot_reward;
            active_slots++;
        }
    }
    
    return active_slots > 0 ? total_reward / active_slots : 0.0;
}

/**
 * Reconfigure slots based on learned statistics
 */
void reconfigure_slots_adaptive(struct tsch_slotframe *sf, struct tsch_link **links) {
    if (sf == NULL || links == NULL) {
        LOG_WARN("Cannot reconfigure: invalid parameters\n");
        return;
    }
    
    LOG_INFO("============ Slot Reconfiguration Start ============\n");
    
    uint8_t slots_deactivated = 0;
    uint8_t slots_converted_dedicated = 0;
    uint8_t channels_optimized = 0;
    
    // Analyze each slot
    for (int i = 1; i < slot_manager.slotframe_size; i++) {  // Skip slot 0 (advertising)
        slot_statistics_t *slot = &slot_manager.slots[i];
        
        if (links[i] == NULL) continue;
        
        // Calculate utilization
        float utilization = calculate_slot_utilization(slot);
        float collision_rate = calculate_collision_rate(slot);
        
        // Decision 1: Deactivate underutilized slots
        if (slot->usage_count < SLOT_USAGE_THRESHOLD && 
            slot->current_config != SLOT_CONFIG_INACTIVE) {
            
            LOG_INFO("Slot %u: deactivating (usage=%u, util=%.1f%%)\n", 
                     i, slot->usage_count, (double)utilization);
            
            // Remove link
            tsch_schedule_remove_link(sf, links[i]);
            links[i] = NULL;
            slot->current_config = SLOT_CONFIG_INACTIVE;
            slot_manager.num_active_slots--;
            slots_deactivated++;
            continue;
        }
        
        // Decision 2: Convert high-traffic shared slots to dedicated
        if (slot->successful_tx >= DEDICATED_THRESHOLD && 
            slot->current_config == SLOT_CONFIG_SHARED &&
            !linkaddr_cmp(&slot->primary_neighbor, &linkaddr_null) &&
            !linkaddr_cmp(&slot->primary_neighbor, &tsch_broadcast_address)) {
            
            LOG_INFO("Slot %u: converting to dedicated TX (tx=%u, neighbor=%02x:%02x)\n", 
                     i, slot->successful_tx,
                     slot->primary_neighbor.u8[0], slot->primary_neighbor.u8[1]);
            
            // Remove old link
            tsch_schedule_remove_link(sf, links[i]);
            
            // Add dedicated link
            links[i] = tsch_schedule_add_link(sf,
                                              LINK_OPTION_TX,  // TX only
                                              LINK_TYPE_NORMAL,
                                              &slot->primary_neighbor,
                                              i, 
                                              slot->channel_offset,
                                              1);
            
            slot->current_config = SLOT_CONFIG_DEDICATED_TX;
            slot_manager.num_dedicated_slots++;
            slot_manager.num_shared_slots--;
            slots_converted_dedicated++;
            continue;
        }
        
        // Decision 3: Optimize channel offset for high-collision slots
        if (collision_rate > 20.0 && slot->collisions > 5) {
            uint8_t new_channel = recommend_channel_offset(i);
            
            if (new_channel != slot->channel_offset) {
                LOG_INFO("Slot %u: changing channel offset %u->%u (collisions=%u, rate=%.1f%%)\n",
                         i, slot->channel_offset, new_channel, 
                         slot->collisions, (double)collision_rate);
                
                // Remove old link
                uint8_t options = links[i]->link_options;
                uint8_t type = links[i]->link_type;
                linkaddr_t addr;
                linkaddr_copy(&addr, &links[i]->addr);
                
                tsch_schedule_remove_link(sf, links[i]);
                
                // Add link with new channel
                links[i] = tsch_schedule_add_link(sf, options, type, &addr, i, new_channel, 1);
                slot->channel_offset = new_channel;
                channels_optimized++;
            }
        }
    }
    
    LOG_INFO("Reconfiguration complete: deactivated=%u, dedicated=%u, channels=%u\n",
             slots_deactivated, slots_converted_dedicated, channels_optimized);
    LOG_INFO("Active slots: %u (dedicated=%u, shared=%u)\n",
             slot_manager.num_active_slots, slot_manager.num_dedicated_slots, 
             slot_manager.num_shared_slots);
    LOG_INFO("============ Slot Reconfiguration End ============\n");
}

/**
 * Reset slot statistics for new learning cycle
 * IMPORTANT: This should ONLY be called at the END of each Q-learning cycle,
 * NOT when resizing slotframe. This preserves learning across size changes.
 */
void reset_slot_statistics(void) {
    for (int i = 0; i < slot_manager.slotframe_size; i++) {
        slot_statistics_t *slot = &slot_manager.slots[i];
        
        // Keep configuration and primary neighbor, reset counters
        slot->successful_tx = 0;
        slot->successful_rx = 0;
        slot->collisions = 0;
        slot->total_attempts = 0;
        slot->retransmissions = 0;
        slot->usage_count = 0;
        slot->slot_reward = 0.0;
    }
    
    slot_manager.learning_cycle_count++;
    LOG_INFO("Slot statistics reset for cycle %u (configuration preserved)\n", 
             slot_manager.learning_cycle_count);
}

/**
 * Get slot configuration recommendation
 */
slot_config_type_t get_slot_recommendation(uint8_t slot_id) {
    if (slot_id >= MAX_TRACKED_SLOTS || slot_id >= slot_manager.slotframe_size) {
        return SLOT_CONFIG_INACTIVE;
    }
    
    return (slot_config_type_t)slot_manager.slots[slot_id].current_config;
}

/**
 * Get statistics for a specific slot
 */
slot_statistics_t* get_slot_statistics(uint8_t slot_id) {
    if (slot_id >= MAX_TRACKED_SLOTS) {
        return NULL;
    }
    return &slot_manager.slots[slot_id];
}

/**
 * Get overall slot manager state
 */
slot_manager_t* get_slot_manager(void) {
    return &slot_manager;
}

/**
 * Update slotframe size
 * IMPORTANT: Preserves statistics of slots that remain in the new size
 * Only resets statistics at end of Q-learning cycle via reset_slot_statistics()
 */
void update_slotframe_size(uint8_t new_size) {
    if (new_size > MAX_TRACKED_SLOTS) {
        new_size = MAX_TRACKED_SLOTS;
    }
    
    uint8_t old_size = slot_manager.slotframe_size;
    
    if (new_size == old_size) {
        return;  // No change needed
    }
    
    LOG_INFO("Updating slotframe size: %u -> %u (preserving slot statistics)\n", 
             old_size, new_size);
    
    slot_manager.slotframe_size = new_size;
    
    // Initialize new slots if expanded
    if (new_size > old_size) {
        LOG_INFO("Expanding: initializing slots %u to %u\n", old_size, new_size - 1);
        
        for (int i = old_size; i < new_size; i++) {
            // Only reset counters, preserve history for slots 0 to old_size-1
            slot_manager.slots[i].successful_tx = 0;
            slot_manager.slots[i].successful_rx = 0;
            slot_manager.slots[i].collisions = 0;
            slot_manager.slots[i].total_attempts = 0;
            slot_manager.slots[i].retransmissions = 0;
            slot_manager.slots[i].usage_count = 0;
            slot_manager.slots[i].slot_reward = 0.0;
            slot_manager.slots[i].current_config = SLOT_CONFIG_SHARED;
            slot_manager.slots[i].channel_offset = 0;
            linkaddr_copy(&slot_manager.slots[i].primary_neighbor, &linkaddr_null);
        }
        slot_manager.num_active_slots += (new_size - old_size);
        slot_manager.num_shared_slots += (new_size - old_size);
        
        LOG_INFO("Slots 0-%u: PRESERVED (statistics maintained)\n", old_size - 1);
        LOG_INFO("Slots %u-%u: INITIALIZED (new slots)\n", old_size, new_size - 1);
    }
    // Mark excess slots as inactive if shrunk (but DON'T reset slots 0 to new_size-1)
    else if (new_size < old_size) {
        LOG_INFO("Shrinking: deactivating slots %u to %u\n", new_size, old_size - 1);
        
        for (int i = new_size; i < old_size; i++) {
            if (slot_manager.slots[i].current_config != SLOT_CONFIG_INACTIVE) {
                slot_manager.num_active_slots--;
                if (slot_manager.slots[i].current_config == SLOT_CONFIG_DEDICATED_TX ||
                    slot_manager.slots[i].current_config == SLOT_CONFIG_DEDICATED_RX) {
                    slot_manager.num_dedicated_slots--;
                } else {
                    slot_manager.num_shared_slots--;
                }
            }
            slot_manager.slots[i].current_config = SLOT_CONFIG_INACTIVE;
            // Keep statistics for potential future re-expansion
        }
        
        LOG_INFO("Slots 0-%u: PRESERVED (statistics maintained)\n", new_size - 1);
        LOG_INFO("Slots %u-%u: DEACTIVATED (statistics kept for future)\n", new_size, old_size - 1);
    }
}

/**
 * Print slot configuration summary
 */
void print_slot_summary(void) {
    LOG_INFO("========== Slot Summary ==========\n");
    LOG_INFO("Slotframe size: %u\n", slot_manager.slotframe_size);
    LOG_INFO("Active: %u | Dedicated: %u | Shared: %u\n",
             slot_manager.num_active_slots, slot_manager.num_dedicated_slots,
             slot_manager.num_shared_slots);
    
    // Show top 5 most used slots
    LOG_INFO("Top utilized slots:\n");
    for (int i = 1; i < slot_manager.slotframe_size && i < 6; i++) {
        slot_statistics_t *slot = &slot_manager.slots[i];
        if (slot->usage_count > 0) {
            LOG_INFO("  Slot %u: tx=%u rx=%u coll=%u ch=%u\n",
                     i, slot->successful_tx, slot->successful_rx,
                     slot->collisions, slot->channel_offset);
        }
    }
    LOG_INFO("==================================\n");
}

/**
 * Compute slot-level reward bonus/penalty
 */
float compute_slot_efficiency_reward(void) {
    float efficiency_bonus = 0.0;
    
    // Bonus for having dedicated slots (more efficient)
    efficiency_bonus += slot_manager.num_dedicated_slots * 2.0;
    
    // Penalty for too many inactive slots (wasted space)
    uint8_t inactive_slots = slot_manager.slotframe_size - slot_manager.num_active_slots;
    if (inactive_slots > slot_manager.slotframe_size / 3) {
        efficiency_bonus -= inactive_slots * 0.5;
    }
    
    // Bonus for low overall collision rate
    uint16_t total_collisions = 0;
    uint16_t total_attempts = 0;
    for (int i = 0; i < slot_manager.slotframe_size; i++) {
        total_collisions += slot_manager.slots[i].collisions;
        total_attempts += slot_manager.slots[i].total_attempts;
    }
    
    if (total_attempts > 0) {
        float collision_rate = (float)total_collisions / total_attempts;
        if (collision_rate < 0.1) {  // Less than 10% collision rate
            efficiency_bonus += 5.0;
        } else if (collision_rate > 0.3) {  // More than 30% collision rate
            efficiency_bonus -= 5.0;
        }
    }
    
    return efficiency_bonus;
}

/**
 * Recommend channel offset based on collision history
 */
uint8_t recommend_channel_offset(uint8_t slot_id) {
    if (slot_id >= MAX_TRACKED_SLOTS) return 0;
    
    // Simple strategy: try different channel based on slot position
    // More sophisticated: track per-channel collision rates
    uint8_t current = slot_manager.slots[slot_id].channel_offset;
    
    // Rotate through channels if collisions detected
    uint8_t new_offset = (current + 1) % NUM_CHANNEL_OFFSETS;
    
    return new_offset;
}

/**
 * Check if should reconfigure slots
 */
uint8_t should_reconfigure_slots(void) {
    return (slot_manager.learning_cycle_count % SLOT_RECONFIG_INTERVAL) == 0 &&
           slot_manager.learning_cycle_count > 0;
}
