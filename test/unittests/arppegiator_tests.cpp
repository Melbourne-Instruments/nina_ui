#include "gtest/gtest.h"

#include "arpeggiator_manager.h"

#define private public

// A simple test case
TEST (SampleTest, SimpleTestCase) {
    ASSERT_TRUE (1);
}

// A more complex test case where tests can be grouped
// And setup and teardown functions added.
class ArpInitTestCase : public ::testing::Test
{
    protected:
    ArpInitTestCase()
    {
    }
    void SetUp()
    {
    }

    void TearDown()
    {
    }
};

TEST_F(ArpInitTestCase, ArpInitTest)
{
    EXPECT_FALSE(0);
}
