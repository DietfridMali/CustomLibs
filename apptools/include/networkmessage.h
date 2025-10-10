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
        ManagedArray<String>    m_values;

        NetworkMessage() 
            : m_numValues (0), m_address ("", 0), m_result (0) 
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



        inline int ToInt(int i) {
            /*
            return i-th parameter value as int

            Parameters:
            -----------
                i: Index of the requested parameter
            */
            try {
                return int(m_values[i]);
            }
            catch (...) {
                return 0;
            }
        }


        inline int ToUInt16(int i) {
            /*
            return i-th parameter value as int

            Parameters:
            -----------
                i: Index of the requested parameter
            */
            try {
                return uint16_t(m_values[i]);
            }
            catch (...) {
                return 0;
            }
        }


        inline int ToUInt32(int i) {
            /*
            return i-th parameter value as int

            Parameters:
            -----------
                i: Index of the requested parameter
            */
            try {
                return uint32_t(m_values[i]);
            }
            catch (...) {
                return 0;
            }
        }


        inline float ToFloat(int i) {
            /*
            return i-th parameter value as float

            Parameters:
            -----------
                i: Index of the requested parameter
            */
            try {
                return float(m_values[i]);
            }
            catch (...) {
                return 0;
            }
        }


        // format: <x>,<y>,<z> (3 x float)
        inline Vector3f ToVector3f(int i) {
            /*
            return i-th parameter value as 3D float vector

            Parameters:
            -----------
                i: Index of the requested parameter
            */
            try {
                ManagedArray<String> coords = m_values[i].Split(',');
                return Vector3f{ float(coords[0]), float(coords[1]), float(coords[2]) };
            }
            catch (...) {
                return Vector3f::ZERO;
            }
        }



        // format: <ip v4 address>":"<port>
        // <ip address> = "//.//.//.//" (// = one to three digit subnet id)
        inline String ToAddress(int i, uint16_t& port) {
            /*
            return i-th parameter value as ip address:port pair

            Parameters:
            -----------
                i: Index of the requested parameter
            */
            try {
                ManagedArray<String> values = m_values[i].Split(':');
                port = uint16_t(values[1]);
                return values[0];
            }
            catch (...) {
                port = 0;
                return String("127.0.0.1");
            }
        }
};

// =================================================================================================
