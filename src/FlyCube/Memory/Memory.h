#pragma once
#include "Instance/BaseTypes.h"

class Memory {
public:
    virtual ~Memory() = default;
    virtual MemoryType GetMemoryType() const = 0;
};
