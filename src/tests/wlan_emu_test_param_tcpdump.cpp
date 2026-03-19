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
#include "wlan_emu_ext_sta_mgr.h"
#include "wlan_emu_ext_agent_interface.h"
#include "wlan_emu_err_code.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>

static int external_tcpdump_grace_timeout = 3;

/**
 * @brief Execute the tcpdump step.
 *
 * Encodes a JSON subdoc describing either a start or stop tcpdump operation,
 * writes it to a temporary file and HTTP POSTs it to the external agent's
 * /Testconfig/ endpoint.
 *
 * @return RETURN_OK on success, RETURN_ERR on failure.
 */
int test_step_param_tcpdump::step_execute()
{
    test_step_params_t *step = this;
    std::string agent_subdoc;

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Called for Test Step Num : %d\n",
            __func__, __LINE__, step->step_number);

    // Execute based on operation type
    if (step->u.tcpdump->input_operation == tcpdump_operation_type_start) {
        // Start external tcpdump
        if (encode_external_tcpdump_start_subdoc(agent_subdoc) != RETURN_OK) {
            wlan_emu_print(wlan_emu_log_level_err,
                "%s:%d: Failed to start tcpdump for step %d\n",
                __func__, __LINE__, step->step_number);
            step->test_state = wlan_emu_tests_state_cmd_abort;
            return RETURN_ERR;
        }

        // Set execution time based on duration
        if (step->u.tcpdump->u.start_conf.duration > 0) {
            step->execution_time = step->u.tcpdump->u.start_conf.duration;
            step->test_state = wlan_emu_tests_state_cmd_continue;
        } else {
            step->test_state = wlan_emu_tests_state_cmd_results;
        }
        
    } else if (step->u.tcpdump->input_operation == tcpdump_operation_type_stop) {
        // Stop external tcpdump
        if (encode_external_tcpdump_stop_subdoc(agent_subdoc) != RETURN_OK) {
            wlan_emu_print(wlan_emu_log_level_err,
                "%s:%d: Failed to stop tcpdump for step %d\n",
                __func__, __LINE__, step->step_number);
            step->test_state = wlan_emu_tests_state_cmd_abort;
            return RETURN_ERR;
        }
        step->test_state = wlan_emu_tests_state_cmd_results;
        
    } else {
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: Invalid tcpdump operation type: %d\n",
            __func__, __LINE__, step->u.tcpdump->input_operation);
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    return RETURN_OK;
}
/**
 * @brief Upload tcpdump result files to the test controller.
 *
 * Stores the downloaded result file path in the step config and calls
 * step_upload_files() to push it to the TDA.
 *
 * @param files List of downloaded result file paths.
 * @return RETURN_OK on success, RETURN_ERR on failure.
 */

int test_step_param_tcpdump::step_upload_files(FILE *output_file, bool *update_to_tda)
{
    test_step_params_t *step = this;
    wlan_emu_pcap_captures *res_file = nullptr;
    unsigned int results_count = 0;
    char *temp_res_file = nullptr;
    char res_file_name[128] = {0};
    char *remote_test_results_loc = nullptr;

    if (step->capture_frames == true) {
        if (step->test_results_queue == nullptr) {
            wlan_emu_print(wlan_emu_log_level_err, 
                          "%s:%d: test_results_queue is null for step %d\n", 
                          __func__, __LINE__, step->step_number);
            return RETURN_ERR;
        }
        
        results_count = queue_count(step->test_results_queue);
        if (results_count == 0) {
            wlan_emu_print(wlan_emu_log_level_err, 
                          "%s:%d: No test results files to upload for step number %d\n", 
                          __func__, __LINE__, step->step_number);
            return RETURN_ERR;
        }

        res_file = (wlan_emu_pcap_captures *)queue_pop(step->test_results_queue);
        remote_test_results_loc = step->m_ui_mgr->get_remote_test_results_loc();

        while (res_file != nullptr) {
            wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: File: %s\n", 
                          __func__, __LINE__, res_file->pcap_file);
                          
            if (step->m_ui_mgr->upload_file_to_server(res_file->pcap_file, 
                                                       remote_test_results_loc) != RETURN_OK) {
                wlan_emu_print(wlan_emu_log_level_err, 
                              "%s:%d: failed to upload %s\n", 
                              __func__, __LINE__, res_file->pcap_file);
                delete res_file;
                return RETURN_ERR;
            } else {
                wlan_emu_print(wlan_emu_log_level_info, 
                              "%s:%d: uploaded %s\n", 
                              __func__, __LINE__, res_file->pcap_file);
                              
                *update_to_tda = true;
                temp_res_file = strdup(res_file->pcap_file);
                
                if (step->m_ui_mgr->get_last_substring_after_slash(temp_res_file, 
                                                                    res_file_name, 
                                                                    sizeof(res_file_name)) != RETURN_OK) {
                    wlan_emu_print(wlan_emu_log_level_err, 
                                  "%s:%d: get_last_substring_after_slash failed for str : %s\n",
                                  __func__, __LINE__, temp_res_file);
                    free(temp_res_file);
                    delete res_file;
                    return RETURN_ERR;
                }
                
                fprintf(output_file, "%s\n", res_file_name);
                free(temp_res_file);
            }
            
            delete res_file;
            res_file = (wlan_emu_pcap_captures *)queue_pop(step->test_results_queue);
        }
        
        queue_destroy(step->test_results_queue);
        step->test_results_queue = nullptr;
    }

    return RETURN_OK;
}

