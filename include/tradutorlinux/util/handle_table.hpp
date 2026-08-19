#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>

namespace tradutorlinux::util {

// Tabela de handles/slots thread-safe de capacidade fixa.
template <typename SlotType, std::size_t Capacity>
class HandleTable {
public:
    static constexpr std::size_t kCapacity = Capacity;

    HandleTable() = default;

    // Procura e reserva o primeiro slot livre.
    template <typename InitFn>
    SlotType* allocate(InitFn&& init) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!slots_[i].used) {
                slots_[i].used = true;
                init(slots_[i], i);
                return &slots_[i];
            }
        }
        return nullptr;
    }

    // Libera um slot específico.
    void release(const SlotType* slot) {
        if (slot == nullptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto* base = slots_.data();
        if (slot >= base && slot < base + Capacity) {
            const auto index = static_cast<std::size_t>(slot - base);
            slots_[index] = SlotType{};
        }
    }

    // Busca um slot por predicado com lock.
    template <typename Predicate>
    SlotType* find(Predicate&& pred) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& slot : slots_) {
            if (slot.used && pred(slot)) {
                return &slot;
            }
        }
        return nullptr;
    }

    // Itera por todos os slots usados com lock.
    template <typename Visitor>
    void for_each(Visitor&& visitor) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (slots_[i].used) {
                visitor(slots_[i], i);
            }
        }
    }

    // Verifica se um ponteiro aponta para um slot válido e em uso.
    [[nodiscard]] bool is_valid_pointer(const void* ptr) const noexcept {
        const auto* base = slots_.data();
        const auto* target = static_cast<const SlotType*>(ptr);
        if (target >= base && target < base + Capacity) {
            return target->used;
        }
        return false;
    }

    // Retorna o array de slots para acesso direto onde necessário.
    [[nodiscard]] std::array<SlotType, Capacity>& raw_slots() noexcept { return slots_; }
    [[nodiscard]] const std::array<SlotType, Capacity>& raw_slots() const noexcept { return slots_; }
    [[nodiscard]] std::mutex& mutex() noexcept { return mutex_; }

private:
    std::array<SlotType, Capacity> slots_{};
    mutable std::mutex mutex_;
};

}  // namespace tradutorlinux::util
