#include "stdafx.h"
#include "Profiler.h"
#include "Dependencies/glew.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <memory>

#pragma comment(lib, "psapi.lib")

namespace Profile
{
namespace
{
constexpr std::size_t MetricCount = static_cast<std::size_t>(Metric::Count);
constexpr unsigned MarkerCount = 7;
constexpr std::uint64_t PartLimit = 8 * 1024 * 1024;
const char* const Names[] = {
#define PROFILE_NAME(id, key) #key,
    PROFILE_METRICS(PROFILE_NAME)
#undef PROFILE_NAME
};
const char* const GpuNames[] = {
    "gpu_world_ms",          "gpu_msaa_resolve_ms", "gpu_bloom_horizontal_ms",
    "gpu_bloom_vertical_ms", "gpu_postprocess_ms",  "gpu_ui_ms",
    "gpu_frame_ms"};

struct Distribution
{
    std::vector<double> values;
    double sum = 0;
    double maximum = 0;

    void Push(double value)
    {
        values.push_back(value);
        sum += value;
        maximum = std::max(maximum, value);
    }

    void Clear()
    {
        values.clear();
        sum = maximum = 0;
    }
};

struct QueryFrame
{
    GLuint ids[MarkerCount] = {};
    std::uint64_t frame = 0;
    unsigned markers = 0;
    bool pending = false;
};

struct ReportData;

struct Output
{
    std::string json;
    std::string console;
    std::unique_ptr<ReportData> report;
};

struct ModelTotals
{
    double cpuMs = 0;
    std::uint64_t draws = 0, vertices = 0;
    bool isStatic = false;
};

struct ReportData
{
    bool final = false;
    double elapsedMs = 0, windowElapsedMs = 0;
    std::uint64_t frame = 0, windowFrames = 0, missedBudget = 0, severeFrames = 0;
    std::uint64_t worstFrame = 0, gpuFirstFrame = 0, gpuLastFrame = 0, gpuWorstFrame = 0;
    unsigned maxQueryAge = 0;
    std::array<double, MetricCount> worst{};
    std::array<Distribution, MetricCount> distributions;
    std::array<Distribution, MarkerCount> gpuDistributions;
    std::unordered_map<std::string, ModelTotals> models;
};

struct State
{
    bool enabled = false;
    bool gpuSupported = false;
    bool gameplay = false;
    bool hasPresentation = false;
    std::uint64_t frame = 0, windowFrames = 0, missedBudget = 0, severeFrames = 0;
    std::uint64_t worstFrame = 0, gpuFirstFrame = 0, gpuLastFrame = 0, gpuWorstFrame = 0;
    double worstInterval = 0, gpuWorstTime = 0;
    unsigned maxQueryAge = 0;
    std::array<double, MetricCount> current{}, worst{};
    std::array<Distribution, MetricCount> distributions;
    std::array<Distribution, MarkerCount> gpuDistributions;
    std::array<QueryFrame, 8> queries;
    std::unordered_map<std::string, ModelTotals> models;
    int activeQuery = -1;
    Clock::time_point sessionStart, windowStart, lastPresentation;
    std::filesystem::path directory;
    std::string session, metadata;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Output> output;
    std::unique_ptr<ReportData> recycledReport;
    std::thread writer;
    bool stopping = false;
    std::atomic<unsigned> dropped{0};
    std::atomic<double> writerLastMs{0};
    std::atomic<bool> fileFailed{false};
    unsigned logicalProcessors = 1;
    std::uint64_t previousProcessTicks = 0;
    Clock::time_point processSampleTime;
};

State state;

std::string Quote(const std::string& text)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out << '\\' << c;
        }
        else if (c < 32)
        {
            out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
        }
        else
        {
            out << c;
        }
    }
    out << '"';
    return out.str();
}

std::ostringstream JsonStream()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(4);
    return out;
}

double SessionMs()
{
    return std::chrono::duration<double, std::milli>(Clock::now() - state.sessionStart).count();
}

