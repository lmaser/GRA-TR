#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <cstdio>

#ifndef GRATR_PERF_TRACE
 #define GRATR_PERF_TRACE 0
#endif

#if GRATR_PERF_TRACE

struct PerfBlockEntry
{
    double   timestampSec;
    double   durationUs;
    int      blockSize;
    float    cpuPercent;
};

class PerfTrace
{
public:
    static constexpr int kRingSize = 8192;

    PerfTrace() = default;

    void recordBlock (double durationUs, int blockSize, double sampleRate) noexcept
    {
        const double blockDurationSec = static_cast<double>(blockSize) / sampleRate;
        const double blockDurationUs  = blockDurationSec * 1'000'000.0;
        const float  cpuPct = (blockDurationUs > 0.0)
                                ? static_cast<float>((durationUs / blockDurationUs) * 100.0)
                                : 0.0f;

        const int idx = writeIndex.fetch_add (1, std::memory_order_relaxed) & (kRingSize - 1);
        ring[idx].timestampSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
        ring[idx].durationUs   = durationUs;
        ring[idx].blockSize    = blockSize;
        ring[idx].cpuPercent   = cpuPct;

        float prev = peakCpuPercent.load (std::memory_order_relaxed);
        while (cpuPct > prev)
        {
            if (peakCpuPercent.compare_exchange_weak (prev, cpuPct, std::memory_order_relaxed))
                break;
        }
    }

    float getPeakCpuPercent() const noexcept { return peakCpuPercent.load (std::memory_order_relaxed); }
    void resetPeak() noexcept { peakCpuPercent.store (0.0f, std::memory_order_relaxed); }

    bool dumpToFile (const juce::String& filePath) const
    {
        juce::File f (filePath);
        if (auto stream = f.createOutputStream())
        {
            stream->writeText ("timestamp_s,duration_us,block_size,cpu_percent\n", false, false, nullptr);
            const int total = juce::jmin (writeIndex.load (std::memory_order_relaxed), kRingSize);
            const int startIdx = writeIndex.load (std::memory_order_relaxed) - total;
            for (int i = 0; i < total; ++i)
            {
                const auto& e = ring[(startIdx + i) & (kRingSize - 1)];
                juce::String line;
                line << juce::String (e.timestampSec, 6) << ","
                     << juce::String (e.durationUs, 2) << ","
                     << e.blockSize << ","
                     << juce::String (e.cpuPercent, 3) << "\n";
                stream->writeText (line, false, false, nullptr);
            }
            stream->flush();
            return true;
        }
        return false;
    }

    juce::String getSummary() const
    {
        const int total = juce::jmin (writeIndex.load (std::memory_order_relaxed), kRingSize);
        if (total == 0) return "No data";
        double sumUs = 0.0, maxUs = 0.0;
        float  maxCpu = 0.0f;
        const int startIdx = writeIndex.load (std::memory_order_relaxed) - total;
        for (int i = 0; i < total; ++i)
        {
            const auto& e = ring[(startIdx + i) & (kRingSize - 1)];
            sumUs += e.durationUs;
            if (e.durationUs > maxUs)  maxUs  = e.durationUs;
            if (e.cpuPercent > maxCpu) maxCpu = e.cpuPercent;
        }
        const double avgUs = sumUs / total;
        juce::String s;
        s << "Blocks: " << total
          << "  Avg: " << juce::String (avgUs, 1) << " us"
          << "  Peak: " << juce::String (maxUs, 1) << " us"
          << "  Peak CPU: " << juce::String (maxCpu, 2) << "%";
        return s;
    }

    void setAutoDumpPath (const juce::String& path) { autoDumpPath = path; }

    ~PerfTrace()
    {
        if (autoDumpPath.isNotEmpty() && writeIndex.load (std::memory_order_relaxed) > 0)
            dumpToFile (autoDumpPath);
    }

    void enableDesktopAutoDump (const juce::String& filename = "gratr_perf_dump.csv")
    {
        auto desktop = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
        setAutoDumpPath (desktop.getChildFile (filename).getFullPathName());
    }

private:
    PerfBlockEntry ring[kRingSize] {};
    std::atomic<int> writeIndex { 0 };
    std::atomic<float> peakCpuPercent { 0.0f };
    juce::String autoDumpPath;
};

#define PERF_BLOCK_BEGIN()  const auto _perfStart = juce::Time::getHighResolutionTicks()
#define PERF_BLOCK_END(tracer, nSamples, sr)  do { \
        const auto _perfEnd = juce::Time::getHighResolutionTicks(); \
        const double _perfUs = juce::Time::highResolutionTicksToSeconds (_perfEnd - _perfStart) * 1000000.0; \
        (tracer).recordBlock (_perfUs, (nSamples), (sr)); \
    } while(0)

#else

class PerfTrace
{
public:
    float getPeakCpuPercent() const noexcept { return 0.0f; }
    void resetPeak() noexcept {}
    bool dumpToFile (const juce::String&) const { return false; }
    juce::String getSummary() const { return {}; }
    void setAutoDumpPath (const juce::String&) {}
    void enableDesktopAutoDump (const juce::String& = {}) {}
};

#define PERF_BLOCK_BEGIN()           ((void)0)
#define PERF_BLOCK_END(tracer, n, sr) ((void)0)

#endif
