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

    static T& Instance() {
#ifdef _DEBUG
        static thread_local bool constructing = false;
        if (constructing) {
            std::fputs("BaseSingleton: reentrant construction detected\n", stderr);
            std::abort();
        }
        constructing = true;
        static T instance;
        constructing = false;
#else
        static T instance;
#endif
        return instance;
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
    PolymorphSingleton() = default;

    static inline CLASS_T* _instance = nullptr;
};

// =================================================================================================
