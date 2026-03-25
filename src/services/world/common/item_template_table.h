#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace svr {

	struct ItemTemplate {
		std::uint32_t item_id = 0;
		int equip_part = 0;
		int equip_tribe = 0;
		int attack = 0;
		int defense = 0;
		int life = 0;
		int mana = 0;
		int vitality = 0;
		int ki = 0;
	};

	class ItemTemplateTable {
	public:
		const ItemTemplate* Find(std::uint32_t item_id) const;
		[[nodiscard]] bool loaded() const noexcept { return loaded_; }
		[[nodiscard]] bool empty() const noexcept { return templates_.empty(); }
		[[nodiscard]] std::size_t size() const noexcept { return templates_.size(); }
		void Reset(bool loaded, std::vector<ItemTemplate> templates);

	private:
		bool loaded_ = false;
		std::vector<ItemTemplate> templates_{};
		std::unordered_map<std::uint32_t, std::size_t> index_by_item_id_{};
	};

	ItemTemplateTable& GetMutableCanonicalItemTemplateTable();
	const ItemTemplateTable& GetCanonicalItemTemplateTable();

} // namespace svr
