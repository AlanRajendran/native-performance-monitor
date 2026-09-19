#include "collector.h"
#include <algorithm>
#include <dxgi1_6.h>
#include <pdhmsg.h>
#include <set>
#include <unordered_map>
#include <wrl/client.h>

namespace perf
{
using Microsoft::WRL::ComPtr;
static uint64_t luidValue(LUID l)
{
    return uint64_t(uint32_t(l.HighPart)) << 32 | l.LowPart;
}
static uint64_t fileTime(FILETIME t)
{
    return uint64_t(t.dwHighDateTime) << 32 | t.dwLowDateTime;
}
std::vector<Adapter> enumerateAdapters()
{
    std::vector<Adapter> list;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return list;
    ComPtr<IDXGIFactory6> modern;
    factory.As(&modern);
    auto primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    for (UINT i = 0; i < 32; ++i)
    {
        ComPtr<IDXGIAdapter1> a;
        HRESULT hr = modern ? modern->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                 IID_PPV_ARGS(&a))
                            : factory->EnumAdapters1(i, &a);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
            continue;
        DXGI_ADAPTER_DESC1 d{};
        if (FAILED(a->GetDesc1(&d)) || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            continue;
        Adapter row{luidValue(d.AdapterLuid), d.Description, double(d.DedicatedVideoMemory), false};
        for (UINT j = 0; j < 32; ++j)
        {
            ComPtr<IDXGIOutput> o;
            if (FAILED(a->EnumOutputs(j, &o)))
                break;
            DXGI_OUTPUT_DESC od{};
            if (SUCCEEDED(o->GetDesc(&od)) && od.Monitor == primary)
                row.primary = true;
        }
        list.push_back(std::move(row));
    }
    // Adapter preference order is supplied by DXGI. Keep it stable throughout a run.
    return list;
}
struct Counter
{
    PDH_HCOUNTER handle = nullptr;
    std::vector<BYTE> buffer;
    bool add(PDH_HQUERY q, const wchar_t *path)
    {
        return PdhAddEnglishCounterW(q, path, 0, &handle) == ERROR_SUCCESS;
    }
    double value()
    {
        if (!handle)
            return missing;
        PDH_FMT_COUNTERVALUE v{};
        auto st = PdhGetFormattedCounterValue(handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v);
        return st == ERROR_SUCCESS &&
                       (v.CStatus == PDH_CSTATUS_VALID_DATA || v.CStatus == PDH_CSTATUS_NEW_DATA) &&
                       valid(v.doubleValue)
                   ? v.doubleValue
                   : missing;
    }
    bool each(const std::function<void(std::wstring_view, double)> &f)
    {
        if (!handle)
            return false;
        DWORD bytes = DWORD(buffer.size()), count = 0;
        auto st =
            PdhGetFormattedCounterArrayW(handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count,
                                         reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data()));
        if (st == PDH_MORE_DATA)
        {
            if (bytes > 8 * 1024 * 1024)
                return false;
            buffer.resize(bytes + std::min(DWORD(4096), DWORD(8 * 1024 * 1024) - bytes));
            bytes = DWORD(buffer.size());
            st = PdhGetFormattedCounterArrayW(handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &count,
                                              reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data()));
        }
        if (st != ERROR_SUCCESS)
            return false;
        auto values = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
        for (DWORD i = 0; i < count; ++i)
        {
            const auto &v = values[i].FmtValue;
            double n = (v.CStatus == PDH_CSTATUS_VALID_DATA || v.CStatus == PDH_CSTATUS_NEW_DATA) &&
                               valid(v.doubleValue)
                           ? v.doubleValue
                           : missing;
            f(values[i].szName, n);
        }
        return true;
    }
};
struct Meta
{
    HANDLE handle = nullptr;
    uint64_t created = 0, lastCpu = 0, lastAt = 0, lastSeen = 0;
    std::wstring path, name;
    bool excluded = false;
    ~Meta()
    {
        if (handle)
            CloseHandle(handle);
    }
    Meta() = default;
    Meta(const Meta &) = delete;
};
// NVIDIA's management library ships with the driver. It is loaded only when
// the selected adapter is NVIDIA, and only from System32; without it the
// memory bus row is simply not shown.
class Nvml
{
    struct Utilization
    {
        unsigned gpu, memory;
    };
    using Init = int (*)();
    using Count = int (*)(unsigned *);
    using Handle = int (*)(unsigned, void **);
    using Name = int (*)(void *, char *, unsigned);
    using Rates = int (*)(void *, Utilization *);
    HMODULE dll_ = nullptr;
    Init shutdown_ = nullptr;
    Rates rates_ = nullptr;
    void *device_ = nullptr;

