#include "person_event.h"

#include <inttypes.h>
#include <stdio.h>

static void feed(PersonEventTracker *tracker,
                 uint32_t frame_id,
                 int person_detected,
                 uint8_t score)
{
    PersonEventInput input;
    PersonEventResult result;

    input.frame_id = frame_id;
    input.timestamp_ms = (int64_t)frame_id * 200;
    input.person_count = person_detected ? 1U : 0U;
    input.max_score = person_detected ? score : 0U;

    if (person_event_update(tracker, &input, &result) != 0)
    {
        printf("update failed\n");
        return;
    }

    printf("frame=%2u raw=%d score=%3u event=%-7s state=%-12s "
           "stable=%d event_id=%" PRIu64
           " enter=%u/%u misses=%u\n",
           result.frame_id,
           person_detected,
           result.max_score,
           person_event_type_string(result.event),
           person_event_state_string(result.state),
           result.person_present ? 1 : 0,
           result.event_id,
           result.enter_hits,
           result.enter_samples,
           result.consecutive_misses);
}

int main(void)
{
    PersonEventConfig config;
    PersonEventTracker tracker;
    const int sequence[] = {
        0, 0, 0,
        1, 0, 1,
        1, 1,
        0, 1,
        0, 0, 0, 0, 0
    };
    uint32_t i;

    person_event_default_config(&config);

    if (person_event_init(&tracker, &config) != 0)
    {
        fprintf(stderr, "person_event_init failed\n");
        return 1;
    }

    printf("config: enter=%u-of-%u leave_misses=%u\n\n",
           config.enter_required,
           config.enter_window,
           config.leave_count);

    for (i = 0U;
         i < (uint32_t)(sizeof(sequence) / sizeof(sequence[0]));
         ++i)
    {
        feed(&tracker,
             i + 1U,
             sequence[i],
             sequence[i] ? 88U : 0U);
    }

    return 0;
}
