module engine.instruments.adsr;

import core.stdc.math : powf;
import engine.core.math : clampVal;

private float normToTime(float norm, float max_s) @nogc nothrow {
    return 0.001f * powf(max_s / 0.001f, norm);
}

extern (C++, "hibiki") {

/// ADSR envelope generator in @nogc D, directly ABI-compatible with C++ `hibiki::Adsr`.
extern (C++, class) struct Adsr {
public:
    enum Stage : int {
        ENV_IDLE = 0,
        ENV_ATTACK = 1,
        ENV_DECAY = 2,
        ENV_SUSTAIN = 3,
        ENV_RELEASE = 4,
    }

    void setParams(float attack_s, float decay_s, float sustain, float release_s) @nogc nothrow pure @safe {
        attack_ = (attack_s > 0.001f) ? attack_s : 0.001f;
        decay_ = (decay_s > 0.001f) ? decay_s : 0.001f;
        sustain_ = clampVal(sustain, 0.0f, 1.0f);
        release_ = (release_s > 0.001f) ? release_s : 0.001f;
    }

    void setNormalized(float a, float d, float s, float r) @nogc nothrow {
        setParams(normToTime(a, 5.0f), normToTime(d, 5.0f), s, normToTime(r, 10.0f));
    }

    void noteOn() @nogc nothrow pure @safe {
        stage_ = Stage.ENV_ATTACK;
        phase_ = 0.0f;
    }

    void noteOff() @nogc nothrow pure @safe {
        if (stage_ != Stage.ENV_IDLE && stage_ != Stage.ENV_RELEASE) {
            release_start_ = value_;
            stage_ = Stage.ENV_RELEASE;
            phase_ = 0.0f;
        }
    }

    float process(float sample_rate) @nogc nothrow pure @safe {
        float dt = 1.0f / sample_rate;
        final switch (stage_) {
            case Stage.ENV_ATTACK:
                phase_ += dt;
                value_ = phase_ / attack_;
                if (value_ >= 1.0f) {
                    value_ = 1.0f;
                    stage_ = Stage.ENV_DECAY;
                    phase_ = 0.0f;
                }
                break;
            case Stage.ENV_DECAY:
                phase_ += dt;
                value_ = 1.0f - (1.0f - sustain_) * (phase_ / decay_);
                if (phase_ >= decay_) {
                    value_ = sustain_;
                    stage_ = Stage.ENV_SUSTAIN;
                }
                break;
            case Stage.ENV_SUSTAIN:
                value_ = sustain_;
                break;
            case Stage.ENV_RELEASE:
                phase_ += dt;
                value_ = release_start_ * (1.0f - phase_ / release_);
                if (value_ <= 0.0f || phase_ >= release_) {
                    value_ = 0.0f;
                    stage_ = Stage.ENV_IDLE;
                }
                break;
            case Stage.ENV_IDLE:
                value_ = 0.0f;
                break;
        }
        return value_;
    }

    bool isIdle() const @nogc nothrow pure @safe {
        return stage_ == Stage.ENV_IDLE;
    }

    float getValue() const @nogc nothrow pure @safe {
        return value_;
    }

    Stage getStage() const @nogc nothrow pure @safe {
        return stage_;
    }

private:
    Stage stage_ = Stage.ENV_IDLE;
    float attack_ = 0.01f;
    float decay_ = 0.1f;
    float sustain_ = 0.7f;
    float release_ = 0.3f;
    float phase_ = 0.0f;
    float value_ = 0.0f;
    float release_start_ = 0.0f;
}

} // extern (C++, "hibiki")
