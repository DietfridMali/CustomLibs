#if defined(COMPILE_MATRIX)

#include "conversions.hpp"
#include "matrix.hpp"

const Matrix4f Matrix4f::IDENTITY;

// =================================================================================================

Vector4f* Matrix4f::Mul(Vector4f* result, Vector4f* op1, Vector4f* op2, bool colMajor, int dimensions) {
    Vector4f tempOp[4];
    Vector4f transp[4];

    if (colMajor) {
        if (result == op2) {
            memcpy(tempOp, op2, 4 * sizeof(Vector4f));
            op2 = tempOp;
        }
        Transpose(transp, op1, dimensions);
        // Matrixmultiplikation: result = op1 * op2
        for (int col = 0; col < dimensions; ++col) {
            for (int row = 0; row < dimensions; ++row) {
                result[col][row] = transp[row].Dot(op2[col], dimensions);
            }
        }
    }
    else {
        if (result == op1) {
            memcpy(tempOp, op1, 4 * sizeof(Vector4f));
            op1 = tempOp;
        }
        Transpose(transp, op2, dimensions);
        // Matrixmultiplikation: result = op1 * op2
        for (int col = 0; col < dimensions; ++col) {
            for (int row = 0; row < dimensions; ++row) {
                result[col][row] = transp[row].Dot(op1[col], dimensions);
            }
        }
    }
    return result;
}

#if 0
Matrix4f Matrix4f::operator* (Matrix4f& other) {
    Matrix4f m;
    Matrix4f o;
    other.Transpose(o);
    Vector4f& v = other.m_data[0];
    m[0][0] = m_data[0].Dot(v);
    m[1][0] = m_data[1].Dot(v);
    m[2][0] = m_data[2].Dot(v);
    m[3][0] = m_data[3].Dot(v);
    v = other.m_data[1];
    m[0][1] = m_data[0].Dot(v);
    m[1][1] = m_data[1].Dot(v);
    m[2][1] = m_data[2].Dot(v);
    m[3][1] = m_data[3].Dot(v);
    v = other.m_data[2];
    m[0][2] = m_data[0].Dot(v);
    m[1][2] = m_data[1].Dot(v);
    m[2][2] = m_data[2].Dot(v);
    m[3][2] = m_data[3].Dot(v);
    v = other.m_data[3];
    m[0][3] = m_data[0].Dot(v);
    m[1][3] = m_data[1].Dot(v);
    m[2][3] = m_data[2].Dot(v);
    m[3][3] = m_data[3].Dot(v);
    return m;
}
#endif

Matrix4f& Matrix4f::operator*= (Matrix4f& other) {
    Mul(m_data, m_data, other.m_data, IsColMajor());
    m_hasChanged = true;
    return *this;
}


Matrix4f Matrix4f::AffineInverse(void) {
    Matrix4f i;
    if (not Inverse3x3(i))
        return *this; // Fehlerindikator, je nach Implementierung anders prüfen
    // 2. Translation extrahieren
    Vector3f t{ m_data[0][3], m_data[1][3], m_data[2][3] };
    // 3. Inverse Translation berechnen: -R^-1 * t
    i[0][3] = -i[0].Dot(t, 3);
    i[1][3] = -i[1].Dot(t, 3);
    i[2][3] = -i[2].Dot(t, 3);
    i[3] = { 0,0,0,1 };

    i.m_hasChanged = true;
    return i;
}


float Matrix4f::Det(void) {
    auto& a = m_data;

    float det =
        a[0][0] * (
            a[1][1] * (a[2][2] * a[3][3] - a[2][3] * a[3][2]) -
            a[1][2] * (a[2][1] * a[3][3] - a[2][3] * a[3][1]) +
            a[1][3] * (a[2][1] * a[3][2] - a[2][2] * a[3][1]))
        - a[0][1] * (
            a[1][0] * (a[2][2] * a[3][3] - a[2][3] * a[3][2]) -
            a[1][2] * (a[2][0] * a[3][3] - a[2][3] * a[3][0]) +
            a[1][3] * (a[2][0] * a[3][2] - a[2][2] * a[3][0]))
        + a[0][2] * (
            a[1][0] * (a[2][1] * a[3][3] - a[2][3] * a[3][1]) -
            a[1][1] * (a[2][0] * a[3][3] - a[2][3] * a[3][0]) +
            a[1][3] * (a[2][0] * a[3][1] - a[2][1] * a[3][0]))
        - a[0][3] * (
            a[1][0] * (a[2][1] * a[3][2] - a[2][2] * a[3][1]) -
            a[1][1] * (a[2][0] * a[3][2] - a[2][2] * a[3][0]) +
            a[1][2] * (a[2][0] * a[3][1] - a[2][1] * a[3][0]));
    return det;
}


