module tools.d_rules.d_interop_lib;

/// Computes a pure @nogc linear interpolation between a and b.
extern (C++) float d_lerp(float a, float b, float t) @nogc nothrow pure @safe {
    return a + (b - a) * t;
}

/// Computes the sum of a float buffer using D slices under @nogc.
extern (C++) float d_sum_buffer(const(float)* data, int count) @nogc nothrow pure {
    if (data is null || count <= 0) {
        return 0.0f;
    }
    const(float)[] slice = data[0 .. count];
    float total = 0.0f;
    foreach (v; slice) {
        total += v;
    }
    return total;
}
