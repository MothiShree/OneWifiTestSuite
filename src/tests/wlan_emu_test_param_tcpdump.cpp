/**
 * Copyright 2025 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wlan_emu_log.h"
#include "wlan_emu_test_params.h"
#include "wlan_emu_err_code.h"
#include <assert.h>

int test_step_param_tcpdump::step_execute()
{
    test_step_params_t *step = this;
    char timestamp[24] = { 0 };
    char tar_file_path[256] = { 0 };
    int ret = RETURN_OK;

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Called for Test Step Num : %d\n", __func__,
        __LINE__, step->step_number);
    step->test_state = wlan_emu_tests_state_cmd_continue;

    // Step 1: Bring up network interfaces
    wlan_emu_print(wlan_emu_log_level_info, "%s:%d: Bringing up network interfaces\n", __func__,
        __LINE__);
    
    if (system("ifconfig wlan0 up") != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to bring up wlan0\n", __func__,
            __LINE__);
    }

    if (system("ovs-vsctl add-port brlan0 wlan0") != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to add wlan0 to brlan0\n", __func__,
            __LINE__);
    }

    if (system("ifconfig hwsim0 up") != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to bring up hwsim0\n", __func__,
            __LINE__);
    }

    if (system("ovs-vsctl add-port brlan0 hwsim0") != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to add hwsim0 to brlan0\n", __func__,
            __LINE__);
    }

    // Step 2: Start tcpdump processes
    wlan_emu_print(wlan_emu_log_level_info, "%s:%d: Starting tcpdump processes\n", __func__,
        __LINE__);

    if (system("tcpdump -s0 -vv -i hwsim0 -w /tmp/test_hwsim0 &") != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to start tcpdump on hwsim0\n",
            __func__, __LINE__);
    }

    if (system("tcpdump -s0 -vv -i wlan0 -w /tmp/test_wlan0 &") != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to start tcpdump on wlan0\n",
            __func__, __LINE__);
    }

    if (system("tcpdump -s0 -vv -i brlan0 -w /tmp/test_brlan0 &") != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to start tcpdump on brlan0\n",
            __func__, __LINE__);
    }

    // Small delay to ensure tcpdump processes have started
    sleep(1);

    // Step 3: Wait for configured duration (handled by step_timeout)
    wlan_emu_print(wlan_emu_log_level_info,
        "%s:%d: Waiting for capture duration of %d seconds\n", __func__, __LINE__,
        step->execution_time);

    return RETURN_OK;
}

int test_step_param_tcpdump::step_timeout()
{
    test_step_params_t *step = this;
    char timestamp[24] = { 0 };
    char tar_file_path[256] = { 0 };

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Test Step Num : %d timeout_count : %d\n",
        __func__, __LINE__, step->step_number, step->timeout_count);

    if (step->test_state != wlan_emu_tests_state_cmd_results) {
        step->timeout_count++;

        // When execution_time is reached, stop tcpdump and create tar file
        if (step->execution_time == step->timeout_count) {
            wlan_emu_print(wlan_emu_log_level_info,
                "%s:%d: Capture duration completed. Stopping tcpdump processes\n", __func__,
                __LINE__);

            // Step 4: Kill all tcpdump processes
            if (system("killall tcpdump") != 0) {
                wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to kill tcpdump processes\n",
                    __func__, __LINE__);
            }

            // Small delay to ensure tcpdump processes have stopped and flushed data
            sleep(1);

            // Generate timestamp for the tar file
            if (get_current_time_string(timestamp, sizeof(timestamp)) != RETURN_OK) {
                wlan_emu_print(wlan_emu_log_level_err, "%s:%d: get_current_time_string failed\n",
                    __func__, __LINE__);
                step->m_ui_mgr->cci_error_code = ESYSOPS;
                step->test_state = wlan_emu_tests_state_cmd_abort;
                return RETURN_ERR;
            }

            // Step 5: Create tar file with timestamp
            snprintf(tar_file_path, sizeof(tar_file_path), "%s/%s_%d_%s_test_tcp.tar.gz",
                step->m_ui_mgr->get_test_results_dir_path(), step->test_case_id, step->step_number,
                timestamp);

            snprintf(step->u.tcpdump->tar_filename, sizeof(step->u.tcpdump->tar_filename), "%s",
                tar_file_path);

            wlan_emu_print(wlan_emu_log_level_info, "%s:%d: Creating tar file: %s\n", __func__,
                __LINE__, tar_file_path);

            char tar_cmd[512] = { 0 };
            snprintf(tar_cmd, sizeof(tar_cmd), "tar -zcvf %s /tmp/test_hwsim0 /tmp/test_wlan0 /tmp/test_brlan0 2>/dev/null",
                tar_file_path);

            if (system(tar_cmd) != 0) {
                wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to create tar file\n",
                    __func__, __LINE__);
                step->m_ui_mgr->cci_error_code = ESYSOPS;
                step->test_state = wlan_emu_tests_state_cmd_abort;
                return RETURN_ERR;
            }

            // Step 6: Upload tar file to controller
            if (step->m_ui_mgr->step_upload_files(step->u.tcpdump->tar_filename) != RETURN_OK) {
                wlan_emu_print(wlan_emu_log_level_err, "%s:%d: step_upload_files failed for %s\n",
                    __func__, __LINE__, step->u.tcpdump->tar_filename);
                step->test_state = wlan_emu_tests_state_cmd_abort;
                step->m_ui_mgr->cci_error_code = EPUSHTSTRESFILE;
                return RETURN_ERR;
            }

            wlan_emu_print(wlan_emu_log_level_info,
                "%s:%d: Successfully uploaded tar file: %s\n", __func__, __LINE__,
                step->u.tcpdump->tar_filename);

            // Clean up temporary capture files
            system("rm -f /tmp/test_hwsim0 /tmp/test_wlan0 /tmp/test_brlan0");

            step->test_state = wlan_emu_tests_state_cmd_results;
            wlan_emu_print(wlan_emu_log_level_info,
                "%s:%d: Tcpdump capture completed for step %d\n", __func__, __LINE__,
                step->step_number);
            return RETURN_OK;
        }
    }
    return RETURN_OK;
}

void test_step_param_tcpdump::step_remove()
{
    test_step_param_tcpdump *step = dynamic_cast<test_step_param_tcpdump *>(this);
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Destructor for tcpdump called\n", __func__,
        __LINE__);

    if (step == NULL) {
        return;
    }
    if (step->is_step_initialized == true) {
        delete step->u.tcpdump;
    }
    delete step;
    step = NULL;

    return;
}

int test_step_param_tcpdump::step_frame_filter(wlan_emu_msg_t *msg)
{
    test_step_params_t *step = this;
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: step number : %d\n", __func__, __LINE__,
        step->step_number);
    if (msg == NULL) {
        return RETURN_UNHANDLED;
    }
    switch (msg->get_msg_type()) {
    case wlan_emu_msg_type_webconfig:
    case wlan_emu_msg_type_cfg80211:
    case wlan_emu_msg_type_frm80211:
    default:
        wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Not supported msg_type : %d\n", __func__,
            __LINE__, msg->get_msg_type());
        break;
    }
    return RETURN_UNHANDLED;
}

test_step_param_tcpdump::test_step_param_tcpdump()
{
    test_step_params_t *step = this;
    step->is_step_initialized = true;
    step->u.tcpdump = new (std::nothrow) tcpdump_t;
    if (step->u.tcpdump == nullptr) {
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: allocation of memory for tcpdump failed for %d\n", __func__, __LINE__,
            step->step_number);
        step->is_step_initialized = false;
        return;
    }
    memset(step->u.tcpdump, 0, sizeof(tcpdump_t));
    step->execution_time = 30; // Default 30 seconds capture duration
    step->timeout_count = 0;
    step->capture_frames = false;
}

test_step_param_tcpdump::~test_step_param_tcpdump()
{
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Destructor for tcpdump called\n", __func__,
        __LINE__);
}
