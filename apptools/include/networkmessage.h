#pragma once 

#include <stdint.h>

#include "string.hpp"
#include "list.hpp"
#include "vector.hpp"
#include "networkendpoint.h"

// =================================================================================================
// network data and address

class NetworkMessage {
    /*
    Network message container class

    Attributes:
    -----------
        payload:
            the net message data containing application parameters
        address:
            ip address of the sender
        port:
            udp port of the sender
        values:
            List of single values #include "the payload
        numValues:
            Number of values
        result:
            Result of message processing (test for keyword match and required (minimum) number of values)

    Methods:
    --------
        IsValid (keyword, valueCount = 0):
            check and deconstruct a message
    */

    public:
        String                  m_payload;
        NetworkEndpoint         m_address;
        int                     m_numValues;
        int                     m_result;
        bool                    m_isBroadcast; // used for sending this message as a broadcast in a LAN
        bool                    m_valueError;
        ManagedArray<String>    m_values;

        NetworkMessage() 
            : m_numValues (0)
            , m_address ("", 0)
            , m_isBroadcast(false)
            , m_valueError(false)
            , m_result (0) 
        { }

        NetworkMessage(String message, NetworkEndpoint address) {
            /*
            Setup meaningful default values during class instance construction

            Parameters:
            -----------
                message:
                    data payload as received over the UDP interface
                address:
                    sender ip address
                port:
                    sender udp port
            */
            m_payload = message;
            m_address = address;
            m_numValues = 0;
            m_result = 0;
        }


        inline NetworkEndpoint& Address(void) noexcept {
            return m_address;
        }

        inline const String& IpAddress(void) const noexcept {
            return m_address.IpAddress();
        }

        inline uint16_t GetPort(void) noexcept {
            return m_address.GetPort();
        }

        inline void SetPort(uint16_t port) noexcept {
            m_address.SetPort(port);
        }

        inline String& Payload(void) noexcept {
            return m_payload;
        }

        inline int NumValues(void) noexcept {
            return m_numValues;
        }

        inline bool HasValues(int minValues) noexcept {
            return m_numValues >= minValues;
        }

        inline int Result(void) noexcept {
            return m_result;
        }

        inline bool IsEmpty(void) noexcept {
            return m_payload.IsEmpty();
        }

        bool IsValid(int valueCount = 0);

        inline bool IsBroadcast(void) {
            return m_isBroadcast;
        }

        inline void Broadcast(bool isBroadcast) {
            m_isBroadcast = isBroadcast;
        }

        inline String ToStr(int i) {
            /*
            return i-th parameter value as text

            Parameters:
            -----------
                i: Index of the requested parameter
            */
            try {
                return m_values[i];
            }
            catch (...) {
                return String("");
            }
        }


        inline bool InvalidDataError(const String caller, const String valueName, const String value) {
            m_valueError = true;
            fprintf(stderr, "%s (%s): value '%s' out of range\n", (const char*) caller, (const char*) valueName, (const char*)value);
            return false;
        }


        template <typename T>
        inline bool StringToNumber(T& v, String caller, String valueName, String value, T minVal = std::numeric_limits<T>::lowest(), T maxVal = std::numeric_limits<T>::max()) {
            /*
            return i-th parameter value as int

            Parameters:
            -----------
                i: Index of the requested parameter
            */
            m_valueError = false;
            try {
                v = static_cast<T>(value);
            }
            catch (...) {
                return InvalidDataError(caller, valueName, value);
            }
            T i = std::clamp<T>(v, minVal, maxVal);
            return (v == i) ? true : InvalidDataError(caller, valueName, value);
        }

        inline bool IsValidIndex(const String& caller, const String& valueName, int valueIndex) {
            if (valueIndex < m_values.Length())
                return true;
            m_valueError = true;
            fprintf(stderr, "%s (%s): invalid field index '%d'\n", (const char*)caller, (const char*)valueName, valueIndex);
            return false;
        }

        template <typename T>
        inline T StringToNumber(String caller, String valueName, String value, T minVal = std::numeric_limits<T>::lowest(), T maxVal = std::numeric_limits<T>::max()) {
            T v;
            return StringToNumber(v, caller, valueName, value, minVal, maxVal) ? v : minVal;
        }

