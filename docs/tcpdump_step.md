# Tcpdump Test Step

## Overview
The tcpdump test step allows capturing network packets on device interfaces (wlan0, hwsim0, brlan0) and uploading the captured data to the controller as a tar archive.

## JSON Configuration

### Basic Configuration
```json
{
    "TestStepNumber": 1,
    "Tcpdump": {
        "Duration": 30
    }
}
```

### Configuration Parameters

- **TestStepNumber** (required): The step number in the test sequence
- **Tcpdump** (required): Object containing tcpdump configuration
  - **Duration** (optional): Capture duration in seconds. Default is 30 seconds if not specified.

## How It Works

The tcpdump step performs the following operations:

1. **Network Interface Setup**:
   - Brings up wlan0 interface: `ifconfig wlan0 up`
   - Adds wlan0 to brlan0 bridge: `ovs-vsctl add-port brlan0 wlan0`
   - Brings up hwsim0 interface: `ifconfig hwsim0 up`
   - Adds hwsim0 to brlan0 bridge: `ovs-vsctl add-port brlan0 hwsim0`

2. **Packet Capture**:
   - Starts tcpdump on hwsim0: `tcpdump -s0 -vv -i hwsim0 -w /tmp/test_hwsim0 &`
   - Starts tcpdump on wlan0: `tcpdump -s0 -vv -i wlan0 -w /tmp/test_wlan0 &`
   - Starts tcpdump on brlan0: `tcpdump -s0 -vv -i brlan0 -w /tmp/test_brlan0 &`

3. **Capture Duration**:
   - Waits for the specified duration (default 30 seconds)

4. **Cleanup and Archive**:
   - Stops all tcpdump processes: `killall tcpdump`
   - Creates a compressed tar archive: `tar -zcvf test_tcp.tar.gz /tmp/test_hwsim0 /tmp/test_wlan0 /tmp/test_brlan0`
   - Uploads the tar file to the controller
   - Removes temporary capture files

## Example Test Case

```json
{
    "TestCaseID": "TC001",
    "TestCaseName": "Network Packet Capture Test",
    "Steps": [
        {
            "TestStepNumber": 1,
            "Tcpdump": {
                "Duration": 60
            }
        }
    ]
}
```

## Output

The step creates a tar.gz file with the following naming convention:
```
<TestCaseID>_<StepNumber>_<Timestamp>_test_tcp.tar.gz
```

This tar file contains three pcap files:
- `/tmp/test_hwsim0` - Packets captured on hwsim0 interface
- `/tmp/test_wlan0` - Packets captured on wlan0 interface
- `/tmp/test_brlan0` - Packets captured on brlan0 interface

The tar file is automatically uploaded to the controller and can be downloaded for analysis.

## Notes

- All network interface setup commands are executed automatically; no user intervention required
- The default capture duration is 30 seconds if not specified
- Temporary capture files are automatically cleaned up after the tar file is created
- Ensure tcpdump is installed on the device before using this step
- Ensure ovs-vsctl is available if using Open vSwitch
