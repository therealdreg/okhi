import subprocess

# By Dreg

def parse_memory_usage(output):
    # Define the total RAM in bytes (264 KB for RP2040)
    total_ram = 264 * 1024  # 270336 bytes

    # Initialize memory usage dictionary
    memory_usage = {}

    # Extract relevant information from objdump output
    for line in output.splitlines():
        parts = line.split()
        if len(parts) > 1 and parts[1] in ['.data', '.bss', '.text', '.firam']:
            section_name = parts[1]
            section_size = int(parts[2], 16)  # Convert hex size to decimal
            memory_usage[section_name] = section_size

    total_used_ram = 0
    # Calculate and print the percentage of RAM used by each section
    for section, size in memory_usage.items():
        percentage = (size / total_ram) * 100
        print(f"Memory used in {section}: {size} bytes, {percentage:.2f}%")
        total_used_ram += size

    # Calculate and print total percentage of RAM used
    total_percentage = (total_used_ram / total_ram) * 100
    print(f"Total memory used: {total_used_ram} bytes, {total_percentage:.2f}%")

def main():
    # Command to execute objdump and path to the ELF file
    command = ["arm-none-eabi-objdump", "-h", "build/okhi_crc.elf"]
    result = subprocess.run(command, capture_output=True, text=True)

    if result.returncode == 0:
        parse_memory_usage(result.stdout)
    else:
        print("Error running objdump:")
        print(result.stderr)

if __name__ == "__main__":
    main()