void Queue(std::string json, std::string console = {})
{
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.output.size() >= 8)
    {
        ++state.dropped;
        return;
    }
    state.output.push_back({std::move(json), std::move(console)});
    state.wake.notify_one();
}

void SerializeReport(Output& item);

void WriteLoop()
{
    std::error_code error;
    std::filesystem::create_directories(state.directory, error);
    std::ofstream file;
    unsigned part = 0;
    std::uint64_t bytes = 0;
    auto open = [&]()
    {
        file.close();
        file.clear();
        file.open(state.directory / (state.session + "-" + std::to_string(part) + ".jsonl"),
                  std::ios::binary | std::ios::trunc);
        if (file)
        {
            file << state.metadata << '\n';
            bytes = state.metadata.size() + 1;
        }
        return static_cast<bool>(file);
    };
    if (error || !open())
    {
        state.fileFailed = true;
        std::cerr << "[Profile] Cannot create performance log. Console summaries remain active.\n";
    }
    for (;;)
    {
        Output item;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.wake.wait(lock,
                            []
                            {
                                return state.stopping || !state.output.empty();
                            });
            if (state.output.empty() && state.stopping)
            {
                break;
            }
            item = std::move(state.output.front());
            state.output.pop_front();
        }
        auto start = Clock::now();
        if (item.report)
        {
            SerializeReport(item);
        }
        if (!state.fileFailed)
        {
            if (bytes + item.json.size() + 1 > PartLimit)
            {
                part = (part + 1) % 4;
                if (!open())
                {
                    state.fileFailed = true;
                }
            }
            if (!state.fileFailed)
            {
                file << item.json << '\n';
                file.flush();
                bytes += item.json.size() + 1;
                if (!file)
                {
                    state.fileFailed = true;
                    std::cerr << "[Profile] Log write failed. Console summaries remain active.\n";
                }
            }
        }
        if (!item.console.empty())
        {
            std::cout << item.console << '\n';
        }
        state.writerLastMs =
            std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (item.report)
        {
            for (auto& distribution : item.report->distributions)
            {
                distribution.Clear();
            }
            for (auto& distribution : item.report->gpuDistributions)
            {
                distribution.Clear();
            }
            // Reuse allocated sample buffers on the next reporting window.
            std::lock_guard<std::mutex> guard(state.mutex);
            state.recycledReport = std::move(item.report);
        }
    }
}

void WriteDistribution(std::ostream& out, Distribution& distribution)
{
    if (distribution.values.empty())
    {
        out << "null";
        return;
    }
    auto index = static_cast<std::size_t>(std::ceil(distribution.values.size() * .95)) - 1;
    std::nth_element(distribution.values.begin(), distribution.values.begin() + index,
                     distribution.values.end());
    out << "{\"samples\":" << distribution.values.size()
        << ",\"mean\":" << distribution.sum / distribution.values.size()
        << ",\"p95\":" << distribution.values[index] << ",\"max\":" << distribution.maximum
        << ",\"sum\":" << distribution.sum << '}';
}