  public:
    ~Nvml()
    {
        close();
    }
    void close()
    {
        if (shutdown_)
            shutdown_();
        if (dll_)
            FreeLibrary(dll_);
        dll_ = nullptr;
        shutdown_ = nullptr;
        rates_ = nullptr;
        device_ = nullptr;
    }
    void open(const std::wstring &adapter)
    {
        close();
        if (adapter.find(L"NVIDIA") == adapter.npos)
            return;
        dll_ = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll_)
            return;
        auto init = reinterpret_cast<Init>(GetProcAddress(dll_, "nvmlInit_v2"));
        auto count = reinterpret_cast<Count>(GetProcAddress(dll_, "nvmlDeviceGetCount_v2"));
        auto handle = reinterpret_cast<Handle>(GetProcAddress(dll_, "nvmlDeviceGetHandleByIndex_v2"));
        auto name = reinterpret_cast<Name>(GetProcAddress(dll_, "nvmlDeviceGetName"));
        rates_ = reinterpret_cast<Rates>(GetProcAddress(dll_, "nvmlDeviceGetUtilizationRates"));
        if (!init || !count || !handle || !name || !rates_ || init() != 0)
        {
            FreeLibrary(dll_);
            dll_ = nullptr;
            rates_ = nullptr;
            return;
        }
        shutdown_ = reinterpret_cast<Init>(GetProcAddress(dll_, "nvmlShutdown"));
        unsigned devices = 0;
        count(&devices);

