// Project-added PCM capture command. Runtime setup is implemented by capture_session.
#include "capture_internal.hpp"
#include "command_line.hpp"
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <memory>

namespace {
namespace fs = std::filesystem;
std::string Json(const std::string& value)
{
    std::ostringstream output;
    output << '"';
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') output << '\\' << ch;
        else if (ch < 32) output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(ch);
        else output << ch;
    }
    output << '"';
    return output.str();
}

std::string Sha256(const fs::path& path)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        throw std::runtime_error("Cannot initialize SHA-256 provider");
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(provider, 0);
        throw std::runtime_error("Cannot initialize SHA-256 hash");
    }
    std::ifstream input(path, std::ios::binary);
    char data[16384];
    bool ok = input.is_open();
    while (input.read(data, sizeof(data)) || input.gcount())
        if (!CryptHashData(hash, reinterpret_cast<BYTE*>(data), DWORD(input.gcount()), 0)) { ok = false; break; }
    BYTE digest[32];
    DWORD size = sizeof(digest);
    ok = ok && !input.bad() && CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!ok) throw std::runtime_error("Cannot hash DLL: " + path.string());
    std::ostringstream result;
    for (BYTE value : digest) result << std::hex << std::setw(2) << std::setfill('0') << unsigned(value);
    return result.str();
}

struct RunMetadata {
    fs::path output;
    std::string dll, wave, dll_hash, scene, arguments;
    unsigned duration_ms, timeout_ms;
    void Write(const char* status, unsigned exit_code) const {
        std::ofstream stream(output / "run.json");
        stream << "{\n  \"schema_version\": 1,\n  \"tool\": \"a3d_pcm_capture\",\n"
            << "  \"tool_version\": \"1\",\n  \"build\": " << Json(__DATE__ " " __TIME__)
            << ",\n  \"architecture\": \"x86\",\n  \"dll\": " << Json(dll)
            << ",\n  \"dll_sha256\": " << Json(dll_hash) << ",\n  \"wave\": " << Json(wave)
            << ",\n  \"scene\": " << Json(scene) << ",\n  \"arguments\": " << arguments
            << ",\n  \"duration_ms\": " << duration_ms << ",\n  \"timeout_ms\": " << timeout_ms
            << ",\n  \"status\": " << Json(status) << ",\n  \"exit_code\": " << exit_code << "\n}\n";
        if (!stream) throw std::runtime_error("Cannot write run.json");
    }
};

struct Deadline {
    HANDLE completed;
    const RunMetadata* metadata;
};
DWORD WINAPI EnforceDeadline(void* argument)
{
    const auto& deadline = *static_cast<Deadline*>(argument);
    if (WaitForSingleObject(deadline.completed, deadline.metadata->timeout_ms) == WAIT_TIMEOUT) {
        try { deadline.metadata->Write("timeout", 124); } catch (...) {}
        ExitProcess(124);
    }
    return 0;
}

const RunMetadata* active_metadata = nullptr;

void WriteResults(const fs::path& directory, int exit_code)
{
    using namespace a3dcapture;
    const CaptureState& state = capture_state;
    std::ofstream output(directory / "capture.tsv");
    output << "A3D_PCM_CAPTURE\t1\nSTATUS\t" << exit_code << "\n";
    output << "STATS\t" << state.buffer_creation_count << '\t' << state.sample_count << '\t' << state.peak_amplitude << '\n';
    for (int i = 0; i < state.buffer_count; ++i) {
        const auto& buffer = state.buffers[i];
        output << "BUFFER\t" << i << '\t' << buffer.dwBufferBytes << '\t' << buffer.dwFlags
               << '\t' << buffer.dwSamplesPerSec << '\t' << buffer.nChannels << '\t' << buffer.nUnlocks
               << '\t' << buffer.cbStream << '\t' << buffer.fOverflow << '\n';
        if (buffer.cbStream) {
            const auto wave = directory / ("capture-" + std::to_string(i) + ".wav");
            if (!fs::is_regular_file(wave)) throw std::runtime_error("Capture WAV was not written");
            output << "WAV\t" << i << '\t' << wave.filename().string() << '\n';
        }
    }
    if (!output) throw std::runtime_error("Cannot write capture.tsv");
}
} // namespace

namespace a3dcapture {
void RecordCaptureFault(DWORD code)
{
    if (active_metadata) {
        try { active_metadata->Write("fault", code); } catch (...) {}
    }
}
}

