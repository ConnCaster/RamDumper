#include <iostream>
#include <unistd.h>

#include "avml/manager.h"

int main() {
    if (geteuid() != 0) {
        std::cerr << "[WARNING] run as root for full memory access\n";
    }

    try {
        std::string dst = "dump.lime";
        avml::DumpModuleImpl manager(dst);
        avml::ModuleResult ret = manager.Run();
        return (ret != avml::ModuleResult::kError) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 3;
    }
}