// Project-added command-line parsing shared by developer executables.
#pragma once
#include <cerrno>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace a3dtools {
class CommandLine {
public:
    CommandLine(int argc, char** argv, const char* values, const char* flags) {
        const std::string value_names = std::string(" ") + values + " ";
        const std::string flag_names = std::string(" ") + flags + " help ";
        for (int i = 1; i < argc; ++i) {
            std::string argument = argv[i];
            if (argument == "-h") argument = "--help";
            if (argument.compare(0, 2, "--") != 0) {
                positional.push_back(argument);
                continue;
            }
            const std::string name = argument.substr(2);
            if (options.count(name)) throw std::runtime_error("Repeated option: " + argument);
            const std::string token = " " + name + " ";
            if (flag_names.find(token) != std::string::npos) options[name] = "1";
            else if (value_names.find(token) != std::string::npos) {
                if (++i == argc || std::string(argv[i]).compare(0, 2, "--") == 0)
                    throw std::runtime_error("Missing value: " + argument);
                options[name] = argv[i];
            } else throw std::runtime_error("Unknown option: " + argument);
        }
        if (!positional.empty() && !options.empty())
            throw std::runtime_error("Do not mix named options and legacy positional arguments");
    }
    bool Has(const char* name) const { return options.count(name) != 0; }
    std::string Get(const char* name, const char* fallback = "") const {
        const auto entry = options.find(name);
        return entry == options.end() ? fallback : entry->second;
    }
    unsigned Number(const char* name, unsigned fallback, unsigned minimum, unsigned maximum) const {
        if (!Has(name)) return fallback;
        const std::string text = Get(name);
        char* end = nullptr;
        errno = 0;
        unsigned long value = std::strtoul(text.c_str(), &end, 10);
        if (text.empty() || text[0] == '-' || *end || errno || value < minimum || value > maximum)
            throw std::runtime_error(std::string("Invalid --") + name + " value: " + text);
        return static_cast<unsigned>(value);
    }
    std::vector<std::string> positional;
private:
    std::map<std::string, std::string> options;
};

inline int InvokeLegacy(int (*entry)(int, char**), std::vector<std::string> arguments) {
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(&argument[0]);
    pointers.push_back(nullptr);
    return entry(static_cast<int>(arguments.size()), pointers.data());
}
} // namespace a3dtools
