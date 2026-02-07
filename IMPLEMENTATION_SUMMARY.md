# Tcpdump Test Step Implementation Summary

## Overview
This implementation adds a new test step type to the OneWifi Test Suite that allows automated tcpdump packet capture on device network interfaces.

## Changes Made

### 1. Data Structures (inc/wlan_emu_common.h)
- Added `tcpdump_t` structure to hold the tar filename
- Added `step_param_type_tcpdump` to the `step_param_type_t` enum

### 2. Test Step Class (inc/wlan_emu_test_params.h)
- Added `tcpdump_t *tcpdump` to the union in `test_step_params_t`
- Created `test_step_param_tcpdump` class with required methods

### 3. Implementation (src/tests/wlan_emu_test_param_tcpdump.cpp)
New file implementing the tcpdump step with the following features:

#### step_execute()
- Brings up network interfaces (wlan0, hwsim0)
- Adds interfaces to brlan0 bridge using ovs-vsctl
- Starts tcpdump processes on three interfaces:
  - hwsim0
  - wlan0
  - brlan0
- Returns control while tcpdump runs in background

#### step_timeout()
- Monitors the capture duration
- When duration is reached:
  - Kills all tcpdump processes
  - Creates a timestamped tar.gz archive containing all captures
  - Uploads the tar file to the controller
  - Cleans up temporary files

#### Constructor
- Initializes tcpdump_t structure
- Sets default capture duration to 30 seconds

### 4. UI Manager Integration (src/ui/wlan_emu_ui_mgr.cpp)
- Added `decode_step_tcpdump()` function to parse JSON configuration
- Added step instantiation logic to create tcpdump step from JSON
- Supports optional Duration parameter in JSON

### 5. Header Declaration (inc/wlan_emu_ui_mgr.h)
- Added declaration for `decode_step_tcpdump()` function

### 6. Documentation (docs/tcpdump_step.md)
- Complete user guide with JSON configuration examples
- Detailed explanation of step operations
- Notes on requirements and output format

### 7. Sample Configuration (config/sample_tcpdump_test.json)
- Example test case using the tcpdump step
- Shows 60-second capture duration configuration

## How to Use

### JSON Configuration Format
```json
{
    "TestStepNumber": 1,
    "Tcpdump": {
        "Duration": 30
    }
}
```

### Parameters
- **Duration** (optional): Capture duration in seconds. Default: 30 seconds

## Implementation Details

### Commands Executed
1. `ifconfig wlan0 up` - Bring up wlan0 interface
2. `ovs-vsctl add-port brlan0 wlan0` - Add wlan0 to bridge
3. `ifconfig hwsim0 up` - Bring up hwsim0 interface
4. `ovs-vsctl add-port brlan0 hwsim0` - Add hwsim0 to bridge
5. `tcpdump -s0 -vv -i hwsim0 -w /tmp/test_hwsim0 &` - Capture on hwsim0
6. `tcpdump -s0 -vv -i wlan0 -w /tmp/test_wlan0 &` - Capture on wlan0
7. `tcpdump -s0 -vv -i brlan0 -w /tmp/test_brlan0 &` - Capture on brlan0
8. Wait for configured duration
9. `killall tcpdump` - Stop all captures
10. `tar -zcvf <filename> /tmp/test_*` - Create archive
11. Upload archive to controller
12. `rm -f /tmp/test_*` - Clean up temporary files

### Output File Format
The tar.gz file is named: `<TestCaseID>_<StepNumber>_<Timestamp>_test_tcp.tar.gz`

It contains three pcap files:
- `/tmp/test_hwsim0` - hwsim0 capture
- `/tmp/test_wlan0` - wlan0 capture
- `/tmp/test_brlan0` - brlan0 capture

## Testing
The user mentioned they will:
1. Run unit tests manually
2. Raise the PR manually

## Notes
- The implementation follows the existing code patterns in the repository
- All includes are already available through existing headers
- The CMakeLists.txt uses GLOB_RECURSE, so the new .cpp file is automatically included
- Error handling is consistent with other test steps
- Logging uses the standard wlan_emu_print facility

## Requirements
- tcpdump must be installed on the device
- ovs-vsctl must be available for Open vSwitch operations
- Network interfaces (wlan0, hwsim0, brlan0) should exist on the device
