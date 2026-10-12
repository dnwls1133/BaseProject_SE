#include "stdafx.h"
#include "WorldMonitor.h"
#include <windows.h>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <thread>

namespace
{
class Writer
{
  public:
    Writer()
        : worker(
              [this]
              {
                  Run();
              })
    {
    }

    ~Writer()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        ready.notify_one();
        worker.join();
    }

    void Submit(std::filesystem::path path, std::string text)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            directory = std::move(path);
            pending = std::move(text);
        }
        ready.notify_one();
    }

  private:
    std::mutex mutex;
    std::condition_variable ready;
    std::filesystem::path directory;
    std::string pending;
    bool stopping = false;
    std::thread worker;

    void Run()
    {
        bool reported = false;
        for (;;)
        {
            std::filesystem::path destination;
            std::string snapshot;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready.wait(lock,
                           [this]
                           {
                               return stopping || !pending.empty();
                           });
                if (pending.empty() && stopping)
                {
                    return;
                }
                destination = directory;
                snapshot.swap(pending);
            }
            try
            {
                std::filesystem::create_directories(destination);
                const auto temporary = destination / "snapshot.pending";
                const auto final = destination / "snapshot.json";
                {
                    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                    out.write(snapshot.data(), static_cast<std::streamsize>(snapshot.size()));
                    out.close();
                    if (!out)
                    {
                        throw std::runtime_error("snapshot write failed");
                    }
                }
                if (!MoveFileExW(temporary.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING))
                {
                    throw std::runtime_error("snapshot replacement failed");
                }
                reported = false;
            }
            catch (const std::exception& error)
            {
                // The worker never reads game state or calls the game-thread profiler.
                if (!reported)
                {
                    std::cerr << "[world_monitor_write_error] " << error.what() << '\n';
                    reported = true;
                }
            }
        }
    }
};
} // namespace

void WorldMonitor::Publish(std::filesystem::path directory, std::string json)
{
    static Writer writer;
    writer.Submit(std::move(directory), std::move(json));
}
