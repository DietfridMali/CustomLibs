#if USE_GLM

#include "glm_vector.hpp"

const Vector2f Vector2f::ZERO = Vector2f({ 0.0f, 0.0f });
const Vector2f Vector2f::NONE = Vector2f({ NAN, NAN });
const Vector2f Vector2f::HALF = Vector2f({ 0.5f, 0.5f });
const Vector2f Vector2f::ONE  = Vector2f({ 1.0f, 1.0f });

const Vector3f Vector3f::ZERO = Vector3f({ 0.0f, 0.0f, 0.0f });
const Vector3f Vector3f::NONE = Vector3f({ NAN, NAN, NAN });
const Vector3f Vector3f::HALF = Vector3f({ 0.5f, 0.5f, 0.5f });
const Vector3f Vector3f::ONE  = Vector3f({ 1.0f, 1.0f, 1.0f });

const Vector4f Vector4f::ZERO = Vector4f({ 0.0f, 0.0f, 0.0f, 0.0f });
const Vector4f Vector4f::NONE = Vector4f({ NAN, NAN, NAN, NAN });
const Vector4f Vector4f::HALF = Vector4f({ 0.5f, 0.5f, 0.5f, 0.5f });
const Vector4f Vector4f::ONE  = Vector4f({ 1.0f, 1.0f, 1.0f, 1.0f });

#endif