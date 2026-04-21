#include "GstBootstrap.h"

#include <gst/gst.h>

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace {

namespace fs = std::filesystem;

std::once_flag envOnce;
std::once_flag initOnce;
bool gstInitSucceeded = false;

#ifdef __APPLE__
fs::path getGStreamerPluginDir() {
    Dl_info info = {};
    if (dladdr(reinterpret_cast<const void*>(&gst_init_check), &info) == 0 ||
        info.dli_fname == nullptr) {
        return {};
    }

    const fs::path libPath(info.dli_fname);
    const fs::path pluginDir = libPath.parent_path() / "gstreamer-1.0";
    if (!fs::exists(pluginDir) || !fs::is_directory(pluginDir)) {
        return {};
    }
    return pluginDir;
}

fs::path createFilteredPluginDir(const fs::path& sourceDir) {
    if (sourceDir.empty()) {
        return {};
    }

    const std::string key = std::to_string(std::hash<std::string>{}(sourceDir.string()));
    const fs::path filteredDir =
        fs::temp_directory_path() / ("hailo-gstreamer-plugins-" + key);

    std::error_code ec;
    fs::create_directories(filteredDir, ec);
    if (ec) {
        return {};
    }

    for (const fs::directory_entry& entry : fs::directory_iterator(sourceDir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }

        const fs::path filename = entry.path().filename();
        if (filename.extension() != ".dylib") {
            continue;
        }
        if (filename == "libgstgtk.dylib") {
            continue;
        }

        const fs::path linkPath = filteredDir / filename;
        fs::remove(linkPath, ec);
        ec.clear();
        fs::create_symlink(entry.path(), linkPath, ec);
        if (ec) {
            fs::copy_file(entry.path(), linkPath,
                          fs::copy_options::overwrite_existing, ec);
            ec.clear();
        }
    }

    return filteredDir;
}

void configureMacOSPluginSearchPath() {
    const fs::path pluginDir = getGStreamerPluginDir();
    const fs::path filteredDir = createFilteredPluginDir(pluginDir);
    if (!filteredDir.empty()) {
        const std::string filtered = filteredDir.string();
        g_setenv("GST_PLUGIN_SYSTEM_PATH_1_0", filtered.c_str(), TRUE);
        g_setenv("GST_PLUGIN_SYSTEM_PATH", filtered.c_str(), TRUE);
        g_unsetenv("GST_PLUGIN_PATH_1_0");
        g_unsetenv("GST_PLUGIN_PATH");

        const std::string registry = (filteredDir / "registry.bin").string();
        g_setenv("GST_REGISTRY", registry.c_str(), TRUE);
    }
}

void preferGtk4PluginFeatures() {
    // On macOS, exclude the gtk3 plugin during scanning
    // and keep the remaining gtk4 sink as an auto-selectable candidate.
    g_setenv("GST_PLUGIN_FEATURE_RANK",
             "gtksink:NONE,"
             "gtkglsink:NONE,"
             "gtkwaylandsink:NONE,"
             "gtk4paintablesink:PRIMARY",
             TRUE);
}

void removeGtkPluginsFromRegistry() {
    GstRegistry* registry = gst_registry_get();
    if (!registry) {
        return;
    }

    const char* pluginNames[] = {"gtk"};
    for (const char* name : pluginNames) {
        GstPlugin* plugin = gst_registry_find_plugin(registry, name);
        if (!plugin) {
            continue;
        }

        gst_registry_remove_plugin(registry, plugin);
        gst_object_unref(plugin);
    }

    const char* featureNames[] = {
        "gtksink",
        "gtkglsink",
        "gtkwaylandsink",
    };
    for (const char* name : featureNames) {
        GstPluginFeature* feature = gst_registry_lookup_feature(registry, name);
        if (!feature) {
            continue;
        }

        gst_registry_remove_feature(registry, feature);
        gst_object_unref(feature);
    }
}
#endif

}  // namespace

void prepareGStreamerEnvironment() {
    std::call_once(envOnce, []() {
#ifdef __APPLE__
        configureMacOSPluginSearchPath();
        preferGtk4PluginFeatures();
#endif
    });
}

bool initializeGStreamer(int* argc, char*** argv) {
    prepareGStreamerEnvironment();

    std::call_once(initOnce, [argc, argv]() {
        GError* err = nullptr;
        gstInitSucceeded = gst_init_check(argc, argv, &err);
        if (!gstInitSucceeded) {
            std::fprintf(stderr, "[GstBootstrap] gst_init_check failed: %s\n",
                         (err && err->message) ? err->message : "unknown");
            if (err) {
                g_error_free(err);
            }
            return;
        }

#ifdef __APPLE__
        removeGtkPluginsFromRegistry();
#endif
    });

    return gstInitSucceeded;
}
