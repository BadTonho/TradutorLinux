#pragma once

#include "kernel32_common.hpp"

#include "tradutorlinux/runtime/teb.hpp"
#include "tradutorlinux/runtime/unwind.hpp"

#include <array>
#include <csetjmp>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>
