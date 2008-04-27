/* $Id$ */
/*
   Copyright (C) 2007 - 2008 by Mark de Wever <koraq@xs4all.nl>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2
   or at your option any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

//! @file event_handler.cpp
//! Implementation of event_handler.hpp.
//!
//! More documentation at the end of the file.

#include "gui/widgets/event_handler.hpp"

#include "config.hpp"
#include "gui/widgets/widget.hpp"
#include "gui/widgets/window.hpp"
#include "log.hpp"
#include "serialization/parser.hpp"
#include "tstring.hpp"
#include "variable.hpp"

#define DBG_G_E LOG_STREAM_INDENT(debug, gui_event)
#define LOG_G_E LOG_STREAM_INDENT(info, gui_event)
#define WRN_G_E LOG_STREAM_INDENT(warn, gui_event)
#define ERR_G_E LOG_STREAM_INDENT(err, gui_event)

namespace gui2{

static Uint32 hover_callback(Uint32 /*interval*/, void *param)
{
	DBG_G_E << "Pushing hover event in queue.\n";

	SDL_Event event;
	SDL_UserEvent data;

	data.type = HOVER_EVENT;
	data.code = 0;
	data.data1 = param;
	data.data2 = 0;

	event.type = HOVER_EVENT;
	event.user = data;
	
	SDL_PushEvent(&event);
	return 0;
}

static Uint32 popup_callback(Uint32 /*interval*/, void*)
{
	DBG_G_E << "Pushing popup removal event in queue.\n";

	SDL_Event event;
	SDL_UserEvent data;

	data.type = HOVER_REMOVE_POPUP_EVENT;
	data.code = 0;
	data.data1 = 0;
	data.data2 = 0;

	event.type = HOVER_REMOVE_POPUP_EVENT;
	event.user = data;
	
	SDL_PushEvent(&event);
	return 0;
}

//! At construction we should get the state and from that moment on we keep
//! track of the changes ourselves, not yet sure what happens when an input
//! blocker is used.
tevent_handler::tevent_handler() :
	// fixme get state at construction
	events::handler(false), // don't join we haven't created a context yet
	event_context_(),
	mouse_x_(-1),
	mouse_y_(-1),
	left_("left",
		&tevent_executor::mouse_left_button_down,
		&tevent_executor::mouse_left_button_up,
		&tevent_executor::mouse_left_button_click,
		&tevent_executor::mouse_left_button_double_click,
		&tevent_executor::wants_mouse_left_double_click),
	middle_("middle",
		&tevent_executor::mouse_middle_button_down,
		&tevent_executor::mouse_middle_button_up,
		&tevent_executor::mouse_middle_button_click,
		&tevent_executor::mouse_middle_button_double_click,
		&tevent_executor::wants_mouse_middle_double_click),
	right_("right",
		&tevent_executor::mouse_right_button_down,
		&tevent_executor::mouse_right_button_up,
		&tevent_executor::mouse_right_button_click,
		&tevent_executor::mouse_right_button_double_click,
		&tevent_executor::wants_mouse_right_double_click),
	hover_pending_(false),
	hover_id_(0),
	hover_box_(),
	had_hover_(false),
	tooltip_(0),
	help_popup_(0),
	mouse_focus_(0),
	mouse_captured_(false),
	keyboard_focus_(0)
{
	if(SDL_WasInit(SDL_INIT_TIMER) == 0) {
		if(SDL_InitSubSystem(SDL_INIT_TIMER) == -1) {
			assert(false);
		}
	}

	// The event context is created now we join it.
	join();
}