int test_step_param_tcpdump::push_tcpdump_result_files(const std::vector<std::string> &files)
{
    int ret;
    test_step_params_t *step = this;

    for (const std::string &file : files) {
        wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: result file: %s\n", __func__, __LINE__,
            file.c_str());

        ret = snprintf(step->u.tcpdump->u.start_conf.result_file,
            sizeof(step->u.tcpdump->u.start_conf.result_file), "%s", file.c_str());
        if (ret < 0 || ret >= (int)sizeof(step->u.tcpdump->u.start_conf.result_file)) {
            wlan_emu_print(wlan_emu_log_level_err, "%s:%d: failed to write file name\n", __func__,
                __LINE__);
            return RETURN_ERR;
        }
    }

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: result file is : %s\n", __func__, __LINE__,
        step->u.tcpdump->u.start_conf.result_file);

    if (step->u.tcpdump->input_operation != tcpdump_operation_type_stop) {
        if (step->m_ui_mgr->step_upload_files(
                step->u.tcpdump->u.start_conf.result_file) != RETURN_OK) {
            wlan_emu_print(wlan_emu_log_level_err, "%s:%d: step_upload_files failed\n", __func__,
                __LINE__);
            step->m_ui_mgr->cci_error_code = EPUSHTSTRESFILE;
            step->test_state = wlan_emu_tests_state_cmd_abort;
            return RETURN_ERR;
        }
    }

    return RETURN_OK;
}

/**
 * @brief Timeout handler for the tcpdump step.
 *
 * Polls the external agent for test status. On completion, downloads the
 * result files and uploads them to the TDA.
 *
 * @return RETURN_OK on success, RETURN_ERR on failure.
 */
int test_step_param_tcpdump::step_timeout()
{
    test_step_params_t *step = this;

    wlan_emu_print(wlan_emu_log_level_dbg, 
                  "%s:%d: Test Step Num : %d timeout_count : %d\n",
                  __func__, __LINE__, step->step_number, step->timeout_count);

    if (step->test_state != wlan_emu_tests_state_cmd_results) {
        step->timeout_count++;

        if (step->execution_time == step->timeout_count) {
            step->test_state = wlan_emu_tests_state_cmd_results;
            wlan_emu_print(wlan_emu_log_level_info, 
                          "%s:%d: Test duration of %d completed for step %d\n",
                          __func__, __LINE__, step->execution_time, step->step_number);
            return RETURN_OK;
        }
    }
    
    return RETURN_OK;
}
/**
 * @brief Clean up the tcpdump step resources.
 */
void test_step_param_tcpdump::step_remove()
{
    test_step_param_tcpdump *step = dynamic_cast<test_step_param_tcpdump *>(this);
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Destructor for tcpdump step called\n", __func__,
        __LINE__);

    if (step == nullptr) {
        return;
    }
    if (step->is_step_initialized == true) {
        delete step->u.tcpdump;
    }
    delete step;
    step = nullptr;
}

/**
 * @brief Frame filter — not used by the tcpdump step.
 */
