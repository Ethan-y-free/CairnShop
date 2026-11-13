#include <gtest/gtest.h>

#include "cairnshop/crypto.h"

namespace cairnshop
{

TEST(CryptoTest, Sha256KnownVector)
{
    EXPECT_EQ(Sha256Hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(Sha256Hex(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(CryptoTest, PasswordRoundTrip)
{
    const std::string stored = HashPassword("correct horse battery staple");
    ASSERT_FALSE(stored.empty());
    EXPECT_EQ(stored.rfind("pbkdf2$120000$", 0), 0u);
    EXPECT_TRUE(VerifyPassword("correct horse battery staple", stored));
    EXPECT_FALSE(VerifyPassword("wrong password", stored));
}

TEST(CryptoTest, PasswordHashIsSalted)
{
    const std::string first = HashPassword("same-password");
    const std::string second = HashPassword("same-password");
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(second.empty());
    EXPECT_NE(first, second);
    EXPECT_TRUE(VerifyPassword("same-password", first));
    EXPECT_TRUE(VerifyPassword("same-password", second));
}

TEST(CryptoTest, RandomHexHasExpectedLength)
{
    const std::string token = RandomHex(32);
    ASSERT_EQ(token.size(), 64u);
    EXPECT_EQ(token.find_first_not_of("0123456789abcdef"), std::string::npos);
}

TEST(CryptoTest, RandomTokensDiffer)
{
    EXPECT_NE(RandomHex(16), RandomHex(16));
}

}
