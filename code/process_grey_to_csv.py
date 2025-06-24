import numpy as np
import glob
import os
import csv
from datetime import datetime

# Frame dimensions for Lepton 3.1R
FRAME_WIDTH = 160
FRAME_HEIGHT = 120

def raw_to_temperature(raw_value):
    # Simplified conversion: raw value to Kelvin (adjust based on calibration)
    # For Lepton 3.1R, raw values are typically scaled (e.g., 100 * Kelvin)
    return raw_value / 100.0

def process_grey_file(grey_file, output_csv):
    # Read raw 16-bit frame
    frame = np.fromfile(grey_file, dtype=np.uint16).reshape(FRAME_HEIGHT, FRAME_WIDTH)
    
    # Open CSV file for writing
    with open(output_csv, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        # Write header
        writer.writerow(['x', 'y', 'temperature_kelvin', 'temperature_celsius'])
        
        # Process each pixel
        for y in range(FRAME_HEIGHT):
            for x in range(FRAME_WIDTH):
                raw_value = frame[y, x]
                temp_k = raw_to_temperature(raw_value)
                temp_c = temp_k - 273.15  # Convert to Celsius
                writer.writerow([x, y, f"{temp_k:.2f}", f"{temp_c:.2f}"])

def main():
    # Find all .grey files in current directory
    grey_files = glob.glob("lepton_*.grey")
    if not grey_files:
        print("No .grey files found in current directory")
        return
    
    # Process each .grey file
    for grey_file in grey_files:
        # Generate output CSV filename (e.g., lepton_20250624_142355.csv)
        base_name = os.path.splitext(grey_file)[0]
        output_csv = f"{base_name}.csv"
        
        print(f"Processing {grey_file} to {output_csv}")
        process_grey_file(grey_file, output_csv)

if __name__ == "__main__":
    main()