void tevent_handler::handle_event(const SDL_Event& event)
{

	twidget* mouse_over = 0; 
	switch(event.type) {
		case SDL_MOUSEMOTION:

			mouse_x_ = event.motion.x;
			mouse_y_ = event.motion.y;
			mouse_over =
				get_widget(get_window().client_position(tpoint(mouse_x_, mouse_y_)));

			mouse_move(event, mouse_over);

			break;

		case SDL_MOUSEBUTTONDOWN:

			mouse_x_ = event.button.x;
			mouse_y_ = event.button.y;
			mouse_over =
				get_widget(get_window().client_position(tpoint(mouse_x_, mouse_y_)));

			switch(event.button.button) {
				case SDL_BUTTON_LEFT : 
					mouse_button_down(event, mouse_over, left_);
					break;
				case SDL_BUTTON_MIDDLE :
					mouse_button_down(event, mouse_over, middle_);
					break;
				case SDL_BUTTON_RIGHT :
					mouse_button_down(event, mouse_over, right_);
					break;
				default:
					// cast to avoid being printed as char.
					WRN_G_E << "Unhandled 'mouse button down' event for button " 
						<< static_cast<Uint32>(event.button.button) << ".\n";
					assert(false);
					break;
			}
			break;

		case SDL_MOUSEBUTTONUP:

			mouse_x_ = event.button.x;
			mouse_y_ = event.button.y;
			mouse_over =
				get_widget(get_window().client_position(tpoint(mouse_x_, mouse_y_)));

			switch(event.button.button) {

				case SDL_BUTTON_LEFT : 
					mouse_button_up(event, mouse_over, left_);
					break;
				case SDL_BUTTON_MIDDLE :
					mouse_button_up(event, mouse_over, middle_);
					break;
				case SDL_BUTTON_RIGHT :
					mouse_button_up(event, mouse_over, right_);
					break;
				default:
					// cast to avoid being printed as char.
					WRN_G_E << "Unhandled 'mouse button up' event for button " 
						<< static_cast<Uint32>(event.button.button) << ".\n";
					assert(false);
					break;
			}
			break;

		case HOVER_EVENT:
			mouse_hover(event, 0);
			break;

		case HOVER_REMOVE_POPUP_EVENT:
			remove_tooltip();
			remove_help_popup();
			break;

		case SDL_KEYDOWN:
			key_down(event);
			break;

		case SDL_VIDEORESIZE:
			get_window().window_resize(*this, event.resize.w, event.resize.h);
			break;

		default:
		
			// cast to avoid being printed as char.
			WRN_G_E << "Unhandled event " << static_cast<Uint32>(event.type) << ".\n";
			break;
		}
}

void tevent_handler::mouse_capture(const bool capture)
{
	assert(mouse_focus_);
	mouse_captured_ = capture;
}

tpoint tevent_handler::get_mouse() const
{ 
	return get_window().client_position(tpoint(mouse_x_, mouse_y_)); 
}

void tevent_handler::show_tooltip(const t_string& tooltip, const unsigned timeout)
{
	DBG_G_E << "Event: show tooltip.\n";

	assert(!tooltip_);

	if(help_popup_) {
		remove_help_popup();
	}

	tooltip_ = mouse_focus_;

	do_show_tooltip(get_window().client_position(tpoint(mouse_x_, mouse_y_)), tooltip);

	if(timeout) {
		SDL_AddTimer(timeout, popup_callback, 0);
	}
}

void tevent_handler::remove_tooltip()
{
	if(!tooltip_) {
		return;
	}

	tooltip_ = 0;

	do_remove_tooltip();
}

void tevent_handler::show_help_popup(const t_string& help_popup, const unsigned timeout)
{
	DBG_G_E << "Event: show help popup.\n";

	if(help_popup_) {
		DBG_G_E << "Help is already there, bailing out.\n";
		return;
	}

	if(tooltip_) {
		remove_tooltip();
	}

	// Kill hover events FIXME not documented.
	had_hover_ = true;
	hover_pending_ = false;

	help_popup_ = mouse_focus_;

	do_show_help_popup(get_window().client_position(tpoint(mouse_x_, mouse_y_)), help_popup);

	if(timeout) {
		SDL_AddTimer(timeout, popup_callback, 0);
	}
}

void tevent_handler::remove_help_popup()
{
	if(!help_popup_) {
		return;
	}

	help_popup_ = 0;

	do_remove_help_popup();
}

void tevent_handler::mouse_enter(const SDL_Event& /*event*/, twidget* mouse_over)
{
	DBG_G_E << "Event: remove tooltip.\n";

	assert(mouse_over);

	mouse_focus_ = mouse_over;
	mouse_over->mouse_enter(*this);

	set_hover();
}

