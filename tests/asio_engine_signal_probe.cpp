// Opt-in real ASIO host. This executable never emulates the engine or accesses
// its shared memory. Run send and receive in separate processes with real routes.
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <iasiodrv.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kAmplitude = 0.0005; // two tones: total peak <= -60 dBFS
struct Basis { double s1, c1, s2, c2; };
struct Context {
    ASIOBufferInfo buffer{};
    std::vector<Basis> basis;
    long frames = 0;
    bool receive = false;
    std::uint64_t warmup = 0, target = 0, position = 0, measured = 0;
    double energy = 0, sum = 0, s1 = 0, c1 = 0, s2 = 0, c2 = 0;
    std::uint64_t nonfinite = 0, zeros = 0, callbacks = 0;
    std::atomic<bool> done{false}, reset{false};
} context;
static_assert(std::atomic<bool>::is_always_lock_free);

void bufferSwitch(long index, ASIOBool) {
    auto& c = context;
    if (index < 0 || index > 1 || !c.buffer.buffers[index]) {
        c.reset.store(true, std::memory_order_relaxed);
        return;
    }
    auto* samples = static_cast<float*>(c.buffer.buffers[index]);
    ++c.callbacks;
    for (long frame = 0; frame < c.frames; ++frame) {
        const auto& b = c.basis[c.position % c.basis.size()];
        if (!c.receive) samples[frame] = static_cast<float>(kAmplitude * (b.s1 + b.s2));
        if (c.position >= c.warmup && c.measured < c.target) {
            if (c.receive) {
                const double sample = samples[frame];
                if (!std::isfinite(sample)) ++c.nonfinite;
                else {
                    c.energy += sample * sample;
                    c.sum += sample;
                    c.s1 += sample * b.s1; c.c1 += sample * b.c1;
                    c.s2 += sample * b.s2; c.c2 += sample * b.c2;
                    if (sample == 0) ++c.zeros;
                }
            }
            ++c.measured;
        }
        ++c.position;
    }
    if (c.measured == c.target) c.done.store(true, std::memory_order_release);
}
void rateChanged(ASIOSampleRate) { context.reset.store(true, std::memory_order_relaxed); }
long messages(long selector, long value, void*, double*) {
    if (selector == kAsioSelectorSupported)
        return value == kAsioResetRequest || value == kAsioResyncRequest || value == kAsioEngineVersion;
    if (selector == kAsioEngineVersion) return 2;
    if (selector == kAsioResetRequest || selector == kAsioResyncRequest) {
        context.reset.store(true, std::memory_order_relaxed); return 1;
    }
    return 0;
}
ASIOTime* timedSwitch(ASIOTime*, long index, ASIOBool direct) {
    bufferSwitch(index, direct); return nullptr;
}
struct Driver {
    HMODULE module = nullptr;
    IASIO* asio = nullptr;
    bool buffers = false, started = false;
    ~Driver() {
        if (asio) {
            if (started) asio->stop();
            if (buffers) asio->disposeBuffers();
            asio->Release();
        }
        if (module) FreeLibrary(module);
    }
};
}

