/*
 * Copyright (c) 2020 Valve Corporation
 * Copyright (c) 2020 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 * - Christophe Riccio <christophe@lunarg.com>
 */

#include "path_manager.h"
#include "util.h"
#include "platform.h"

#include <cassert>
#include <cstddef>

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QSettings>
#include <QWidget>
#include <QFileDialog>

struct DirectoryDesc {
    const char* label;
    const char* default_extension;  // file extension used to path
    const char* setting;            // system token to store the path, if empty the directory is not saved
    const char* default_filename;   // a default filename, if empty there is no default filename
    const bool resettable;
    Path alternative;
};

static const DirectoryDesc& GetDesc(Path directory) {
    assert(directory >= PATH_FIRST && directory <= PATH_LAST);

    static const DirectoryDesc table[] = {
        {"configuration", ".json", nullptr, nullptr, false, PATH_CONFIGURATION},                        // PATH_CONFIGURATION
        {"override settings", ".txt", nullptr, "vk_layer_settings", false, PATH_OVERRIDE_SETTINGS},     // PATH_OVERRIDE_SETTINGS
        {"override layers", ".json", nullptr, "VkLayer_override", false, PATH_OVERRIDE_LAYERS},         // PATH_OVERRIDE_LAYERS
        {"configuration import", ".json", "lastImportPath", nullptr, true, PATH_EXPORT_CONFIGURATION},  // PATH_IMPORT
        {"configuration export", ".json", "lastExportPath", nullptr, true, PATH_IMPORT_CONFIGURATION},  // PATH_EXPORT
#if PLATFORM_WINDOWS
        {"executable", ".exe", "lastExecutablePath", nullptr, true, PATH_WORKING_DIR},  // PATH_EXECUTABLE
#else
        {"executable", "", "lastExecutablePath", nullptr, true, PATH_WORKING_DIR},  // PATH_EXECUTABLE
#endif
        {"working directory", nullptr, "lastWorkingDirPath", nullptr, true, PATH_EXECUTABLE},          // PATH_EXECUTABLE
        {"home path", nullptr, nullptr, nullptr, false, PATH_HOME},                                    // PATH_HOME
        {"log file", ".txt", "lastLauncherLogFile", "log", true, PATH_LAUNCHER_LOG_FILE},              // PATH_LAUNCHER_LOG_FILE
        {"custom layer path", ".json", "lastCustomLayerPath", nullptr, true, PATH_CUSTOM_LAYER_PATH},  // PATH_CUSTOM_LAYER_PATH
    };
    static_assert(countof(table) == PATH_COUNT, "The tranlation table size doesn't match the enum number of elements");

    return table[directory];
}

struct FilenameDesc {
    const char* filename;
};

static const FilenameDesc& GetDesc(Filename filename) {
    static const FilenameDesc table[] = {"applist.json"};

    return table[filename];
}

PathManager::PathManager() {
    const bool result = Load();
    assert(result);
}

PathManager::~PathManager() {
    const bool result = Save();
    assert(result);
}