void tevent_handler::mouse_hover(const SDL_Event& event, twidget* /*mouse_over*/)
{

	const unsigned hover_id = *static_cast<unsigned*>(event.user.data1);
	delete static_cast<unsigned*>(event.user.data1);

	if(!hover_pending_ || hover_id != hover_id_) {
		return;
	}
	
	assert(mouse_focus_);

	mouse_focus_->mouse_hover(*this);

	had_hover_ = true;
}

void tevent_handler::mouse_move(const SDL_Event& event, twidget* mouse_over)
{
	// Note we use the fact that a NULL pointer evaluates to false
	// and non NULL pointer to true;
	if(mouse_captured_) {
		mouse_focus_->mouse_move(*this);
		set_hover(true); 
	} else {
		if(!mouse_focus_ && mouse_over) {
			mouse_enter(event, mouse_over);
		} else if (mouse_focus_ && !mouse_over) {
			mouse_leave(event, mouse_over);
		} else if(mouse_focus_ && mouse_focus_ == mouse_over) {	
			mouse_over->mouse_move(*this);
			set_hover();
		} else if(mouse_focus_ && mouse_over) {
			// moved from one widget to the next
			mouse_leave(event, mouse_over);
			mouse_enter(event, mouse_over);
		} else {
			assert(!mouse_focus_ && !mouse_over);
		}
	}
}

void tevent_handler::mouse_leave(const SDL_Event& /*event*/, twidget* /*mouse_over*/)
{
	assert(mouse_focus_);

	had_hover_ = false;
	hover_pending_ =false;

	remove_tooltip();
	remove_help_popup();

	mouse_focus_->mouse_leave(*this);
	mouse_focus_ = 0;
}

void tevent_handler::mouse_button_down(const SDL_Event& event, twidget* mouse_over, tmouse_button& button)
{
	if(button.is_down) {
		WRN_G_E << "In 'button down' for button '" << button.name 
			<< "' but the mouse button is already down, we missed an event.\n";
		return;
	}
	button.is_down = true;
	hover_pending_ = false;

	if(mouse_captured_) {
		button.focus = mouse_focus_;
		(mouse_focus_->*button.down)(*this);
	} else {
		if(!mouse_over) {
			return;
		}

		if(mouse_over != mouse_focus_) {
			WRN_G_E << "Mouse down event on non focussed widget "
				<< "and mouse not captured, we missed events.\n";
		}

		button.focus = mouse_over;
		(mouse_over->*button.down)(*this);
	}
}

void tevent_handler::mouse_button_up(const SDL_Event& event, twidget* mouse_over, tmouse_button& button)
{
	if(!button.is_down) {
		WRN_G_E << "In 'button up' for button '" << button.name 
			<< "' but the mouse button is already up, we missed an event.\n";
		return;
	}

	button.is_down = false;
	if(button.focus) {
		(button.focus->*button.up)(*this);
	}

	if(mouse_captured_) {

		if(mouse_focus_ != mouse_over) {
			if (!left_.is_down && !middle_.is_down && !right_.is_down) {
				mouse_captured_ = false;

				mouse_leave(event, mouse_over);

				if(mouse_over) {
					mouse_enter(event, mouse_over);
				}
			}
		} else {
			mouse_click(mouse_focus_, button);
		}
	} else if(button.focus && button.focus == mouse_over) {
		mouse_click(button.focus, button);
	}

	button.focus = 0;
	set_hover();
}

void tevent_handler::mouse_click(twidget* widget, tmouse_button& button)
{
	if((widget->*button.wants_double_click)()) {
		Uint32 stamp = SDL_GetTicks();
		if(button.last_click_stamp + 500 >= stamp) { // FIXME 500 should be variable

			(widget->*button.double_click)(*this);
			button.last_click_stamp = 0;

		} else {

			(widget->*button.click)(*this);
			button.last_click_stamp = stamp;
		}

	} else {
	
		widget->mouse_left_button_click(*this);
	}
}

