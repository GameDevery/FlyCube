#pragma once
#include "BindlessTypedViewPool/BindlessTypedViewPool.h"

class BindlessTypedViewPoolBase : public BindlessTypedViewPool {
public:
    virtual void WriteViewImpl(uint32_t index, View* view) = 0;
};
