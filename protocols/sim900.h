#pragma once

#include <functional>
#include <string>

enum class Sim900Event {
    IncomingCall,
    CallEnded,
    SmsSent,
    SmsFailed,
};

void sim900_init();
void sim900_task(void* arg);

bool sim900_answer_call();
bool sim900_hangup_call();
bool sim900_dial(const std::string& phone_number);
bool sim900_send_sms(const std::string& phone_number, const std::string& content);

bool sim900_has_incoming_call();
std::string sim900_get_incoming_number();
void sim900_set_event_callback(std::function<void(Sim900Event, const std::string&)> callback);
