#ifndef ZZT_HANDLE_H
#define ZZT_HANDLE_H

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace zzt::qrcode {
template <typename HandleT, typename = void>
struct HandleTraits {
    using key_t = std::uintptr_t;
    static key_t to_key(HandleT handle) { return static_cast<key_t>(handle); }
    static HandleT from_key(key_t handle) { return static_cast<HandleT>(handle); }
};

template <typename HandleT>
struct HandleTraits<HandleT, std::enable_if_t<std::is_pointer_v<HandleT>>> {
    using key_t = std::uintptr_t;
    static key_t to_key(HandleT handle) { return reinterpret_cast<key_t>(handle); }
    static HandleT from_key(key_t handle) { return reinterpret_cast<HandleT>(handle); }
};

template <typename T, typename HandleT = intptr_t>
class Handle {
public:
    using handle_t = HandleT;
    using traits = HandleTraits<handle_t>;
    using key_t = typename traits::key_t;

    template <typename... Args>
    static handle_t create_handle(Args&&... args) {
        auto ptr = std::make_shared<T>(std::forward<Args>(args)...);

        while (true) {
            key_t index = generate_handle();
            if (index != 0) {
                std::unique_lock g(mutex());
                auto [it, success] = handle_map().try_emplace(index, std::move(ptr));
                if (success) {
                    return traits::from_key(index);
                }
            }
        }
    }

    static bool release_handle(handle_t handle) {
        std::unique_lock g(mutex());
        return handle_map().erase(traits::to_key(handle)) > 0;
    }

    static std::shared_ptr<T> get(handle_t handle) {
        std::shared_lock g(mutex());
        const auto key = traits::to_key(handle);
        const auto& iter = handle_map().find(key);
        if (iter == handle_map().end()) {
            return nullptr;
        }
        return iter->second;
    }

private:
    static key_t generate_handle() {
        static std::atomic<key_t> counter{0};
        static const key_t seed = []() -> key_t {
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            auto count = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            return static_cast<key_t>(count);
        }();

        constexpr bool is64 = sizeof(key_t) == 8;
        constexpr key_t M1 = is64
            ? static_cast<key_t>(0xbf58476d1ce4e5b9ULL)
            : static_cast<key_t>(0x85ebca6bUL);
        constexpr key_t M2 = is64
            ? static_cast<key_t>(0x94d049bb133111ebULL)
            : static_cast<key_t>(0xc2b2ae35UL);
        constexpr int S1 = is64 ? 30 : 16;
        constexpr int S2 = is64 ? 27 : 13;
        constexpr int S3 = is64 ? 31 : 16;

        while (true) {
            key_t x = ++counter;

            // SplitMix-style bijective permutation
            x = (x ^ (x >> S1)) * M1;
            x = (x ^ (x >> S2)) * M2;
            x = x ^ (x >> S3);

            key_t handle = x ^ seed;
            if (handle != 0) return handle;
        }
    }

    static std::unordered_map<key_t, std::shared_ptr<T>>& handle_map() {
        static std::unordered_map<key_t, std::shared_ptr<T>> map;
        return map;
    }

    static std::shared_mutex& mutex() {
        static std::shared_mutex mutex;
        return mutex;
    }
};
}  // namespace zzt::qrcode

#endif  // ZZT_HANDLE_H
