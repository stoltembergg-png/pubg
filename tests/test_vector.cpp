#include <gtest/gtest.h>
#include <cmath>

#include "Vector.h"

TEST(Vector2Test, SupportsArithmeticAndDistance)
{
    const Vector2 left(3.0f, 4.0f);
    const Vector2 right(1.0f, 2.0f);

    EXPECT_EQ(left + right, Vector2(4.0f, 6.0f));
    EXPECT_EQ(left - right, Vector2(2.0f, 2.0f));
    EXPECT_EQ(left * right, Vector2(3.0f, 8.0f));
    EXPECT_EQ(left / right, Vector2(3.0f, 2.0f));
    EXPECT_FLOAT_EQ(Vector2::Distance(left, right), std::sqrt(8.0f));
    EXPECT_TRUE(Vector2::Zero().IsZero());
}

TEST(Vector3Test, SupportsArithmeticAndDotProduct)
{
    const Vector3 left(1.0f, 2.0f, 3.0f);
    const Vector3 right(4.0f, -5.0f, 6.0f);

    EXPECT_EQ(left + right, Vector3(5.0f, -3.0f, 9.0f));
    EXPECT_EQ(left - right, Vector3(-3.0f, 7.0f, -3.0f));
    EXPECT_EQ(left * 2.0f, Vector3(2.0f, 4.0f, 6.0f));
    EXPECT_FLOAT_EQ(Vector3::Dot(left, right), 12.0f);
    EXPECT_FLOAT_EQ(left.LengthSqr(), 14.0f);
    EXPECT_FLOAT_EQ(left.Length(), std::sqrt(14.0f));
}

TEST(Vector3Test, LerpAndClampPreserveExpectedValues)
{
    EXPECT_EQ(Vector3::Lerp(Vector3::Zero(), Vector3(10.0f, 20.0f, 30.0f), 0.5f),
              Vector3(5.0f, 10.0f, 15.0f));
    EXPECT_EQ(Vector3(100.0f, -200.0f, 7.0f).Clamp(), Vector3(-260.0f, 160.0f, 0.0f));
}
