#pragma once
#include <random>

namespace tryengine::core {
class RandomUtil {
    public:
    static uint64_t GenerateInt64() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        return dis(gen);
    }

};
}