void PollGpu()
{
    if (!state.gpuSupported)
    {
        return;
    }
    for (auto& query : state.queries)
    {
        if (!query.pending)
        {
            continue;
        }
        GLint available = GL_FALSE;
        glGetQueryObjectiv(query.ids[MarkerCount - 1], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
        {
            continue;
        }
        GLuint64 stamps[MarkerCount] = {};
        for (unsigned i = 0; i < MarkerCount; ++i)
        {
            glGetQueryObjectui64v(query.ids[i], GL_QUERY_RESULT, &stamps[i]);
        }
        query.pending = false;
        bool valid = query.markers == (1u << MarkerCount) - 1;
        for (unsigned i = 1; i < MarkerCount; ++i)
        {
            valid = valid && stamps[i] >= stamps[i - 1];
        }
        if (!valid)
        {
            continue;
        }
        for (unsigned i = 0; i + 1 < MarkerCount; ++i)
        {
            state.gpuDistributions[i].Push((stamps[i + 1] - stamps[i]) / 1000000.0);
        }
        double total = (stamps[MarkerCount - 1] - stamps[0]) / 1000000.0;
        state.gpuDistributions.back().Push(total);
        if (!state.gpuFirstFrame || query.frame < state.gpuFirstFrame)
        {
            state.gpuFirstFrame = query.frame;
        }
        state.gpuLastFrame = std::max(state.gpuLastFrame, query.frame);
        state.maxQueryAge = std::max(state.maxQueryAge, unsigned(state.frame - query.frame));
        if (total > state.gpuWorstTime)
        {
            state.gpuWorstTime = total;
            state.gpuWorstFrame = query.frame;
        }
    }
}

void SerializeReport(Output& item)
{
    auto& snapshot = *item.report;
    auto out = JsonStream();
    auto& intervals = snapshot.distributions[static_cast<std::size_t>(Metric::FrameInterval)];
    double fps = intervals.sum > 0 ? intervals.values.size() * 1000.0 / intervals.sum : 0;
    out << "{\"schema_version\":1,\"type\":\"window\",\"elapsed_ms\":" << snapshot.elapsedMs
        << ",\"window_elapsed_ms\":" << snapshot.windowElapsedMs
        << ",\"final\":" << (snapshot.final ? "true" : "false")
        << ",\"frame_first\":" << snapshot.frame - snapshot.windowFrames + 1
        << ",\"frame_last\":" << snapshot.frame << ",\"frame_count\":" << snapshot.windowFrames
        << ",\"fps\":";
    if (intervals.values.empty())
    {
        out << "null";
    }
    else
    {
        out << fps;
    }
    out << ",\"target_frame_ms\":16.6667,\"over_budget_frame_count\":" << snapshot.missedBudget
        << ",\"over_33ms_frame_count\":" << snapshot.severeFrames << ",\"per_frame\":{";
    for (std::size_t i = 0; i < MetricCount; ++i)
    {
        out << (i ? "," : "") << Quote(Names[i]) << ':';
        WriteDistribution(out, snapshot.distributions[i]);
    }
    out << "},\"models_window_totals\":{";
    bool firstModel = true;
    for (auto& entry : snapshot.models)
    {
        auto& model = entry.second;
        if (!model.draws)
        {
            continue;
        }
        out << (firstModel ? "" : ",") << Quote(entry.first) << ":{\"cpu_draw_ms\":" << model.cpuMs
            << ",\"draw_count\":" << model.draws
            << ",\"transformed_vertex_count\":" << model.vertices
            << ",\"static\":" << (model.isStatic ? "true" : "false") << '}';
        firstModel = false;
        model = {};
    }
    out << "},\"gpu_completed_samples\":{";
    for (unsigned i = 0; i < MarkerCount; ++i)
    {
        out << (i ? "," : "") << Quote(GpuNames[i]) << ':';
        WriteDistribution(out, snapshot.gpuDistributions[i]);
    }
    out << "},\"gpu_source_frame_first\":" << snapshot.gpuFirstFrame
        << ",\"gpu_source_frame_last\":" << snapshot.gpuLastFrame
        << ",\"gpu_result_max_age_frames\":" << snapshot.maxQueryAge
        << ",\"gpu_worst_source_frame\":" << snapshot.gpuWorstFrame
        << ",\"worst_present_frame\":{\"frame_id\":" << snapshot.worstFrame << ",\"metrics\":{";
    for (std::size_t i = 0; i < MetricCount; ++i)
    {
        out << (i ? "," : "") << Quote(Names[i]) << ':' << snapshot.worst[i];
    }
    out << "}},\"log_dropped_record_count\":" << state.dropped.exchange(0)
        << ",\"log_writer_last_record_ms\":" << state.writerLastMs.load()
        << ",\"log_file_failed\":" << (state.fileFailed ? "true" : "false");
    PROCESS_MEMORY_COUNTERS_EX memory = {};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
    {
        out << ",\"process_working_set_bytes\":" << memory.WorkingSetSize
            << ",\"process_private_commit_bytes\":" << memory.PrivateUsage;
    }
    else
    {
        out << ",\"process_working_set_bytes\":null,\"process_private_commit_bytes\":null";
    }
    FILETIME created, exited, kernel, user;
    out << ",\"process_cpu_core_equivalents\":";
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    {
        auto ticks = [](FILETIME time)
        {
            return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        };
        auto currentTicks = ticks(kernel) + ticks(user);
        auto now = Clock::now();
        double elapsed = std::chrono::duration<double>(now - state.processSampleTime).count();
        if (state.previousProcessTicks && elapsed > 0)
        {
            out << (currentTicks - state.previousProcessTicks) / (elapsed * 10000000.0);
        }
        else
        {
            out << "null";
        }
        state.previousProcessTicks = currentTicks;
        state.processSampleTime = now;
    }
    else
    {
        out << "null";
    }
    out << ",\"process_sample_elapsed_ms\":" << SessionMs() << '}';
    auto console = JsonStream();
    const auto& calls = snapshot.distributions[static_cast<std::size_t>(Metric::DrawCalls)];
    console << std::setprecision(1) << "[Profile] FPS=" << fps << " frame_p95_ms=";
    if (!intervals.values.empty())
    {
        console
            << intervals
                   .values[static_cast<std::size_t>(std::ceil(intervals.values.size() * .95)) - 1];
    }
    else
    {
        console << "pending";
    }
    console << " draw_calls_mean=" << calls.sum / snapshot.windowFrames << " gpu_ms=";
    const auto& gpu = snapshot.gpuDistributions.back();
    if (gpu.values.empty())
    {
        console << "unavailable_or_pending";
    }
    else
    {
        console << gpu.sum / gpu.values.size();
    }
    console << " over_budget=" << snapshot.missedBudget << '/' << intervals.values.size();
    item.json = out.str();
    item.console = console.str();
}

void Report(bool final)
{
    if (!state.windowFrames)
    {
        return;
    }
    std::unique_ptr<ReportData> snapshot;
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        snapshot = std::move(state.recycledReport);
    }
    if (!snapshot)
    {
        snapshot = std::make_unique<ReportData>();
    }
    snapshot->final = final;
    snapshot->elapsedMs = SessionMs();
    snapshot->windowElapsedMs =
        std::chrono::duration<double, std::milli>(Clock::now() - state.windowStart).count();
    snapshot->frame = state.frame;
    snapshot->windowFrames = state.windowFrames;
    snapshot->missedBudget = state.missedBudget;
    snapshot->severeFrames = state.severeFrames;
    snapshot->worstFrame = state.worstFrame;
    snapshot->gpuFirstFrame = state.gpuFirstFrame;
    snapshot->gpuLastFrame = state.gpuLastFrame;
    snapshot->gpuWorstFrame = state.gpuWorstFrame;
    snapshot->maxQueryAge = state.maxQueryAge;
    snapshot->worst = state.worst;
    snapshot->distributions.swap(state.distributions);
    snapshot->gpuDistributions.swap(state.gpuDistributions);
    snapshot->models.swap(state.models);
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (state.output.size() < 8)
        {
            state.output.push_back({{}, {}, std::move(snapshot)});
            state.wake.notify_one();
        }
        else
        {
            ++state.dropped;
        }
    }
    for (auto& distribution : state.distributions)
    {
        distribution.Clear();
        if (distribution.values.capacity() < 256)
        {
            distribution.values.reserve(256);
        }
    }
    for (auto& distribution : state.gpuDistributions)
    {
        distribution.Clear();
        if (distribution.values.capacity() < 256)
        {
            distribution.values.reserve(256);
        }
    }
    state.windowFrames = state.missedBudget = state.severeFrames = 0;
    state.worstInterval = state.gpuWorstTime = 0;
    state.gpuFirstFrame = state.gpuLastFrame = state.gpuWorstFrame = 0;
    state.maxQueryAge = 0;
    state.windowStart = Clock::now();
}
} // namespace

