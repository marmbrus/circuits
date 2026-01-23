#!/usr/bin/env python3
"""
LED Calibration Script

Measures LED output at various intensity levels using a colorimeter.
Logs results as JSONL for analysis.
"""

import subprocess
import threading
import time
import sys
import json
from collections import deque
from datetime import datetime
import argparse
import serial
from serial.tools import list_ports
import pyvisa

class OutputBuffer:
    """Thread-safe buffer for subprocess output."""
    def __init__(self):
        self.buffer = deque()  # No maxlen - we need all output for mark tracking
        self.lock = threading.Lock()
        self.mark = 0
    
    def append(self, data):
        with self.lock:
            self.buffer.extend(data)
    
    def get_text(self):
        with self.lock:
            return ''.join(self.buffer)
    
    def get_new_text(self):
        """Get text added since last mark."""
        with self.lock:
            full_text = ''.join(self.buffer)
            new_text = full_text[self.mark:]
            self.mark = len(full_text)
            return new_text

def reader_thread(stream, buffer, name):
    """Read from stream, mirror to console, and store in buffer."""
    print(f"\n[{name} reader thread STARTED]", file=sys.stderr)
    try:
        while True:
            char = stream.read(1)
            if not char:
                print(f"\n[{name} reader thread: EOF - stream closed]", file=sys.stderr)
                break
            text = char.decode('utf-8', errors='replace')
            print(text, end='', flush=True)
            buffer.append(text)
    except Exception as e:
        print(f"\n[{name} reader thread EXCEPTION: {e}]", file=sys.stderr)
    finally:
        print(f"\n[{name} reader thread EXITED]", file=sys.stderr)

def serial_reader_thread(ser, output, name):
    """Background thread to read serial port output."""
    print(f"\n[SERIAL reader thread STARTED]", file=sys.stderr)
    try:
        while True:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                with output.lock:
                    output.buffer.extend(data)
            time.sleep(0.01)
    except Exception as e:
        print(f"\n[SERIAL reader thread EXCEPTION: {e}]", file=sys.stderr)
    finally:
        print(f"\n[SERIAL reader thread EXITED]", file=sys.stderr)

