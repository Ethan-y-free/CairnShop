#pragma once

#include <string>
#include <string_view>

namespace cairnshop
{

std::string RandomHex(size_t bytes);
std::string Sha256Hex(std::string_view data);

// Format: pbkdf2$<iterations>$<salt_hex>$<hash_hex>
std::string HashPassword(const std::string& password);
bool VerifyPassword(const std::string& password, const std::string& stored);

bool ConstantTimeEquals(const std::string& a, const std::string& b);

bool IsValidEmail(const std::string& email);

// Strips spaces, hyphens and a leading +86/86; stores a mainland 11-digit
// mobile number.
std::string NormalizePhone(std::string_view value);
bool IsValidPhone(const std::string& phone);

bool IsValidPassword(const std::string& password);
bool IsValidDisplayName(const std::string& name);
bool IsValidSlug(const std::string& slug);
bool IsValidProductStatus(const std::string& status);

}
