#include "physical_asio_host.h"

int main() {
    StageTimingAccumulator accumulator;
    auto empty = accumulator.Consume();
    if (empty.samples || empty.sumTicks || empty.worstTicks) return 1;
    accumulator.Add(12);
    accumulator.Add(7);
    const auto window = accumulator.Consume();
    if (window.samples != 2 || window.sumTicks != 19 || window.worstTicks != 12) return 2;
    empty = accumulator.Consume();
    if (empty.samples || empty.sumTicks || empty.worstTicks) return 3;
}
