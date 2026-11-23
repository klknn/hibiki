module engine.instruments.multi_point_envelope;

import core.stdc.math : fabsf, powf;
import engine.core.math : clampVal;

extern (C++, "hibiki") {

struct EnvPointD {
    float time;
    float value;
    float tension;
}

float d_env_norm_to_time(float norm, float max_s) @nogc nothrow {
    return 0.001f * powf(max_s / 0.001f, norm);
}

float d_env_interpolate(float t, float tension) @nogc nothrow {
    t = clampVal(t, 0.0f, 1.0f);
    if (fabsf(tension) < 0.01f) {
        return t;
    }
    float exponent = 1.0f + fabsf(tension) * 3.0f;
    if (tension > 0.0f) {
        return powf(t, exponent);
    } else {
        return 1.0f - powf(1.0f - t, exponent);
    }
}

float d_env_process(
    const(EnvPointD)* points,
    int num_points,
    int sustain_index,
    int* stage,
    int* current_segment,
    float* segment_time,
    float* value,
    float sample_rate
) @nogc nothrow {
    enum int STAGE_IDLE = 0;
    enum int STAGE_PLAYING = 1;
    enum int STAGE_SUSTAIN = 2;
    enum int STAGE_RELEASING = 3;

    if (points is null || num_points < 2 || *stage == STAGE_IDLE) {
        *value = 0.0f;
        return *value;
    }

    if (*stage == STAGE_SUSTAIN) {
        return *value;
    }

    float dt = 1.0f / sample_rate;

    if (*stage == STAGE_PLAYING) {
        if (*current_segment >= num_points - 1) {
            *value = points[num_points - 1].value;
            *stage = STAGE_IDLE;
            return *value;
        }
        if (sustain_index >= 0 && *current_segment >= sustain_index) {
            *value = points[sustain_index].value;
            *stage = STAGE_SUSTAIN;
            return *value;
        }

        const EnvPointD p0 = points[*current_segment];
        const EnvPointD p1 = points[*current_segment + 1];
        float seg_duration = (p1.time > 0.001f) ? p1.time : 0.001f;

        *segment_time += dt;
        float t = clampVal(*segment_time / seg_duration, 0.0f, 1.0f);
        float curved_t = d_env_interpolate(t, p0.tension);
        *value = p0.value + (p1.value - p0.value) * curved_t;

        if (*segment_time >= seg_duration) {
            (*current_segment)++;
            *segment_time = 0.0f;
            if (sustain_index >= 0 && *current_segment >= sustain_index) {
                *value = points[sustain_index].value;
                *stage = STAGE_SUSTAIN;
            }
        }
        return *value;
    }

    if (*stage == STAGE_RELEASING) {
        if (*current_segment >= num_points - 1) {
            *value = points[num_points - 1].value;
            *stage = STAGE_IDLE;
            return *value;
        }

        const EnvPointD p0 = points[*current_segment];
        const EnvPointD p1 = points[*current_segment + 1];
        float seg_duration = (p1.time > 0.001f) ? p1.time : 0.001f;

        *segment_time += dt;
        float t = clampVal(*segment_time / seg_duration, 0.0f, 1.0f);
        float curved_t = d_env_interpolate(t, p0.tension);

        float start_val = (*current_segment == sustain_index) ? *value : p0.value;
        if (*segment_time <= dt * 2.0f && *current_segment == sustain_index) {
            start_val = *value;
        }
        *value = start_val + (p1.value - start_val) * curved_t;

        if (*segment_time >= seg_duration) {
            (*current_segment)++;
            *segment_time = 0.0f;
            if (*current_segment >= num_points - 1) {
                *value = points[num_points - 1].value;
                *stage = STAGE_IDLE;
            }
        }
        return *value;
    }

    return *value;
}

} // extern (C++, "hibiki")