bool Matrix4f::Inverse(Matrix4f& i) {
    auto& a = m_data;
    float det = this->Det();

    if (det == 0.0f)
        return false; // oder Fehlerbehandlung

    // Verkürzt: Berechnung der adjungierten Matrix von a
#define M(i,j) a[i][j]

    i[0] = {
        +(M(1,1) * (M(2,2) * M(3,3) - M(2,3) * M(3,2)) - M(1,2) * (M(2,1) * M(3,3) - M(2,3) * M(3,1)) + M(1,3) * (M(2,1) * M(3,2) - M(2,2) * M(3,1))) / det,
        -(M(0,1) * (M(2,2) * M(3,3) - M(2,3) * M(3,2)) - M(0,2) * (M(2,1) * M(3,3) - M(2,3) * M(3,1)) + M(0,3) * (M(2,1) * M(3,2) - M(2,2) * M(3,1))) / det,
        +(M(0,1) * (M(1,2) * M(3,3) - M(1,3) * M(3,2)) - M(0,2) * (M(1,1) * M(3,3) - M(1,3) * M(3,1)) + M(0,3) * (M(1,1) * M(3,2) - M(1,2) * M(3,1))) / det,
        -(M(0,1) * (M(1,2) * M(2,3) - M(1,3) * M(2,2)) - M(0,2) * (M(1,1) * M(2,3) - M(1,3) * M(2,1)) + M(0,3) * (M(1,1) * M(2,2) - M(1,2) * M(2,1))) / det
    };
    i[1] = {
        -(M(1,0) * (M(2,2) * M(3,3) - M(2,3) * M(3,2)) - M(1,2) * (M(2,0) * M(3,3) - M(2,3) * M(3,0)) + M(1,3) * (M(2,0) * M(3,2) - M(2,2) * M(3,0))) / det,
        +(M(0,0) * (M(2,2) * M(3,3) - M(2,3) * M(3,2)) - M(0,2) * (M(2,0) * M(3,3) - M(2,3) * M(3,0)) + M(0,3) * (M(2,0) * M(3,2) - M(2,2) * M(3,0))) / det,
        -(M(0,0) * (M(1,2) * M(3,3) - M(1,3) * M(3,2)) - M(0,2) * (M(1,0) * M(3,3) - M(1,3) * M(3,0)) + M(0,3) * (M(1,0) * M(3,2) - M(1,2) * M(3,0))) / det,
        +(M(0,0) * (M(1,2) * M(2,3) - M(1,3) * M(2,2)) - M(0,2) * (M(1,0) * M(2,3) - M(1,3) * M(2,0)) + M(0,3) * (M(1,0) * M(2,2) - M(1,2) * M(2,0))) / det
    };
    i[2] = {
        +(M(1,0) * (M(2,1) * M(3,3) - M(2,3) * M(3,1)) - M(1,1) * (M(2,0) * M(3,3) - M(2,3) * M(3,0)) + M(1,3) * (M(2,0) * M(3,1) - M(2,1) * M(3,0))) / det,
        -(M(0,0) * (M(2,1) * M(3,3) - M(2,3) * M(3,1)) - M(0,1) * (M(2,0) * M(3,3) - M(2,3) * M(3,0)) + M(0,3) * (M(2,0) * M(3,1) - M(2,1) * M(3,0))) / det,
        +(M(0,0) * (M(1,1) * M(3,3) - M(1,3) * M(3,1)) - M(0,1) * (M(1,0) * M(3,3) - M(1,3) * M(3,0)) + M(0,3) * (M(1,0) * M(3,1) - M(1,1) * M(3,0))) / det,
        -(M(0,0) * (M(1,1) * M(2,3) - M(1,3) * M(2,1)) - M(0,1) * (M(1,0) * M(2,3) - M(1,3) * M(2,0)) + M(0,3) * (M(1,0) * M(2,1) - M(1,1) * M(2,0))) / det
    };
    i[3] = {
        -(M(1,0) * (M(2,1) * M(3,2) - M(2,2) * M(3,1)) - M(1,1) * (M(2,0) * M(3,2) - M(2,2) * M(3,0)) + M(1,2) * (M(2,0) * M(3,1) - M(2,1) * M(3,0))) / det,
        +(M(0,0) * (M(2,1) * M(3,2) - M(2,2) * M(3,1)) - M(0,1) * (M(2,0) * M(3,2) - M(2,2) * M(3,0)) + M(0,2) * (M(2,0) * M(3,1) - M(2,1) * M(3,0))) / det,
        -(M(0,0) * (M(1,1) * M(3,2) - M(1,2) * M(3,1)) - M(0,1) * (M(1,0) * M(3,2) - M(1,2) * M(3,0)) + M(0,2) * (M(1,0) * M(3,1) - M(1,1) * M(3,0))) / det,
        +(M(0,0) * (M(1,1) * M(2,2) - M(1,2) * M(2,1)) - M(0,1) * (M(1,0) * M(2,2) - M(1,2) * M(2,0)) + M(0,2) * (M(1,0) * M(2,1) - M(1,1) * M(2,0))) / det
    };
    i.m_hasChanged = true;
    return true;
}


