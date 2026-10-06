#include "peer_thread_quiescence.hpp"

#include <tlhelp32.h>

#include <algorithm>
#include <limits>

namespace wawvr::mod {
namespace {

class ScopedHandle final {
public:
    explicit ScopedHandle(const HANDLE handle) noexcept : handle_(handle) {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    ~ScopedHandle() {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

private:
    HANDLE handle_{};
};

void set_failure(
    PeerThreadQuiesceResult* const result,
    const PeerThreadQuiesceStatus status,
    const DWORD system_error,
    const DWORD thread_id = 0) noexcept {
    if (result != nullptr) {
        result->status = status;
        result->system_error = system_error;
        result->thread_id = thread_id;
    }
}

void note_vanished_thread(PeerThreadQuiesceResult* const result) noexcept {
    if (result != nullptr) {
        ++result->vanished_thread_count;
    }
}

[[nodiscard]] bool has_nonempty_patch_range(
    const std::span<const PeerThreadPatchRange> ranges) noexcept {
    for (const auto& range : ranges) {
        if (range.size != 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool readable_stack_page(const DWORD protection) noexcept {
    if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    const DWORD access = protection & 0xFFU;
    return access == PAGE_READONLY || access == PAGE_READWRITE ||
           access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE ||
           access == PAGE_EXECUTE_WRITECOPY;
}

#if defined(_M_IX86)
struct ThreadBasicInformation32 final {
    LONG exit_status{};
    std::uint32_t teb_base_address{};
    std::uint32_t unique_process{};
    std::uint32_t unique_thread{};
    std::uint32_t affinity_mask{};
    LONG priority{};
    LONG base_priority{};
};
static_assert(sizeof(ThreadBasicInformation32) == 28);

struct NtTibPrefix32 final {
    std::uint32_t exception_list{};
    std::uint32_t stack_base{};
    std::uint32_t stack_limit{};
};
static_assert(sizeof(NtTibPrefix32) == 12);

using NtQueryInformationThreadFn = LONG(NTAPI*)(
    HANDLE, LONG, void*, ULONG, ULONG*);
#endif

} // namespace

SuspendedPeerThreads::~SuspendedPeerThreads() { resume(); }

bool SuspendedPeerThreads::suspend(
    const std::span<const PeerThreadPatchRange> patch_ranges,
    PeerThreadQuiesceResult* const result,
    const std::span<const std::uintptr_t> forbidden_stack_returns) noexcept {
    if (result != nullptr) {
        *result = {};
    }
    if (thread_count_ != 0 || !has_nonempty_patch_range(patch_ranges)) {
        set_failure(
            result, PeerThreadQuiesceStatus::invalid_patch_ranges,
            ERROR_INVALID_PARAMETER);
        return false;
    }

#if defined(_M_IX86)
    NtQueryInformationThreadFn query_thread = nullptr;
    if (!forbidden_stack_returns.empty()) {
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        query_thread = ntdll != nullptr
            ? reinterpret_cast<NtQueryInformationThreadFn>(
                  GetProcAddress(ntdll, "NtQueryInformationThread"))
            : nullptr;
        if (query_thread == nullptr) {
            set_failure(
                result,
                PeerThreadQuiesceStatus::thread_stack_query_failed,
                GetLastError());
            return false;
        }
    }
#else
    if (!forbidden_stack_returns.empty()) {
        set_failure(
            result,
            PeerThreadQuiesceStatus::thread_stack_query_failed,
            ERROR_NOT_SUPPORTED);
        return false;
    }
#endif

    const DWORD process_id = GetCurrentProcessId();
    const DWORD current_thread_id = GetCurrentThreadId();
    const HANDLE snapshot_handle =
        CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot_handle == INVALID_HANDLE_VALUE) {
        set_failure(
            result, PeerThreadQuiesceStatus::snapshot_failed,
            GetLastError());
        return false;
    }
    const ScopedHandle snapshot(snapshot_handle);

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (!Thread32First(snapshot.get(), &entry)) {
        set_failure(
            result, PeerThreadQuiesceStatus::enumeration_failed,
            GetLastError());
        return false;
    }

    for (;;) {
        if (entry.th32OwnerProcessID == process_id &&
            entry.th32ThreadID != current_thread_id) {
            if (thread_count_ == threads_.size()) {
                set_failure(
                    result, PeerThreadQuiesceStatus::capacity_exceeded,
                    ERROR_INSUFFICIENT_BUFFER, entry.th32ThreadID);
                return false;
            }

            HANDLE thread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_QUERY_INFORMATION | SYNCHRONIZE,
                FALSE, entry.th32ThreadID);
            if (thread == nullptr) {
                const DWORD error = GetLastError();
                if (classify_peer_thread_failure(
                        PeerThreadFailureStage::open, error, WAIT_FAILED) ==
                    PeerThreadFailureDisposition::vanished) {
                    note_vanished_thread(result);
                } else {
                    set_failure(
                        result, PeerThreadQuiesceStatus::thread_open_failed,
                        error, entry.th32ThreadID);
                    return false;
                }
            } else {
                const DWORD owner_process_id = GetProcessIdOfThread(thread);
                if (owner_process_id == 0) {
                    const DWORD query_error = GetLastError();
                    const DWORD wait_result =
                        WaitForSingleObject(thread, 0);
                    if (classify_peer_thread_failure(
                            PeerThreadFailureStage::context, query_error,
                            wait_result) ==
                        PeerThreadFailureDisposition::vanished) {
                        note_vanished_thread(result);
                        CloseHandle(thread);
                        thread = nullptr;
                    } else {
                        CloseHandle(thread);
                        set_failure(
                            result,
                            PeerThreadQuiesceStatus::thread_query_failed,
                            query_error, entry.th32ThreadID);
                        return false;
                    }
                } else if (owner_process_id != process_id) {
                    // The snapshot thread exited and its ID was recycled for
                    // another process before OpenThread. Never suspend it.
                    note_vanished_thread(result);
                    CloseHandle(thread);
                    thread = nullptr;
                }

                if (thread != nullptr) {
                    const DWORD initial_wait = WaitForSingleObject(thread, 0);
                    if (initial_wait == WAIT_OBJECT_0) {
                        note_vanished_thread(result);
                        CloseHandle(thread);
                        thread = nullptr;
                    } else if (initial_wait != WAIT_TIMEOUT) {
                        const DWORD wait_error = initial_wait == WAIT_FAILED
                            ? GetLastError()
                            : ERROR_GEN_FAILURE;
                        CloseHandle(thread);
                        set_failure(
                            result,
                            PeerThreadQuiesceStatus::thread_query_failed,
                            wait_error, entry.th32ThreadID);
                        return false;
                    }
                }

                if (thread != nullptr &&
                    SuspendThread(thread) == static_cast<DWORD>(-1)) {
                    const DWORD suspend_error = GetLastError();
                    const DWORD wait_result =
                        WaitForSingleObject(thread, 0);
                    if (classify_peer_thread_failure(
                            PeerThreadFailureStage::suspend, suspend_error,
                            wait_result) ==
                        PeerThreadFailureDisposition::vanished) {
                        note_vanished_thread(result);
                        CloseHandle(thread);
                        thread = nullptr;
                    } else {
                        CloseHandle(thread);
                        set_failure(
                            result,
                            PeerThreadQuiesceStatus::thread_suspend_failed,
                            suspend_error, entry.th32ThreadID);
                        return false;
                    }
                }

                if (thread != nullptr) {
                    threads_[thread_count_++] = {
                        thread, entry.th32ThreadID, true};
                }
            }
        }

        entry.dwSize = sizeof(entry);
        SetLastError(ERROR_SUCCESS);
        if (!Thread32Next(snapshot.get(), &entry)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                set_failure(
                    result, PeerThreadQuiesceStatus::enumeration_failed,
                    error);
                return false;
            }
            break;
        }
    }

    for (std::size_t index = 0; index < thread_count_; ++index) {
        auto& thread = threads_[index];
        CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(thread.handle, &context)) {
            const DWORD context_error = GetLastError();
            const DWORD wait_result =
                WaitForSingleObject(thread.handle, 0);
            if (classify_peer_thread_failure(
                    PeerThreadFailureStage::context, context_error,
                    wait_result) ==
                PeerThreadFailureDisposition::vanished) {
                note_vanished_thread(result);
                thread.suspended = false;
                CloseHandle(thread.handle);
                thread.handle = nullptr;
                continue;
            }
            set_failure(
                result, PeerThreadQuiesceStatus::thread_context_failed,
                context_error, thread.id);
            return false;
        }
#if defined(_M_IX86)
        const std::uintptr_t instruction = context.Eip;
#elif defined(_M_X64)
        const std::uintptr_t instruction = context.Rip;
#else
#error Unsupported Windows architecture for peer-thread quiescence.
#endif
        if (instruction_in_patch_ranges(instruction, patch_ranges)) {
            set_failure(
                result,
                PeerThreadQuiesceStatus::thread_inside_patch_range,
                ERROR_BUSY, thread.id);
            return false;
        }

#if defined(_M_IX86)
        if (!forbidden_stack_returns.empty()) {
            ThreadBasicInformation32 information{};
            ULONG returned_bytes = 0;
            constexpr LONG kThreadBasicInformation = 0;
            const LONG query_status = query_thread(
                thread.handle, kThreadBasicInformation, &information,
                static_cast<ULONG>(sizeof(information)), &returned_bytes);
            if (query_status < 0 ||
                returned_bytes < sizeof(information) ||
                information.teb_base_address == 0) {
                set_failure(
                    result,
                    PeerThreadQuiesceStatus::thread_stack_query_failed,
                    query_status < 0
                        ? static_cast<DWORD>(query_status)
                        : ERROR_INVALID_DATA,
                    thread.id);
                return false;
            }

            NtTibPrefix32 tib{};
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(
                    GetCurrentProcess(),
                    reinterpret_cast<const void*>(
                        information.teb_base_address),
                    &tib, sizeof(tib), &bytes_read) ||
                bytes_read != sizeof(tib)) {
                set_failure(
                    result,
                    PeerThreadQuiesceStatus::thread_stack_read_failed,
                    GetLastError(), thread.id);
                return false;
            }

            const std::uintptr_t stack_pointer = context.Esp;
            constexpr std::size_t kMaximumStackProofBytes = 1U << 20;
            if (tib.stack_limit == 0 ||
                tib.stack_base <= tib.stack_limit ||
                stack_pointer < tib.stack_limit ||
                stack_pointer > tib.stack_base ||
                (stack_pointer % sizeof(std::uintptr_t)) != 0 ||
                (tib.stack_base % sizeof(std::uintptr_t)) != 0 ||
                tib.stack_base - stack_pointer >
                    kMaximumStackProofBytes) {
                set_failure(
                    result,
                    PeerThreadQuiesceStatus::thread_stack_bounds_invalid,
                    ERROR_INVALID_DATA, thread.id);
                return false;
            }

            std::array<std::uintptr_t, 4096> words{};
            std::uintptr_t cursor = stack_pointer;
            const std::uintptr_t stack_base = tib.stack_base;
            while (cursor < stack_base) {
                MEMORY_BASIC_INFORMATION memory{};
                if (VirtualQuery(
                        reinterpret_cast<const void*>(cursor), &memory,
                        sizeof(memory)) != sizeof(memory)) {
                    set_failure(
                        result,
                        PeerThreadQuiesceStatus::thread_stack_read_failed,
                        GetLastError(), thread.id);
                    return false;
                }
                const auto region_begin =
                    reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
                if (memory.RegionSize == 0 ||
                    region_begin >
                        (std::numeric_limits<std::uintptr_t>::max)() -
                            memory.RegionSize) {
                    set_failure(
                        result,
                        PeerThreadQuiesceStatus::thread_stack_bounds_invalid,
                        ERROR_INVALID_DATA, thread.id);
                    return false;
                }
                const std::uintptr_t region_end =
                    region_begin + memory.RegionSize;
                if (cursor < region_begin || cursor >= region_end ||
                    memory.State != MEM_COMMIT ||
                    !readable_stack_page(memory.Protect)) {
                    set_failure(
                        result,
                        PeerThreadQuiesceStatus::thread_stack_read_failed,
                        ERROR_NOACCESS, thread.id);
                    return false;
                }
                const std::uintptr_t readable_end =
                    (std::min)(region_end, stack_base);
                while (cursor < readable_end) {
                    const std::size_t chunk_bytes =
                        static_cast<std::size_t>((std::min)(
                            readable_end - cursor,
                            static_cast<std::uintptr_t>(sizeof(words))));
                    if (chunk_bytes == 0 ||
                        chunk_bytes % sizeof(std::uintptr_t) != 0) {
                        set_failure(
                            result,
                            PeerThreadQuiesceStatus::
                                thread_stack_bounds_invalid,
                            ERROR_INVALID_DATA, thread.id);
                        return false;
                    }
                    bytes_read = 0;
                    if (!ReadProcessMemory(
                            GetCurrentProcess(),
                            reinterpret_cast<const void*>(cursor),
                            words.data(), chunk_bytes, &bytes_read) ||
                        bytes_read != chunk_bytes) {
                        set_failure(
                            result,
                            PeerThreadQuiesceStatus::
                                thread_stack_read_failed,
                            GetLastError(), thread.id);
                        return false;
                    }
                    if (stack_contains_forbidden_return(
                            std::span<const std::uintptr_t>(
                                words.data(),
                                chunk_bytes / sizeof(std::uintptr_t)),
                            forbidden_stack_returns)) {
                        set_failure(
                            result,
                            PeerThreadQuiesceStatus::
                                thread_critical_return_address,
                            ERROR_BUSY, thread.id);
                        return false;
                    }
                    cursor += chunk_bytes;
                }
            }
        }
#endif
    }

    if (result != nullptr) {
        result->status = PeerThreadQuiesceStatus::acquired;
        result->system_error = ERROR_SUCCESS;
        result->thread_id = 0;
    }
    return true;
}

void SuspendedPeerThreads::resume() noexcept {
    while (thread_count_ != 0) {
        auto& thread = threads_[--thread_count_];
        if (thread.handle != nullptr) {
            if (thread.suspended) {
                ResumeThread(thread.handle);
            }
            CloseHandle(thread.handle);
        }
        thread = {};
    }
}

} // namespace wawvr::mod