void tevent_handler::set_hover(const bool test_on_widget)
{
	// Only one hover event.
	if(had_hover_) {
		return;
	}

	// Don't want a hover.
	if(!mouse_focus_ || !mouse_focus_->wants_mouse_hover()) {
		return;
	}

	// Have an hover and still in the bounding rect.
	if(hover_pending_ && point_in_rect(mouse_x_, mouse_y_, hover_box_)) {
		return;
	}

	// Mouse down, no hovering
	if(left_.is_down || middle_.is_down || right_.is_down) {
		return;
	} 

	if(test_on_widget) {
		// FIXME implement
	}

	static unsigned hover_id = 0;

	hover_pending_ = true;
	// FIXME hover dimentions should be from the settings
	// also should check the entire box is on the widget???
	hover_box_ = ::create_rect(mouse_x_ - 5, mouse_y_ - 5, 10, 10);

	unsigned *hover = new unsigned;
	*hover = hover_id;
	hover_id_ = hover_id++;
			
	// Fixme delay show be a setting
	SDL_AddTimer(1500, hover_callback, hover);
}

void tevent_handler::key_down(const SDL_Event& event)
{
	// We capture the F! for the help, but only if the mouse is on an object.
	if(mouse_focus_/* && !mouse_captured_ 
			&& event.key.keysym.mod == 0 */ && event.key.keysym.sym == SDLK_F1) {

		mouse_focus_->help_key(*this);
		return;
	}

	bool handled = false;
	if(keyboard_focus_) {
		keyboard_focus_->key_press(*this, handled, event.key.keysym.sym, event.key.keysym.mod, event.key.keysym.unicode);
	}

	if(!handled) {
		get_window().key_press(*this, handled, event.key.keysym.sym, event.key.keysym.mod, event.key.keysym.unicode);
	}
}

