#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

extern "C" int loganalyqt_mosquitto_main(int argc, char **argv);

int main(int argc, char **argv) {
#ifdef _WIN32
    // Pipe output must reach the parent's readiness/client-event parser immediately.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#endif
    // The parent owns this pipe. EOF also stops the broker after an app crash.
    if (std::getenv("LOGANALYQT_BROKER_MANAGED")) {
        std::thread([] {
            while (std::getchar() != EOF) {}
            std::raise(SIGTERM);
            // No persistent broker state: bound shutdown if a library call stalls.
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            std::_Exit(0);
        }).detach();
    }
    return loganalyqt_mosquitto_main(argc, argv);
}
