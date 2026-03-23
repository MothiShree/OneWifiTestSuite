#include "wlan_emu_log.h"
#include "wlan_emu_test_params.h"
#include "wlan_emu_err_code.h"
#include <assert.h>
#include <experimental/filesystem>
extern "C" {
#include <secure_wrapper.h>
}
int v_secure_system(const char *command, ...);
namespace fs = std::experimental::filesystem;

int test_step_param_tcpdump::step_execute()
{
    char *buff;
    int ret = RETURN_OK;
    char command[512];

    test_step_params_t *step = this;

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Called for Test Step Num : %d\n", __func__,
        __LINE__, step->step_number);

    if (step->capture_frames == false) {
        wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Test Step Num : %d Invalid capture frames\n",
            __func__, __LINE__, step->step_number);
        step->m_ui_mgr->cci_error_code = EFRAMECAP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    // Execute interface setup and capture commands
    wlan_emu_print(wlan_emu_log_level_info,
        "%s:%d: Setting up interfaces and starting packet capture\n", __func__, __LINE__);

    // Bring up wlan0 interface
    snprintf(command, sizeof(command), "ifconfig wlan0 up");
    ret = v_secure_system("%s", command);
    if (ret != 0) {
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: Failed to bring up wlan0 interface, ret=%d\n", __func__, __LINE__, ret);
        step->m_ui_mgr->cci_error_code = EFRAMECAP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    // Add wlan0 to brlan0 bridge
    snprintf(command, sizeof(command), "ovs-vsctl add-port brlan0 wlan0");
    ret = v_secure_system("%s", command);
    if (ret != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to add wlan0 to brlan0, ret=%d\n",
            __func__, __LINE__, ret);
        step->m_ui_mgr->cci_error_code = EFRAMECAP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    // Bring up hwsim0 interface
    snprintf(command, sizeof(command), "ifconfig hwsim0 up");
    ret = v_secure_system("%s", command);
    if (ret != 0) {
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: Failed to bring up hwsim0 interface, ret=%d\n", __func__, __LINE__, ret);
        step->m_ui_mgr->cci_error_code = EFRAMECAP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    // Add hwsim0 to brlan0 bridge
    snprintf(command, sizeof(command), "ovs-vsctl add-port brlan0 hwsim0");
    ret = v_secure_system("%s", command);
    if (ret != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to add hwsim0 to brlan0, ret=%d\n",
            __func__, __LINE__, ret);
        step->m_ui_mgr->cci_error_code = EFRAMECAP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    // Start tcpdump on hwsim0 in background
    snprintf(command, sizeof(command), "tcpdump -s0 -vv -i hwsim0 -w /tmp/test_hwsim0 &");
    ret = v_secure_system("%s", command);
    if (ret != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to start tcpdump on hwsim0, ret=%d\n",
            __func__, __LINE__, ret);
        step->m_ui_mgr->cci_error_code = EFRAMECAP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    // Start tcpdump on wlan0 in background
    snprintf(command, sizeof(command), "tcpdump -s0 -vv -i wlan0 -w /tmp/test_wlan0 &");
    ret = v_secure_system("%s", command);
    if (ret != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to start tcpdump on wlan0, ret=%d\n",
            __func__, __LINE__, ret);
        step->m_ui_mgr->cci_error_code = EFRAMECAP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    // Start tcpdump on brlan0 in background
    snprintf(command, sizeof(command), "tcpdump -s0 -vv -i brlan0 -w /tmp/test_brlan0 &");
    ret = v_secure_system("%s", command);
    if (ret != 0) {
        wlan_emu_print(wlan_emu_log_level_err, "%s:%d: Failed to start tcpdump on brlan0, ret=%d\n",
            __func__, __LINE__, ret);
        step->m_ui_mgr->cci_error_code = EFRAMECAP;
        step->test_state = wlan_emu_tests_state_cmd_abort;
        return RETURN_ERR;
    }

    wlan_emu_print(wlan_emu_log_level_info,
        "%s:%d: Successfully started packet captures on all interfaces\n", __func__, __LINE__);

    if (step->u.tcpdump->duration > 0) {
        step->execution_time = step->u.tcpdump->duration;
        step->test_state = wlan_emu_tests_state_cmd_continue;
    } else {
        step->test_state = wlan_emu_tests_state_cmd_results;
    }

    return RETURN_OK;
}

int test_step_param_tcpdump::step_timeout()
{

    test_step_params_t *step = this;

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Test Step Num : %d timeout_count : %d\n",
        __func__, __LINE__, step->step_number, step->timeout_count);

    if (step->test_state != wlan_emu_tests_state_cmd_results) {
        step->timeout_count++;

        if (step->execution_time == step->timeout_count) {
            step->test_state = wlan_emu_tests_state_cmd_results;
            wlan_emu_print(wlan_emu_log_level_info,
                "%s:%d: Test duration of %d  completed for step %d\n", __func__, __LINE__,
                step->execution_time, step->step_number);
            step_upload_files("/tmp/test_hwsim0");
            step_upload_files("/tmp/test_wlan0");
            step_upload_files("/tmp/test_brlan0");
            return RETURN_OK;
        }
    }
    return RETURN_OK;
}

void test_step_param_tcpdump::step_remove()
{
    test_step_param_tcpdump *step = dynamic_cast<test_step_param_tcpdump *>(this);
    int ret = 0;

    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Destructor for command called\n", __func__,
        __LINE__);

    if (step == nullptr) {
        return;
    }

    // Stop all running tcpdump processes
    wlan_emu_print(wlan_emu_log_level_info, "%s:%d: Stopping all tcpdump processes\n", __func__,
        __LINE__);

    ret = v_secure_system("killall tcpdump");
    if (ret != 0) {
        wlan_emu_print(wlan_emu_log_level_warn,
            "%s:%d: Failed to kill tcpdump processes, ret=%d (may not be running)\n", __func__,
            __LINE__, ret);
    } else {
        wlan_emu_print(wlan_emu_log_level_info, "%s:%d: Successfully stopped tcpdump processes\n",
            __func__, __LINE__);
    }

    if (step->is_step_initialized == true) {
        delete step->u.tcpdump;
    }
    delete step;
    step = nullptr;

    return;
}

test_step_param_tcpdump::test_step_param_tcpdump()
{
    test_step_params_t *step = this;
    step->is_step_initialized = true;
    step->u.tcpdump = new (std::nothrow) tcpdump_config_t;
    if (step->u.tcpdump == nullptr) {
        wlan_emu_print(wlan_emu_log_level_err,
            "%s:%d: allocation of memory for tcpdump_config_t failed for %d\n", __func__, __LINE__,
            step->step_number);
        step->is_step_initialized = false;
        return;
    }
    memset(step->u.tcpdump, 0, sizeof(tcpdump_config_t));
    step->execution_time = 5;
    step->timeout_count = 0;
    step->capture_frames = false;
}

test_step_param_tcpdump::~test_step_param_tcpdump()
{
    wlan_emu_print(wlan_emu_log_level_dbg, "%s:%d: Destructor for command called\n", __func__,
        __LINE__);
}