        template <typename T>
        inline bool FieldToNumber(T& v, String caller, String valueName, int valueIndex, T minVal = std::numeric_limits<T>::lowest(), T maxVal = std::numeric_limits<T>::max()) {
            return IsValidIndex(caller, valueName, valueIndex) ? StringToNumber<T>(v, caller, valueName, m_values[valueIndex], minVal, maxVal) : false;
        }

        inline bool ToInt(int& v, String caller, String valueName, int valueIndex, int minVal = std::numeric_limits<int>::lowest(), int maxVal = std::numeric_limits<int>::max()) {
            return FieldToNumber<int>(v, caller, valueName, valueIndex, minVal, maxVal);
        }

        inline bool ToUInt8(uint8_t& v, String caller, String valueName, int valueIndex, uint8_t minVal = std::numeric_limits<uint8_t>::lowest(), uint8_t maxVal = std::numeric_limits<uint8_t>::max()) {
            return FieldToNumber<uint8_t>(v, caller, valueName, valueIndex, minVal, maxVal);
        }

        inline bool ToUInt16(uint16_t& v, String caller, String valueName, int valueIndex, uint16_t minVal = std::numeric_limits<uint16_t>::lowest(), uint16_t maxVal = std::numeric_limits<uint16_t>::max()) {
            return FieldToNumber<uint16_t>(v, caller, valueName, valueIndex, minVal, maxVal);
        }

        inline bool ToUInt32(uint32_t& v, String caller, String valueName, int valueIndex, uint32_t minVal = std::numeric_limits<uint32_t> ::lowest(), uint32_t maxVal = std::numeric_limits<uint32_t>::max()) {
            return FieldToNumber<uint32_t>(v, caller, valueName, valueIndex, minVal, maxVal);
        }

        inline bool ToFloat(float& v, String caller, String valueName, int valueIndex, float minVal = std::numeric_limits<float> ::lowest(), float maxVal = std::numeric_limits<float>::max()) {
            return FieldToNumber<float>(v, caller, valueName, valueIndex, minVal, maxVal);
        }

        inline int ToInt(String caller, String valueName, int valueIndex, int minVal = std::numeric_limits<int>::lowest(), int maxVal = std::numeric_limits<int>::max()) {
            int v;
            return FieldToNumber<int>(v, caller, valueName, valueIndex, minVal, maxVal) ? v : minVal;
        }

        inline uint8_t ToUInt8(String caller, String valueName, int valueIndex, uint8_t minVal = std::numeric_limits<uint8_t>::lowest(), uint8_t maxVal = std::numeric_limits<uint8_t>::max()) {
            uint8_t v;
            return FieldToNumber<uint8_t>(v, caller, valueName, valueIndex, minVal, maxVal) ? v : minVal;
        }

        inline uint16_t ToUInt16(String caller, String valueName, int valueIndex, uint16_t minVal = std::numeric_limits<uint16_t>::lowest(), uint16_t maxVal = std::numeric_limits<uint16_t>::max()) {
            uint16_t v;
            return FieldToNumber<uint16_t>(v, caller, valueName, valueIndex, minVal, maxVal) ? v : minVal;
        }

        inline uint32_t ToUInt32(String caller, String valueName, int valueIndex, uint32_t minVal = std::numeric_limits<uint32_t> ::lowest(), uint32_t maxVal = std::numeric_limits<uint32_t>::max()) {
            uint32_t v;
            return FieldToNumber<uint32_t>(v, caller, valueName, valueIndex, minVal, maxVal) ? v : minVal;
        }

        inline float ToFloat(String caller, String valueName, int valueIndex, float minVal = std::numeric_limits<float> ::lowest(), float maxVal = std::numeric_limits<float>::max()) {
            float v;
            return FieldToNumber<float>(v, caller, valueName, valueIndex, minVal, maxVal) ? v : minVal;
        }

        /*
        return i-th parameter value as 3D float vector

        Parameters:
        -----------
            i: Index of the requested parameter
        */
        // format: <x>,<y>,<z> (3 x float)
        bool ToVector3f(Vector3f& v, String caller, String valueName, int valueIndex) noexcept;


        // format: <ip v4 address>":"<port>
        // <ip address> = "//.//.//.//" (// = one to three digit subnet id)
        bool ToNetworkEndpoint(String caller, String valueName, int valueIndex, NetworkEndpoint& address);

        inline bool ValueError(void) noexcept {
            return m_valueError;
        }
};

// =================================================================================================
