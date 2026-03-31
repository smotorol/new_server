#include "services/world/runtime/world_runtime_private.h"

namespace svr {

bool WorldRuntime::IsDungeonMapTemplate_(std::uint32_t map_template_id) noexcept
{
	return map_template_id >= 2000;
}

std::optional<MapAssignmentEntry> WorldRuntime::TryGetMapAssignment(
	std::uint32_t map_template_id,
	std::uint32_t instance_id) const
{
	const auto key = MakeMapAssignmentKey_(map_template_id, instance_id);
	if (auto it = map_assignments_.find(key); it != map_assignments_.end()) {
		return it->second;
	}
	return std::nullopt;
}

std::uint32_t WorldRuntime::ResolvePortalTargetInstanceId_(std::uint32_t map_template_id, std::uint32_t requested_instance_id)
{
	if (requested_instance_id != 0) {
		return requested_instance_id;
	}
	if (!IsDungeonMapTemplate_(map_template_id)) {
		return 0;
	}
	auto& next_id = next_dynamic_instance_id_by_map_[map_template_id];
	if (next_id == 0) {
		next_id = 1;
	}
	return next_id++;
}

} // namespace svr
