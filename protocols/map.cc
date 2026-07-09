#include "map.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <cmath>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define TAG "MAP"
#define MAP_KEY "2MZBZ-VPSWQ-6VQ5B-4BPYJ-IZBRT-XGFKE"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static std::string url_encode(const std::string& value)
{
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << std::setw(2) << int(c) << std::nouppercase;
        }
    }

    return escaped.str();
}

static bool ends_with(const std::string& text, const char* suffix)
{
    size_t suffix_len = strlen(suffix);
    return text.size() >= suffix_len &&
           text.compare(text.size() - suffix_len, suffix_len, suffix) == 0;
}

static std::string clean_address(std::string address)
{
    while (!address.empty()) {
        unsigned char c = static_cast<unsigned char>(address.back());
        if (std::isspace(c) || c == '.' || c == ',' || c == '?' || c == '!') {
            address.pop_back();
            continue;
        }
        if (ends_with(address, "。") || ends_with(address, "，") ||
            ends_with(address, "？") || ends_with(address, "！")) {
            address.resize(address.size() - 3);
            continue;
        }
        break;
    }
    return address;
}

static bool http_get(const std::string& url, std::string& out)
{
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.user_agent = "xiaozhi-esp32-navigation";

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "client init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    char buffer[512];
    out.clear();
    int read_len = 0;
    while ((read_len = esp_http_client_read(client, buffer, sizeof(buffer))) > 0) {
        out.append(buffer, read_len);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status_code == 200 && !out.empty();
}

static double transform_lat(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y +
                 0.1 * x * y + 0.2 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * M_PI) + 20.0 * sin(2.0 * x * M_PI)) * 2.0 / 3.0;
    ret += (20.0 * sin(y * M_PI) + 40.0 * sin(y / 3.0 * M_PI)) * 2.0 / 3.0;
    ret += (160.0 * sin(y / 12.0 * M_PI) + 320.0 * sin(y * M_PI / 30.0)) * 2.0 / 3.0;
    return ret;
}

static double transform_lng(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x +
                 0.1 * x * y + 0.1 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * M_PI) + 20.0 * sin(2.0 * x * M_PI)) * 2.0 / 3.0;
    ret += (20.0 * sin(x * M_PI) + 40.0 * sin(x / 3.0 * M_PI)) * 2.0 / 3.0;
    ret += (150.0 * sin(x / 12.0 * M_PI) + 300.0 * sin(x / 30.0 * M_PI)) * 2.0 / 3.0;
    return ret;
}

static bool out_of_china(double lat, double lng)
{
    return lng < 72.004 || lng > 137.8347 || lat < 0.8293 || lat > 55.8271;
}

void map_wgs84_to_gcj02(double wgs_lat, double wgs_lng, double& gcj_lat, double& gcj_lng)
{
    if (out_of_china(wgs_lat, wgs_lng)) {
        gcj_lat = wgs_lat;
        gcj_lng = wgs_lng;
        return;
    }

    const double a = 6378245.0;
    const double ee = 0.00669342162296594323;
    double d_lat = transform_lat(wgs_lng - 105.0, wgs_lat - 35.0);
    double d_lng = transform_lng(wgs_lng - 105.0, wgs_lat - 35.0);
    double rad_lat = wgs_lat / 180.0 * M_PI;
    double magic = sin(rad_lat);
    magic = 1 - ee * magic * magic;
    double sqrt_magic = sqrt(magic);
    d_lat = (d_lat * 180.0) / ((a * (1 - ee)) / (magic * sqrt_magic) * M_PI);
    d_lng = (d_lng * 180.0) / (a / sqrt_magic * cos(rad_lat) * M_PI);
    gcj_lat = wgs_lat + d_lat;
    gcj_lng = wgs_lng + d_lng;
}

bool map_get_location(const std::string& address, double& lat, double& lng)
{
    std::string cleaned = clean_address(address);
    if (cleaned.empty()) {
        ESP_LOGE(TAG, "empty address");
        return false;
    }

    std::string url = "https://apis.map.qq.com/ws/geocoder/v1/?address=" +
                      url_encode(cleaned) + "&key=" + MAP_KEY;

    std::string body;
    if (!http_get(url, body)) {
        ESP_LOGE(TAG, "geocode HTTP failed");
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGE(TAG, "geocode parse failed");
        return false;
    }

    cJSON* status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsNumber(status) || status->valueint != 0) {
        ESP_LOGE(TAG, "geocode API error: %s", body.c_str());
        cJSON_Delete(root);
        return false;
    }

    cJSON* result = cJSON_GetObjectItem(root, "result");
    cJSON* location = result ? cJSON_GetObjectItem(result, "location") : nullptr;
    cJSON* lat_json = location ? cJSON_GetObjectItem(location, "lat") : nullptr;
    cJSON* lng_json = location ? cJSON_GetObjectItem(location, "lng") : nullptr;
    if (!cJSON_IsNumber(lat_json) || !cJSON_IsNumber(lng_json)) {
        cJSON_Delete(root);
        return false;
    }

    lat = lat_json->valuedouble;
    lng = lng_json->valuedouble;
    ESP_LOGI(TAG, "Location %s: %.6f, %.6f", cleaned.c_str(), lat, lng);

    cJSON_Delete(root);
    return true;
}

