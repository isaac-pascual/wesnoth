/*
	Copyright (C) 2008 - 2022
	by Mark de Wever <koraq@xs4all.nl>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "gui/widgets/minimap.hpp"

#include "gui/core/log.hpp"
#include "gui/core/widget_definition.hpp"
#include "gui/core/window_builder.hpp"
#include "gui/core/register_widget.hpp"
#include "gui/widgets/settings.hpp"
#include "map/map.hpp"
#include "map/exception.hpp"
#include "sdl/rect.hpp"
#include "../../minimap.hpp" // We want the file in src/

#include <functional>

#include <algorithm>

static lg::log_domain log_config("config");
#define ERR_CF LOG_STREAM_INDENT(err, log_config)

#define LOG_SCOPE_HEADER get_control_type() + " [" + id() + "] " + __func__
#define LOG_HEADER LOG_SCOPE_HEADER + ':'

// Define this to enable debug output for the minimap cache.
//#define DEBUG_MINIMAP_CACHE

namespace gui2
{

// ------------ WIDGET -----------{

REGISTER_WIDGET(minimap)

minimap::minimap(const implementation::builder_minimap& builder)
	: styled_widget(builder, type())
	, map_data_()
{
}

void minimap::set_active(const bool /*active*/)
{
	/* DO NOTHING */
}

bool minimap::get_active() const
{
	return true;
}

unsigned minimap::get_state() const
{
	return 0;
}

/** Key type for the cache. */
struct key_type
{
	key_type(const int w, const int h, const std::string& map_data)
		: w(w), h(h), map_data(map_data)
	{
	}

	/** Width of the image. */
	const int w;

	/** Height of the image. */
	const int h;

	/** The data used to generate the image. */
	const std::string map_data;
};

static bool operator<(const key_type& lhs, const key_type& rhs)
{
	return std::tie(lhs.w, lhs.h, lhs.map_data) < std::tie(rhs.w, rhs.h, rhs.map_data);
}

/** Value type for the cache. */
struct value_type
{
	value_type(const surface& surf) : surf(surf), age(1)
	{
	}

	/** The cached image. */
	const surface surf;

	/**
	 * The age of the image.
	 *
	 * Every time an image is used its age is increased by one. Once the cache
	 * is full 25% of the cache is emptied. This is done by halving the age of
	 * the items in the cache and then erase the 25% with the lowest age. If
	 * items have the same age their order is unspecified.
	 */
	unsigned age;
};

/**
 * Maximum number of items in the cache (multiple of 4).
 *
 * No testing on the optimal number is done, just seems a nice number.
 */
static const size_t cache_max_size = 100;

/** The cache. */
typedef std::map<key_type, value_type> tcache;
static tcache cache;

static bool compare(const std::pair<unsigned, tcache::iterator>& lhs,
					const std::pair<unsigned, tcache::iterator>& rhs)
{
	return lhs.first < rhs.first;
}

static void shrink_cache()
{
#ifdef DEBUG_MINIMAP_CACHE
	std::cerr << "\nShrink cache from " << cache.size();
#else
	DBG_GUI_D << "Shrinking the minimap cache.\n";
#endif

	std::vector<std::pair<unsigned, tcache::iterator>> items;
	for(tcache::iterator itor = cache.begin(); itor != cache.end(); ++itor) {

		itor->second.age /= 2;
		items.emplace_back(itor->second.age, itor);
	}

	std::partial_sort(items.begin(),
					  items.begin() + cache_max_size / 4,
					  items.end(),
					  compare);

	for(std::vector<std::pair<unsigned, tcache::iterator>>::iterator vitor
		= items.begin();
		vitor < items.begin() + cache_max_size / 4;
		++vitor) {

		cache.erase(vitor->second);
	}

#ifdef DEBUG_MINIMAP_CACHE
	std::cerr << " to " << cache.size() << ".\n";
#endif
}

bool minimap::disable_click_dismiss() const
{
	return false;
}

const surface minimap::get_image(const int w, const int h) const
{
	const key_type key(w, h, map_data_);
	tcache::iterator itor = cache.find(key);

	if(itor != cache.end()) {
#ifdef DEBUG_MINIMAP_CACHE
		std::cerr << '+';
#endif
		itor->second.age++;
		return itor->second.surf;
	}

	if(cache.size() >= cache_max_size) {
		shrink_cache();
	}

	try
	{
		const gamemap map(map_data_);
		const surface surf = image::getMinimap(w, h, map, nullptr, nullptr, true);
		cache.emplace(key, value_type(surf));
#ifdef DEBUG_MINIMAP_CACHE
		std::cerr << '-';
#endif
		return surf;
	}
	catch(const incorrect_map_format_error& e)
	{
		ERR_CF << "Error while loading the map: " << e.message << '\n';
#ifdef DEBUG_MINIMAP_CACHE
		std::cerr << 'X';
#endif
	}
	return nullptr;
}

void minimap::impl_draw_background(surface& frame_buffer,
									int x_offset,
									int y_offset)
{
	DBG_GUI_D << LOG_HEADER << " size "
			  << calculate_blitting_rectangle(x_offset, y_offset) << ".\n";

	if(map_data_.empty()) {
		return;
	}

	SDL_Rect rect = calculate_blitting_rectangle(x_offset, y_offset);
	assert(rect.w > 0 && rect.h > 0);

	const ::surface surf = get_image(rect.w, rect.h);
	if(surf) {
		sdl_blit(surf, nullptr, frame_buffer, &rect);
	}
}

// }---------- DEFINITION ---------{

minimap_definition::minimap_definition(const config& cfg)
	: styled_widget_definition(cfg)
{
	DBG_GUI_P << "Parsing minimap " << id << '\n';

	load_resolutions<resolution>(cfg);
}

minimap_definition::resolution::resolution(const config& cfg)
	: resolution_definition(cfg)
{
	// Note the order should be the same as the enum state_t in minimap.hpp.
	state.emplace_back(cfg.child("state_enabled"));
}

// }---------- BUILDER -----------{

namespace implementation
{

builder_minimap::builder_minimap(const config& cfg) : builder_styled_widget(cfg)
{
}

widget* builder_minimap::build() const
{
	minimap* widget = new minimap(*this);

	DBG_GUI_G << "Window builder: placed minimap '" << id
			  << "' with definition '" << definition << "'.\n";

	return widget;
}

} // namespace implementation

// }------------ END --------------

} // namespace gui2
