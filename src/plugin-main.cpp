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
#include <QDockWidget>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	blog(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);

	// Register Modules
	HomeInAudioHandler::Register();
	HomeInRenderer::Register();

	// Register the Dock UI
	// We create the widget and pass it to OBS; OBS takes ownership of the pointer
	HomeInDock *dock = new HomeInDock();
	obs_frontend_add_dock_by_id("home_indeed_dock", "Home Indeed", (void*)dock);

	// Register the Tools menu action
	QAction *action = (QAction*)obs_frontend_add_tools_menu_qaction(obs_module_text("Home Indeed"));
	QObject::connect(action, &QAction::triggered, []() {
		QMainWindow *main_window = (QMainWindow*)obs_frontend_get_main_window();
		QDockWidget *dock = main_window->findChild<QDockWidget*>("home_indeed_dock");
		if (dock) {
			dock->show();
			dock->raise();
			dock->setFocus();
		}
	});
	
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "plugin unloaded");
}
