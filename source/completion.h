#pragma once

#include "ce.h"
#include "ce_config.h"
#include "auto_complete.h"

void completion_update_buffer(Buffer_t* completion_buffer, AutoComplete_t* auto_complete, const char* match);
bool completion_gen_files_in_current_dir(AutoComplete_t* auto_complete, const char* dir);
bool completion_calc_start_and_path(AutoComplete_t* auto_complete, const char* line, Point_t cursor,
                                    Buffer_t* completion_buffer, const char* start_path);
void clang_completion(ConfigState_t* config_state, Point_t start_completion);