/**
 * The event handling system.
 *
 * In this system there are two kind of focus
 * - mouse focus, the widget that will get the mouse events. There are two modes
 *   captured and uncaptured. Basically when a mouse button is pressed that 
 *   widget will capture the mouse and all following mouse events are send to
 *   that widget.
 *   
 * - keyboard focus, the widget that will get the keyboard events.
 *
 * Keyboard events are first processed by the top level window and if not 
 * handled it will be send to the item with the keyboard focus (if any).
 *
 * * Mouse events *
 *
 * Note by button the X can be:
 * - left button
 * - middle button
 * - right button
 *
 * For the mouse the following events are defined:
 * - mouse enter, the mouse moves on a widget it wasn't on before.
 * - mouse move, the mouse moves. This can either be that the mouse moves over
 *   the widget under the mouse or it's send to the widget that captured the 
 *   focus.
 * - mouse leave, the mouse leaves the area that bounds the mouse, in captured
 *   mode the release is delayed until the focus is released.
 * - hover, a widget needs to tell it wants this event. The event will be send
 *   when the user doesn't move (or just a little) for a while. (Times are 
 *   themable.)
 *
 * - mouse_button_X_down, the mouse button is being pressed on this widget.
 * - mouse_button_X_up, the mouse button has been moved up again.  
 * - mouse_button_X_click, a single click on a widget.
 * - mouse_button_X_double_click, a double click on the widget. The widget 
 *   needs to subscribe to this event and when doing so a single click will be
 *   delayed a bit. (The double click time is themable.)
 *
 * * Mouse event flow chart *
 *
 *       --------------------
 *      ( mouse somewhere    ) 
 *       --------------------
 *                 |
 *  -------------->|
 * |               |
 * |               V 
 * |     --------------------        -------------------- 
 * |    | moves upon widget  | -->  / fire mouse enter  /
 * |     --------------------      ---------------------
 * |                                        |
 * |                                        V
 * |                                        /\  Want hover event?
 * |                                    no /  \ yes     --------------------
 * |               ----------------------- \  / -----> / place hover event /  
 * |              |                         \/         --------------------
 * |              |                                            |
 * |              |<--------------------------------------------
 * |              |
 * |              V
 * |     --------------------        -------------------- 
 * |    | moves on widget    | -->  / fire mouse move   /
 * |     --------------------      ---------------------
 * |                                        |
 * |                                        V
 * |                                        /\  Want hover event?
 * |                                    no /  \ 
 * |               ----------------------- \  /
 * |              |                         \/ 
 * |              |                         | yew
 * |              |                         V
 * |              |                         /\  Hover location outside threshold? 
 * |              |                     no /  \ yes     --------------------
 * |              |<---------------------- \  / -----> / place hover event /  
 * |              |                         \/         --------------------
 * |              |                                            |
 * |              |<--------------------------------------------
 * |              |                                             
 * |              V                         
 * |     --------------------        -------------------- 
 * |    | receive hover event| -->  / set hover shown   /  
 * |     --------------------      ---------------------
 * |                                        |  
 * |                                        V
 * |                                 -------------------- 
 * |                                / fire hover event  /
 * |                               ---------------------
 * |                                        |
 * |               ------------------------- 
 * |              |
 * |              |--------------------------------------------------
 * |              V                                                  |
 * |     --------------------        --------------------            |
 * |    | moves off widget   | -->  / fire mouse leave  /            |
 * |     --------------------      ---------------------             |
 * |                                         |                       |
 * |                                         V                       |
 * |                                 ---------------------           |
 * |                                / cancel pending     /           |
 * |                               /  hover events      /            |
 * |                               ---------------------             |
 * |                                         |                       |
 * |                                         V                       |
 * |                                 --------------------            |
 * |                                / reset hover shown /            |
 * |                               ---------------------             |
 * |                                         |                       |
 * |<----------------------------------------                        |
 * |                                                                 |
 * |                                                                 |
 * |                                                                 |
 * |                                                                 |
 * |                                                                 |
 * |               --------------------------------------------------
 * |              |
 * |              V                         
 * |     --------------------        -------------------- 
 * |    | mouse down         | -->  / cancel pending    /
 * |     --------------------      /  hover events     / 
 * |                               --------------------
 * |                                        |  
 * |                                        V
 * |                                 -------------------- 
 * |                                / fire 'mouse down' /
 * |                               ---------------------
 * |                                        |
 * |               -------------------------
 * |              |              
 * |              V
 * |     --------------------        -------------------- 
 * |    | moves on widget    | -->  / fire mouse move   /
 * |     --------------------      ---------------------
 * |                                        |
 * |               --------------------------------------------------
 * |              |                                                  |
 * |              V                                                  |
 * |     --------------------        --------------------            |
 * |    | moves off widget   | -->  / fire mouse move   /            |
 * |     --------------------      ---------------------             |
 * |                                        |                        |
 * |               -------------------------                         |
 * |              |                                                  |
 * |              V                                                  |
 * |     --------------------        --------------------            |
 * |    | moves up           | -->  / release capture   /            |
 * |     --------------------      ---------------------             |
 * |                                        |                        |
 * |                                        V                        |
 * |                                 --------------------            |
 * |                                / fire mouse up     /            |
 * |                               ---------------------             |
 * |                                        |                        |
 * |                                        V                        |
 * |                                 --------------------            |
 * |                                / fire mouse leave  /            |
 * |                               ---------------------             |
 * |                                         |                       |
 * |<----------------------------------------                        |
 * |                                                                 |
 * |                                                                 |
 * |                                                                 |
 * |                                                                 |
 * |                                                                 |
 * |               --------------------------------------------------
 * |              |
 * |              V 
 * |     -------------------- 
 * |    | capture mouse      |
 * |     --------------------
 * |              |              
 * |              V
 * |     --------------------        -------------------- 
 * |    | moves up           | -->  / release capture   /
 * |     --------------------      ---------------------
 * |                                        |  
 * |                                        V
 * |                                 -------------------- 
 * |                                / fire mouse up     /
 * |                               ---------------------
 * |                                        |  
 * |                                        V
 * |                                        /\
 * |                                       /  \ Want double click? 
 * |                                       \  / no       -------------------- 
 * |                                        \/  ------> / fire click        /
 * |                                   yes  |          --------------------- 
 * |                                        |                  |   
 * |                                        V                   -----------------
 * |                                        /\                                   |
 * |                                       /  \ Has click pending?               |
 * |         -----------------------  yes  \  / no       --------------------    |
 * |        / remove click pending /<------ \/  ------> / set click pending /    |
 * |        -----------------------                    ---------------------     |
 * |                 |                                         |                 |
 * |                 V                                         V                 |
 * |         -----------------------                     -------------------     |
 * |        / fire double click    /                    / fire click       /     |
 * |       ------------------------                     -------------------      |
 * |                 |                                         |                 |
 * |                 V                                         V                 |
 *  -----------------------------------------------------------------------------
 * 
 */

} // namespace gui2

