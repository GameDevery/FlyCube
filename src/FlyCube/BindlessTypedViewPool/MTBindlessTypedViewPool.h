#pragma once
#include "BindlessTypedViewPool/BindlessTypedViewPoolBase.h"
#include "Instance/BaseTypes.h"

class MTDevice;
class MTGPUArgumentBufferRange;
class MTView;

class MTBindlessTypedViewPool : public BindlessTypedViewPoolBase {
public:
    MTBindlessTypedViewPool(MTDevice& device, ViewType view_type, uint32_t view_count);

    // BindlessTypedViewPool:
    uint32_t GetBaseDescriptorId() const override;
    uint32_t GetViewCount() const override;
    void WriteView(uint32_t index, const std::shared_ptr<View>& view) override;

    // BindlessTypedViewPoolBase:
    void WriteViewImpl(uint32_t index, View* view) override;

private:
    uint32_t view_count_;
    std::shared_ptr<MTGPUArgumentBufferRange> range_;
};
