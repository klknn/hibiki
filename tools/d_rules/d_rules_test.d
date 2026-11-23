module tools.d_rules.d_rules_test;

import tools.d_rules.d_interop_lib : d_lerp, d_sum_buffer;

@safe unittest {
    assert(d_lerp(0.0f, 10.0f, 0.5f) == 5.0f, "d_lerp midpoint failed");
    assert(d_lerp(-4.0f, 4.0f, 0.25f) == -2.0f, "d_lerp quarter failed");
}

unittest {
    float[4] buf = [1.5f, 2.5f, 3.0f, -1.0f];
    assert(d_sum_buffer(buf.ptr, 4) == 6.0f, "d_sum_buffer valid slice failed");
    assert(d_sum_buffer(null, 4) == 0.0f, "d_sum_buffer null check failed");
    assert(d_sum_buffer(buf.ptr, 0) == 0.0f, "d_sum_buffer empty count failed");
    assert(d_sum_buffer(buf.ptr, -5) == 0.0f, "d_sum_buffer negative count failed");
}
