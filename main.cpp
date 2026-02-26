#include "memory_dumper.h"
#include <iostream>
#include <cstdlib>
#include <unistd.h>

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -o <path>    Output path for memory dump\n"
              << "  -m <method>  Specific method (kcore, crash)\n"
              << "  -l           List available methods\n"
              << "  -h           Show this help\n";
}

int main(int argc, char* argv[]) {
    if (geteuid() != 0) {
        std::cerr << "[WARNING] This program should be run as root for full memory access\n";
    }

    std::string output_path = "/tmp/memory_dump.bin";
    std::string specific_method;
    bool list_methods = false;

    // Простой парсинг аргументов
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "-m" && i + 1 < argc) {
            specific_method = argv[++i];
        } else if (arg == "-l") {
            list_methods = true;
        } else if (arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    MemoryDump::MemoryDumper dumper;

    if (list_methods) {
        std::cout << "Available memory dump methods:\n";
        auto methods = dumper.getAvailableMethods();
        for (const auto& method : methods) {
            std::cout << "  - " << method << "\n";
        }
        return 0;
    }

    std::cout << "=== Linux Memory Dumper ===\n";
    std::cout << "Output: " << output_path << "\n\n";

    MemoryDump::DumpResult result;

    if (!specific_method.empty()) {
        std::cout << "Using specific method: " << specific_method << "\n";
        result = dumper.dumpWithMethod(specific_method, output_path);
    } else {
        std::cout << "Using fallback chain (auto-select best method)\n";
        result = dumper.dump(output_path);
    }

    std::cout << "\n=== Result ===\n";
    if (result.success) {
        std::cout << "SUCCESS!\n";
        std::cout << "  Path: " << result.output_path << "\n";
        std::cout << "  Size: " << result.bytes_dumped << " bytes\n";
        return 0;
    } else {
        std::cerr << "FAILED!\n";
        std::cerr << "  Error: " << result.error_message << "\n";
        return 1;
    }
}