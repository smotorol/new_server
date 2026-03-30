#pragma once

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

namespace dc::cfg {

inline std::filesystem::path ResolveRuntimeConfigPath(
    const char* env_var_name,
    std::initializer_list<std::filesystem::path> relative_candidates)
{
    namespace fs = std::filesystem;

    if (env_var_name != nullptr) {
        if (const char* raw = std::getenv(env_var_name); raw != nullptr && raw[0] != '\0') {
            return fs::path(raw);
        }
    }

    const fs::path cwd = fs::current_path();
    for (const auto& candidate : relative_candidates) {
        const auto resolved = cwd / candidate;
        if (fs::exists(resolved)) {
            return resolved;
        }
    }

    if (!relative_candidates.size()) {
        return cwd;
    }

    return cwd / *relative_candidates.begin();
}

inline std::filesystem::path ResolveRuntimeConfigPath(
    const char* env_var_name,
    const std::filesystem::path& fallback_relative_path)
{
    return ResolveRuntimeConfigPath(env_var_name, { fallback_relative_path });
}

} // namespace dc::cfg