Matrix4f Matrix4f::Inverse(void) {
    Matrix4f i;
    return Inverse(i) ? i : *this;
}


float Matrix4f::Det3x3(void) const {
    auto& a = m_data;
    return
        a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
        - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
        + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
}


bool Matrix4f::Inverse3x3(Matrix4f& i) const {
    auto& a = m_data;
    float det = Det3x3();
    if (det == 0.0f) {
        i = Matrix4f::IDENTITY; // oder Fehlerbehandlung
        return false;
    }

    i[0] = {
         (a[1][1] * a[2][2] - a[1][2] * a[2][1]) / det,
        -(a[0][1] * a[2][2] - a[0][2] * a[2][1]) / det,
         (a[0][1] * a[1][2] - a[0][2] * a[1][1]) / det,
         0.0f
    };
    i[1] = {
        -(a[1][0] * a[2][2] - a[1][2] * a[2][0]) / det,
         (a[0][0] * a[2][2] - a[0][2] * a[2][0]) / det,
        -(a[0][0] * a[1][2] - a[0][2] * a[1][0]) / det,
         0.0f
    };
    i[2] = {
         (a[1][0] * a[2][1] - a[1][1] * a[2][0]) / det,
        -(a[0][0] * a[2][1] - a[0][1] * a[2][0]) / det,
         (a[0][0] * a[1][1] - a[0][1] * a[1][0]) / det,
         0.0f
    };
    i[3] = { 0,0,0,1 };

    i.m_hasChanged = true;
    return true;
}


Matrix4f Matrix4f::Inverse3x3(void) const {
    Matrix4f i;
    return Inverse3x3(i) ? i : *this;
}


void Matrix4f::Transpose(Vector4f* dest, const Vector4f* source, int dimensions) {
    if (dest != source)
        memcpy(dest, source, 4 * sizeof(Vector4f));
    // transpose to self -> swap upper matrix triangle with lower one, leave det alone, for 18 copy operations
    // no need to copy data to remain untransposed here as it is already in the destination (== source) buffer
    for (int i = 0; i < dimensions; i++)
        for (int j = i + 1; j < dimensions; j++) {
            float h = source[i][j];
            dest[i][j] = source[j][i];
            dest[j][i] = h;
        }
#if 0
    else {
        // make sure to copy untransposed data if dimensions < 4
        // I guess memcpy might be much faster than copying that data 
        // row by row and col by col with for loops and index operations
        if (dimensions < 4)
            memcpy(dest, source, 4 * sizeof(Vector4f));
        // 32 copy operations
        for (int i = 0; i < dimensions; i++)
            for (int j = 0; j < dimensions; j++)
                dest[i][j] = source[j][i];
    }
#endif
}


// flip column order (row major -> column major and vice versa)
// dimensions allow to limit the number of rows and cols to be transposed
// this is important for creating OpenGL compatible matrices
// as these must have the T vector in the 3rd row (not column),
// so AsArray() will put it in indices 12 - 14 of the linear array buffer
// passed to OpenGL
Matrix4f& Matrix4f::Transpose(Matrix4f& dest, int dimensions) const {
    Transpose(dest.m_data, m_data, dimensions); // will also take care of transposing a matrix to itself
    dest.m_hasChanged = true;
    return dest;
}


Matrix4f Matrix4f::Transpose(int dimensions) {
    Matrix4f t;
    return Transpose(t, dimensions);
}


