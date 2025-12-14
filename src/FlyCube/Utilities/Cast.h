#pragma once
#include <memory>

template <typename T, typename U>
T* CastToImpl(const std::shared_ptr<U>& obj)
{
    return static_cast<T*>(obj.get());
}
