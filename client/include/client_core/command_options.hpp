// client_core/command_options.hpp

#include "bench/debug_mode.hpp"
#include "bench/bench_runner.hpp"
#include "client_core/local_server.hpp"

struct CommandLineOptions {
    ServerMode mode = ServerMode::CPU;

    const char* host = "localhost";
    uint16_t port = 5070;
    const char* username = "player";

    bool isBench = false;
    bool isDebug = false;

    int radius = -1;

    BenchArgs::Mode benchMode = BenchArgs::Mode::Gen;
    bool benchGPU = false;

    int benchPlayers = 4;
    float benchDuration = 60.f;
    float benchSpawnR = 80.f;
    float benchStationary = 10.f;

    const char* benchOutput = nullptr;

    bool singleplayer = true;
};

bool requireValue(int argc, int i) {
    if (i + 1 < argc) return true;

    fprintf(stderr, "Missing value for argument.\n");
    return false;
}

bool parse(int argc, char* argv[], CommandLineOptions& opt) {
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg = argv[i];

        if (arg == "--gpu")
            opt.mode = ServerMode::GPU;
        else if (arg == "--cpu")
            opt.mode = ServerMode::CPU;
        else if (arg == "--bench")
            opt.isBench = true;
        else if (arg == "--debug")
            opt.isDebug = true;
        else if (arg == "--server")
            opt.singleplayer = false;
        else if (arg == "--host") {
            if (!requireValue(argc, i))
                return false;

            opt.host = argv[++i];
            opt.singleplayer = false;
        }
        else if (arg == "--port") {
            if (!requireValue(argc, i))
                return false;

            opt.port = static_cast<uint16_t>(std::stoi(argv[++i]));
            opt.singleplayer = false;
        }
        else if (arg == "--user") {
            if (!requireValue(argc, i))
                return false;

            opt.username = argv[++i];
        }
        else if (arg == "--bench-radius") {
            if (!requireValue(argc, i))
                return false;

            opt.radius = std::stoi(argv[++i]);
        }
        else if (arg == "--bench-mode")
        {
            if (!requireValue(argc, i))
                return false;

            auto mode = std::string_view(argv[++i]);

            if (mode == "load")
                opt.benchMode = BenchArgs::Mode::Load;
            else if (mode == "gen")
                opt.benchMode = BenchArgs::Mode::Gen;
            else
            {
                fprintf(stderr,
                        "Unknown bench mode: %s\n",
                        argv[i]);
                return false;
            }
        }
        else if (arg == "--bench-gpu")
        {
            opt.benchGPU = true;
        }
        else if (arg == "--bench-players")
        {
            if (!requireValue(argc, i))
                return false;

            opt.benchPlayers = std::stoi(argv[++i]);
        }
        else if (arg == "--bench-duration")
        {
            if (!requireValue(argc, i))
                return false;

            opt.benchDuration = std::stof(argv[++i]);
        }
        else if (arg == "--bench-spawn")
        {
            if (!requireValue(argc, i))
                return false;

            opt.benchSpawnR = std::stof(argv[++i]);
        }
        else if (arg == "--bench-stationary")
        {
            if (!requireValue(argc, i))
                return false;

            opt.benchStationary = std::stof(argv[++i]);
        }
        else if (arg == "--bench-output")
        {
            if (!requireValue(argc, i))
                return false;

            opt.benchOutput = argv[++i];
        }
        else
        {
            // positional args: host port

            if (!opt.host)
            {
                opt.host = argv[i];
            }
            else if (opt.port == 0)
            {
                opt.port =
                    static_cast<uint16_t>(std::stoi(argv[i]));
            }
            else
            {
                fprintf(stderr,
                        "Unknown argument: %s\n",
                        argv[i]);
                return false;
            }
        }
    }

    return true;
}