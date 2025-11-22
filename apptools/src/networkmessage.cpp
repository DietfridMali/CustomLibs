#include "networkmessage.h"

// =================================================================================================
// network data and address

bool NetworkMessage::IsValid(int requiredValueCount, int maxValueCount) {
    /*
        check a message for a match with the requested keyword
        deconstruct message (Split payload it into separate values)
        check message correctness (value count)

    Parameters:
    -----------
        keyword:
            the keyword to check the message for
        requiredValueCount:
            number of application parameters that the message payload should contain.
            > 0: specifies the exact required number of parameters
            < 0: specifies the required minimum number of parameters
            == 0: don't check parameter count
    */
    try {
        m_values.Clear();
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
        if (requiredValueCount == 0) {
            m_result = 1;
            return true;
        }
        if (requiredValueCount > 0) {
            if (m_numValues == requiredValueCount) {
                m_result = 1;
                return true;
            }
        }
        else if (requiredValueCount < 0) {
            if ((m_numValues >= -requiredValueCount) and ((maxValueCount < 1) or (m_numValues <= maxValueCount))) {
                m_result = 1;
                return true;
            }
        }
#ifdef _DEBUG
        fprintf(stderr, "message %s has wrong number of values (expected %d, found %d)\n", static_cast<char*>(keyword), requiredValueCount, m_numValues);
#endif
    }
    catch (...) {
        m_result = -1;
        return false;
    }
    m_result = -1;
    return false;
}


/*
return i-th parameter value as ip address:port pair

Parameters:
-----------
    i: Index of the requested parameter
*/
bool NetworkMessage::ToNetworkEndpoint(String caller, String valueName, int valueIndex, NetworkEndpoint& address) {
    if (not IsValidIndex(caller, valueName, valueIndex))
        return false;
    try {
        ManagedArray<String> ipParts = m_values[valueIndex].Split(':');
        if (ipParts.Length() != 2)
            return false;
        address.SocketAddress().host = StringToNumber<uint32_t>(caller, valueName, ipParts[0]);
        if (m_valueError)
            return false;
        address.SocketAddress().port = StringToNumber<uint16_t>(caller, valueName, ipParts[1]);
        if (m_valueError)
            return false;
        address.UpdateFromSocketAddress();
        return true;
    }
    catch (...) {
    }
    return InvalidDataError(caller, valueName, m_values[valueIndex]);
}


bool NetworkMessage::ToVector3f(Vector3f& v, String caller, String valueName, int valueIndex) noexcept {
    v = Vector3f::ZERO;
    if (not IsValidIndex(caller, valueName, valueIndex))
        return false;
    try {
        ManagedArray<String> coords = m_values[valueIndex].Split(',');
        if (coords.Length() != 3)
            return InvalidDataError(caller, valueName, m_values[valueIndex]);
        Vector3f w;
        w.X() = StringToNumber<float>(caller, valueName, coords[0]);
        if (ValueError())
            return InvalidDataError(caller, valueName, m_values[valueIndex]);
        w.Y() = StringToNumber<float>(caller, valueName, coords[1]);
        if (ValueError())
            return InvalidDataError(caller, valueName, m_values[valueIndex]);
        w.Z() = StringToNumber<float>(caller, valueName, coords[2]);
        if (ValueError())
            return InvalidDataError(caller, valueName, m_values[valueIndex]);
        v = w;
        return true;
    }
    catch (...) {
    }
    return InvalidDataError(caller, valueName, m_values[valueIndex]);
}




// =================================================================================================