bool PathManager::Load() {
    paths[PATH_HOME] = QDir::toNativeSeparators(QDir::homePath()).toStdString();

    QSettings settings;
    for (std::size_t i = 0; i < PATH_COUNT; ++i) {
        const Path type = static_cast<Path>(i);
        if (GetDesc(type).setting == nullptr) continue;
        paths[type] = settings.value(GetDesc(type).setting).toString().toUtf8().constData();
    }

// Where is stuff
#if PLATFORM_WINDOWS
    QDir home = QDir::home();
    QString main_path = home.path() + QString("/AppData/Local/");
    home.setPath(main_path);
    if (!home.cd("LunarG")) {
        home.mkpath("LunarG");
        home.cd("LunarG");
    }

    if (!home.cd("vkconfig")) {
        home.mkpath("vkconfig");
    }

    if (!home.cd("override")) {
        home.mkpath("override");
    }

    SetPath(PATH_CONFIGURATION, main_path + "LunarG/vkconfig");
    SetPath(PATH_OVERRIDE_LAYERS, main_path + "LunarG/vkconfig/override");
    SetPath(PATH_OVERRIDE_SETTINGS, main_path + "LunarG/vkconfig/override");
#elif PLATFORM_LINUX || PLATFORM_MACOS
    QDir home = QDir::home();
    if (!home.cd(".local")) {
        home.mkpath(".local");
        home.cd(".local");
    }

    if (!home.cd("share")) {
        home.mkpath("share");
        home.cd("share");
    }

    if (!home.cd("vulkan")) {
        home.mkpath("vulkan");
        home.cd("vulkan");
    }

    if (!home.cd("implicit_layer.d")) {
        home.mkpath("implicit_layer.d");
        home.cd("implicit_layer.d");
    }

    home.cd("..");
    if (!home.cd("settings.d")) {
        home.mkpath("settings.d");
        home.cd("settings.d");
    }

    home.cd("..");
    if (!home.cd("lunarg-vkconfig")) {
        home.mkpath("lunarg-vkconfig");
        home.cd("lunarg-vkconfig");
    }

    home = QDir::home();
    const QString& main_path = home.path() + QString("/.local/share/vulkan/");
    SetPath(PATH_CONFIGURATION, main_path + "lunarg-vkconfig/");
    SetPath(PATH_OVERRIDE_LAYERS, main_path + "implicit_layer.d");
    SetPath(PATH_OVERRIDE_SETTINGS, main_path + "settings.d");
#else
#error "Unknown platform"
#endif

    return true;
}

bool PathManager::Save() {
    QSettings settings;
    for (std::size_t i = 0; i < PATH_COUNT; ++i) {
        const Path type = static_cast<Path>(i);
        if (GetDesc(type).setting == nullptr) continue;
        settings.setValue(GetDesc(type).setting, paths[type].c_str());
    }

    return true;
}

void PathManager::Clear() {
    for (std::size_t i = 0; i < PATH_COUNT; ++i) {
        const Path type = static_cast<Path>(i);
        paths[type].clear();
    }

    paths[PATH_HOME] = QDir::toNativeSeparators(QDir::homePath()).toStdString();
}

void PathManager::Reset() {
    for (std::size_t i = 0; i < PATH_COUNT; ++i) {
        const Path type = static_cast<Path>(i);

        if (GetDesc(type).resettable) {
            paths[type].clear();
        }
    }
}

const char* PathManager::GetPath(Path path) const {
    assert(path >= PATH_FIRST && path <= PATH_LAST);

    if (!paths[path].empty()) {
        return paths[path].c_str();
    }

    const Path alternative_path = GetDesc(path).alternative;
    if (!paths[alternative_path].empty()) {
        return paths[alternative_path].c_str();
    }

    // No path found, return home directory
    assert(!paths[PATH_HOME].empty());
    return paths[PATH_HOME].c_str();
}

void PathManager::SetPath(Path path, const char* path_value) {
    assert(path >= PATH_FIRST && path <= PATH_LAST);
    assert(path_value);

    const QDir directory(path_value);
    const QString native_path = QDir::toNativeSeparators(directory.absolutePath());
    paths[path] = native_path.toUtf8().constData();
}

void PathManager::SetPath(Path directory, const QString& path_value) {
    assert(!path_value.isEmpty());

    SetPath(directory, path_value.toStdString().c_str());
}

QString PathManager::GetFullPath(Path path, const char* filename) const {
    const QFileInfo file_info(filename);

    const QString path_base = GetPath(path);

    const QString path_filename = filename != nullptr ? QFileInfo(filename).baseName() : GetDesc(path).default_filename;
    assert(!path_filename.isEmpty());  // Did you really mean to use GetFullPath? GetPath seem to be the right function here

    const QString path_suffix =
        !file_info.completeSuffix().isEmpty() ? QString(".") + file_info.completeSuffix() : GetDesc(path).default_extension;
    assert(!path_suffix.isEmpty() || !PLATFORM_WINDOWS);  // Only Windows has a suffix for executable

    const QString full_path = QDir::toNativeSeparators(path_base + "/" + path_filename + path_suffix);
    return full_path;
}

