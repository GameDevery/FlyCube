#pragma once
#include "Instance/BaseTypes.h"

class QueryHeap {
public:
    virtual ~QueryHeap() = default;
    virtual QueryHeapType GetType() const = 0;
};
