/********************************************************************************
 * Filename: person_event.c
 * Description: PERSON event state machine implementation.
 ********************************************************************************/

#include "person_event.h"

#include <stddef.h>
#include <string.h>

static int config_is_valid(const PersonEventConfig *config)
{
    if (config == NULL)
        return 0;

    if (config->enter_window == 0U ||
        config->enter_window > PERSON_EVENT_MAX_ENTER_WINDOW)
        return 0;

    if (config->enter_required == 0U ||
        config->enter_required > config->enter_window)
        return 0;

    if (config->leave_count == 0U)
        return 0;

    return 1;
}

static void clear_entry_history(PersonEventTracker *tracker)
{
    memset(tracker->enter_history, 0, sizeof(tracker->enter_history));
    tracker->history_pos = 0U;
    tracker->history_count = 0U;
    tracker->history_hits = 0U;
}

static void add_entry_sample(PersonEventTracker *tracker, int detected)
{
    const uint32_t window = tracker->config.enter_window;
    const uint8_t value = detected ? 1U : 0U;

    if (tracker->history_count < window)
    {
        tracker->enter_history[tracker->history_pos] = value;
        tracker->history_hits += value;
        tracker->history_count++;
    }
    else
    {
        const uint8_t old_value =
            tracker->enter_history[tracker->history_pos];

        tracker->history_hits -= old_value;
        tracker->enter_history[tracker->history_pos] = value;
        tracker->history_hits += value;
    }

    tracker->history_pos++;
    if (tracker->history_pos >= window)
        tracker->history_pos = 0U;
}

static PersonEventState current_state(const PersonEventTracker *tracker)
{
    if (tracker->active)
    {
        if (tracker->consecutive_misses > 0U)
            return PERSON_STATE_EXIT_PENDING;

        return PERSON_STATE_PRESENT;
    }

    if (tracker->history_hits > 0U)
        return PERSON_STATE_CANDIDATE;

    return PERSON_STATE_IDLE;
}

static void fill_result(const PersonEventTracker *tracker,
                        const PersonEventInput *input,
                        PersonEventType event,
                        PersonEventResult *result)
{
    memset(result, 0, sizeof(*result));
    result->event = event;
    result->state = current_state(tracker);
    result->person_present = tracker->active;
    result->event_id = tracker->event_id;
    result->frame_id = input->frame_id;
    result->timestamp_ms = input->timestamp_ms;
    result->person_count = input->person_count;
    result->max_score = input->max_score;
    result->enter_hits = tracker->history_hits;
    result->enter_samples = tracker->history_count;
    result->consecutive_misses = tracker->consecutive_misses;
}

void person_event_default_config(PersonEventConfig *config)
{
    if (config == NULL)
        return;

    config->enter_window = 3U;
    config->enter_required = 2U;
    config->leave_count = 5U;
}

int person_event_init(PersonEventTracker *tracker,
                      const PersonEventConfig *config)
{
    if (tracker == NULL || !config_is_valid(config))
        return -1;

    memset(tracker, 0, sizeof(*tracker));
    tracker->config = *config;
    return 0;
}

void person_event_reset(PersonEventTracker *tracker)
{
    PersonEventConfig config;

    if (tracker == NULL)
        return;

    config = tracker->config;
    memset(tracker, 0, sizeof(*tracker));
    tracker->config = config;
}

int person_event_update(PersonEventTracker *tracker,
                        const PersonEventInput *input,
                        PersonEventResult *result)
{
    int detected;
    PersonEventType event = PERSON_EVENT_NONE;

    if (tracker == NULL || input == NULL || result == NULL)
        return -1;

    if (!config_is_valid(&tracker->config))
        return -1;

    detected = (input->person_count > 0U);

    if (!tracker->active)
    {
        add_entry_sample(tracker, detected);

        if (tracker->history_hits >= tracker->config.enter_required)
        {
            tracker->active = true;
            tracker->consecutive_misses = 0U;
            tracker->event_id++;
            if (tracker->event_id == 0U)
                tracker->event_id = 1U;

            clear_entry_history(tracker);
            event = PERSON_EVENT_ENTER;
        }
    }
    else if (detected)
    {
        tracker->consecutive_misses = 0U;
        event = PERSON_EVENT_PRESENT;
    }
    else
    {
        tracker->consecutive_misses++;

        if (tracker->consecutive_misses >= tracker->config.leave_count)
        {
            tracker->active = false;
            tracker->consecutive_misses = 0U;
            clear_entry_history(tracker);
            event = PERSON_EVENT_LEAVE;
        }
        else
        {
            /* A short miss does not end the stable PERSON event. */
            event = PERSON_EVENT_PRESENT;
        }
    }

    fill_result(tracker, input, event, result);
    return 0;
}

const char *person_event_type_string(PersonEventType event)
{
    switch (event)
    {
    case PERSON_EVENT_NONE:    return "NONE";
    case PERSON_EVENT_ENTER:   return "ENTER";
    case PERSON_EVENT_PRESENT: return "PRESENT";
    case PERSON_EVENT_LEAVE:   return "LEAVE";
    default:                   return "UNKNOWN";
    }
}

const char *person_event_state_string(PersonEventState state)
{
    switch (state)
    {
    case PERSON_STATE_IDLE:         return "IDLE";
    case PERSON_STATE_CANDIDATE:    return "CANDIDATE";
    case PERSON_STATE_PRESENT:      return "PRESENT";
    case PERSON_STATE_EXIT_PENDING: return "EXIT_PENDING";
    default:                        return "UNKNOWN";
    }
}
