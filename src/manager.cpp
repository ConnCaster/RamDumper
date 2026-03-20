#include <iostream>

#include "avml/manager.h"

namespace avml {

    DumpModuleImpl::DumpModuleImpl(const std::string& dump_file_path)
        : dump_file_path_(dump_file_path)
    {
        // Порядок приоритета: /dev/crash → /proc/kcore → /dev/mem
        strategies_.push_back(std::make_unique<PhysicalMemoryDumpStrategy>("/dev/crash"));
        strategies_.push_back(std::make_unique<KCoreDumpStrategy>());
        strategies_.push_back(std::make_unique<PhysicalMemoryDumpStrategy>("/dev/mem"));
    }

    ModuleResult DumpModuleImpl::Run() {
        auto ranges = io_mem_parser_.ParseSystemRam();
        if (!ranges) {
            return ModuleResult::kError;
        }

        for (const auto& strategy : strategies_) {
            if (strategy->Dump(*ranges, dump_file_path_) == 0) {
                return ModuleResult::kSuccess;
            }
        }
        return ModuleResult::kError;
    }

} // namespace avml