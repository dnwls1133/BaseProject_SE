#pragma once
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

// Small, versioned UTF-8 records shared by world, model and gameplay content.
namespace ContentData
{
inline void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

inline std::string Path(const std::string& root, const std::string& relative)
{
    std::filesystem::path path(relative);
    Require(!relative.empty() && !path.has_root_path(), "relative content path required");
    for (const auto& part : path)
    {
        Require(part != "..", "parent paths are not allowed in content references");
    }
    return (std::filesystem::path(root) / path).string();
}

inline std::string Read(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(static_cast<bool>(file), path + ": cannot open content file");
    auto size = file.tellg();
    Require(size > 0 && size <= 1024 * 1024, path + ": invalid content size");
    std::string text(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    Require(static_cast<bool>(file), path + ": incomplete content read");
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
    {
        text.erase(0, 3);
    }
    return text;
}

class Row
{
  public:
    explicit Row(const std::string& text) : stream(text)
    {
    }

    std::string Word()
    {
        std::string value;
        Require(static_cast<bool>(stream >> std::quoted(value)), "missing value");
        Require(!value.empty(), "empty value");
        return value;
    }

    float Number(float minimum, float maximum)
    {
        std::string word = Word();
        std::size_t end = 0;
        float value = std::stof(word, &end);
        Require(end == word.size() && std::isfinite(value) && value >= minimum && value <= maximum,
                "number out of range: " + word);
        return value;
    }

    int Integer(int minimum, int maximum)
    {
        float value = Number(static_cast<float>(minimum), static_cast<float>(maximum));
        Require(std::floor(value) == value, "integer required");
        return static_cast<int>(value);
    }

    void End()
    {
        stream >> std::ws;
        Require(stream.eof() || stream.peek() == '#', "unexpected trailing values");
    }

  private:
    std::istringstream stream;
};

inline void Rows(const std::string& path, const std::string& text,
                 const std::function<void(const std::string&, Row&)>& consume)
{
    std::istringstream file(text);
    std::string line;
    int lineNumber = 0;
    bool version = false;
    while (std::getline(file, line))
    {
        ++lineNumber;
        auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#')
        {
            continue;
        }
        try
        {
            Row row(line);
            std::string command = row.Word();
            if (!version)
            {
                Require(command == "version" && row.Integer(1, 1) == 1, "version 1 required");
                version = true;
            }
            else
            {
                consume(command, row);
            }
            row.End();
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    Require(version, path + ": missing version");
}

inline void Load(const std::string& path,
                 const std::function<void(const std::string&, Row&)>& consume)
{
    Rows(path, Read(path), consume);
}
} // namespace ContentData
