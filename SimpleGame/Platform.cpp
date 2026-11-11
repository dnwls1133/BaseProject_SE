#include "stdafx.h"
#include "Platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#if defined(_WIN32)
#include <Windows.h>
#include <Psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <sys/resource.h>
#include <unistd.h>
#else
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

namespace Platform
{
std::filesystem::path ExecutablePath()
{
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        throw std::runtime_error("cannot locate executable path");
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
#elif defined(__APPLE__)
    std::uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::vector<char> buffer(length + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &length) != 0)
    {
        throw std::runtime_error("cannot locate executable path");
    }
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(buffer.data(), error);
    return error ? std::filesystem::path(buffer.data()) : path;
#else
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error)
    {
        throw std::runtime_error("cannot locate executable path: " + error.message());
    }
    return path;
#endif
}

std::filesystem::path ExecutableDirectory()
{
    return ExecutablePath().parent_path();
}

std::uint64_t ProcessId()
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::optional<std::uint64_t> ProcessCpuTime100ns()
{
#if defined(_WIN32)
    FILETIME created = {}, exited = {}, kernel = {}, user = {};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    {
        return std::nullopt;
    }

    const auto ticks = [](FILETIME time)
    {
        ULARGE_INTEGER value = {};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return static_cast<std::uint64_t>(value.QuadPart);
    };

    return ticks(kernel) + ticks(user);
#else
    rusage usage = {};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return std::nullopt;
    }

    const auto to100Nanoseconds = [](const timeval& value)
    {
        return static_cast<std::uint64_t>(value.tv_sec) * 10000000ull +
               static_cast<std::uint64_t>(value.tv_usec) * 10ull;
    };

    return to100Nanoseconds(usage.ru_utime) + to100Nanoseconds(usage.ru_stime);
#endif
}

MemoryUsage GetMemoryUsage()
{
    MemoryUsage result;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)))
    {
        result.workingSetBytes = static_cast<std::uint64_t>(counters.WorkingSetSize);
        result.privateCommitBytes = static_cast<std::uint64_t>(counters.PrivateUsage);
    }
#elif defined(__APPLE__)
    task_vm_info_data_t info = {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS)
    {
        result.workingSetBytes = static_cast<std::uint64_t>(info.resident_size);
        result.privateCommitBytes = static_cast<std::uint64_t>(info.phys_footprint);
    }
#else
    std::ifstream status("/proc/self/statm");
    std::uint64_t virtualPages = 0;
    std::uint64_t residentPages = 0;
    if (status >> virtualPages >> residentPages)
    {
        static_cast<void>(virtualPages);
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize > 0 &&
            residentPages <= std::numeric_limits<std::uint64_t>::max() /
                                 static_cast<std::uint64_t>(pageSize))
        {
            result.workingSetBytes = residentPages * static_cast<std::uint64_t>(pageSize);
        }
    }
#endif
    return result;
}

unsigned int LogicalProcessorCount()
{
    const unsigned int count = std::thread::hardware_concurrency();
    return count == 0 ? 1 : count;
}

std::tm UtcTime()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm value = {};
#if defined(_WIN32)
    gmtime_s(&value, &now);
#else
    gmtime_r(&now, &value);
#endif
    return value;
}

bool AtomicReplace(const std::filesystem::path& source, const std::filesystem::path& destination,
                   std::string& error)
{
#if defined(_WIN32)
    if (MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return true;
    }
    error = "Windows error " + std::to_string(GetLastError());
    return false;
#else
    std::error_code code;
    std::filesystem::rename(source, destination, code);
    if (!code)
    {
        return true;
    }
    error = code.message();
    return false;
#endif
}

bool CopyFilePortable(const std::filesystem::path& source,
                      const std::filesystem::path& destination, std::string& error)
{
    std::error_code code;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing,
                               code);
    if (!code)
    {
        return true;
    }
    error = code.message();
    return false;
}
} // namespace Platform
