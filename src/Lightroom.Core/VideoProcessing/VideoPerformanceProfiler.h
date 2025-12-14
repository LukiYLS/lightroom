#pragma once

#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>

namespace LightroomCore {

    class VideoPerformanceProfiler {
    public:
        struct TimingInfo {
            std::string name;
            double timeMs;
            int64_t frameIndex;
        };

        static VideoPerformanceProfiler& GetInstance() {
            static VideoPerformanceProfiler instance;
            return instance;
        }

        void StartTiming(const std::string& name, int64_t frameIndex = -1) {
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            m_Frequency = freq.QuadPart;

            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            m_StartTimes[name] = counter.QuadPart;
            m_CurrentFrameIndex = frameIndex;
        }

        void EndTiming(const std::string& name) {
            auto it = m_StartTimes.find(name);
            if (it == m_StartTimes.end()) {
                return;
            }

            LARGE_INTEGER endCounter;
            QueryPerformanceCounter(&endCounter);

            double elapsedMs = (double)(endCounter.QuadPart - it->second) * 1000.0 / m_Frequency;

            TimingInfo info;
            info.name = name;
            info.timeMs = elapsedMs;
            info.frameIndex = m_CurrentFrameIndex;
            m_Timings.push_back(info);

            m_StartTimes.erase(it);
        }

        void RecordTiming(const std::string& name, double timeMs, int64_t frameIndex = -1) {
            TimingInfo info;
            info.name = name;
            info.timeMs = timeMs;
            info.frameIndex = frameIndex;
            m_Timings.push_back(info);
        }

        void PrintStatistics(int frameCount = 100) {
            if (m_Timings.empty()) {
                OutputDebugStringW(L"[VideoPerformanceProfiler] No timing data to print\n");
                return;
            }

            std::map<std::string, std::vector<double>> timingMap;
            for (const auto& timing : m_Timings) {
                timingMap[timing.name].push_back(timing.timeMs);
            }

            // 使用 OutputDebugString 输出到 VS 输出面板
            std::wstringstream wss;
            wss << L"\n========== Video Performance Statistics (Last " << frameCount << L" frames) ==========\n";
            
            for (const auto& pair : timingMap) {
                const std::string& name = pair.first;
                const std::vector<double>& times = pair.second;
                
                if (times.empty()) continue;

                double sum = 0.0;
                double min = times[0];
                double max = times[0];
                for (double t : times) {
                    sum += t;
                    if (t < min) min = t;
                    if (t > max) max = t;
                }
                double avg = sum / times.size();
                
                std::vector<double> sortedTimes = times;
                std::sort(sortedTimes.begin(), sortedTimes.end());
                double median = sortedTimes[sortedTimes.size() / 2];

                // 转换 name 为宽字符串
                int nameLen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
                std::vector<wchar_t> nameWide(nameLen);
                MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nameWide.data(), nameLen);
                
                wss << std::fixed << std::setprecision(2);
                wss << nameWide.data() << L":\n";
                wss << L"  Count: " << times.size() << L"\n";
                wss << L"  Avg:   " << avg << L" ms\n";
                wss << L"  Min:   " << min << L" ms\n";
                wss << L"  Max:   " << max << L" ms\n";
                wss << L"  Median: " << median << L" ms\n";
                wss << L"\n";
            }

            wss << L"================================================================\n";
            
            // 输出到 VS 输出面板
            std::wstring output = wss.str();
            OutputDebugStringW(output.c_str());
        }

        void ClearOldRecords(int keepCount = 200) {
            if (m_Timings.size() > keepCount) {
                m_Timings.erase(m_Timings.begin(), m_Timings.end() - keepCount);
            }
        }

        void Clear() {
            m_Timings.clear();
            m_StartTimes.clear();
        }

    private:
        VideoPerformanceProfiler() : m_Frequency(0), m_CurrentFrameIndex(-1) {}
        ~VideoPerformanceProfiler() {}

        std::vector<TimingInfo> m_Timings;
        std::map<std::string, int64_t> m_StartTimes;
        int64_t m_Frequency;
        int64_t m_CurrentFrameIndex;
    };

    class ScopedTimer {
    public:
        ScopedTimer(const std::string& name, int64_t frameIndex = -1)
            : m_Name(name) {
            VideoPerformanceProfiler::GetInstance().StartTiming(name, frameIndex);
        }

        ~ScopedTimer() {
            VideoPerformanceProfiler::GetInstance().EndTiming(m_Name);
        }

    private:
        std::string m_Name;
    };

} // namespace LightroomCore