static std::vector<std::pair<double, double>> parse_polyline(cJSON* polyline)
{
    std::vector<std::pair<double, double>> points;
    if (!cJSON_IsArray(polyline) || cJSON_GetArraySize(polyline) < 2) {
        return points;
    }

    std::vector<double> values;
    int count = cJSON_GetArraySize(polyline);
    values.reserve(count);
    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(polyline, i);
        if (!cJSON_IsNumber(item)) {
            return points;
        }
        values.push_back(item->valuedouble);
    }

    if (fabs(values[0]) > 180.0) {
        values[0] /= 1000000.0;
    }
    if (fabs(values[1]) > 180.0) {
        values[1] /= 1000000.0;
    }
    for (size_t i = 2; i < values.size(); ++i) {
        values[i] = values[i - 2] + values[i] / 1000000.0;
    }
    for (size_t i = 0; i + 1 < values.size(); i += 2) {
        points.emplace_back(values[i], values[i + 1]);
    }

    return points;
}

static bool get_step_end_from_route(cJSON* step,
                                    const std::vector<std::pair<double, double>>& route_points,
                                    double& lat,
                                    double& lng)
{
    cJSON* polyline_idx = cJSON_GetObjectItem(step, "polyline_idx");
    if (!cJSON_IsArray(polyline_idx) || route_points.empty()) {
        return false;
    }

    cJSON* end_item = cJSON_GetArrayItem(polyline_idx, 1);
    if (!cJSON_IsNumber(end_item)) {
        return false;
    }

    int end_index = end_item->valueint;
    if (end_index >= 0 && end_index < static_cast<int>(route_points.size())) {
        lat = route_points[end_index].first;
        lng = route_points[end_index].second;
        return true;
    }

    int point_index = end_index / 2;
    if (point_index >= 0 && point_index < static_cast<int>(route_points.size())) {
        lat = route_points[point_index].first;
        lng = route_points[point_index].second;
        return true;
    }

    return false;
}

bool map_get_route(double from_lat, double from_lng,
                   double to_lat, double to_lng,
                   NavigationMode mode,
                   std::vector<map_route_step_t>& steps,
                   double& total_distance)
{
    const char* profile = (mode == NavigationMode::Bicycling) ? "bicycling" : "walking";
    char url[512];
    snprintf(url, sizeof(url),
             "https://apis.map.qq.com/ws/direction/v1/%s/?from=%.6f,%.6f&to=%.6f,%.6f&key=%s",
             profile, from_lat, from_lng, to_lat, to_lng, MAP_KEY);

    std::string body;
    if (!http_get(url, body)) {
        ESP_LOGE(TAG, "route HTTP failed");
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGE(TAG, "route parse failed");
        return false;
    }

    cJSON* status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsNumber(status) || status->valueint != 0) {
        ESP_LOGE(TAG, "route API error: %s", body.c_str());
        cJSON_Delete(root);
        return false;
    }

    cJSON* result = cJSON_GetObjectItem(root, "result");
    cJSON* routes = result ? cJSON_GetObjectItem(result, "routes") : nullptr;
    cJSON* route0 = cJSON_IsArray(routes) ? cJSON_GetArrayItem(routes, 0) : nullptr;
    cJSON* steps_json = route0 ? cJSON_GetObjectItem(route0, "steps") : nullptr;
    if (!cJSON_IsArray(steps_json)) {
        cJSON_Delete(root);
        return false;
    }

    total_distance = 0;
    cJSON* distance_json = cJSON_GetObjectItem(route0, "distance");
    if (cJSON_IsNumber(distance_json)) {
        total_distance = distance_json->valuedouble;
    }

    std::vector<std::pair<double, double>> route_points =
        parse_polyline(cJSON_GetObjectItem(route0, "polyline"));

    steps.clear();
    int step_count = cJSON_GetArraySize(steps_json);
    steps.reserve(step_count);
    for (int i = 0; i < step_count; ++i) {
        cJSON* step_json = cJSON_GetArrayItem(steps_json, i);
        if (!cJSON_IsObject(step_json)) {
            continue;
        }

        map_route_step_t step;
        cJSON* instruction = cJSON_GetObjectItem(step_json, "instruction");
        cJSON* distance = cJSON_GetObjectItem(step_json, "distance");
        if (cJSON_IsString(instruction)) {
            step.instruction = instruction->valuestring;
        }
        if (cJSON_IsNumber(distance)) {
            step.distance = distance->valuedouble;
        }

        std::vector<std::pair<double, double>> step_points =
            parse_polyline(cJSON_GetObjectItem(step_json, "polyline"));
        if (!step_points.empty()) {
            step.end_lat = step_points.back().first;
            step.end_lng = step_points.back().second;
        } else if (!get_step_end_from_route(step_json, route_points, step.end_lat, step.end_lng)) {
            step.end_lat = to_lat;
            step.end_lng = to_lng;
        }

        if (step.instruction.empty()) {
            step.instruction = "沿当前道路前行";
        }
        steps.push_back(std::move(step));
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "route mode=%s steps=%u distance=%.0f", profile, static_cast<unsigned>(steps.size()), total_distance);
    return !steps.empty();
}

bool map_get_route(double from_lat, double from_lng,
                   double to_lat, double to_lng,
                   std::string& instruction)
{
    std::vector<map_route_step_t> steps;
    double total_distance = 0;
    if (!map_get_route(from_lat, from_lng, to_lat, to_lng,
                       NavigationMode::Walking, steps, total_distance)) {
        instruction = "路线规划失败";
        return false;
    }

    instruction = steps.front().instruction;
    return true;
}