bool Enabled()
{
    return state.enabled;
}

void Add(Metric metric, double value)
{
    if (state.enabled)
    {
        state.current[static_cast<std::size_t>(metric)] += value;
    }
}

void Set(Metric metric, double value)
{
    if (state.enabled)
    {
        state.current[static_cast<std::size_t>(metric)] = value;
    }
}

void Maximum(Metric metric, double value)
{
    if (state.enabled)
    {
        auto& current = state.current[static_cast<std::size_t>(metric)];
        current = std::max(current, value);
    }
}

void Event(const std::string& name, const std::string& detail)
{
    if (!state.enabled)
    {
        return;
    }
    auto out = JsonStream();
    out << "{\"schema_version\":1,\"type\":\"event\",\"elapsed_ms\":" << SessionMs()
        << ",\"frame_id\":" << state.frame << ",\"name\":" << Quote(name)
        << ",\"detail\":" << Quote(detail) << '}';
    Queue(out.str());
}

void RecordModel(const std::string& name, std::size_t vertices, bool isStatic, double elapsedMs)
{
    auto& totals = state.models[name];
    totals.cpuMs += elapsedMs;
    ++totals.draws;
    totals.vertices += vertices;
    totals.isStatic = isStatic;
}

void Initialize()
{
    wchar_t setting[8] = {};
    GetEnvironmentVariableW(L"HB_PROFILE", setting, 8);
    if (state.enabled || setting[0] == L'0')
    {
        return;
    }
    state.enabled = true;
    state.sessionStart = state.windowStart = Clock::now();
    SYSTEM_INFO system = {};
    GetSystemInfo(&system);
    state.logicalProcessors = system.dwNumberOfProcessors;
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    state.directory = std::filesystem::path(executable).parent_path() / "Logs" / "Performance";
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    std::ostringstream id;
    id << "profile-" << utc.wYear << '-' << unsigned(utc.wMonth) << '-' << unsigned(utc.wDay) << '-'
       << unsigned(utc.wHour) << '-' << unsigned(utc.wMinute) << '-' << unsigned(utc.wSecond) << '-'
       << GetCurrentProcessId();
    state.session = id.str();
    GLint bits = 0;
    if (GLEW_VERSION_3_3 || GLEW_ARB_timer_query)
    {
        glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
    }
    state.gpuSupported = bits > 0;
    if (state.gpuSupported)
    {
        for (auto& query : state.queries)
        {
            glGenQueries(MarkerCount, query.ids);
        }
    }
    for (auto& distribution : state.distributions)
    {
        distribution.values.reserve(256);
    }
    for (auto& distribution : state.gpuDistributions)
    {
        distribution.values.reserve(256);
    }
    auto glText = [](GLenum key)
    {
        const auto* value = glGetString(key);
        return value ? reinterpret_cast<const char*>(value) : "unavailable";
    };
    auto out = JsonStream();
    out << "{\"schema_version\":1,\"type\":\"session\",\"session_id\":" << Quote(state.session)
        << ",\"build\":"
#ifdef _DEBUG
        << "\"Debug\""
#else
        << "\"Release\""
#endif
        << ",\"build_stamp\":" << Quote(__DATE__ " " __TIME__)
        << ",\"pointer_bits\":" << sizeof(void*) * 8
        << ",\"logical_processor_count\":" << state.logicalProcessors
        << ",\"gpu_vendor\":" << Quote(glText(GL_VENDOR))
        << ",\"gpu_renderer\":" << Quote(glText(GL_RENDERER))
        << ",\"opengl_version\":" << Quote(glText(GL_VERSION))
        << ",\"gpu_timer_supported\":" << (state.gpuSupported ? "true" : "false")
        << ",\"report_processing\":\"background_snapshot_v2\""
        << ",\"cpu_timer\":\"steady_clock_inclusive_wall_ms\""
        << ",\"gpu_timer\":\"deferred_timestamp_elapsed_ms_not_utilization\""
        << ",\"window_target_ms\":1000,\"target_fps\":60,\"timer_request_ms\":16"
        << ",\"swap_interval\":null,\"log_part_limit_bytes\":" << PartLimit
        << ",\"log_ring_parts\":4,\"scope\":\"engine_instrumented_regions\"}";
    state.metadata = out.str();
    state.writer = std::thread(WriteLoop);
    std::cout << "[Profile] Logs: " << state.directory.u8string() << '/' << state.session
              << "-*.jsonl\n";
}

