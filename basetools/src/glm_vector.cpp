#if USE_GLM

#include "glm_vector.hpp"

const Vector2f Vector2f::ZERO{ 0.0f, 0.0f };
const Vector2f Vector2f::NONE{ NAN, NAN };
const Vector2f Vector2f::HALF{ 0.5f, 0.5f };
const Vector2f Vector2f::ONE{ 1.0f, 1.0f };

const Vector3f Vector3f::ZERO{ 0.0f, 0.0f, 0.0f };
const Vector3f Vector3f::NONE{ NAN, NAN, NAN };
const Vector3f Vector3f::HALF{ 0.5f, 0.5f, 0.5f };
const Vector3f Vector3f::ONE{ 1.0f, 1.0f, 1.0f };

const Vector4f Vector4f::ZERO{ 0.0f, 0.0f, 0.0f, 0.0f };
const Vector4f Vector4f::HALF{ 0.5f, 0.5f, 0.5f, 0.5f };
const Vector4f Vector4f::ONE{ 1.0f, 1.0f, 1.0f, 1.0f };
const Vector4f Vector4f::NONE{ NAN, NAN, NAN, NAN };

#endif