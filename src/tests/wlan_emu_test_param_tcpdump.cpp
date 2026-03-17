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

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Called for Test Step Num : %d\n", __func__,
        __LINE__, step->step_number);

    if (step->u.tcpdump->input_operation == tcpdump_operation_type_stop) {
        wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: stop_step_number : %d\n", __func__,
            __LINE__, step->u.tcpdump->u.stop_conf.stop_step_number);

        if (encode_external_tcpdump_stop_subdoc(agent_subdoc) == RETURN_ERR) {
            wlan_emu_print(wlan_emu_log_level_err,
                "%s:%d: encode external tcpdump stop failed for step : %d\n", __func__, __LINE__,
                step->step_number);
            step->test_state = wlan_emu_tests_state_cmd_abort;
            return RETURN_ERR;
        }
    } else if (step->u.tcpdump->input_operation == tcpdump_operation_type_start) {
        wlan_emu_print(wlan_emu_log_level_dbg,
            "%s:%d: interface : %s cmd_options : %s duration : %d output_file : %s\n",
            __func__, __LINE__,
            step->u.tcpdump->u.start_conf.interface_name,
            step->u.tcpdump->u.start_conf.cmd_options,
            step->u.tcpdump->u.start_conf.duration,
            step->u.tcpdump->u.start_conf.output_file_name);

        if (encode_external_tcpdump_start_subdoc(agent_subdoc) == RETURN_ERR) {
            wlan_emu_print(wlan_emu_log_level_err,
                "%s:%d: encode external tcpdump start failed for step : %d\n", __func__, __LINE__,
                step->step_number);
            step->test_state = wlan_emu_tests_state_cmd_abort;
            return RETURN_ERR;
        }
    } else {
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: invalid tcpdump operation for step : %d\n", __func__, __LINE__,
            step->step_number);
        step->m_ui_mgr->cci_error_code = ESTEP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    if (step->fork == true) {
        step->test_state = wlan_emu_tests_state_cmd_wait;
    } else {
        step->test_state = wlan_emu_tests_state_cmd_continue;
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
    wlan_emu_ext_agent_interface_t *ext_agent;
    ext_agent_status_resp_t status = {};

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: sta_key: %s\n", __func__, __LINE__,
        step->u.tcpdump->sta_key.c_str());

    ext_agent = step->m_ext_sta_mgr->get_ext_agent(
        (char *)step->u.tcpdump->sta_key.c_str());
    if (ext_agent == NULL) {
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: failed to find external agent for key: %s\n", __func__, __LINE__,
            step->u.tcpdump->sta_key.c_str());
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    if (ext_agent->get_external_agent_test_status(status, step->m_ui_mgr->cci_error_code) == RETURN_ERR) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: failed to get external agent status\n",
            __func__, __LINE__);
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: name: %s agent status: %s step count: %d\n",
        __func__, __LINE__, status.agent_name.c_str(),
        ext_agent->agent_state_as_string(status.state).c_str(), status.step_count);

    for (const ext_agent_step_status_t &s : status.steps) {
        wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: step num: %d step state: %s\n", __func__,
            __LINE__, s.step_number, step_state_as_string(s.state).c_str());
        for (const std::string &file : s.result_files) {
            wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: result file: %s\n", __func__, __LINE__,
                file.c_str());
        }
    }

    step->timeout_count++;

    if ((step->timeout_count == external_tcpdump_grace_timeout) &&
        (status.state == ext_agent_test_state_idle)) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: agent state : %s for step : %d\n", __func__,
            __LINE__, ext_agent->agent_state_as_string(status.state).c_str(), step->step_number);
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    if (status.state == ext_agent_test_state_fail) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: external agent state failed for %d\n",
            __func__, __LINE__, step->step_number);
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    auto step_iter = status.steps.begin();
    for (; step_iter != status.steps.end(); ++step_iter) {
        wlan_emu_print(wlan_emu_log_level_dbg,
            "%s:%d: step_iter->step_number : %d step_state : %s agent_state : %s\n", __func__,
            __LINE__, step_iter->step_number, step_state_as_string(step_iter->state).c_str(),
            ext_agent->agent_state_as_string(status.state).c_str());
        if (step_iter->step_number == step_number) {
            step->test_state = step_iter->state;
            break;
        }
    }

    if (step_iter == status.steps.end()) {
        wlan_emu_print(wlan_emu_log_level_info,
            "%s:%d: failed to get step state for number: %d agent_state : %s\n", __func__, __LINE__,
            step_number, ext_agent->agent_state_as_string(status.state).c_str());
        if (step->fork == true) {
            step->test_state = wlan_emu_tests_state_cmd_wait;
        } else {
            step->test_state = wlan_emu_tests_state_cmd_continue;
        }
        return RETURN_OK;
    }

    if (step->test_state == wlan_emu_tests_state_cmd_results) {

        if (step->u.tcpdump->input_operation == tcpdump_operation_type_stop) {
            return RETURN_OK;
        }

        if (ext_agent->download_external_agent_result_files(step_iter->result_files,
                step->m_ui_mgr->cci_error_code) != RETURN_OK) {
            wlan_emu_print(wlan_emu_log_level_err, "%s:%d: failed to download test results\n",
                __func__, __LINE__);
            step->test_state = wlan_emu_tests_state_cmd_abort;
            return RETURN_ERR;
        }

        if (push_tcpdump_result_files(step_iter->result_files) != RETURN_OK) {
            wlan_emu_print(wlan_emu_log_level_err, "%s:%d: failed to push test results\n", __func__,
                __LINE__);
            step->test_state = wlan_emu_tests_state_cmd_abort;
            return RETURN_ERR;
        }
        return RETURN_OK;

    } else if (step->test_state == wlan_emu_tests_state_cmd_abort) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: abort step number: %d\n", __func__, __LINE__,
            step_number);
        return RETURN_ERR;
    } else {
        wlan_emu_print(wlan_emu_log_level_info, "%s:%d: test state: %s step number: %d\n", __func__,
            __LINE__, step_state_as_string(step->test_state).c_str(), step_number);
    }

    if (step->fork == true) {
        step->test_state = wlan_emu_tests_state_cmd_wait;
    } else {
        step->test_state = wlan_emu_tests_state_cmd_continue;
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
        step->u.tcpdump = nullptr;
    }
    delete step;
    step = nullptr;

    return;
}

/**
 * @brief Frame filter — not used by the tcpdump step.
 */
int test_step_param_tcpdump::step_frame_filter(wlan_emu_msg_t *msg)
{
    test_step_params_t *step = this;
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: unhandled frame for step number : %d\n",
        __func__, __LINE__, step->step_number);
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
    step->execution_time = 30;
    step->timeout_count = 0;
    step->capture_frames = false;
}

test_step_param_tcpdump::~test_step_param_tcpdump()
{
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Destructor for tcpdump step called\n", __func__,
        __LINE__);
}