void StartGameplay()
{
    if (!state.enabled)
    {
        return;
    }
    auto out = JsonStream();
    out << "{\"schema_version\":1,\"type\":\"startup\",\"elapsed_ms\":" << SessionMs()
        << ",\"metrics\":{";
    for (std::size_t i = 0; i < MetricCount; ++i)
    {
        out << (i ? "," : "") << Quote(Names[i]) << ':' << state.current[i];
    }
    out << "}}";
    Queue(out.str());
    state.current.fill(0);
    state.models.clear();
    state.gameplay = true;
    state.windowStart = Clock::now();
}

void BeginFrame()
{
    if (state.enabled)
    {
        ++state.frame;
        PROFILE_SCOPE(Profiler);
        PollGpu();
    }
}

void Presented()
{
    if (!state.enabled)
    {
        return;
    }
    auto now = Clock::now();
    if (state.hasPresentation)
    {
        Set(Metric::FrameInterval,
            std::chrono::duration<double, std::milli>(now - state.lastPresentation).count());
    }
    state.lastPresentation = now;
    state.hasPresentation = true;
}

void EndFrame()
{
    if (!state.enabled || !state.gameplay)
    {
        return;
    }
    auto start = Clock::now();
    ++state.windowFrames;
    double interval = state.current[static_cast<std::size_t>(Metric::FrameInterval)];
    if (interval > state.worstInterval || state.windowFrames == 1)
    {
        state.worst = state.current;
        state.worstInterval = interval;
        state.worstFrame = state.frame;
    }
    state.missedBudget += interval > 1000.0 / 60;
    state.severeFrames += interval > 1000.0 / 30;
    for (std::size_t i = 0; i < MetricCount; ++i)
    {
        if (i != static_cast<std::size_t>(Metric::FrameInterval) || interval > 0)
        {
            state.distributions[i].Push(state.current[i]);
        }
    }
    state.current.fill(0);
    if (Clock::now() - state.windowStart >= std::chrono::seconds(1) || state.windowFrames >= 4096)
    {
        Report(false);
    }
    // Reporting happens after presentation and is attributed to the next frame's interval.
    Add(Metric::Profiler, std::chrono::duration<double, std::milli>(Clock::now() - start).count());
}

