#pragma once
#include "CommandList/CommandList.h"
#include "Fence/Fence.h"

class CommandQueue {
public:
    virtual ~CommandQueue() = default;
    virtual void Wait(const std::shared_ptr<Fence>& fence, uint64_t value) = 0;
    virtual void Signal(const std::shared_ptr<Fence>& fence, uint64_t value) = 0;
    virtual void ExecuteCommandLists(const std::vector<std::shared_ptr<CommandList>>& command_lists) = 0;
};
