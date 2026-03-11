#!/bin/bash
#
# Flash and Debug script for STM32F103ZE
# Usage: ./flash-debug.sh [command]
# Commands: flash, debug, reset, halt

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE="$SCRIPT_DIR/Targets/stm32f103ze/Debug/arcana-embedded-f103.elf"
OPENOCD_CFG="$SCRIPT_DIR/openocd.cfg"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

print_info() {
    echo -e "${YELLOW}ℹ️  $1${NC}"
}

# Check if firmware exists
check_firmware() {
    if [ ! -f "$FIRMWARE" ]; then
        print_error "Firmware not found: $FIRMWARE"
        exit 1
    fi
    print_success "Firmware found: $FIRMWARE"
    ls -lh "$FIRMWARE"
}

# Check if OpenOCD config exists
check_config() {
    if [ ! -f "$OPENOCD_CFG" ]; then
        print_error "OpenOCD config not found: $OPENOCD_CFG"
        exit 1
    fi
    print_success "OpenOCD config found: $OPENOCD_CFG"
}

# Flash firmware
flash() {
    print_header "Flashing STM32F103ZE"
    check_firmware
    check_config
    
    print_info "Connecting to target via SWD..."
    print_info "This may take a few seconds..."
    echo ""
    
    # Flash and verify
    openocd -f "$OPENOCD_CFG" \
        -c "init" \
        -c "reset halt" \
        -c "flash write_image erase $FIRMWARE" \
        -c "verify_image $FIRMWARE" \
        -c "reset run" \
        -c "shutdown"
    
    echo ""
    print_success "Flash completed successfully!"
    print_info "Firmware has been written to the target."
}

# Start debug server
debug() {
    print_header "Starting Debug Server"
    check_config
    
    print_info "Starting OpenOCD GDB server..."
    print_info "GDB port: 3333"
    print_info "Telnet port: 4444"
    print_info ""
    print_info "You can now connect with GDB:"
    print_info "  arm-none-eabi-gdb $FIRMWARE"
    print_info "  (gdb) target remote localhost:3333"
    print_info ""
    print_info "Or use telnet for OpenOCD commands:"
    print_info "  telnet localhost 4444"
    print_info ""
    print_info "Press Ctrl+C to stop the server"
    echo ""
    
    # Start OpenOCD in foreground
    openocd -f "$OPENOCD_CFG"
}

# Reset target
reset_target() {
    print_header "Resetting Target"
    check_config
    
    openocd -f "$OPENOCD_CFG" \
        -c "init" \
        -c "reset run" \
        -c "shutdown"
    
    print_success "Target reset successfully!"
}

# Halt target
halt() {
    print_header "Halting Target"
    check_config
    
    openocd -f "$OPENOCD_CFG" \
        -c "init" \
        -c "halt" \
        -c "shutdown"
    
    print_success "Target halted!"
}

# Main
case "${1:-flash}" in
    flash)
        flash
        ;;
    debug)
        debug
        ;;
    reset)
        reset_target
        ;;
    halt)
        halt
        ;;
    *)
        echo "Usage: $0 [flash|debug|reset|halt]"
        echo ""
        echo "Commands:"
        echo "  flash  - Flash firmware to target (default)"
        echo "  debug  - Start OpenOCD GDB server"
        echo "  reset  - Reset the target"
        echo "  halt   - Halt the target"
        exit 1
        ;;
esac
