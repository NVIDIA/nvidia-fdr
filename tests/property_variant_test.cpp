/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for property_variant.hpp — PropertyVariant type system
 *
 * Covers:
 *   - isValidVariant / isInvalidVariant for all held types
 *   - Type alias construction (RetCoreApi, PassthroughFPGA, Association)
 *   - InvalidMonoState as default
 */

#include "testCommon.hpp"

#include "property_variant.hpp"

// --- isValidVariant / isInvalidVariant ---

TEST(PropertyVariant, DefaultConstructedIsInvalid)
{
    PropertyVariant v;
    EXPECT_TRUE(isInvalidVariant(v));
    EXPECT_FALSE(isValidVariant(v));
}

TEST(PropertyVariant, BoolIsValid)
{
    PropertyVariant v = true;
    EXPECT_TRUE(isValidVariant(v));
    EXPECT_FALSE(isInvalidVariant(v));
}

TEST(PropertyVariant, Uint8IsValid)
{
    EXPECT_TRUE(isValidVariant(PropertyVariant{uint8_t{42}}));
}

TEST(PropertyVariant, Int16IsValid)
{
    EXPECT_TRUE(isValidVariant(PropertyVariant{int16_t{-100}}));
}

TEST(PropertyVariant, Uint16IsValid)
{
    EXPECT_TRUE(isValidVariant(PropertyVariant{uint16_t{1000}}));
}

TEST(PropertyVariant, Int32IsValid)
{
    EXPECT_TRUE(isValidVariant(PropertyVariant{int32_t{-50000}}));
}

TEST(PropertyVariant, Uint32IsValid)
{
    EXPECT_TRUE(isValidVariant(PropertyVariant{uint32_t{100000}}));
}

TEST(PropertyVariant, Int64IsValid)
{
    EXPECT_TRUE(isValidVariant(PropertyVariant{int64_t{-9999999}}));
}

TEST(PropertyVariant, Uint64IsValid)
{
    EXPECT_TRUE(
        isValidVariant(PropertyVariant{uint64_t{18446744073709551615ULL}}));
}

TEST(PropertyVariant, DoubleIsValid)
{
    EXPECT_TRUE(isValidVariant(PropertyVariant{3.14}));
}

TEST(PropertyVariant, StringIsValid)
{
    EXPECT_TRUE(isValidVariant(PropertyVariant{std::string("hello")}));
}

TEST(PropertyVariant, VectorStringIsValid)
{
    PropertyVariant v = std::vector<std::string>{"a", "b"};
    EXPECT_TRUE(isValidVariant(v));
}

TEST(PropertyVariant, TupleBoolUint32IsValid)
{
    PropertyVariant v = std::make_tuple(true, uint32_t{42});
    EXPECT_TRUE(isValidVariant(v));
}

TEST(PropertyVariant, VectorAssociationIsValid)
{
    std::vector<Association> assocs = {{"forward", "reverse", "/path"}};
    PropertyVariant v = assocs;
    EXPECT_TRUE(isValidVariant(v));
}

// --- Type alias construction ---

TEST(PropertyVariant, RetCoreApiConstruction)
{
    RetCoreApi ret = std::make_tuple(0, std::string("OK"), uint64_t{100});
    EXPECT_EQ(std::get<0>(ret), 0);
    EXPECT_EQ(std::get<1>(ret), "OK");
    EXPECT_EQ(std::get<2>(ret), 100u);
}

TEST(PropertyVariant, PassthroughFPGAConstruction)
{
    PassthroughFPGA pt = std::make_tuple(0, uint64_t{0xFF});
    EXPECT_EQ(std::get<0>(pt), 0);
    EXPECT_EQ(std::get<1>(pt), 0xFFu);
}

TEST(PropertyVariant, AssociationConstruction)
{
    Association a = std::make_tuple("forward", "reverse", "/xyz/path");
    EXPECT_EQ(std::get<0>(a), "forward");
    EXPECT_EQ(std::get<1>(a), "reverse");
    EXPECT_EQ(std::get<2>(a), "/xyz/path");
}

// --- InvalidMonoState ---

TEST(PropertyVariant, InvalidMonoStateIsIndex0)
{
    PropertyVariant v;
    EXPECT_EQ(v.index(), 0u);
    EXPECT_TRUE(std::holds_alternative<InvalidMonoState>(v));
}
