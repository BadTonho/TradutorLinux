#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tradutorlinux::runtime {

// Estrutura do Process Environment Block (PEB) do Windows x86-64.
struct alignas(8) GuestPeb {
    std::uint8_t inherited_address_space{0};
    std::uint8_t read_image_file_exec_options{0};
    std::uint8_t being_debugged{0};
    std::uint8_t bit_field{0};
    std::uint32_t padding0{0};
    std::uint64_t mutant{0};
    std::uint64_t image_base_address{0};
    std::uint64_t ldr{0};
    std::uint64_t process_parameters{0};
    std::uint64_t sub_system_data{0};
    std::uint64_t process_heap{0};
    std::uint64_t fast_peb_lock{0};
    std::uint64_t atl_thunk_list_ptr{0};
    std::uint64_t ifeo_key{0};
    std::uint64_t cross_process_flags{0};
    std::uint64_t user_shared_info_ptr{0};
    std::uint32_t system_reserved{0};
    std::uint32_t atl_thunk_list_ptr32{0};
    std::uint64_t api_set_map{0};
    std::uint32_t tls_expansion_counter{0};
    std::uint32_t padding1{0};
    std::uint64_t tls_bitmap{0};
    std::uint32_t tls_bitmap_bits[2]{0, 0};
    std::uint64_t read_only_shared_memory_base{0};
    std::uint64_t shared_data{0};
    std::uint64_t read_only_static_server_data{0};
    std::uint64_t ansi_code_page_data{0};
    std::uint64_t oem_code_page_data{0};
    std::uint64_t unicode_case_table_data{0};
    std::uint32_t number_of_processors{1};
    std::uint32_t nt_global_flag{0};
};

// Estrutura do Thread Environment Block (TEB) do Windows x86-64.
// O TEB começa em uma página alinhada apontada pelo registrador de segmento
// %gs. Como TlsSlots começa em 0x1480, os 64 slots ocupam parte da segunda
// página; a alocação precisa, portanto, cobrir duas páginas.
struct alignas(4096) GuestTeb {
    // NT_TIB (0x00 - 0x38)
    std::uint64_t exception_list{0};             // 0x00
    std::uint64_t stack_base{0};                 // 0x08
    std::uint64_t stack_limit{0};                // 0x10
    std::uint64_t subsystem_tib{0};              // 0x18
    std::uint64_t fiber_data{0};                 // 0x20
    std::uint64_t arbitrary_user_pointer{0};     // 0x28
    std::uint64_t self{0};                       // 0x30: Ponteiro para o próprio TEB (%gs:[0x30])
    
    std::uint64_t environment_pointer{0};        // 0x38
    std::uint64_t unique_process_id{1};          // 0x40: ClientId.UniqueProcess
    std::uint64_t unique_thread_id{1};           // 0x48: ClientId.UniqueThread
    std::uint64_t active_rpc_handle{0};          // 0x50
    std::uint64_t thread_local_storage_ptr{0};   // 0x58
    std::uint64_t peb{0};                        // 0x60: ProcessEnvironmentBlock (%gs:[0x60])
    
    std::uint32_t last_error_value{0};           // 0x68: LastErrorValue (%gs:[0x68])
    std::uint32_t count_of_owned_critical_sections{0}; // 0x6C
    std::uint64_t csr_client_thread{0};          // 0x70
    std::uint64_t win32_thread_info{0};          // 0x78

    std::uint8_t padding_to_tls[0x1480 - 0x80]{}; // Preenchimento até 0x1480
    std::array<std::uint64_t, 64> tls_slots{};    // 0x1480: TlsSlots[64] (%gs:[0x1480])

    std::uint8_t reserved_page_tail[0x2000 - 0x1480 - 64 * 8]{};
};

static_assert(offsetof(GuestTeb, self) == 0x30, "TEB::self deve estar no offset 0x30");
static_assert(offsetof(GuestTeb, unique_thread_id) == 0x48, "TEB::unique_thread_id deve estar no offset 0x48");
static_assert(offsetof(GuestTeb, peb) == 0x60, "TEB::peb deve estar no offset 0x60");
static_assert(offsetof(GuestTeb, last_error_value) == 0x68, "TEB::last_error_value deve estar no offset 0x68");
static_assert(offsetof(GuestTeb, tls_slots) == 0x1480, "TEB::tls_slots deve estar no offset 0x1480");
static_assert(sizeof(GuestTeb) == 0x2000, "GuestTeb deve ocupar exatamente duas páginas");

// Inicializa a página do TEB com os ponteiros essenciais e IDs.
inline void initialize_guest_teb(GuestTeb* teb, GuestPeb* peb, std::uint64_t stack_base,
                                 std::uint64_t stack_limit, std::uint32_t thread_id,
                                 std::uint32_t process_id = 1) noexcept {
    if (teb == nullptr) {
        return;
    }
    teb->self = reinterpret_cast<std::uint64_t>(teb);
    teb->stack_base = stack_base;
    teb->stack_limit = stack_limit;
    teb->unique_process_id = process_id;
    teb->unique_thread_id = thread_id;
    teb->peb = reinterpret_cast<std::uint64_t>(peb);
    teb->last_error_value = 0;
    teb->tls_slots.fill(0);
}

}  // namespace tradutorlinux::runtime
