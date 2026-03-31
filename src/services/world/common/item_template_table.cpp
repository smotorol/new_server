#include "services/world/common/item_template_table.h"

namespace svr {

	const ItemTemplate* ItemTemplateTable::Find(std::uint32_t item_id) const
	{
		const auto it = index_by_item_id_.find(item_id);
		if (it == index_by_item_id_.end() || it->second >= templates_.size()) {
			return nullptr;
		}
		return &templates_[it->second];
	}

	void ItemTemplateTable::Reset(bool loaded, std::vector<ItemTemplate> templates)
	{
		loaded_ = loaded;
		templates_ = std::move(templates);
		index_by_item_id_.clear();
		for (std::size_t i = 0; i < templates_.size(); ++i) {
			index_by_item_id_[templates_[i].item_id] = i;
		}
	}

	ItemTemplateTable& GetMutableCanonicalItemTemplateTable()
	{
		static ItemTemplateTable table{};
		return table;
	}

	const ItemTemplateTable& GetCanonicalItemTemplateTable()
	{
		return GetMutableCanonicalItemTemplateTable();
	}

} // namespace svr
