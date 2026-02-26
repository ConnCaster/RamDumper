#ifndef RAMDUMPER_KCORE_STRATEGY_H
#define RAMDUMPER_KCORE_STRATEGY_H

#include <cstdint>

#include "dump_strategy.h"
#include <vector>
#include <functional>

namespace MemoryDump {

    class KCoreStrategy : public IDumpStrategy {
    public:
        KCoreStrategy() = default;

        DumpResult dump(const std::string& output_path) override;
        bool isAvailable() const override;
        StrategyInfo getInfo() const override;
        std::string getName() const override { return "/proc/kcore"; }

        // Установить callback прогресса
        void setProgressCallback(std::function<void(size_t, size_t)> callback) {
            progress_callback_ = std::move(callback);
        }

    private:
        struct Segment {
            uint64_t phys_addr;
            uint64_t virt_addr;
            size_t size;
            size_t offset;
        };

        bool parseElfSegments(const std::string& kcore_path,
                              std::vector<Segment>& segments);

        bool readAndWriteSegments(const std::string& kcore_path,
                                  const std::string& output_path,
                                  const std::vector<Segment>& segments,
                                  size_t& bytes_dumped);

        std::function<void(size_t, size_t)> progress_callback_;
    };

} // namespace MemoryDump

#endif //RAMDUMPER_KCORE_STRATEGY_H