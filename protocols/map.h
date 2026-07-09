#pragma once
#include <string>
#include <vector>

enum class NavigationMode {
    Walking,
    Bicycling,
};

struct map_route_step_t {
    std::string instruction;
    double distance = 0;
    double end_lat = 0;
    double end_lng = 0;
};

bool map_get_location(const std::string& address, double& lat, double& lng);

bool map_get_route(double from_lat, double from_lng,
                   double to_lat, double to_lng,
                   std::string& instruction);

bool map_get_route(double from_lat, double from_lng,
                   double to_lat, double to_lng,
                   NavigationMode mode,
                   std::vector<map_route_step_t>& steps,
                   double& total_distance);

void map_wgs84_to_gcj02(double wgs_lat, double wgs_lng, double& gcj_lat, double& gcj_lng);
