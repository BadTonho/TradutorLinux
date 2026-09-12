#pragma once

// Fachada interna de compatibilidade. Código novo deve incluir diretamente o
// contrato de domínio correspondente; esta fachada permanece para testes e
// consumidores internos que ainda dependem do conjunto histórico.
#include "runtime_state_common.hpp"
#include "runtime_thread_state.hpp"
#include "runtime_process_state.hpp"
#include "runtime_handle_state.hpp"
#include "runtime_memory_state.hpp"
#include "runtime_gui_state.hpp"
