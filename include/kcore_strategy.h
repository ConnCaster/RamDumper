#ifndef RAMDUMPER_KCORE_STRATEGY_H
#define RAMDUMPER_KCORE_STRATEGY_H

#include "dump_strategy.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace MemoryDump {

    class KCoreStrategy : public IDumpStrategy {
    public:
        KCoreStrategy() = default;

        DumpResult dump(const std::string& output_path) override;
        bool isAvailable() const override;
        StrategyInfo getInfo() const override;
        std::string getName() const override { return "/proc/kcore"; }

        void setProgressCallback(std::function<void(size_t, size_t)> callback) {
            progress_callback_ = std::move(callback);
        }

    private:
        struct Segment {
            std::uint64_t phys_addr;
            std::uint64_t virt_addr;
            std::uint64_t mem_size;
            std::uint64_t file_size;
            std::uint64_t offset;
        };

        bool parseElfSegments(const std::string& kcore_path,
                              std::vector<Segment>& segments,
                              std::string& error_message) const;

        bool writeReport(const std::string& output_path,
                         const std::vector<Segment>& segments,
                         std::size_t& bytes_described,
                         std::string& error_message) const;

        std::function<void(size_t, size_t)> progress_callback_;
    };

} // namespace MemoryDump

#endif // RAMDUMPER_KCORE_STRATEGY_H