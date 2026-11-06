// SPDX-License-Identifier: AGPL-3.0-only
//
// Opt-in boot-path markers. The external boot harness supplies timestamps;
// these markers provide stable stage boundaries and elapsed durations without
// adding normal-session log noise.

#pragma once

#include "log.hpp"

#include <chrono>
#include <cstdlib>

namespace BootProf
{

inline bool enabled()
{
	static const bool value = [] {
		const char* setting = std::getenv("SLSSTEAM_BOOTPROF");
		return setting && *setting && *setting != '0';
	}();
	return value;
}

class Span
{
public:
	Span(CLog* log, const char* stage)
		: m_log(log), m_stage(stage), m_start(std::chrono::steady_clock::now()),
		  m_enabled(enabled() && log != nullptr)
	{
		if (m_enabled)
			m_log->info("BOOTPROF event=begin stage=%s\n", m_stage);
	}

	~Span() noexcept
	{
		if (!m_enabled) return;
		try
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - m_start).count();
			m_log->info("BOOTPROF event=end stage=%s elapsed_ms=%lld\n",
			             m_stage, static_cast<long long>(elapsed));
		}
		catch (...)
		{
			// Profiling must never affect the Steam callback boundary.
		}
	}

	Span(const Span&) = delete;
	Span& operator=(const Span&) = delete;

private:
	CLog* m_log;
	const char* m_stage;
	std::chrono::steady_clock::time_point m_start;
	bool m_enabled;
};

} // namespace BootProf
