/*
Plugin Name
Copyright (C) 2026 DeeThunderNexus Ventures <info@deethundernexus.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <QAction>
#include <QMainWindow>
#include "audio/homein-audio.hpp"
#include "dock/homein-dock.hpp"
#include "renderer/homein-renderer.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static void on_show_dock(void *data)
{
	UNUSED_PARAMETER(data);
	obs_frontend_set_dock_obj("HomeIndeedDock", nullptr); // Placeholder for now
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	// Register Modules
	HomeInAudioHandler::Register();
	HomeInRenderer::Register();

	// Register the Dock UI
	auto factory = []() {
		return new HomeInDock();
	};
	obs_frontend_add_dock_by_id("home_indeed_dock", "Home Indeed", factory);

	// Register the Tools menu action
	QAction *action = (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("Home Indeed Settings"));
	
	// QObject::connect(action, &QAction::triggered, []() {
	//     // Future: show settings dialog
	// });
	
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
