#include "memory_dumper.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  -o <path>    Output path for diagnostic report\n"
              << "  -m <method>  Specific method (kcore, crash)\n"
              << "  -l           List available methods\n"
              << "  -h           Show this help\n";
}

bool parseArgs(int argc,
               char* argv[],
               std::string& output_path,
               std::string& specific_method,
               bool& list_methods) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "Error: -o requires a path\n";
                return false;
            }
            output_path = argv[++i];
        } else if (arg == "-m") {
            if (i + 1 >= argc) {
                std::cerr << "Error: -m requires a method name\n";
                return false;
            }
            specific_method = argv[++i];
        } else if (arg == "-l") {
            list_methods = true;
        } else if (arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Error: unknown argument: " << arg << '\n';
            return false;
        }
    }

    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (geteuid() != 0) {
        std::cerr << "[WARNING] Running without root; access checks may be limited\n";
    }

    std::string output_path = "./memory_report.txt";
    std::string specific_method;
    bool list_methods = false;

    if (!parseArgs(argc, argv, output_path, specific_method, list_methods)) {
        printUsage(argv[0]);
        return 1;
    }

    MemoryDump::MemoryDumper dumper;

    if (list_methods) {
        std::cout << "Available diagnostic methods:\n";
        const auto methods = dumper.getAvailableMethods();
        if (methods.empty()) {
            std::cout << "  (none)\n";
        } else {
            for (const auto& method : methods) {
                std::cout << "  - " << method << '\n';
            }
        }
        return 0;
    }

    std::cout << "=== Linux Memory Access Diagnostics ===\n";
    std::cout << "Output report: " << output_path << "\n\n";

    MemoryDump::DumpResult result;

    if (!specific_method.empty()) {
        std::cout << "Using specific method: " << specific_method << '\n';
        result = dumper.dumpWithMethod(specific_method, output_path);
    } else {
        std::cout << "Using fallback chain (first available method)\n";
        result = dumper.dump(output_path);
    }

    std::cout << "\n=== Result ===\n";
    if (result.success) {
        std::cout << "SUCCESS\n";
        std::cout << "  Path: " << result.output_path << '\n';
        std::cout << "  Bytes described: " << result.bytes_dumped << '\n';
        return 0;
    }

    std::cerr << "FAILED\n";
    std::cerr << "  Error: " << result.error_message << '\n';
    return 1;
}