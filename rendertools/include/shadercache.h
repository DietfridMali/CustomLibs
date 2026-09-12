#pragma once

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <vector>

#include "string.hpp"

// =================================================================================================

namespace ShaderCache {

    inline constexpr uint64_t kHashSeed = 14695981039346656037ull;
    inline constexpr uint64_t kHashPrime = 1099511628211ull;
    inline constexpr uint32_t kMagic = 0x43535452;

    struct Header {
        uint32_t    magic{ kMagic };
        uint32_t    tag{ 0 };
        uint64_t    key{ 0 };
        uint64_t    size{ 0 };
    };


    inline uint64_t Hash(uint64_t hash, const void* data, size_t size) noexcept {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= kHashPrime;
        }
        return hash;
    }


    inline uint64_t Hash(uint64_t hash, const char* s) noexcept {
        uint64_t length = uint64_t(std::strlen(s));
        hash = Hash(hash, &length, sizeof(length));
        return Hash(hash, s, size_t(length));
    }


    inline uint64_t Hash(uint64_t hash, const wchar_t* s) noexcept {
        uint64_t length = uint64_t(std::wcslen(s));
        hash = Hash(hash, &length, sizeof(length));
        return Hash(hash, s, size_t(length) * sizeof(wchar_t));
    }


    inline uint64_t Hash(uint64_t hash, const wchar_t* const* args, size_t count) noexcept {
        for (size_t i = 0; i < count; ++i)
            hash = Hash(hash, args[i]);
        return hash;
    }


    inline std::filesystem::path FilePath(const String& folder, const String& fileName) {
        return std::filesystem::path(static_cast<const char*>(folder)) / static_cast<const char*>(fileName);
    }


    inline bool ReadFile(const String& folder, const String& fileName, std::vector<uint8_t>& data) {
        std::ifstream f(FilePath(folder, fileName), std::ios::binary | std::ios::ate);
        if (not f)
            return false;
        std::streamoff size = f.tellg();
        if (size <= 0)
            return false;
        data.resize(size_t(size));
        f.seekg(0, std::ios::beg);
        f.read(reinterpret_cast<char*>(data.data()), size);
        return f.good();
    }


    inline bool WriteFile(const String& folder, const String& fileName, const uint8_t* data, size_t size, const Header* header = nullptr) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(static_cast<const char*>(folder)), ec);
        std::ofstream f(FilePath(folder, fileName), std::ios::binary | std::ios::trunc);
        if (not f)
            return false;
        if (header)
            f.write(reinterpret_cast<const char*>(header), sizeof(Header));
        f.write(reinterpret_cast<const char*>(data), std::streamsize(size));
        return f.good();
    }


    inline bool Read(const String& folder, const String& fileName, uint64_t key, std::vector<uint8_t>& payload, uint32_t& tag) {
        std::vector<uint8_t> data;
        if (not ReadFile(folder, fileName, data))
            return false;
        if (data.size() <= sizeof(Header))
            return false;
        Header header;
        std::memcpy(&header, data.data(), sizeof(Header));
        if ((header.magic != kMagic) or (header.key != key) or (header.size != uint64_t(data.size() - sizeof(Header))))
            return false;
        tag = header.tag;
        payload.assign(data.begin() + sizeof(Header), data.end());
        return true;
    }


    inline bool Write(const String& folder, const String& fileName, uint64_t key, uint32_t tag, const uint8_t* payload, size_t size) {
        Header header;
        header.tag = tag;
        header.key = key;
        header.size = uint64_t(size);
        return WriteFile(folder, fileName, payload, size, &header);
    }

}

// =================================================================================================
