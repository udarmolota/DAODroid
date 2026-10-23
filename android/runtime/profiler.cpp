// See profiler.h.
#include "stdafx.h"

#include "profiler.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <ctime>
#include <dirent.h>
#include <dlfcn.h>
#include <link.h>
#include <map>
#include <string>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <cxxabi.h>

#include "log.h"

namespace dao::profiler
{
    namespace
    {
        // One sample: the thread, the PC and the return addresses up the frame-pointer
        // chain (the NDK keeps frame pointers on arm64; prebuilt libraries without them
        // end the chain early).
        constexpr size_t kRingSize = 1 << 18;
        constexpr int kMaxFrames = 8;
        struct Sample
        {
            uint32_t tid;
            uint32_t depth;
            uint64_t frames[kMaxFrames]; // [0] = pc
        };
        Sample* s_ring;
        std::atomic<uint32_t> s_writeIndex{ 0 };
        uint32_t s_readIndex = 0;
        std::atomic<uint32_t> s_dropped{ 0 };

        void OnSample(int, siginfo_t*, void* context)
        {
            const auto* uc = static_cast<ucontext_t*>(context);
            const uint32_t index = s_writeIndex.fetch_add(1, std::memory_order_relaxed);
            if (index - s_readIndex >= kRingSize)
            {
                s_dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            Sample& sample = s_ring[index % kRingSize];
            sample.tid = uint32_t(gettid());
            sample.frames[0] = uc->uc_mcontext.pc;
            int depth = 1;
            // Walk the frame pointers: each frame holds [previous fp, return address], and
            // the chain must only go up the stack, by less than a megabyte per step.
            uint64_t fp = uc->uc_mcontext.regs[29];
            const uint64_t sp = uc->uc_mcontext.sp;
            uint64_t low = sp;
            while (depth < kMaxFrames && fp > low && fp - low < (1u << 20) && (fp & 7) == 0)
            {
                const uint64_t* frame = reinterpret_cast<const uint64_t*>(fp);
                const uint64_t ret = frame[1];
                if (!ret)
                    break;
                sample.frames[depth++] = ret;
                low = fp;
                fp = frame[0];
            }
            sample.depth = uint32_t(depth);
        }

        // Linux's per-thread CPU clock id (glibc's pthread_getcpuclockid encoding).
        clockid_t ThreadCpuClock(pid_t tid)
        {
            return clockid_t((~tid) << 3) | 6;
        }

        std::unordered_map<pid_t, timer_t> s_timers;

        void AttachNewThreads()
        {
            DIR* dir = opendir("/proc/self/task");
            if (!dir)
                return;
            while (dirent* entry = readdir(dir))
            {
                const pid_t tid = atoi(entry->d_name);
                if (tid <= 0 || tid == gettid() || s_timers.count(tid))
                    continue;
                sigevent ev{};
                ev.sigev_notify = SIGEV_THREAD_ID;
                ev.sigev_signo = SIGPROF;
                ev.sigev_notify_thread_id = tid;
                timer_t timer;
                if (timer_create(ThreadCpuClock(tid), &ev, &timer) != 0)
                    continue;
                itimerspec spec{};
                spec.it_interval.tv_nsec = 1000000; // 1 ms of the thread's CPU time
                spec.it_value = spec.it_interval;
                timer_settime(timer, 0, &spec, nullptr);
                s_timers[tid] = timer;
            }
            closedir(dir);
        }

        std::string ThreadName(pid_t tid)
        {
            char path[64];
            snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
            FILE* f = fopen(path, "r");
            if (!f)
                return "gone";
            char name[32] = {};
            if (!fgets(name, sizeof(name), f))
                name[0] = 0;
            fclose(f);
            std::string s(name);
            while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
                s.pop_back();
            return s;
        }

        std::string Symbolize(uint64_t pc, uint64_t& offset)
        {
            Dl_info info{};
            if (!dladdr(reinterpret_cast<void*>(pc), &info) || !info.dli_fname)
            {
                offset = pc;
                return "?";
            }
            offset = pc - reinterpret_cast<uint64_t>(info.dli_fbase);
            std::string lib = info.dli_fname;
            if (const auto slash = lib.rfind('/'); slash != std::string::npos)
                lib = lib.substr(slash + 1);
            if (!info.dli_sname)
                return lib + "+" + fmt::format("{:#x}", offset);
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
            std::string name = status == 0 && demangled ? demangled : info.dli_sname;
            free(demangled);
            // The recompiled guest functions: keep only sub_<address>.
            if (name.rfind("sub_", 0) == 0)
                if (const auto paren = name.find('('); paren != std::string::npos)
                    name.resize(paren);
            if (name.size() > 90)
                name.resize(90);
            return name;
        }

        void Report()
        {
            // Drain the ring into per-thread function histograms (by the function's
            // start address, so samples inside one function add up).
            const uint32_t end = s_writeIndex.load(std::memory_order_acquire);
            std::map<pid_t, std::unordered_map<uint64_t, uint32_t>> perThread, inclusive;
            std::map<pid_t, uint32_t> totals;
            std::unordered_map<uint64_t, uint64_t> functionOf; // address -> function start
            auto function = [&](uint64_t pc) {
                auto it = functionOf.find(pc);
                if (it != functionOf.end())
                    return it->second;
                Dl_info info{};
                const uint64_t key = dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_saddr
                    ? reinterpret_cast<uint64_t>(info.dli_saddr) : pc;
                functionOf[pc] = key;
                return key;
            };
            for (uint32_t i = s_readIndex; i != end; i++)
            {
                const Sample& sample = s_ring[i % kRingSize];
                const pid_t tid = pid_t(sample.tid);
                perThread[tid][function(sample.frames[0])]++;
                totals[tid]++;
                // Inclusive: every function on the stack gets the sample once.
                uint64_t seen[kMaxFrames];
                int seenCount = 0;
                for (uint32_t d = 0; d < sample.depth; d++)
                {
                    const uint64_t key = function(sample.frames[d]);
                    bool dup = false;
                    for (int k = 0; k < seenCount; k++)
                        dup |= seen[k] == key;
                    if (dup)
                        continue;
                    seen[seenCount++] = key;
                    inclusive[tid][key]++;
                }
            }
            s_readIndex = end;
            uint32_t grand = 0;
            for (const auto& [tid, n] : totals)
                grand += n;
            // Linux delivers CPU-clock timers at the scheduler tick, so a sample is ~4 ms of
            // CPU time on this phone, not the 1 ms asked for; the shares are what matter.
            LOGI("==== profile: {} samples (~4 ms of CPU time each, {} dropped) ====", grand, s_dropped.exchange(0));
            // Threads by CPU time, hottest first.
            std::vector<std::pair<uint32_t, pid_t>> order;
            for (const auto& [tid, n] : totals)
                order.emplace_back(n, tid);
            std::sort(order.rbegin(), order.rend());
            for (const auto& [n, tid] : order)
            {
                if (n * 50 < grand) // under 2 % of the process: skip
                    continue;
                LOGI("-- {} (tid {}): {} samples, {:.0f}% of the process", ThreadName(tid), tid, n, 100.0 * n / grand);
                std::vector<std::pair<uint32_t, uint64_t>> funcs;
                for (const auto& [key, count] : perThread[tid])
                    funcs.emplace_back(count, key);
                std::sort(funcs.rbegin(), funcs.rend());
                int shown = 0;
                for (const auto& [count, key] : funcs)
                {
                    if (shown++ >= 20 || count * 200 < n) // 20 rows or under 0.5 % of the thread
                        break;
                    uint64_t offset;
                    const std::string name = Symbolize(key, offset);
                    LOGI("   {:5.1f}%  {:>5} smp  {}  (+{:#x})", 100.0 * count / n, count, name, offset);
                }
                // With callers: functions by the samples anywhere under them.
                funcs.clear();
                for (const auto& [key, count] : inclusive[tid])
                    funcs.emplace_back(count, key);
                std::sort(funcs.rbegin(), funcs.rend());
                LOGI("   inclusive (self + callees):");
                shown = 0;
                for (const auto& [count, key] : funcs)
                {
                    if (shown++ >= 20 || count * 50 < n) // 20 rows or under 2 % of the thread
                        break;
                    uint64_t offset;
                    const std::string name = Symbolize(key, offset);
                    LOGI("   {:5.1f}%  {:>5} smp  {}  (+{:#x})", 100.0 * count / n, count, name, offset);
                }
            }
        }
    }

    void Start(int reportSeconds)
    {
        s_ring = new Sample[kRingSize];
        struct sigaction sa{};
        sa.sa_sigaction = OnSample;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGPROF, &sa, nullptr);
        std::thread([reportSeconds] {
            pthread_setname_np(pthread_self(), "DAO profiler");
            for (;;)
            {
                AttachNewThreads();
                std::this_thread::sleep_for(std::chrono::seconds(reportSeconds));
                Report();
            }
        }).detach();
        LOGI("Profiler: sampling every thread's CPU time, report every {} s", reportSeconds);
    }
}
