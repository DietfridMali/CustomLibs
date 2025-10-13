#pragma once

#include <mutex>
#include <stdexcept>

// =================================================================================================

template <typename T>
class BaseSingleton
{
public:
    BaseSingleton(const BaseSingleton&) = delete;
    BaseSingleton& operator=(const BaseSingleton&) = delete;

    static T& Instance()
    {
        static std::once_flag flag;
        static T* instance = nullptr;
        std::call_once(flag, []() { instance = new T(); });
        return *instance;
    }

protected:
    BaseSingleton() = default;
    virtual ~BaseSingleton() = default;
};

// =================================================================================================

template <typename CLASS_T>
class PolymorphSingleton {
public:
    PolymorphSingleton(const PolymorphSingleton&) = delete;
    PolymorphSingleton& operator=(const PolymorphSingleton&) = delete;
    virtual ~PolymorphSingleton() = default;

    static CLASS_T& Instance()
    {
        if (!_instance)
            throw std::runtime_error("Not initialized!");
        return *_instance;
    }

protected:
    // every class using PolymorphSingleton needs a (default) c'tor to initialize _instance
    PolymorphSingleton() { 
        _instance = static_cast<CLASS_T*>(this); 
    }

    static inline CLASS_T* _instance = nullptr;
};

// =================================================================================================
