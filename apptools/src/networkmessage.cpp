#include "networkmessage.h"

// =================================================================================================
// network data and address

bool NetworkMessage::IsValid(int valueCount) {
    /*
        check a message for a match with the requested keyword
        deconstruct message (Split payload it into separate values)
        check message correctness (value count)

    Parameters:
    -----------
        keyword:
            the keyword to check the message for
        valueCount:
            number of application parameters that the message payload should contain.
            > 0: specifies the exact required number of parameters
            < 0: specifies the required minimum number of parameters
            == 0: don't check parameter count
    */
    try {
        m_values = m_payload.Split('#');
        String keyword = m_values[0];
        if (m_values.Length() < 2)
            m_numValues = 0;
        else {
            m_values = m_values[1].Split(';');
            if (not m_values[0].IsEmpty())
                m_numValues = m_values.Length();
            else {
                m_values.Clear();
                m_numValues = 0;
            }
        }
        if (valueCount == 0) {
            m_result = 1;
            return true;
        }
        if (valueCount > 0) {
            if (m_numValues == valueCount) {
                m_result = 1;
                return true;
            }
        }
        else if (valueCount < 0) {
            if (m_numValues >= -valueCount) {
                m_result = 1;
                return true;
            }
        }
#ifdef _DEBUG
        fprintf(stderr, "message %s has wrong number of values (expected %d, found %d)\n", static_cast<char*>(keyword), valueCount, m_numValues);
#endif
    }
    catch (...) {
        m_result = -1;
        return false;
    }
    m_result = -1;
    return false;
}



NetworkEndpoint& NetworkMessage::ToIpAddress(int i, NetworkEndpoint& address) {
    /*
    return i-th parameter value as ip address:port pair

    Parameters:
    -----------
        i: Index of the requested parameter
    */
    try {
        ManagedArray<String> ipParts = m_values[i].Split(':');
        if (ipParts.Length() < 2)
            address.Set("127.0.0.1", 1);
        else {
            address.SocketAddress().host = uint32_t(ipParts[0]);
            address.SocketAddress().port = uint16_t(ipParts[1]);
            address.UpdateFromSocketAddress();
        }
    }
    catch (...) {
        address.Set("127.0.0.1", 1);
    }
    return address;
}

// =================================================================================================
