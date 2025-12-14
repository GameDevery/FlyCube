#pragma once
#include "Instance/BaseTypes.h"

class BindingSet {
public:
    virtual ~BindingSet() = default;
    virtual void WriteBindings(const WriteBindingsDesc& desc) = 0;
};
