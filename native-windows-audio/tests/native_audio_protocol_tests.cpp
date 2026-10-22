#include "native_audio_packet_validation.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

extern "C" int NativeAudioProtocolCHeaderUsable(void);
using namespace timox_native;
namespace {
int failures = 0;
void Check(bool value, const char* name) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
void Put(std::vector<unsigned char>& bytes, std::size_t offset, uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes[offset + i] = static_cast<unsigned char>(value >> (8 * i));
}
std::vector<unsigned char> Fixture() {
    // Independent wire fixture: magic TVWA, 64 bytes, v1.0, render data,
    // generation 0x102030405060708, sequence 5, no timestamp, 2 PCM32 stereo frames.
    std::vector<unsigned char> bytes(80, 0);
    const unsigned char header[] = {
        'T','V','W','A', 64,0,0,0, 1,0,0,0, 1,0,1,0,
        8,7,6,5,4,3,2,1, 5,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0, 2,0,0,0, 0x80,0xbb,0,0,
        2,0,1,0, 16,0,0,0, 0,0,0,0, 0,0,0,0
    };
    std::memcpy(bytes.data(), header, sizeof(header));
    // PCM edge values are payload, never mistaken for metadata or floats.
    const unsigned char pcm[] = {0,0,0,0x80, 0xff,0xff,0xff,0x7f, 0,0,0,0, 1,0,0,0};
    std::memcpy(bytes.data() + 64, pcm, sizeof(pcm));
    return bytes;
}
const PacketContext render{0x0102030405060708ULL, 1, 4096};
void Expect(const std::vector<unsigned char>& bytes, PacketError error, const char* name,
            PacketContext context = render) {
    const auto result = ValidatePacket(bytes.data(), bytes.size(), context);
    Check(result.error == error, name);
    if (error != PacketError::None) {
        Check(result.metadata.generation == 0 && result.metadata.frames == 0 &&
              result.metadata.payloadBytes == 0, "failure exposes no accepted metadata");
    }
}
void Mutate(std::size_t offset, uint64_t value, unsigned width, PacketError error, const char* name) {
    auto bytes = Fixture(); Put(bytes, offset, value, width); Expect(bytes, error, name);
}
}

int main() {
    Check(NativeAudioProtocolCHeaderUsable() != 0, "C-compatible protocol vocabulary");
    auto bytes = Fixture();
    const auto good = ValidatePacket(bytes.data(), bytes.size(), render);
    Check(good.error == PacketError::None, "accept independent render fixture");
    Check(good.metadata.generation == 0x0102030405060708ULL && good.metadata.sequence == 5 &&
          good.metadata.frames == 2 && good.metadata.payloadBytes == 16 && good.metadata.direction == 1,
          "decode little-endian metadata");
    auto unaligned = std::vector<unsigned char>(81, 0);
    std::memcpy(unaligned.data() + 1, bytes.data(), bytes.size());
    Check(ValidatePacket(unaligned.data() + 1, 80, render).error == PacketError::None,
          "unaligned byte input accepted without typed access");
    Check(ValidatePacket(nullptr, 80, render).error == PacketError::Truncated, "null pointer rejected");
    for (std::size_t size = 0; size < 64; ++size)
        Check(ValidatePacket(bytes.data(), size, render).error == PacketError::Truncated, "every short header rejected");
    Expect(bytes, PacketError::InvalidContext, "zero trusted generation", {0, 1, 4096});
    Expect(bytes, PacketError::InvalidContext, "invalid trusted direction", {render.generation, 3, 4096});
    Expect(bytes, PacketError::InvalidContext, "zero trusted capacity", {render.generation, 1, 0});
    Expect(bytes, PacketError::InvalidContext, "excess trusted capacity", {render.generation, 1, 4097});
    Expect(bytes, PacketError::Frames, "packet exceeds receiver capacity", {render.generation, 1, 1});
    Expect(bytes, PacketError::Direction, "render payload cannot inject capture", {render.generation, 2, 4096});
    Put(bytes, 12, 2, 2); Put(bytes, 14, 2, 2);
    Expect(bytes, PacketError::None, "capture payload accepted only on capture path", {render.generation, 2, 4096});
    Expect(bytes, PacketError::Direction, "capture payload cannot masquerade as render");
    Mutate(0, 0, 4, PacketError::Header, "bad magic");
    Mutate(4, 63, 4, PacketError::Header, "short declared header");
    Mutate(4, 65, 4, PacketError::Header, "unknown extended header");
    Mutate(8, 2, 2, PacketError::Version, "unknown major");
    Mutate(10, 1, 2, PacketError::Version, "unnegotiated minor");
    Mutate(12, 2, 2, PacketError::Direction, "opcode direction mismatch");
    Mutate(12, 99, 2, PacketError::Direction, "unknown opcode");
    Mutate(14, 0, 2, PacketError::Direction, "unknown packet direction");
    Mutate(16, 0, 8, PacketError::Generation, "zero packet generation");
    Mutate(16, 42, 8, PacketError::Generation, "stale generation");
    Mutate(40, 0, 4, PacketError::Frames, "zero-frame packet");
    Mutate(40, 4097, 4, PacketError::Frames, "oversize frame count");
    Mutate(40, 0xffffffffULL, 4, PacketError::Frames, "overflow-sized frame count");
    Mutate(44, 44100, 4, PacketError::Format, "unnegotiated sample rate");
    Mutate(48, 0, 2, PacketError::Format, "zero channels");
    Mutate(48, 65535, 2, PacketError::Format, "overflow-sized channels");
    Mutate(50, 3, 2, PacketError::Format, "unnegotiated sample format");
    Mutate(52, 15, 4, PacketError::Payload, "partial frame byte count");
    Mutate(52, 0xffffffffULL, 4, PacketError::Payload, "overflow-sized payload count");
    Mutate(56, 4, 4, PacketError::Flags, "unknown flags");
    Mutate(60, 1, 4, PacketError::Reserved, "reserved field nonzero");
    Mutate(32, 1, 8, PacketError::Flags, "timestamp without validity flag");
    bytes = Fixture(); Put(bytes, 32, 123456789, 8); Put(bytes, 56, 3, 4);
    const auto timestamped = ValidatePacket(bytes.data(), bytes.size(), render);
    Check(timestamped.error == PacketError::None && timestamped.metadata.qpc == 123456789 &&
          timestamped.metadata.flags == 3, "timestamp and discontinuity preserved");
    bytes = Fixture(); bytes.pop_back(); Expect(bytes, PacketError::Payload, "truncated payload");
    bytes = Fixture(); bytes.push_back(0); Expect(bytes, PacketError::Payload, "trailing bytes rejected");
    bytes = Fixture(); bytes.resize(32832, 0); Put(bytes, 40, 4096, 4); Put(bytes, 52, 32768, 4);
    Expect(bytes, PacketError::None, "maximum packet accepted");
    Check(ValidatePacket(bytes.data(), (std::numeric_limits<std::size_t>::max)(), render).error == PacketError::Payload,
          "host length cannot overflow header addition");
    if (failures) return EXIT_FAILURE;
    std::puts("Native audio packet validation: all checks passed");
    return EXIT_SUCCESS;
}
