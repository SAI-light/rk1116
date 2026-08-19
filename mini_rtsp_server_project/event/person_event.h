/********************************************************************************
 * Filename: person_event.h
 * Description:
 *   Convert per-AI-frame PERSON detection results into stable ENTER/PRESENT/
 *   LEAVE events using N-of-M entry confirmation and consecutive-miss exit
 *   confirmation.
 ********************************************************************************/

#ifndef PERSON_EVENT_H
#define PERSON_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PERSON_EVENT_MAX_ENTER_WINDOW 32U

typedef enum
{
    PERSON_EVENT_NONE = 0,
    PERSON_EVENT_ENTER,
    PERSON_EVENT_PRESENT,
    PERSON_EVENT_LEAVE
} PersonEventType;

typedef enum
{
    PERSON_STATE_IDLE = 0,
    PERSON_STATE_CANDIDATE,
    PERSON_STATE_PRESENT,
    PERSON_STATE_EXIT_PENDING
} PersonEventState;

typedef struct
{
    uint32_t enter_window;
    uint32_t enter_required;
    uint32_t leave_count;
} PersonEventConfig;

typedef struct
{
    uint32_t frame_id;
    int64_t timestamp_ms;
    uint32_t person_count;
    uint8_t max_score;
} PersonEventInput;

typedef struct
{
    PersonEventType event;
    PersonEventState state;
    bool person_present;
    uint64_t event_id;

    uint32_t frame_id;
    int64_t timestamp_ms;
    uint32_t person_count;
    uint8_t max_score;

    uint32_t enter_hits;
    uint32_t enter_samples;
    uint32_t consecutive_misses;
} PersonEventResult;

typedef struct
{
    PersonEventConfig config;
    uint8_t enter_history[PERSON_EVENT_MAX_ENTER_WINDOW];
    uint32_t history_pos;
    uint32_t history_count;
    uint32_t history_hits;
    bool active;
    uint32_t consecutive_misses;
    uint64_t event_id;
} PersonEventTracker;

void person_event_default_config(PersonEventConfig *config);
int person_event_init(PersonEventTracker *tracker,
                      const PersonEventConfig *config);
void person_event_reset(PersonEventTracker *tracker);
int person_event_update(PersonEventTracker *tracker,
                        const PersonEventInput *input,
                        PersonEventResult *result);
const char *person_event_type_string(PersonEventType event);
const char *person_event_state_string(PersonEventState state);

#ifdef __cplusplus
}
#endif

#endif /* PERSON_EVENT_H */
