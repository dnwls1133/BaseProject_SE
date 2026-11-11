#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <ctime>

namespace Platform
{
struct MemoryUsage
{
    std::optional<std::uint64_t> workingSetBytes;
    std::optional<std::uint64_t> privateCommitBytes;
};

std::filesystem::path ExecutablePath();
std::filesystem::path ExecutableDirectory();
std::uint64_t ProcessId();
std::optional<std::uint64_t> ProcessCpuTime100ns();
MemoryUsage GetMemoryUsage();
unsigned int LogicalProcessorCount();
std::tm UtcTime();
bool AtomicReplace(const std::filesystem::path& source, const std::filesystem::path& destination,
                   std::string& error);
bool CopyFilePortable(const std::filesystem::path& source,
                      const std::filesystem::path& destination, std::string& error);
} // namespace Platform
