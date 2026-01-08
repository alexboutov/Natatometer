"""
Natatometer Serial Data Capture

Automatically captures CSV data from ESP32 serial output
without manual copy-paste.

Usage:
    python serial_capture.py                    # Auto-detect COM port
    python serial_capture.py COM6               # Specify port
    python serial_capture.py COM6 output.csv    # Specify port and filename

Commands while running:
    Press Ctrl+C to stop capture
    
The script will:
    1. Connect to ESP32 serial port
    2. Wait for "CSV DATA START" marker
    3. Capture all data until "CSV DATA END"
    4. Save to timestamped CSV file
    5. Optionally send DELETE command
"""

import serial
import serial.tools.list_ports
import sys
import os
from datetime import datetime
import time


def find_esp32_port():
    """Auto-detect ESP32 COM port."""
    ports = serial.tools.list_ports.comports()
    
    for port in ports:
        # Common ESP32 identifiers
        if 'CP210' in port.description or 'CH340' in port.description or \
           'USB' in port.description or 'Serial' in port.description:
            print(f"Found potential ESP32: {port.device} - {port.description}")
            return port.device
    
    # List all ports if auto-detect fails
    print("Available COM ports:")
    for port in ports:
        print(f"  {port.device}: {port.description}")
    
    return None


def capture_csv_data(ser, output_file):
    """Capture CSV data between markers and save to file."""
    
    print("\nWaiting for data dump...")
    print("(Long-press button on ESP32 to dump data)")
    print("-" * 40)
    
    capturing = False
    lines_captured = 0
    
    with open(output_file, 'w') as f:
        while True:
            try:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    # Check for start marker
                    if "CSV DATA START" in line:
                        print(">>> Capture started!")
                        capturing = True
                        continue
                    
                    # Check for end marker
                    if "CSV DATA END" in line:
                        print(f">>> Capture complete! {lines_captured} lines saved.")
                        capturing = False
                        break
                    
                    # Capture data lines
                    if capturing:
                        f.write(line + '\n')
                        lines_captured += 1
                        
                        # Progress indicator
                        if lines_captured % 500 == 0:
                            print(f"    {lines_captured} lines...")
                    else:
                        # Echo non-capture output
                        if line:
                            print(line)
                
                time.sleep(0.001)  # Small delay to prevent CPU spin
                
            except KeyboardInterrupt:
                print("\n\nCapture interrupted by user.")
                if lines_captured > 0:
                    print(f"Partial data saved: {lines_captured} lines")
                return False
    
    return True


def prompt_delete(ser):
    """Ask user if they want to delete data on ESP32."""
    
    print("\n" + "-" * 40)
    response = input("Delete data on ESP32? (y/n): ").strip().lower()
    
    if response == 'y':
        ser.write(b'DELETE\n')
        time.sleep(0.5)
        
        # Read response
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)
        
        print("Delete command sent.")
    else:
        print("Data kept on ESP32.")


def main():
    # Parse arguments
    port = None
    output_file = None
    
    if len(sys.argv) >= 2:
        port = sys.argv[1]
    if len(sys.argv) >= 3:
        output_file = sys.argv[2]
    
    # Auto-detect port if not specified
    if port is None:
        port = find_esp32_port()
        if port is None:
            print("\nCould not auto-detect ESP32.")
            port = input("Enter COM port (e.g., COM6): ").strip()
    
    # Generate output filename if not specified
    if output_file is None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_file = f"test-data/walk_data_{timestamp}.csv"
    
    # Ensure output directory exists
    os.makedirs(os.path.dirname(output_file) if os.path.dirname(output_file) else '.', exist_ok=True)
    
    print(f"\n{'='*50}")
    print("Natatometer Serial Data Capture")
    print(f"{'='*50}")
    print(f"Port: {port}")
    print(f"Output: {output_file}")
    print(f"{'='*50}")
    
    # Connect to serial port
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"\nConnected to {port}")
    except serial.SerialException as e:
        print(f"\nError: Could not open {port}")
        print(f"  {e}")
        print("\nMake sure:")
        print("  1. ESP32 is connected")
        print("  2. Arduino Serial Monitor is closed")
        print("  3. Correct COM port is specified")
        sys.exit(1)
    
    try:
        # Capture data
        success = capture_csv_data(ser, output_file)
        
        if success:
            # Show file info
            file_size = os.path.getsize(output_file)
            print(f"\nSaved to: {output_file}")
            print(f"File size: {file_size / 1024:.1f} KB")
            
            # Offer to delete
            prompt_delete(ser)
            
            # Suggest next step
            print(f"\nTo analyze:")
            print(f"  python analyze_walk_data.py {output_file}")
        
    finally:
        ser.close()
        print("\nSerial port closed.")


if __name__ == "__main__":
    main()