int test_step_param_tcpdump::step_frame_filter(wlan_emu_msg_t *msg)
{
    test_step_params_t *step = this;
    wlan_emu_msg_data_t *f_data = nullptr;
    unsigned int radio_index = 0;
    bool is_radio_index_found = false;
    
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: step number : %d\n", 
                   __func__, __LINE__, step->step_number);
    
    if (msg == nullptr) {
        return RETURN_UNHANDLED;
    }

    // Check if capture is enabled and message type matches
    if ((step->capture_frames != true) || 
        (!(step->frame_request.msg_type & (1<<msg->get_msg_type())))) {
        return RETURN_UNHANDLED;
    }

    switch (msg->get_msg_type()) {
        case wlan_emu_msg_type_webconfig:
            // Handle webconfig messages if needed
            break;
            
        case wlan_emu_msg_type_cfg80211: // beacon/start_ap
            f_data = msg->get_msg();
            if (f_data->u.cfg80211.u.start_ap.phy_index == step->u.tcpdump->radio_index) {
                wlan_emu_print(wlan_emu_log_level_dbg, 
                              "%s:%d: Handled cfg80211 frame for radio_index: %d\n",
                              __func__, __LINE__, step->u.tcpdump->radio_index);
                msg->unload_cfg80211_start_ap(step);
                return RETURN_HANDLED;
            }
            break;
            
        case wlan_emu_msg_type_frm80211: // management frames
            if (!(step->frame_request.frm80211_ops & (1<<msg->get_frm80211_ops_type()))) {
                return RETURN_UNHANDLED;
            }

            f_data = msg->get_msg();
            switch (f_data->u.frm80211.ops) {
                case wlan_emu_frm80211_ops_type_prb_req:
                    // Probe requests - usually broadcast
                    is_radio_index_found = true;
                    break;
                    
                case wlan_emu_frm80211_ops_type_assoc_req:
                case wlan_emu_frm80211_ops_type_reassoc_req:
                case wlan_emu_frm80211_ops_type_action:
                    // Check client MAC address
                    if (step->m_ui_mgr->get_radioindex_from_bssid(
                            f_data->u.frm80211.u.frame.client_macaddr, &radio_index) == RETURN_OK) {
                        if (radio_index == step->u.tcpdump->radio_index) {
                            is_radio_index_found = true;
                        }
                    }
                    break;
                    
                case wlan_emu_frm80211_ops_type_auth:
                case wlan_emu_frm80211_ops_type_eapol:
                case wlan_emu_frm80211_ops_type_deauth:
                case wlan_emu_frm80211_ops_type_disassoc:
                    // Check both AP and client MAC addresses
                    if (step->m_ui_mgr->get_radioindex_from_bssid(
                            f_data->u.frm80211.u.frame.macaddr, &radio_index) == RETURN_OK) {
                        if (radio_index == step->u.tcpdump->radio_index) {
                            is_radio_index_found = true;
                        }
                    }
                    
                    if (is_radio_index_found == false) {
                        if (step->m_ui_mgr->get_radioindex_from_bssid(
                                f_data->u.frm80211.u.frame.client_macaddr, &radio_index) == RETURN_OK) {
                            if (radio_index == step->u.tcpdump->radio_index) {
                                is_radio_index_found = true;
                            }
                        }
                    }
                    break;
                    
                case wlan_emu_frm80211_ops_type_prb_resp:
                case wlan_emu_frm80211_ops_type_assoc_resp:
                case wlan_emu_frm80211_ops_type_reassoc_resp:
                    // Check AP MAC address (source)
                    if (step->m_ui_mgr->get_radioindex_from_bssid(
                            f_data->u.frm80211.u.frame.macaddr, &radio_index) == RETURN_OK) {
                        if (radio_index == step->u.tcpdump->radio_index) {
                            is_radio_index_found = true;
                        }
                    }
                    break;
                    
                default:
                    return RETURN_UNHANDLED;
            }

            if (is_radio_index_found == true) {
                wlan_emu_print(wlan_emu_log_level_dbg, 
                              "%s:%d: Handled frame of type : %d for radio_index: %d\n", 
                              __func__, __LINE__, msg->get_frm80211_ops_type(), 
                              step->u.tcpdump->radio_index);
                msg->unload_frm80211_msg(step);
                return RETURN_HANDLED;
            }
            break;

        default:
            wlan_emu_print(wlan_emu_log_level_dbg, 
                          "%s:%d: Not supported msg_type : %d\n", 
                          __func__, __LINE__, msg->get_msg_type());
            break;
    }
    
    return RETURN_UNHANDLED;
}
/**
 * @brief Encode and send the tcpdump start subdoc to the external agent.
 *
 * Builds a JSON object with the start configuration and HTTP POSTs it to the
 * agent's /Testconfig/ endpoint.
 *
 * @param agent_subdoc  Output string containing the serialised JSON.
 * @return RETURN_OK on success, RETURN_ERR on failure.
 */
