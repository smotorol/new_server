#pragma once

#include <cstdint>

namespace svr {

	struct AppearanceSummary
	{
		std::uint32_t appearance_code = 0;
		std::uint8_t gender = 0;
		std::uint8_t face_style = 0;
		std::uint8_t hair_style = 0;
		std::uint8_t hair_color = 0;
	};

	struct EquipSummary
	{
		std::uint32_t weapon_template_id = 0;
		std::uint32_t armor_template_id = 0;
		std::uint32_t costume_template_id = 0;
		std::uint32_t accessory_template_id = 0;
	};

} // namespace svr
