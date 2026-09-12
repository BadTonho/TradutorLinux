#pragma once

#include "kernel32_common.hpp"

#include "../../core/runtime_handle_state.hpp"
#include "../../core/runtime_memory_state.hpp"
#include "../../core/runtime_process_state.hpp"
#include "../../core/runtime_thread_state.hpp"

#include "tradutorlinux/loader/builtin_modules.hpp"
#include "tradutorlinux/loader/import_resolver.hpp"
#include "tradutorlinux/loader/module.hpp"
#include "tradutorlinux/loader/module_graph.hpp"
#include "tradutorlinux/loader/process.hpp"
#include "tradutorlinux/prefix/prefix.hpp"
#include "tradutorlinux/runtime/environment.hpp"
#include "tradutorlinux/runtime/msvcrt.hpp"
#include "tradutorlinux/runtime/ntdll.hpp"
#include "tradutorlinux/runtime/security.hpp"
#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/util/unicode.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