QString PathManager::GetFullPath(Path path, const QString& filename) const {
    return GetFullPath(path, filename.toStdString().c_str());
}

QString PathManager::GetFullPath(Filename filename) const {
    const QString path = GetPath(PATH_CONFIGURATION);

    const QString full_path = QDir::toNativeSeparators(path + "/" + GetDesc(filename).filename);
    return full_path;
}

QString PathManager::GetFilename(const char* full_path) const {
    assert(full_path);

    return QFileInfo(full_path).fileName();
}

QString PathManager::SelectPath(QWidget* parent, Path path) {
    assert(parent);
    assert(path >= PATH_FIRST && path <= PATH_LAST);

    return SelectPathImpl(parent, path, GetPath(path));
}

QString PathManager::SelectPath(QWidget* parent, Path path, const QString& suggested_path) {
    assert(parent);
    assert(path >= PATH_FIRST && path <= PATH_LAST);

    if (suggested_path.isEmpty())
        return SelectPathImpl(parent, path, GetPath(path));
    else
        return SelectPathImpl(parent, path, suggested_path);
}

QString PathManager::SelectPathImpl(QWidget* parent, Path path, const QString& suggested_path) {
    assert(parent);
    assert(path >= PATH_FIRST && path <= PATH_LAST);
    assert(!suggested_path.isEmpty());

    switch (path) {
        case PATH_LAUNCHER_LOG_FILE: {
            const QString selected_path =
                QFileDialog::getSaveFileName(parent, "Set Log File To...", suggested_path, "Log text(*.txt)");
            if (selected_path.isEmpty())  // The user cancelled
                return "";

            SetPath(path, QFileInfo(selected_path).absolutePath());
            return GetFullPath(path, QFileInfo(selected_path).baseName());
        } break;
        case PATH_EXECUTABLE: {
#if PLATFORM_MACOS
            const QString filter("Applications (*.app, *)");
#elif PLATFORM_WINDOWS
            const QString filter("Applications (*.exe)");
#elif PLATFORM_LINUX
            const QString filter("Applications (*)");
#else
#error "Unknown platform"
#endif

            const QString selected_path =
                QFileDialog::getOpenFileName(parent, "Select a Vulkan Executable...", suggested_path, filter);
            if (selected_path.isEmpty())  // The user cancelled
                return "";

            SetPath(path, QFileInfo(selected_path).absolutePath());
            return GetFullPath(path, QFileInfo(selected_path).baseName());
        }
        case PATH_WORKING_DIR: {
            const QString selected_path = QFileDialog::getExistingDirectory(parent, "Set Working Folder To...", suggested_path);
            if (selected_path.isEmpty())  // The user cancelled
                return "";

            SetPath(path, selected_path);
            return GetPath(path);
        }
        case PATH_CUSTOM_LAYER_PATH: {
            const QString selected_path = QFileDialog::getExistingDirectory(parent, "Add Custom Layer Folder...", suggested_path,
                                                                            QFileDialog::DontUseNativeDialog);
            if (selected_path.isEmpty())  // The user cancelled
                return "";

            SetPath(path, selected_path);
            return GetPath(path);
        }
        case PATH_IMPORT_CONFIGURATION: {
            const QString selected_path = QFileDialog::getOpenFileName(parent, "Import Layers Configuration File", suggested_path,
                                                                       "JSON configuration(*.json)");
            if (selected_path.isEmpty())  // The user cancelled
                return "";

            SetPath(path, QFileInfo(selected_path).absolutePath());
            return GetFullPath(path, QFileInfo(selected_path).baseName());
        }
        case PATH_EXPORT_CONFIGURATION: {
            const QString selected_path = QFileDialog::getSaveFileName(parent, "Export Layers Configuration File", suggested_path,
                                                                       "JSON configuration(*.json)");
            if (selected_path.isEmpty())  // The user cancelled
                return "";

            SetPath(path, QFileInfo(selected_path).absolutePath());
            return GetFullPath(path, QFileInfo(selected_path).baseName());
        }
        default:
            assert(0);
            return "";
    }
}
