#include <gtest/gtest.h>

#include "cairnshop/crypto.h"

namespace cairnshop
{

TEST(ValidationTest, Email)
{
    EXPECT_TRUE(IsValidEmail("alice@example.com"));
    EXPECT_TRUE(IsValidEmail("a.b+tag@shop.example.co"));
    EXPECT_FALSE(IsValidEmail(""));
    EXPECT_FALSE(IsValidEmail("alice"));
    EXPECT_FALSE(IsValidEmail("@example.com"));
    EXPECT_FALSE(IsValidEmail("alice@example"));
    EXPECT_FALSE(IsValidEmail("alice@@example.com"));
    EXPECT_FALSE(IsValidEmail("alice @example.com"));
}

TEST(ValidationTest, Password)
{
    EXPECT_FALSE(IsValidPassword("short"));
    EXPECT_TRUE(IsValidPassword("12345678"));
    EXPECT_TRUE(IsValidPassword(std::string(128, 'a')));
    EXPECT_FALSE(IsValidPassword(std::string(129, 'a')));
}

TEST(ValidationTest, DisplayName)
{
    EXPECT_FALSE(IsValidDisplayName(""));
    EXPECT_TRUE(IsValidDisplayName("Alice"));
    EXPECT_FALSE(IsValidDisplayName(std::string(65, 'a')));
}

TEST(ValidationTest, Slug)
{
    EXPECT_TRUE(IsValidSlug("audio"));
    EXPECT_TRUE(IsValidSlug("home-and-kitchen"));
    EXPECT_FALSE(IsValidSlug("Audio"));
    EXPECT_FALSE(IsValidSlug("-audio"));
    EXPECT_FALSE(IsValidSlug("audio-"));
    EXPECT_FALSE(IsValidSlug("audio_gear"));
    EXPECT_FALSE(IsValidSlug(""));
}

TEST(ValidationTest, ProductStatus)
{
    EXPECT_TRUE(IsValidProductStatus("draft"));
    EXPECT_TRUE(IsValidProductStatus("active"));
    EXPECT_TRUE(IsValidProductStatus("archived"));
    EXPECT_FALSE(IsValidProductStatus("deleted"));
}

}
