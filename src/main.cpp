#include <iostream>
#include <unistd.h>

#include "memory_dump/manager.h"

int main() {
    if (geteuid() != 0) {
        std::cerr << "[WARNING] run as root for full memory access\n";
    }

    try {
        std::string dst = "dump.lime";
        memory_dump::DumpModuleImpl manager(dst);
        memory_dump::ModuleResult ret = manager.Run();
        return (ret != memory_dump::ModuleResult::kError) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 3;
    }
}