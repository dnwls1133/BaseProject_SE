#pragma once
#include <filesystem>
#include <string>

namespace WorldMonitor
{
// Receives immutable snapshots. The worker keeps at most one pending snapshot.
void Publish(std::filesystem::path directory, std::string json);
} // namespace WorldMonitor