void BeginGpuFrame()
{
    if (!state.enabled || !state.gpuSupported)
    {
        return;
    }
    for (unsigned i = 0; i < state.queries.size(); ++i)
    {
        if (!state.queries[i].pending)
        {
            state.activeQuery = static_cast<int>(i);
            auto& query = state.queries[i];
            query.frame = state.frame;
            query.markers = 0;
            GpuMarker(0);
            return;
        }
    }
    Add(Metric::GpuSkipped);
}

void GpuMarker(unsigned index)
{
    if (state.activeQuery >= 0 && index < MarkerCount)
    {
        auto& query = state.queries[state.activeQuery];
        glQueryCounter(query.ids[index], GL_TIMESTAMP);
        query.markers |= 1u << index;
    }
}

void EndGpuFrame()
{
    if (state.activeQuery >= 0)
    {
        GpuMarker(MarkerCount - 1);
        state.queries[state.activeQuery].pending = true;
        state.activeQuery = -1;
    }
}

void Shutdown()
{
    if (!state.enabled)
    {
        return;
    }
    PollGpu();
    Report(true);
    unsigned pending = 0;
    for (auto& query : state.queries)
    {
        pending += query.pending;
        if (state.gpuSupported)
        {
            glDeleteQueries(MarkerCount, query.ids);
        }
    }
    Event("session_end", "gpu_pending_not_waited=" + std::to_string(pending));
    state.enabled = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.stopping = true;
    }
    state.wake.notify_one();
    if (state.writer.joinable())
    {
        state.writer.join();
    }
}
} // namespace Profile