int main(int argc, char** argv)
{
    std::unique_ptr<RunMetadata> metadata;
    Deadline deadline = {};
    HANDLE watchdog = nullptr;
    auto stop_watchdog = [&] {
        if (watchdog) {
            SetEvent(deadline.completed);
            WaitForSingleObject(watchdog, INFINITE);
            CloseHandle(watchdog);
            watchdog = nullptr;
        }
        if (deadline.completed) {
            CloseHandle(deadline.completed);
            deadline.completed = nullptr;
        }
        active_metadata = nullptr;
    };
    try {
        a3dtools::CommandLine args(argc, argv, "dll wave scene output duration-ms timeout-ms effect client", "list-scenes");
        if (args.Has("help")) {
            puts("a3d_pcm_capture --dll <path> --wave <path> [--scene <name>] [--output <new-directory>]\n"
                 "  [--duration-ms N] [--timeout-ms N] [--effect none|reverb|reflect|manual] [--client default|q3] [--list-scenes]\n"
                 "Record A3D software-mixer output through a process-local DirectSound substitute.\n"
                 "Requires Windows x86 and an A3D DLL. Writes WAV files, capture.tsv, and run.json.\n"
                 "No persistent COM registration changes. Each named run creates a new output directory.\n"
                 "Exit: 0 audible PCM, 1 runtime/capture failure, 2 invalid arguments, 3 missing input,\n"
                 "124 timeout; faults retain the Windows exception exit code. Legacy positional arguments remain supported.");
            return 0;
        }
        if (args.Has("list-scenes")) {
            for (const auto& scene : a3dcapture::capture_scenes) puts(scene.pszName);
            return 0;
        }
        if (!args.positional.empty()) return a3dcapture::RunCapture(argc, argv);
        if (!args.Has("dll") || !args.Has("wave")) throw std::runtime_error("--dll and --wave are required");
        const auto dll = fs::absolute(args.Get("dll"));
        const auto wave = fs::absolute(args.Get("wave"));
        if (!fs::is_regular_file(dll) || !fs::is_regular_file(wave)) {
            fputs("DLL or wave input does not exist\n", stderr);
            return 3;
        }
        const auto scene_name = args.Get("scene", "default");
        int scene_index = -1;
        for (int i = 0; i < CAPTURE_SCENE_COUNT; ++i)
            if (scene_name == a3dcapture::capture_scenes[i].pszName) scene_index = i;
        if (scene_index < 0) throw std::runtime_error("Unknown scene: " + scene_name);
        const auto effect = args.Get("effect", "none");
        if (effect != "none" && effect != "reverb" && effect != "reflect" && effect != "manual") throw std::runtime_error("Unknown effect");
        const auto client = args.Get("client", "default");
        if (client != "default" && client != "q3") throw std::runtime_error("Unknown client");
        if (client == "q3" && effect != "none") throw std::runtime_error("Q3 client capture tests direct playback only");
        const unsigned duration = args.Number("duration-ms", 500, 1, 60000);
        const unsigned timeout = args.Number("timeout-ms", 30000, 1000, 600000);
        if (timeout <= duration + 500) throw std::runtime_error("Timeout must exceed capture duration plus warmup");
        fs::path output = args.Has("output") ? fs::path(args.Get("output")) :
            fs::path("artifacts/captures") / (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        output = fs::absolute(output);
        if (fs::exists(output)) throw std::runtime_error("Output directory already exists; select a new run directory");
        std::string arguments = "[";
        for (int i = 1; i < argc; ++i) { if (i > 1) arguments += ','; arguments += Json(argv[i]); }
        arguments += ']';
        metadata = std::make_unique<RunMetadata>(RunMetadata{output, dll.string(), wave.string(), Sha256(dll), scene_name, arguments, duration, timeout});
        fs::create_directories(output);
        metadata->Write("running", 0);
        active_metadata = metadata.get();
        deadline = {CreateEventA(nullptr, TRUE, FALSE, nullptr), metadata.get()};
        if (!deadline.completed) throw std::runtime_error("Cannot create deadline event");
        watchdog = CreateThread(nullptr, 0, EnforceDeadline, &deadline, 0, nullptr);
        if (!watchdog) throw std::runtime_error("Cannot start watchdog");
        int result = a3dtools::InvokeLegacy(a3dcapture::RunCapture, {argv[0], wave.string(), dll.string(),
            std::to_string(duration), (output / "capture").string(), std::to_string(scene_index), effect, client});
        stop_watchdog();
        WriteResults(output, result);
        metadata->Write(result == 0 ? "completed" : "failed", result);
        return result;
    } catch (const std::exception& error) {
        stop_watchdog();
        const int result = metadata ? 1 : 2;
        if (metadata) {
            try { metadata->Write("failed", result); } catch (...) {}
        }
        fprintf(stderr, "%s\n", error.what());
        return result;
    }
}
