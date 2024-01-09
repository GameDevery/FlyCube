#pragma once
#include "BindingSet/MTBindingSet.h"

#import <Metal/Metal.h>

class MTDevice;
class MTBindingSetLayout;
class Pipeline;

class MTDirectArguments : public MTBindingSet {
public:
    MTDirectArguments(MTDevice& device, const std::shared_ptr<MTBindingSetLayout>& layout);

    void WriteBindings(const std::vector<BindingDesc>& bindings) override;

    void Apply(id<MTLRenderCommandEncoder> render_encoder, const std::shared_ptr<Pipeline>& state) override;
    void Apply(id<MTLComputeCommandEncoder> compute_encoder, const std::shared_ptr<Pipeline>& state) override;

    template <typename CommandEncoderType>
    static void ApplyDirectArgs(CommandEncoderType encoder,
                                const std::vector<BindKey>& bind_keys,
                                const std::vector<BindingDesc>& bindings,
                                MTDevice& device);

    static void ValidateRemappedSlots(const std::shared_ptr<Pipeline>& state, const std::vector<BindKey>& bind_keys);

private:
    MTDevice& m_device;
    std::shared_ptr<MTBindingSetLayout> m_layout;
    std::vector<BindingDesc> m_bindings;
};