int wmain(int argc, wchar_t** argv) {
    // The stdin barrier is deliberately mandatory: the orchestrator installs
    // the route only after observing ready, then sends "start\n" to both hosts.
    if (argc != 7 || std::wstring(argv[1]) != L"--dll" ||
        std::wstring(argv[3]) != L"--role" || std::wstring(argv[5]) != L"--seconds" ||
        (std::wstring(argv[4]) != L"send" && std::wstring(argv[4]) != L"receive")) {
        std::fprintf(stderr, "Usage: AsioEngineSignalProbe --dll ABSOLUTE_DLL --role send|receive --seconds 1..300\n");
        return 2;
    }
    wchar_t* end = nullptr;
    const long seconds = std::wcstol(argv[6], &end, 10);
    if (!end || *end || seconds < 1 || seconds > 300) return 2;
    const std::wstring dllPath(argv[2]);
    if (dllPath.size() < 3 || dllPath[1] != L':' || (dllPath[2] != L'\\' && dllPath[2] != L'/')) return 2;
    Driver driver;
    driver.module = LoadLibraryExW(argv[2], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!driver.module) { std::fprintf(stderr, "DLL load error %lu\n", GetLastError()); return 3; }
    using GetClassObject = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    const auto factoryFn = reinterpret_cast<GetClassObject>(GetProcAddress(driver.module, "DllGetClassObject"));
    CLSID clsid{};
    CLSIDFromString(L"{A4D39126-78CB-4D89-9E0A-54494D4F5856}", &clsid);
    IClassFactory* factory = nullptr;
    if (!factoryFn || FAILED(factoryFn(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory)))) return 4;
    const auto created = factory->CreateInstance(nullptr, clsid, reinterpret_cast<void**>(&driver.asio));
    factory->Release();
    if (FAILED(created) || !driver.asio || driver.asio->init(nullptr) != ASIOTrue) return 5;
    ASIOSampleRate rate = 0;
    long minimum = 0, maximum = 0, granularity = 0;
    if (driver.asio->getSampleRate(&rate) != ASE_OK || !std::isfinite(rate) ||
        rate < 8000 || rate > 384000 || std::floor(rate) != rate ||
        driver.asio->getBufferSize(&minimum, &maximum, &context.frames, &granularity) != ASE_OK ||
        context.frames < 1 || context.frames > 8192) return 6;
    context.receive = std::wstring(argv[4]) == L"receive";
    ASIOChannelInfo channel{};
    channel.channel = 0; channel.isInput = context.receive ? ASIOTrue : ASIOFalse;
    if (driver.asio->getChannelInfo(&channel) != ASE_OK || channel.type != ASIOSTFloat32LSB) return 7;
    context.warmup = static_cast<std::uint64_t>(rate) * 2;
    context.target = static_cast<std::uint64_t>(rate) * seconds;
    context.basis.resize(static_cast<std::size_t>(rate));
    for (std::size_t i = 0; i < context.basis.size(); ++i) {
        const double angle = 2 * kPi * i / rate;
        context.basis[i] = {std::sin(997 * angle), std::cos(997 * angle),
            std::sin(1709 * angle), std::cos(1709 * angle)};
    }
    context.buffer.isInput = channel.isInput;
    context.buffer.channelNum = 0;
    ASIOCallbacks callbacks{bufferSwitch, rateChanged, messages, timedSwitch};
    if (driver.asio->createBuffers(&context.buffer, 1, context.frames, &callbacks) != ASE_OK) return 8;
    driver.buffers = true;
    if (!context.receive) for (auto* buffer : context.buffer.buffers)
        std::fill_n(static_cast<float*>(buffer), context.frames, 0.0f);
    std::printf("{\"event\":\"ready\",\"pid\":%lu,\"role\":\"%s\",\"sampleRate\":%.0f,\"frames\":%ld}\n",
        GetCurrentProcessId(), context.receive ? "receive" : "send", rate, context.frames);
    std::fflush(stdout);
    char command[32]{};
    if (!std::fgets(command, sizeof(command), stdin) || std::string(command) != "start\n") return 9;
    if (driver.asio->start() != ASE_OK) return 10;
    driver.started = true;
    const auto deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds + 12) * 1000;
    while (!context.done.load(std::memory_order_acquire) && !context.reset.load(std::memory_order_relaxed) &&
        GetTickCount64() < deadline) Sleep(10);
    const bool stopped = driver.asio->stop() == ASE_OK;
    if (stopped) driver.started = false;
    // stop joins the callback worker before reading its non-atomic aggregates.
    if (!stopped) return 11;
    const double n = static_cast<double>(context.measured);
    const double a1 = n ? 2 * std::hypot(context.s1, context.c1) / n : 0;
    const double a2 = n ? 2 * std::hypot(context.s2, context.c2) / n : 0;
    const double power = n ? context.energy / n : 0;
    const double tonePower = (a1 * a1 + a2 * a2) / 2;
    const double residual = std::fmax(1e-30, power - tonePower);
    const double snr = tonePower > 0 ? 10 * std::log10(tonePower / residual) : -300;
    const bool passed = context.done.load() && !context.reset.load() && !context.nonfinite &&
        (!context.receive || (std::abs(a1 / kAmplitude - 1) < 0.02 &&
            std::abs(a2 / kAmplitude - 1) < 0.02 && snr > 50));
    std::printf("{\"event\":\"result\",\"passed\":%s,\"callbacks\":%llu,\"measuredFrames\":%llu,"
        "\"zeros\":%llu,\"nonfinite\":%llu,\"reset\":%s,\"amplitude997\":%.12g,\"amplitude1709\":%.12g,"
        "\"rms\":%.12g,\"snrDb\":%.9g}\n", passed ? "true" : "false",
        static_cast<unsigned long long>(context.callbacks), static_cast<unsigned long long>(context.measured),
        static_cast<unsigned long long>(context.zeros), static_cast<unsigned long long>(context.nonfinite),
        context.reset.load() ? "true" : "false", a1, a2, std::sqrt(power), snr);
    return passed ? 0 : 12;
}
