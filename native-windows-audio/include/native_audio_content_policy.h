#pragma once
namespace timox_native {
// One instance per render stream. Fail closed for its lifetime after protection
// or an unknown rights update: old DMA contents cannot become public on downgrade.
class RenderContentPolicy final {
public:
    bool Update(bool rightsKnown,bool copyProtected,bool digitalDisabled) noexcept {
        blocked_=blocked_ || !rightsKnown || copyProtected || digitalDisabled;
        return blocked_;
    }
private:
    bool blocked_=false;
};
}
