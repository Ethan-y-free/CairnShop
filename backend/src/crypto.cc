#include "cairnshop/crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cctype>
#include <cstdint>
#include <sstream>
#include <vector>

namespace cairnshop
{

namespace
{

constexpr int kPbkdf2Iterations = 120000;
constexpr size_t kSaltBytes = 16;
constexpr size_t kHashBytes = 32;

std::string ToHex(const unsigned char* data, size_t size)
{
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (size_t i = 0; i < size; ++i)
    {
        out[2 * i] = kHex[(data[i] >> 4) & 0x0F];
        out[2 * i + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

bool FromHex(const std::string& hex, std::vector<unsigned char>* out)
{
    if (hex.size() % 2 != 0)
    {
        return false;
    }
    out->clear();
    out->reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        int high = 0;
        int low = 0;
        const char a = hex[i];
        const char b = hex[i + 1];
        if (a >= '0' && a <= '9')
        {
            high = a - '0';
        }
        else if (a >= 'a' && a <= 'f')
        {
            high = a - 'a' + 10;
        }
        else
        {
            return false;
        }
        if (b >= '0' && b <= '9')
        {
            low = b - '0';
        }
        else if (b >= 'a' && b <= 'f')
        {
            low = b - 'a' + 10;
        }
        else
        {
            return false;
        }
        out->push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

}

std::string RandomHex(size_t bytes)
{
    std::vector<unsigned char> buffer(bytes);
    if (RAND_bytes(buffer.data(), static_cast<int>(buffer.size())) != 1)
    {
        return "";
    }
    return ToHex(buffer.data(), buffer.size());
}

std::string Sha256Hex(std::string_view data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(data.data(),
                   data.size(),
                   digest,
                   &length,
                   EVP_sha256(),
                   nullptr) != 1)
    {
        return "";
    }
    return ToHex(digest, length);
}

std::string HashPassword(const std::string& password)
{
    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1)
    {
        return "";
    }

    std::vector<unsigned char> hash(kHashBytes);
    if (PKCS5_PBKDF2_HMAC(password.data(),
                          static_cast<int>(password.size()),
                          salt.data(),
                          static_cast<int>(salt.size()),
                          kPbkdf2Iterations,
                          EVP_sha256(),
                          static_cast<int>(hash.size()),
                          hash.data()) != 1)
    {
        return "";
    }

    std::ostringstream out;
    out << "pbkdf2$" << kPbkdf2Iterations << '$' << ToHex(salt.data(), salt.size())
        << '$' << ToHex(hash.data(), hash.size());
    return out.str();
}

bool VerifyPassword(const std::string& password, const std::string& stored)
{
    // pbkdf2$<iterations>$<salt_hex>$<hash_hex>
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= stored.size())
    {
        const size_t end = stored.find('$', start);
        parts.push_back(stored.substr(start, end - start));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    if (parts.size() != 4 || parts[0] != "pbkdf2")
    {
        return false;
    }

    int iterations = 0;
    try
    {
        iterations = std::stoi(parts[1]);
    }
    catch (...)
    {
        return false;
    }
    if (iterations <= 0)
    {
        return false;
    }

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!FromHex(parts[2], &salt) || !FromHex(parts[3], &expected))
    {
        return false;
    }
    if (expected.empty())
    {
        return false;
    }

    std::vector<unsigned char> actual(expected.size());
    if (PKCS5_PBKDF2_HMAC(password.data(),
                          static_cast<int>(password.size()),
                          salt.data(),
                          static_cast<int>(salt.size()),
                          iterations,
                          EVP_sha256(),
                          static_cast<int>(actual.size()),
                          actual.data()) != 1)
    {
        return false;
    }

    return CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
}

bool ConstantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

bool IsValidEmail(const std::string& email)
{
    if (email.size() < 3 || email.size() > 254)
    {
        return false;
    }
    const size_t at = email.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= email.size())
    {
        return false;
    }
    if (email.find('@', at + 1) != std::string::npos)
    {
        return false;
    }
    const std::string domain = email.substr(at + 1);
    if (domain.find('.') == std::string::npos)
    {
        return false;
    }
    for (const char c : email)
    {
        if (std::isspace(static_cast<unsigned char>(c)) != 0)
        {
            return false;
        }
    }
    return true;
}

bool IsValidPassword(const std::string& password)
{
    return password.size() >= 8 && password.size() <= 128;
}

bool IsValidDisplayName(const std::string& name)
{
    if (name.empty() || name.size() > 64)
    {
        return false;
    }
    for (const char c : name)
    {
        if (std::iscntrl(static_cast<unsigned char>(c)) != 0)
        {
            return false;
        }
    }
    return true;
}

bool IsValidSlug(const std::string& slug)
{
    if (slug.empty() || slug.size() > 64)
    {
        return false;
    }
    for (const char c : slug)
    {
        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-')
        {
            return false;
        }
    }
    return slug.front() != '-' && slug.back() != '-';
}

bool IsValidProductStatus(const std::string& status)
{
    return status == "draft" || status == "active" || status == "archived";
}

}
