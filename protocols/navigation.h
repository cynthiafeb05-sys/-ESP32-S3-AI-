#pragma once
#include <string>
#include "map.h"

void start_navigation(const std::string& destination);
void start_navigation(const std::string& destination, NavigationMode mode);
void stop_navigation(void);
void set_navigation_mode(NavigationMode mode);
NavigationMode get_navigation_mode(void);
std::string get_navigation_status(void);
