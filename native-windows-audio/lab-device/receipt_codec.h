#pragma once
#include "device_transaction.h"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace timox::lab {
// Exact wire size: magic TLR1[4], version LE32, stage LE32,
// operation GUID ASCII[36], machine GUID ASCII[36]. No string terminators.
// Other Receipt fields are fixed constants/derived instance, never arbitrary paths.
inline constexpr std::size_t ReceiptWireSize = 84;
bool EncodeReceipt(const Receipt&, std::vector<std::uint8_t>&);
bool DecodeReceipt(const std::uint8_t*, std::size_t, Receipt&);
}