        for (unsigned i = 0; i < devices; ++i)
        {
            void *d = nullptr;
            char n[96]{};
            if (handle(i, &d) != 0)
                continue;
            // With one NVIDIA card it is that card; with several, match by name.
            if (devices == 1 ||
                (name(d, n, sizeof(n)) == 0 && adapter.find(std::wstring(n, n + strlen(n))) != adapter.npos))
            {
                device_ = d;
                break;
            }
        }
    }
    bool available() const
    {
        return device_ != nullptr;
    }
    double memoryBusy() const
    {
        Utilization u{};
        return device_ && rates_(device_, &u) == 0 ? double(std::min(u.memory, 100U)) : missing;
    }
};
struct Collector::Impl
{
    PDH_HQUERY fast = nullptr, slow = nullptr;
    Counter cpu, perCpu, engines, adapterMemory, pids, privateWorkingSet, processGpuMemory;
    Counter standbyReserve, standbyNormal, standbyCore, freeZero, pagesInput;
    Nvml nvml;
    std::map<EngineKey, std::wstring> engineTypes;
    bool busWanted = false;
    // NVML is loaded only while the memory bus row is wanted.
    void applyBus()
    {
        if (busWanted)
            nvml.open(current.gpuName);
        else
            nvml.close();
        current.memoryBusAvailable = nvml.available();
        current.memoryBus = missing;
    }
    std::vector<Adapter> adapters;
    std::unordered_map<DWORD, std::unique_ptr<Meta>> metadata;
    std::map<DWORD, std::map<EngineKey, double>> gpuWindow;
    unsigned gpuSamples = 0;
    bool gpuWindowValid = true;
    unsigned tick = 0, processors = 1;
    DWORD session = 0;
    uint64_t previousMs = 0, selected = 0;
    std::wstring windowsDir;
    Snapshot current;
    Impl()
    {
        adapters = enumerateAdapters();
        current.cores = enumerateCpuCores();
        current.cpuName = cpuBrand();
        processors = std::max(1UL, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        ProcessIdToSessionId(GetCurrentProcessId(), &session);
        wchar_t w[MAX_PATH];
        GetWindowsDirectoryW(w, MAX_PATH);
        windowsDir = w;
        PdhOpenQueryW(nullptr, 0, &fast);
        PdhOpenQueryW(nullptr, 0, &slow);
        if (fast)
        {
            if (!cpu.add(fast, L"\\Processor Information(_Total)\\% Idle Time"))
                cpu.add(fast, L"\\Processor(_Total)\\% Idle Time");
            perCpu.add(fast, L"\\Processor Information(*)\\% Idle Time");
            engines.add(fast, L"\\GPU Engine(*)\\Utilization Percentage");
            adapterMemory.add(fast, L"\\GPU Adapter Memory(*)\\Dedicated Usage");
            standbyReserve.add(fast, L"\\Memory\\Standby Cache Reserve Bytes");
            standbyNormal.add(fast, L"\\Memory\\Standby Cache Normal Priority Bytes");
            standbyCore.add(fast, L"\\Memory\\Standby Cache Core Bytes");
            freeZero.add(fast, L"\\Memory\\Free & Zero Page List Bytes");
            pagesInput.add(fast, L"\\Memory\\Pages Input/sec");
            PdhCollectQueryData(fast);
        }
        if (slow)
        {
            if (pids.add(slow, L"\\Process V2(*)\\ID Process"))
                privateWorkingSet.add(slow, L"\\Process V2(*)\\Working Set - Private");
            else
            {
                pids.add(slow, L"\\Process(*)\\ID Process");
                privateWorkingSet.add(slow, L"\\Process(*)\\Working Set - Private");
            }
            processGpuMemory.add(slow, L"\\GPU Process Memory(*)\\Dedicated Usage");
            PdhCollectQueryData(slow);
        }
    }
    ~Impl()
    {
        if (fast)
            PdhCloseQuery(fast);
        if (slow)
            PdhCloseQuery(slow);
    }
    void select(uint64_t id)
    {
        selected = id;
        current.adapter = id;
        current.vramTotal = missing;
        current.gpuName = L"GPU unavailable";
        for (auto &a : adapters)
            if (a.luid == id)
            {
                current.gpuName = a.name;
                current.vramTotal = a.dedicated;
            }
        current.history.clearGpu();
        current.engines.clear();
        current.memoryMetrics.clear();
        engineTypes.clear();
        applyBus();
        current.memoryBusAvailable = nvml.available();
        gpuWindow.clear();
        gpuSamples = 0;
        gpuWindowValid = true;
        for (auto &r : current.apps)
        {
            r.gpu = r.vram = missing;
        }
    }
    // Who holds RAM and VRAM this second: the cache and free lists from the
    // memory counters, and the applications holding the most of either.
    void sampleMemory(uint64_t now, bool ok)
    {
        auto sum = [](std::initializer_list<double> values)
        {
            double total = 0;
            for (double v : values)
            {
                if (!valid(v))
                    return missing;
                total += v;
            }
            return total;
        };
        current.ramCache =
            ok ? sum({standbyReserve.value(), standbyNormal.value(), standbyCore.value()}) : missing;
        current.ramFree = ok ? freeZero.value() : missing;
        current.paging = ok ? pagesInput.value() * 4096 : missing;
        current.memoryBus = nvml.available() ? nvml.memoryBusy() : missing;
        current.memoryMetrics.push(int64_t(now / 1000),
                                   {current.paging, current.memoryBus, missing, missing});

        MemorySample m;
        if (valid(current.ramTotal) && valid(current.ramCache) && valid(current.ramFree))
        {
            m.cache = float(current.ramCache);
            m.free = float(current.ramFree);
            m.inUse = float(std::max(0.0, current.ramTotal - current.ramCache - current.ramFree));
        }
        if (valid(current.vramUsed))
            m.vramUsed = float(current.vramUsed);
        // The largest holders of each kind, so the owners drawn later are
        // almost always present in every past sample.
        std::vector<const AppRow *> byRam, byVram;
        for (auto &a : current.apps)
        {
            byRam.push_back(&a);
            byVram.push_back(&a);
        }
        auto amount = [](double v) { return valid(v) ? v : 0.0; };
        std::sort(byRam.begin(), byRam.end(),
                  [&](const AppRow *a, const AppRow *b) { return amount(a->ram) > amount(b->ram); });
        std::sort(byVram.begin(), byVram.end(),
                  [&](const AppRow *a, const AppRow *b) { return amount(a->vram) > amount(b->vram); });
        auto add = [&](const AppRow *a)
        {
            const auto id = ownerId(a->key);
            if (m.count == memoryOwners || m.find(id))
                return;
            m.owners[m.count++] = {id, float(amount(a->ram)), float(amount(a->vram))};
        };
        // The apps named in the panel first, then the largest holders of each.
        for (size_t i = 0; i < current.apps.size() && i < 3; ++i)
            add(&current.apps[i]);
        for (size_t i = 0; i < byRam.size() && i < memoryOwners / 2; ++i)
            add(byRam[i]);
        for (size_t i = 0; i < byVram.size() && m.count < memoryOwners; ++i)
            add(byVram[i]);
        current.memory.push(int64_t(now / 1000), m);
    }
    Meta *getMeta(DWORD pid, uint64_t now)
    {
        if (auto it = metadata.find(pid); it != metadata.end())
        {
            DWORD code = 0;
            if (it->second->handle && GetExitCodeProcess(it->second->handle, &code) && code == STILL_ACTIVE)
            {
                it->second->lastSeen = now;
                return it->second.get();
            }
            metadata.erase(it);
        }
        if (metadata.size() >= 4096)
            return nullptr;
        DWORD sid = 0;
        if (!ProcessIdToSessionId(pid, &sid) || sid != session || pid == 0 || pid == 4)
            return nullptr;
        auto m = std::make_unique<Meta>();
        m->lastSeen = now;
        m->handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!m->handle)
            return nullptr; // Protected identities are not misattributed to a guessed executable.
        wchar_t path[32768];
        DWORD len = 32768;
        if (QueryFullProcessImageNameW(m->handle, 0, path, &len))
            m->path.assign(path, len);
        m->name = fileName(m->path);
        if (m->name.empty())
            m->name = L"Process " + std::to_wstring(pid);
        else if (m->name.ends_with(L".exe"))
            m->name.resize(m->name.size() - 4);
        if (m->name == L"msedge")
            m->name = L"Edge";
        else if (m->name == L"chrome")
            m->name = L"Chrome";
        else if (m->name == L"Code")
            m->name = L"VS Code";
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (GetProcessTimes(m->handle, &creation, &exit, &kernel, &user))
        {
            m->created = fileTime(creation);
            m->lastCpu = fileTime(kernel) + fileTime(user);
            m->lastAt = now;
        }
        m->excluded = isBaseWindowsProcess(m->path, windowsDir, pid);
        auto result = m.get();
        metadata.emplace(pid, std::move(m));
        return result;
    }
    void collectProcesses(uint64_t now, bool intervalValid)
    {
        const bool queryOk = slow && PdhCollectQueryData(slow) == ERROR_SUCCESS;
        std::map<std::wstring, DWORD> ids;
        std::unordered_map<DWORD, double> ram, vram;
        bool idOk = queryOk && pids.each(
                                   [&](std::wstring_view name, double value)
                                   {
                                       if (valid(value) && value > 0 && value <= UINT32_MAX)
                                           ids.emplace(name, DWORD(value));
                                   });
        bool ramOk = queryOk && privateWorkingSet.each(
                                    [&](std::wstring_view name, double value)
                                    {
                                        auto it = ids.find(std::wstring(name));
                                        if (it != ids.end())
                                            ram[it->second] = value;
                                    });
        bool vramOk =
            queryOk && processGpuMemory.each(
                           [&](std::wstring_view name, double value)
                           {
                               auto i = parseGpuInstance(name);
                               if (i && i->hasPid && i->key.adapter == selected && i->key.physical == 0)
                               {
                                   if (!valid(value))
                                       vram[i->pid] = missing;
                                   else if (!vram.contains(i->pid))
                                       vram[i->pid] = value;
                                   else if (valid(vram[i->pid]))
                                       vram[i->pid] += value;
                               }
                           });
        std::vector<ProcessRow> rows;
        rows.reserve(std::min(ids.size(), size_t(512)));
        std::set<DWORD> seen;
        unsigned denied = 0;
        for (const auto &[instance, pid] : ids)
        {
            (void)instance;
            if (!seen.insert(pid).second)
                continue;
            auto m = getMeta(pid, now);
            if (!m)
            {
                ++denied;
                continue;
            }
            if (m->excluded)
                continue;
            ProcessRow row;
            row.pid = pid;
            row.created = m->created;
            row.path = m->path;
            row.name = m->name;
            FILETIME cr{}, ex{}, kt{}, ut{};
            if (GetProcessTimes(m->handle, &cr, &ex, &kt, &ut))
            {
                auto cpuTime = fileTime(kt) + fileTime(ut);
                if (intervalValid && m->lastAt > 0 && m->lastAt < now && m->created == fileTime(cr))
                    row.cpu = cpuPercent(m->lastCpu, cpuTime, double(now - m->lastAt) / 1000.0, processors);
                m->lastCpu = cpuTime;
                m->lastAt = now;
                m->created = fileTime(cr);
            }
            row.ram = ramOk && ram.contains(pid) ? ram[pid] : missing;
            row.vram = vramOk && valid(current.vramTotal) && current.vramTotal > 0
                           ? (vram.contains(pid) ? vram[pid] : 0)
                           : missing;
            row.gpuAvailable = intervalValid && gpuWindowValid && gpuSamples > 0;
            if (auto it = gpuWindow.find(pid); it != gpuWindow.end())
                for (auto &[key, n] : it->second)
                    row.engines[key] = n / double(std::max(1U, gpuSamples));
            rows.push_back(std::move(row));
        }
        current.apps = rankApplications(rows, current.vramTotal, current.ramTotal);
        current.processes = unsigned(rows.size());
        current.rankUpdatedMs = now;
        if (!idOk || !ramOk)
            current.status += L"Process memory unavailable. ";
        if (!vramOk)
            current.status += L"Application VRAM unavailable. ";
        if (denied)
            current.status += std::to_wstring(denied) + L" protected/other-session identities omitted. ";
        if (metadata.size() >= 4096)
            current.status += L"Process cache capacity reached; ranking incomplete. ";
        for (auto it = metadata.begin(); it != metadata.end();)
        {
            if (now - it->second->lastSeen > 6000)
                it = metadata.erase(it);
            else
                ++it;
        }
        gpuWindow.clear();
        gpuSamples = 0;
        gpuWindowValid = true;
    }
    Snapshot sample(uint64_t now, bool reset)
    {
        bool intervalValid = previousMs && now - previousMs < 2500 && !reset;
        current.status.clear();
        if (reset || (previousMs && now - previousMs >= 2500))
        {
            gpuWindow.clear();
            gpuSamples = 0;
            gpuWindowValid = true;
            for (auto &[pid, m] : metadata)
            {
                (void)pid;
                m->lastAt = 0;
            }
        }
        bool fastOk = fast && PdhCollectQueryData(fast) == ERROR_SUCCESS;
        if (tick % 60 == 0 || GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) != processors)
        {
            auto discovered = enumerateCpuCores();
            bool changed = discovered.size() != current.cores.size();
            if (!changed)
                for (size_t i = 0; i < discovered.size(); ++i)
                    changed = changed || discovered[i].logical != current.cores[i].logical ||
                              discovered[i].efficiencyClass != current.cores[i].efficiencyClass;
            if (changed && !discovered.empty())
                current.cores = std::move(discovered);
            processors = std::max(1UL, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        }
        std::map<std::pair<unsigned, unsigned>, double> logicalValues;
        bool coresOk = fastOk && perCpu.each(
                                     [&](std::wstring_view name, double value)
                                     {
                                         auto key = parseCpuInstance(name);
                                         if (key)
                                             logicalValues[*key] = valid(value)
                                                                       ? std::clamp(100.0 - value, 0.0, 100.0)
                                                                       : missing;
                                     });
        for (auto &core : current.cores)
        {
            core.current = coresOk && intervalValid ? coreUtilization(core, logicalValues) : missing;
            core.history.push(int64_t(now / 1000), {core.current, missing, missing, missing});
        }
        if (!coresOk)
            current.status += L"Per-core counters unavailable. ";
        current.current = noMetrics;
        current.current[0] = fastOk && intervalValid ? cpu.value() : missing;
        if (valid(current.current[0]))
            current.current[0] = std::clamp(100.0 - current.current[0], 0.0, 100.0);
        MEMORYSTATUSEX mem{sizeof(mem)};
        if (GlobalMemoryStatusEx(&mem))
        {
            current.ramTotal = double(mem.ullTotalPhys);
            current.ramUsed = double(mem.ullTotalPhys - mem.ullAvailPhys);
            current.current[3] = capacityPercent(current.ramUsed, current.ramTotal);
        }
        std::map<EngineKey, double> global;
        bool unknownNode = false, badEngine = false;
        const bool engineOk =
            fastOk && engines.each(
                          [&](std::wstring_view name, double value)
                          {
                              auto i = parseGpuInstance(name);
                              if (!i || i->key.adapter != selected || !i->hasEngine || !i->hasPid)
                                  return;
                              if (i->key.physical != 0)
                              {
                                  unknownNode = true;
                                  return;
                              }
                              if (!valid(value))
                              {
                                  badEngine = true;
                                  return;
                              }
                              global[i->key] += value;
                              gpuWindow[i->pid][i->key] += value;
                              if (!engineTypes.contains(i->key))
                                  engineTypes[i->key] = engineType(name);
                          });
        const bool gpuOk = engineOk && !badEngine && !unknownNode && intervalValid && selected != 0;
        current.current[1] = gpuOk ? busiestEngine(global) : missing;
        // Per engine type, the busiest engine of that type. A type stays listed
        // once seen, so rows never appear and vanish from one second to the next.
        std::map<std::wstring, double> byType;
        for (auto &[key, type] : engineTypes)
            if (engineLabel(type))
                byType[type] = std::max(byType[type], global.contains(key) ? global[key] : 0.0);
        for (auto &[type, value] : byType)
        {
            auto it = std::find_if(current.engines.begin(), current.engines.end(),
                                   [&](const GpuEngine &e) { return e.type == type; });
            if (it == current.engines.end())
            {
                auto label = engineLabel(type);
                GpuEngine e;
                e.type = type;
                e.order = label->first;
                e.label = label->second;
                it = current.engines.insert(std::upper_bound(current.engines.begin(), current.engines.end(),
                                                             e, [](const GpuEngine &a, const GpuEngine &b)
                                                             { return a.order < b.order; }),
                                            std::move(e));
            }
        }
        for (auto &e : current.engines)
        {
            e.current =
                gpuOk ? std::clamp(byType.contains(e.type) ? byType[e.type] : 0.0, 0.0, 100.0) : missing;
            e.history.push(int64_t(now / 1000), {e.current, missing, missing, missing});
        }
        gpuWindowValid = gpuWindowValid && gpuOk;
        ++gpuSamples;
        double dedicated = 0;
        bool found = false, badMemory = false;
        const bool memOk = fastOk && adapterMemory.each(
                                         [&](std::wstring_view name, double value)
                                         {
                                             auto i = parseGpuInstance(name);
                                             if (!i || i->key.adapter != selected)
                                                 return;
                                             if (i->key.physical != 0)
                                             {
                                                 unknownNode = true;
                                                 return;
                                             }
                                             if (!valid(value))
                                             {
                                                 badMemory = true;
                                                 return;
                                             }
                                             dedicated += value;
                                             found = true;
                                         });
        current.vramUsed =
            memOk && found && !badMemory && !unknownNode && valid(current.vramTotal) && current.vramTotal > 0
                ? dedicated
                : missing;
        current.current[2] = capacityPercent(current.vramUsed, current.vramTotal);
        if (!fastOk)
            current.status += L"Performance query unavailable. ";
        if (!valid(current.current[0]))
            current.status += L"CPU baseline/unavailable. ";
        if (!gpuOk)
            current.status += L"GPU baseline/unavailable. ";
        if (unknownNode)
            current.status += L"Linked GPU nodes unsupported; GPU/VRAM unavailable. ";
        if (unknownNode)
        {
            current.current[1] = current.current[2] = current.vramUsed = missing;
            gpuWindowValid = false;
        }
        if ((++tick % 2) == 0)
            collectProcesses(now, intervalValid);
        sampleMemory(now, fastOk && intervalValid);
        current.updatedMs = now;
        current.history.push(int64_t(now / 1000), current.current);
        previousMs = now;
        return current;
    }
};
Collector::Collector()
{
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}
Collector::~Collector()
{
    stop();
    if (stop_)
        CloseHandle(stop_);
}
void Collector::start(uint64_t adapter, HWND notify, UINT message)
{
    adapter_ = adapter;
    ResetEvent(stop_);
    thread_ = std::thread(
        [this, notify, message]
        {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            impl_ = std::make_unique<Impl>();
            impl_->busWanted = memoryBus_;
            impl_->select(adapter_);
            HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
            LARGE_INTEGER due{};
            due.QuadPart = -10000000;
            if (timer)
                SetWaitableTimerEx(timer, &due, 1000, nullptr, nullptr, nullptr, 50);
            HANDLE waits[] = {stop_, timer};
            while (true)
            {
                auto wait = timer ? WaitForMultipleObjects(2, waits, FALSE, INFINITE)
                                  : WaitForSingleObject(stop_, 1000);
                if (wait == WAIT_OBJECT_0)
                    break;
                auto now = GetTickCount64();
                bool reset = reset_.exchange(false);
                if (impl_->busWanted != memoryBus_)
                {
                    impl_->busWanted = memoryBus_;
                    impl_->applyBus();
                }
                if (impl_->selected != adapter_)
                {
                    impl_->select(adapter_);
                    reset = true;
                }
                Snapshot next;
                if (paused_)
                {
                    next = impl_->current;
                    next.paused = true;
                    next.status = L"Paused";
                }
                else
                {
                    next = impl_->sample(now, reset);
                    next.paused = false;
                }
                {
                    std::lock_guard lock(mutex_);
                    latest_ = std::move(next);
                }
                if (notify)
                    PostMessageW(notify, message, 0, 0);
            }
            if (timer)
            {
                CancelWaitableTimer(timer);
                CloseHandle(timer);
            }
            impl_.reset();
            CoUninitialize();
        });
}
void Collector::stop()
{
    if (thread_.joinable())
    {
        SetEvent(stop_);
        thread_.join();
    }
}
Snapshot Collector::snapshot() const
{
    std::lock_guard lock(mutex_);
    return latest_;
}
} // namespace perf