int test_step_param_tcpdump::encode_external_tcpdump_start_subdoc(std::string &agent_subdoc)
{
    cJSON *json = NULL;
    char *str = NULL;
    char tcpdump_config_json[256] = { 0 };
    test_step_params_t *step = this;
    wlan_emu_ext_agent_interface_t *agent_info = NULL;
    std::string agent_url;
    FILE *fp;

    agent_info = step->m_ext_sta_mgr->get_ext_agent(
        (char *)step->u.tcpdump->sta_key.c_str());
    if (agent_info == NULL) {
        step->m_ui_mgr->cci_error_code = EEXTAGENT;
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: failed to find external agent for key: %s\n", __func__, __LINE__,
            step->u.tcpdump->sta_key.c_str());
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    agent_url = agent_info->get_agent_proto() + std::string(agent_info->agent_ip_address) +
        agent_info->get_agent_port() + std::string("/Testconfig/");

    json = cJSON_CreateObject();
    if (json == NULL) {
        step->m_ui_mgr->cci_error_code = EJSONPARSE;
        return RETURN_ERR;
    }

    cJSON_AddStringToObject(json, "SubDocName", "ExternalTcpDump");
    cJSON_AddNumberToObject(json, "StepNumber", step->step_number);
    cJSON_AddNumberToObject(json, "TcpDumpOperation", step->u.tcpdump->input_operation);
    cJSON_AddBoolToObject(json, "Fork", step->fork);
    cJSON_AddStringToObject(json, "TestCaseID", step->test_case_id);
    cJSON_AddStringToObject(json, "TestCaseName", step->test_case_name);
    cJSON_AddStringToObject(json, "Interface", step->u.tcpdump->u.start_conf.interface_name);
    cJSON_AddStringToObject(json, "CmdOptions", step->u.tcpdump->u.start_conf.cmd_options);
    cJSON_AddNumberToObject(json, "Duration", step->u.tcpdump->u.start_conf.duration);
    cJSON_AddStringToObject(json, "OutputFileName", step->u.tcpdump->u.start_conf.output_file_name);

    str = cJSON_Print(json);
    if (str == nullptr) {
        step->m_ui_mgr->cci_error_code = EJSONPARSE;
        cJSON_Delete(json);
        return RETURN_ERR;
    }

    agent_subdoc = std::string(str);
    wlan_emu_print(wlan_emu_log_level_info, "%s:%d: tcpdump start subdoc : %s\n", __func__,
        __LINE__, agent_subdoc.c_str());

    snprintf(tcpdump_config_json, sizeof(tcpdump_config_json), "/tmp/cci_res/%s_%d.json",
        agent_info->agent_hostname.c_str(), step->step_number);

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: tcpdump_config_json : %s\n", __func__, __LINE__,
        tcpdump_config_json);

    if ((fp = fopen(tcpdump_config_json, "w")) == NULL) {
        step->m_ui_mgr->cci_error_code = EFOPEN;
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: fopen failed for %s\n", __func__, __LINE__,
            tcpdump_config_json);
        cJSON_free(str);
        cJSON_Delete(json);
        return RETURN_ERR;
    }
    if (fwrite(agent_subdoc.c_str(), agent_subdoc.length(), 1, fp) != 1) {
        step->m_ui_mgr->cci_error_code = EFWRITE;
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: fwrite failed\n", __func__, __LINE__);
        fclose(fp);
        cJSON_free(str);
        cJSON_Delete(json);
        return RETURN_ERR;
    }
    fclose(fp);

    long status_code;
    http_post_file(agent_url, tcpdump_config_json, status_code, step->m_ui_mgr->cci_error_code);
    if (status_code != http_status_code_ok) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: http_post_file failed : %ld\n", __func__,
            __LINE__, status_code);
        cJSON_free(str);
        cJSON_Delete(json);
        return RETURN_ERR;
    }

    cJSON_free(str);
    cJSON_Delete(json);

    return RETURN_OK;
}

/**
 * @brief Encode and send the tcpdump stop subdoc to the external agent.
 *
 * Resolves the matching start step, finds the agent, and HTTP POSTs the stop
 * command JSON to the agent's /Testconfig/ endpoint.
 *
 * @param agent_subdoc  Output string containing the serialised JSON.
 * @return RETURN_OK on success, RETURN_ERR on failure.
 */
