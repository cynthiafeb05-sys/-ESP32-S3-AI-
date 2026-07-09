#include "navigation.h"

#include "application.h"
#include "assets/lang_config.h"
#include "gps.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#define TAG "NAV"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

struct NavigationState {
    bool active = false;
    std::string destination;
    NavigationMode mode = NavigationMode::Walking;
    size_t step_index = 0;
    TaskHandle_t task = nullptr;
};

std::mutex nav_mutex;
NavigationState nav_state;

const char* mode_name(NavigationMode mode)
{
    return mode == NavigationMode::Bicycling ? "骑行" : "步行";
}

double distance_meters(double lat1, double lng1, double lat2, double lng2)
{
    const double earth_radius = 6371000.0;
    double d_lat = (lat2 - lat1) * M_PI / 180.0;
    double d_lng = (lng2 - lng1) * M_PI / 180.0;
    double r_lat1 = lat1 * M_PI / 180.0;
    double r_lat2 = lat2 * M_PI / 180.0;
    double a = sin(d_lat / 2) * sin(d_lat / 2) +
               cos(r_lat1) * cos(r_lat2) * sin(d_lng / 2) * sin(d_lng / 2);
    return earth_radius * 2 * atan2(sqrt(a), sqrt(1 - a));
}

bool is_active()
{
    std::lock_guard<std::mutex> lock(nav_mutex);
    return nav_state.active;
}

void speak_navigation(const std::string& text, const char* status = "导航")
{
    auto& app = Application::GetInstance();
    app.Alert(status, text.c_str(), "neutral");
    app.SpeakText(text);
}

void finish_navigation(const std::string& message)
{
    {
        std::lock_guard<std::mutex> lock(nav_mutex);
        nav_state.active = false;
        nav_state.step_index = 0;
        nav_state.task = nullptr;
    }
    speak_navigation(message, "导航结束");
}

void navigation_task(void* arg)
{
    std::string destination = *(std::string*)arg;
    delete (std::string*)arg;

    NavigationMode mode;
    {
        std::lock_guard<std::mutex> lock(nav_mutex);
        mode = nav_state.mode;
    }

    auto& app = Application::GetInstance();
    double to_lat = 0;
    double to_lng = 0;
    if (!map_get_location(destination, to_lat, to_lng)) {
        app.Alert("导航错误", "无法解析目的地", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        stop_navigation();
        vTaskDelete(nullptr);
        return;
    }

    gps_data_t gps = gps_get_data();
    if (!gps.valid) {
        app.Alert("GPS", "未定位，请到开阔地带", "cloud_slash", Lang::Sounds::OGG_VIBRATION);
        stop_navigation();
        vTaskDelete(nullptr);
        return;
    }

    double from_lat = 0;
    double from_lng = 0;
    map_wgs84_to_gcj02(gps.latitude, gps.longitude, from_lat, from_lng);

    std::vector<map_route_step_t> steps;
    double total_distance = 0;
    if (!map_get_route(from_lat, from_lng, to_lat, to_lng, mode, steps, total_distance)) {
        app.Alert("导航错误", "路径规划失败", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        stop_navigation();
        vTaskDelete(nullptr);
        return;
    }

    if (steps.empty()) {
        finish_navigation("未获取到可用导航步骤，导航结束");
        vTaskDelete(nullptr);
        return;
    }

    char start_message[192];
    snprintf(start_message, sizeof(start_message), "%s导航开始，全程约%.0f米。%s",
             mode_name(mode), total_distance, steps.front().instruction.c_str());
    speak_navigation(start_message, "正在导航");

    size_t current_step = 0;
    {
        std::lock_guard<std::mutex> lock(nav_mutex);
        nav_state.step_index = current_step;
    }

    while (is_active()) {
        gps = gps_get_data();
        if (!gps.valid) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        map_wgs84_to_gcj02(gps.latitude, gps.longitude, from_lat, from_lng);
        double distance_to_step = distance_meters(from_lat, from_lng,
                                                 steps[current_step].end_lat,
                                                 steps[current_step].end_lng);

        if (distance_to_step <= 10.0) {
            if (current_step + 1 < steps.size()) {
                current_step++;
                {
                    std::lock_guard<std::mutex> lock(nav_mutex);
                    nav_state.step_index = current_step;
                }
                speak_navigation(steps[current_step].instruction);
            } else {
                finish_navigation("已到达目的地附近，导航结束");
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    {
        std::lock_guard<std::mutex> lock(nav_mutex);
        if (nav_state.task == xTaskGetCurrentTaskHandle()) {
            nav_state.task = nullptr;
        }
    }
    vTaskDelete(nullptr);
}

} // namespace

void start_navigation(const std::string& destination)
{
    start_navigation(destination, get_navigation_mode());
}

void start_navigation(const std::string& destination, NavigationMode mode)
{
    if (destination.empty()) {
        Application::GetInstance().Alert("导航错误", "目的地不能为空", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        return;
    }

    stop_navigation();

    {
        std::lock_guard<std::mutex> lock(nav_mutex);
        nav_state.active = true;
        nav_state.destination = destination;
        nav_state.mode = mode;
        nav_state.step_index = 0;
    }

    TaskHandle_t task = nullptr;
    BaseType_t ok = xTaskCreate(navigation_task, "nav_task", 12288,
                                new std::string(destination), 5, &task);
    if (ok != pdPASS) {
        std::lock_guard<std::mutex> lock(nav_mutex);
        nav_state.active = false;
        nav_state.task = nullptr;
        Application::GetInstance().Alert("导航错误", "导航任务创建失败", "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        return;
    }

    std::lock_guard<std::mutex> lock(nav_mutex);
    nav_state.task = task;
}

void stop_navigation(void)
{
    std::lock_guard<std::mutex> lock(nav_mutex);
    nav_state.active = false;
}

void set_navigation_mode(NavigationMode mode)
{
    std::string destination;
    bool restart = false;
    {
        std::lock_guard<std::mutex> lock(nav_mutex);
        nav_state.mode = mode;
        destination = nav_state.destination;
        restart = nav_state.active && !destination.empty();
    }

    if (restart) {
        start_navigation(destination, mode);
    } else {
        std::string message = std::string("已切换为") + mode_name(mode) + "导航";
        speak_navigation(message, "导航模式");
    }
}

NavigationMode get_navigation_mode(void)
{
    std::lock_guard<std::mutex> lock(nav_mutex);
    return nav_state.mode;
}

std::string get_navigation_status(void)
{
    std::lock_guard<std::mutex> lock(nav_mutex);
    if (!nav_state.active) {
        return std::string("未在导航，当前模式：") + mode_name(nav_state.mode);
    }
    return std::string("正在") + mode_name(nav_state.mode) + "导航到" +
           nav_state.destination + "，当前第" + std::to_string(nav_state.step_index + 1) + "步";
}
