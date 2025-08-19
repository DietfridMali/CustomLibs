
#include "singletonbase.hpp"

template <typename BASE_T>
BASE_T* PolymorphSingleton<BASE_T>::_instance = nullptr;