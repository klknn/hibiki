#include <gtest/gtest.h>

extern float d_lerp(float a, float b, float t);
extern float d_sum_buffer(const float* data, int count);

TEST(DInteropTest, CallsDLibraryFunctionsFromCpp) {
  EXPECT_FLOAT_EQ(d_lerp(10.0f, 20.0f, 0.5f), 15.0f);
  EXPECT_FLOAT_EQ(d_lerp(0.0f, 100.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(d_lerp(0.0f, 100.0f, 1.0f), 100.0f);

  const float samples[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  EXPECT_FLOAT_EQ(d_sum_buffer(samples, 5), 15.0f);
  EXPECT_FLOAT_EQ(d_sum_buffer(nullptr, 5), 0.0f);
  EXPECT_FLOAT_EQ(d_sum_buffer(samples, 0), 0.0f);
}