Matrix4f& Matrix4f::EulerComputeZYX(float sinX, float cosX, float sinY, float cosY, float sinZ, float cosZ, bool colMajor) {
    // column major direkt für OpenGL; nur mit diesen Vorzeichen funktioniert die Rotation (visuell) korrekt
    R() = { cosY * cosZ,                              cosY * sinZ,                            -sinY ,      0.0f };
    U() = { sinX * sinY * cosZ - cosX * sinZ,         sinX * sinY * sinZ + cosX * cosZ,       sinX * cosY, 0.0f };
    F() = { cosX * sinY * cosZ + sinX * sinZ,         cosX * sinY * sinZ - sinX * cosZ,       cosX * cosY, 0.0f };
    T() = { 0,                                        0,                                      0,           1.0f };
    if (not colMajor)
        Transpose(m_data, m_data, 3);
    m_hasChanged = true;
    return *this;
}


Matrix4f& Matrix4f::Rotation(Matrix4f& rotation, float angle, float x, float y, float z) {
    float radians = Conversions::DegToRad(angle);
    float c = cos(radians);
    float s = sin(radians);
    float cInv = 1.0f - c;

    // Normiere Achse
    float len = sqrtf(x * x + y * y + z * z);
    if (len == 0.0f)
        return rotation; // keine Rotation

    x /= len;
    y /= len;
    z /= len;

    rotation = {
        Vector4f({ c + cInv * x * x, cInv * x * y + s * z, cInv * x * z - s * y, 0.0f }),
        Vector4f({ cInv * y * x - s * z, c + cInv * y * y, cInv * y * z + s * x, 0.0f }),
        Vector4f({ cInv * z * x + s * y, cInv * z * y - s * x, c + cInv * z * z, 0.0f }),
        Vector4f({ 0, 0, 0, 1 })
    };
    rotation.m_hasChanged = true;
    return rotation;
}


Matrix4f Matrix4f::Rotation(float angle, float x, float y, float z) {
    Matrix4f r;
    return Rotation(r, x, y, z);
}


Matrix4f Matrix4f::Scaling(float sx, float sy, float sz) {
    return Matrix4f({ Vector4f({ sx, 0,  0,  0 }),
                       Vector4f({ 0,  sy, 0,  0 }),
                       Vector4f({ 0,  0,  sz, 0 }),
                       Vector4f({ 0,  0,  0,  1 }) });
}


Matrix4f Matrix4f::Translation(float dx, float dy, float dz, bool colMajor) {
    if (colMajor)
        return Matrix4f({ Vector4f({ 1.0f, 0.0f, 0.0f, dx }),
                          Vector4f({ 0.0f, 1.0f, 0.0f, dy }),
                          Vector4f({ 0.0f, 0.0f, 1.0f, dz }),
                          Vector4f({ 0.0f, 0.0f, 0.0f, 1.0f }) });
    else
        return Matrix4f({ Vector4f({ 1.0f, 0.0f, 0.0f, 0.0f }),
                          Vector4f({ 0.0f, 1.0f, 0.0f, 0.0f }),
                          Vector4f({ 0.0f, 0.0f, 1.0f, 0.0f }),
                          Vector4f({ dx, dy, dz, 1.0f }) });
}


float* Matrix4f::AsArray(void) {
    if (m_hasChanged) {
        m_hasChanged = false;
        auto& a = m_data;
        float* fp = m_array;
        if (m_isColMajor) {
            for (uint32_t col = 0; col < 4; col++) {
                for (uint32_t row = 0; row < 4; row++)
                    *fp++ = a[row][col];
            }
        }
        else {
            auto& a = m_data;
            float* fp = m_array;
            for (uint32_t i = 0; i < 4; i++, fp += 4)
                memcpy(fp, a[i].m_data, 4 * sizeof(float));
        }
    }
    return m_array;
}


Matrix4f& Matrix4f::FromArray(const float* data) {
    if (not data)
        data = m_array; // proper data must already have been stored in matrices' array
    else
        memcpy(m_array, data, sizeof(data));
    auto& a = m_data;
    if (m_isColMajor) {
        for (uint32_t col = 0; col < 4; col++) {
            for (uint32_t row = 0; row < 4; row++)
                a[row][col] = *data++;
        }
    }
    else {
        for (int row = 0; row < 4; row++) {
            auto& aRow = a[row];
            for (int col = 0; col < 4; col++)
                aRow[col] = *data++;
        }
    }
    return *this;
}

// =================================================================================================

#endif //COMPILE_MATRIX