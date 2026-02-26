#include "memory_dumper.h"
#include "dump_factory.h"

#include <iostream>
#include <chrono>

namespace MemoryDump {

MemoryDumper::MemoryDumper() {
    strategies_ = DumpFactory::createStrategyChain();
}

DumpResult MemoryDumper::dump(const std::string& output_path) {
    log("Starting memory dump with fallback chain...");
    
    for (const auto& strategy : strategies_) {
        log("Trying method: " + strategy->getName());
        
        if (!strategy->isAvailable()) {
            log("  -> Not available, skipping");
            continue;
        }
        
        log("  -> Available, attempting dump...");
        
        auto start = std::chrono::steady_clock::now();
        DumpResult result = strategy->dump(output_path);
        auto end = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            end - start).count();
        
        if (result.success) {
            log("  -> SUCCESS! Dumped " + std::to_string(result.bytes_dumped) + 
                " bytes in " + std::to_string(duration) + "s");
            return result;
        } else {
            log("  -> FAILED: " + result.error_message);
        }
    }
    
    DumpResult result{false, "All methods failed", 0, output_path};
    logError("Memory dump failed - all methods exhausted");
    return result;
}

DumpResult MemoryDumper::dumpWithMethod(const std::string& method_name, 
                                         const std::string& output_path) {
    auto strategy = DumpFactory::createStrategy(method_name);
    if (!strategy) {
        return {false, "Unknown method: " + method_name, 0, output_path};
    }
    
    if (!strategy->isAvailable()) {
        return {false, "Method not available: " + method_name, 0, output_path};
    }
    
    return strategy->dump(output_path);
}

std::vector<std::string> MemoryDumper::getAvailableMethods() const {
    std::vector<std::string> available;
    for (const auto& strategy : strategies_) {
        if (strategy->isAvailable()) {
            available.push_back(strategy->getName());
        }
    }
    return available;
}

void MemoryDumper::setProgressCallback(ProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

void MemoryDumper::log(const std::string& message) const {
    std::cout << "[INFO] " << message << std::endl;
}

void MemoryDumper::logError(const std::string& message) const {
    std::cerr << "[ERROR] " << message << std::endl;
}

} // namespace MemoryDump