def wait_for_prompt(buffer, proc, timeout=60):
    """Wait for the 'any other key to take a reading' prompt."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        exit_code = proc.poll()
        if exit_code is not None:
            print(f"\n!!! ERROR: spotread process DIED with exit code {exit_code} !!!", file=sys.stderr)
            return False
        
        # Look at whole buffer - prompt might already be there
        text = buffer.get_text()
        if "any other key to take a reading" in text:
            return True
        time.sleep(0.1)
    return False

def wait_for_serial_prompt(output, timeout=5.0):
    """Wait for esp32> prompt in serial output."""
    start = time.time()
    while time.time() - start < timeout:
        with output.lock:
            text = ''.join(output.buffer)
            if "esp32>" in text:
                return True
        time.sleep(0.1)
    return False

def set_leds(ser, serial_output, led_type, gpio, r, g, b, w, num_pixels=20):
    """Send LED control command and read response from the buffer."""
    # Clear the buffer
    with serial_output.lock:
        serial_output.buffer.clear()
    
    # Send newlines to clear any spurious data
    ser.write(b"\n\n\n")
    ser.flush()
    
    # Wait for prompt
    wait_for_serial_prompt(serial_output, timeout=2.0)
    
    # Clear buffer again before sending command
    with serial_output.lock:
        serial_output.buffer.clear()
    
    # Build command based on LED type
    if led_type == "sk6812":
        # SK6812 uses 8-bit RGBW values (0-255)
        cmd = f"sk6812_set {gpio} {r} {g} {b} {w} {num_pixels}\n"
        success_marker = "SK6812 frame sent"
    elif led_type == "hd108":
        # HD108 uses 16-bit RGB values (0-65535) and gain values (0-31)
        # Values are already in 16-bit range from point generation
        # Use fixed max gain values
        gain_b = 31
        gain_g = 31
        gain_r = 31
        # NOTE: The HD108 firmware command appears to have BGR swapped with RGB
        # When we send what it calls "blue", we get red light (high X)
        # When we send what it calls "red", we get blue light (high Z)
        # So swap b<->r to match physical reality
        cmd = f"hd108_set {gain_b} {gain_g} {gain_r} {r} {g} {b}\n"
        success_marker = "HD108 frame sent"
    else:
        raise ValueError(f"Unknown LED type: {led_type}")
    
    # Send the command
    print(f"    DEBUG: Sending command: {cmd.strip()}")
    ser.write(cmd.encode('utf-8'))
    ser.flush()
    
    # Wait for the confirmation
    start_time = time.time()
    while time.time() - start_time < 5.0:
        with serial_output.lock:
            response = ''.join(serial_output.buffer)
        
        if success_marker in response and "esp32>" in response:
            return True
        
        time.sleep(0.1)
    
    # Failed
    with serial_output.lock:
        response = ''.join(serial_output.buffer)
    import re
    clean_response = re.sub(r'\x1b\[[0-9;]*m', '', response)
    raise RuntimeError(f"LED command failed: {clean_response.strip()}")

def wait_for_result(buffer, proc, timeout=60):
    """Wait for 'Result is XYZ' in the output since last mark."""
    deadline = time.time() + timeout
    start_mark = buffer.mark  # Remember starting position
    
    while time.time() < deadline:
        exit_code = proc.poll()
        if exit_code is not None:
            print(f"\n!!! ERROR: spotread process DIED with exit code {exit_code} !!!", file=sys.stderr)
            return None
        
        # Get text from start_mark to current position WITHOUT advancing mark
        with buffer.lock:
            full_text = ''.join(buffer.buffer)
            new_text = full_text[start_mark:]
        
        if "Result is XYZ" in new_text:
            import re
            match = re.search(r"Result is XYZ:\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)", new_text)
            if match:
                x, y, z = float(match.group(1)), float(match.group(2)), float(match.group(3))
                # NOW advance the mark past the result
                with buffer.lock:
                    buffer.mark = len(full_text)
                return (x, y, z)
        time.sleep(0.1)
    return None

def find_serial_port():
    """Find the USB serial port for the ESP32."""
    ports = list(list_ports.comports())
    for p in ports:
        name = (p.device or "") + " " + (p.description or "")
        if "usbserial" in name.lower():
            return p.device
    if ports:
        return ports[0].device
    return None

def connect_ngp800(ip_address):
    """Connect to NGP800 power supply."""
    rm = pyvisa.ResourceManager('@py')
    resource = f"TCPIP::{ip_address}::INSTR"
    print(f"Connecting to NGP800 at {resource}...")
    ngp = rm.open_resource(resource)
    idn = ngp.query("*IDN?")
    print(f"  Connected: {idn.strip()}")
    return ngp

def read_power(ngp, channel):
    """Read voltage and current from NGP800 channel."""
    # Select channel
    ngp.write(f"INST:NSEL {channel}")
    
    # Query measured values
    voltage = float(ngp.query("MEAS:VOLT?").strip())
    current = float(ngp.query("MEAS:CURR?").strip())
    
    return voltage, current

def generate_points(num_points, max_value=255):
    """Generate logarithmic distribution of points (more at low values)."""
    if num_points == 1:
        return [0]
    
    import math
    
    # Use exponential spacing: more points at low end, fewer at high end
    # Formula: (e^(k*t) - 1) / (e^k - 1) where k controls the curve
    k = 4  # k=4 gives good logarithmic spacing
    
    # Oversample to ensure we get num_points unique values
    # Generate more points than requested, then take first num_points unique
    candidate_count = num_points
    points = []
    seen = set()
    
    while len(points) < num_points:
        # Generate candidate_count logarithmically-spaced points
        for i in range(candidate_count):
            t = i / (candidate_count - 1)
            value = int(round(max_value * (math.exp(k * t) - 1) / (math.exp(k) - 1)))
            value = max(0, min(max_value, value))
            
            if value not in seen:
                seen.add(value)
                points.append(value)
                
                # Stop once we have enough
                if len(points) >= num_points:
                    break
        
        # If we still don't have enough, increase candidate count
        if len(points) < num_points:
            candidate_count = int(candidate_count * 1.5)
    
    # Sort and return exactly num_points
    return sorted(points[:num_points])

def main():
    parser = argparse.ArgumentParser(description='Calibrate LEDs using a colorimeter')
    parser.add_argument('--led-type', choices=['sk6812', 'hd108'], required=True,
                        help='Type of LED strip')
    parser.add_argument('--gpio', type=int, required=True,
                        help='GPIO pin number')
    parser.add_argument('--spotread-bin', default='~/Downloads/Argyll_V3.4.1/bin/spotread',
                        help='Path to spotread binary')
    parser.add_argument('--num-points', type=int, default=10,
                        help='Number of measurement points per channel')
    parser.add_argument('--num-pixels', type=int, default=20,
                        help='Number of LED pixels')
    parser.add_argument('--ngp800-ip', default='10.1.0.245',
                        help='IP address of NGP800 power supply')
    parser.add_argument('--ngp800-channel', type=int, default=2,
                        help='NGP800 channel number to measure')
    
    args = parser.parse_args()
    
    # Generate run ID
    run_id = datetime.now().strftime('%Y-%m-%dT%H-%M-%S-%f')
    output_file = f"{run_id}.json"
    
    print(f"=== LED Calibration Run: {run_id} ===")
    print(f"Output file: {output_file}")
    print(f"LED type: {args.led_type}")
    print(f"GPIO: {args.gpio}")
    print(f"Points per channel: {args.num_points}")
    
    # Generate measurement points (16-bit for HD108, 8-bit for SK6812)
    max_value = 65535 if args.led_type == "hd108" else 255
    points = generate_points(args.num_points, max_value=max_value)
    print(f"Measurement points ({max_value} max): {points}")
    
    # Expand spotread path
    import os
    spotread_bin = os.path.expanduser(args.spotread_bin)
    
    # Start spotread FIRST
    print(f"\nStarting spotread: {spotread_bin} -e -y n")
    env = os.environ.copy()
    env['ARGYLL_NOT_INTERACTIVE'] = '1'
    
    proc = subprocess.Popen(
        [spotread_bin, "-e", "-y", "n"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
        close_fds=True,
        env=env
    )
    
    # Create output buffer
    output = OutputBuffer()
    
    # Start reader thread
    reader = threading.Thread(
        target=reader_thread,
        args=(proc.stdout, output, "stdout"),
        daemon=True
    )
    reader.start()
    
    # Give reader thread a moment
    time.sleep(1.0)
    
    # Check if spotread is still running
    exit_code = proc.poll()
    if exit_code is not None:
        print(f"\n\n!!! ERROR: spotread process DIED immediately with exit code {exit_code} !!!", file=sys.stderr)
        return 1
    
    print(f"[spotread process is running, PID={proc.pid}]", file=sys.stderr)
    
    # Connect to NGP800
    print(f"\n=== Connecting to NGP800 ===")
    try:
        ngp = connect_ngp800(args.ngp800_ip)
        print(f"Will monitor channel {args.ngp800_channel}")
    except Exception as e:
        print(f"ERROR: Failed to connect to NGP800: {e}")
        proc.kill()
        return 1
    
    try:
        # Wait for spotread to initialize
        print("\n=== Waiting for spotread initial prompt (may take 30+ seconds)... ===")
        if not wait_for_prompt(output, proc, timeout=60):
            print("ERROR: Timed out waiting for initial spotread prompt")
            return 1
        
        print("\n=== Spotread ready! Now opening serial port... ===")
        
        # Open serial port
        port = find_serial_port()
        if not port:
            print("ERROR: No serial port found")
            return 1
        
        print(f"Opening serial port: {port}")
        ser = serial.Serial(port, 115200, timeout=0.05)
        print("Serial port opened")
        time.sleep(0.5)  # Let port settle
        
        # Create output buffer for serial port
        serial_output = OutputBuffer()
        
        # Start reader thread for serial port
        serial_reader = threading.Thread(
            target=serial_reader_thread,
            args=(ser, serial_output, "serial"),
            daemon=True
        )
        serial_reader.start()
        time.sleep(0.2)  # Let reader thread start
        print("Serial reader thread started")
        
        # Open output file
        total_measurements = args.num_points * 4  # R, G, B, W
        current_measurement = 0
        
        with open(output_file, 'w') as f:
            # Test each channel
            for channel_name, channel_idx in [('R', 0), ('G', 1), ('B', 2), ('W', 3)]:
                print(f"\n\n{'='*60}")
                print(f"Testing channel: {channel_name}")
                print(f"{'='*60}")
                
                for point_idx, value in enumerate(points):
                    current_measurement += 1
                    progress = (current_measurement / total_measurements) * 100
                    
                    print(f"\n[{current_measurement}/{total_measurements} - {progress:.1f}%] Channel {channel_name}, Point {point_idx+1}/{args.num_points}: value={value}")
                    
                    # Set LED values
                    rgbw = [0, 0, 0, 0]
                    rgbw[channel_idx] = value
                    
                    set_leds(ser, serial_output, args.led_type, args.gpio, rgbw[0], rgbw[1], rgbw[2], rgbw[3], args.num_pixels)
                    
                    # Take measurement (only wait for prompt after first reading)
                    if current_measurement > 1:
                        if not wait_for_prompt(output, proc, timeout=60):
                            print("ERROR: Timed out waiting for prompt")
                            raise RuntimeError("Lost spotread prompt")
                    
                    # Trigger reading
                    proc.stdin.write(b"\n")
                    proc.stdin.flush()
                    
                    # Wait for result
                    xyz = wait_for_result(output, proc, timeout=60)
                    if xyz is None:
                        print("ERROR: Failed to get XYZ result")
                        raise RuntimeError("Failed to get measurement")
                    
                    print(f"  >>> XYZ: {xyz[0]:.6f}, {xyz[1]:.6f}, {xyz[2]:.6f}")
                    
                    # Read power from NGP800
                    voltage, current = read_power(ngp, args.ngp800_channel)
                    print(f"  >>> Power: {voltage:.6f} V, {current:.6f} A ({voltage*current:.6f} W)")
                    
                    # Write to file
                    record = {
                        "run_id": run_id,
                        "led_type": args.led_type,
                        "channel": channel_name,
                        "num_pixels": args.num_pixels,
                        "raw_values": {"r": rgbw[0], "g": rgbw[1], "b": rgbw[2], "w": rgbw[3]},
                        "read_values": {"x": xyz[0], "y": xyz[1], "z": xyz[2]},
                        "power": {"voltage": voltage, "current": current, "watts": voltage * current}
                    }
                    f.write(json.dumps(record) + '\n')
                    f.flush()
        
        print(f"\n\n{'='*60}")
        print("=== Calibration Complete! ===")
        print(f"Results saved to: {output_file}")
        print(f"{'='*60}\n")
        
        # Quit spotread
        print("=== Sending 'q' to quit ===")
        proc.stdin.write(b"q\n")
        proc.stdin.flush()
        
        proc.wait(timeout=5)
        
    except KeyboardInterrupt:
        print("\n\n=== Interrupted by user ===")
        proc.kill()
    except Exception as e:
        print(f"\n\n=== Error: {e} ===", file=sys.stderr)
        import traceback
        traceback.print_exc()
        proc.kill()
        return 1
    finally:
        if 'ser' in locals():
            print("\n=== Closing serial port ===")
            ser.close()
        if 'ngp' in locals():
            print("\n=== Closing NGP800 connection ===")
            ngp.close()
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