int test_step_param_tcpdump::encode_external_tcpdump_stop_subdoc(std::string &agent_subdoc)
{
    cJSON *json = NULL;
    test_step_params_t *step = this;
    test_step_params_t *start_step = NULL;
    char *str = NULL;
    char tcpdump_config_json[256] = { 0 };
    wlan_emu_ext_agent_interface_t *agent_info = NULL;
    std::string agent_url;
    FILE *fp;

    wlan_emu_test_case_config *test_case_config =
        (wlan_emu_test_case_config *)step->param_get_test_case_config();

    // Locate the matching start step to find the agent key
    start_step = (test_step_params_t *)step->m_ui_mgr->get_step_from_step_number(test_case_config,
        step->u.tcpdump->u.stop_conf.stop_step_number);
    if (start_step == NULL) {
        step->m_ui_mgr->cci_error_code = ESTEPSTOPPED;
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: Invalid tcpdump start step : %d in step : %d\n", __func__, __LINE__,
            step->u.tcpdump->u.stop_conf.stop_step_number, step->step_number);
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    step->u.tcpdump->sta_key = start_step->u.tcpdump->sta_key;

    agent_info = step->m_ext_sta_mgr->get_ext_agent(
        (char *)step->u.tcpdump->sta_key.c_str());
    if (agent_info == NULL) {
        step->m_ui_mgr->cci_error_code = EEXTAGENT;
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: failed to find external agent for key: %s\n", __func__, __LINE__,
            step->u.tcpdump->sta_key.c_str());
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    agent_url = agent_info->get_agent_proto() + std::string(agent_info->agent_ip_address) +
        agent_info->get_agent_port() + std::string("/Testconfig/");

    json = cJSON_CreateObject();
    if (json == NULL) {
        step->m_ui_mgr->cci_error_code = EJSONPARSE;
        return RETURN_ERR;
    }

    cJSON_AddStringToObject(json, "SubDocName", "ExternalTcpDump");
    cJSON_AddNumberToObject(json, "StepNumber", step->step_number);
    cJSON_AddNumberToObject(json, "TcpDumpOperation", step->u.tcpdump->input_operation);
    cJSON_AddBoolToObject(json, "Fork", step->fork);
    cJSON_AddStringToObject(json, "TestCaseID", step->test_case_id);
    cJSON_AddStringToObject(json, "TestCaseName", step->test_case_name);
    cJSON_AddNumberToObject(json, "StopTcpDumpStepNumber",
        step->u.tcpdump->u.stop_conf.stop_step_number);

    str = cJSON_Print(json);
    if (str == nullptr) {
        step->m_ui_mgr->cci_error_code = EJSONPARSE;
        cJSON_Delete(json);
        return RETURN_ERR;
    }

    agent_subdoc = std::string(str);
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: tcpdump stop subdoc : %s\n", __func__, __LINE__,
        agent_subdoc.c_str());

    snprintf(tcpdump_config_json, sizeof(tcpdump_config_json), "/tmp/cci_res/%s_%d.json",
        agent_info->agent_hostname.c_str(), step->step_number);

    if ((fp = fopen(tcpdump_config_json, "w")) == NULL) {
        step->m_ui_mgr->cci_error_code = EFOPEN;
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: fopen failed for %s\n", __func__, __LINE__,
            tcpdump_config_json);
        cJSON_free(str);
        cJSON_Delete(json);
        return RETURN_ERR;
    }
    if (fwrite(agent_subdoc.c_str(), agent_subdoc.length(), 1, fp) != 1) {
        step->m_ui_mgr->cci_error_code = EFWRITE;
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: fwrite failed\n", __func__, __LINE__);
        fclose(fp);
        cJSON_free(str);
        cJSON_Delete(json);
        return RETURN_ERR;
    }
    fclose(fp);

    long status_code;
    http_post_file(agent_url, tcpdump_config_json, status_code, step->m_ui_mgr->cci_error_code);
    if (status_code != http_status_code_ok) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: http_post_file failed : %ld\n", __func__,
            __LINE__, status_code);
        cJSON_free(str);
        cJSON_Delete(json);
        return RETURN_ERR;
    }

    cJSON_free(str);
    cJSON_Delete(json);

    return RETURN_OK;
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
    step->execution_time = 5;  // Default 5 seconds
    step->timeout_count = 0;
    step->capture_frames = false;
}

test_step_param_tcpdump::~test_step_param_tcpdump()
{
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Destructor for tcpdump step called\n", __func__,
        __LINE__);
}
