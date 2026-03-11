# GDB Debug Script for FSDB Diagnostics
# Usage: arm-none-eabi-gdb -x gdb_debug.gdb arcana-embedded-f103.elf

# Connect to OpenOCD
target remote localhost:3333

# Halt the target
monitor halt

echo \n========================================\n
echo FSDB Diagnostic Information\necho ========================================\n\n
# Check if SdStorage is initialized
echo [1] Checking SdStorageServiceImpl::mDbReady...\n
# Set convenient variable
set $sdstorage = arcana::sdstorage::SdStorageServiceImpl::getInstance()

# Check key variables
printf "mDbReady = %d\n", $sdstorage.mDbReady
printf "mNonceCounter = %lu\n", (unsigned long)$sdstorage.mNonceCounter
printf "mLastRate = %d\n", $sdstorage.mLastRate
printf "mWritesInWindow = %d\n", $sdstorage.mWritesInWindow

echo \n[2] Checking Stats...\n
printf "mStats.recordCount = %lu\n", (unsigned long)$sdstorage.mStats.recordCount
printf "mStats.writesPerSec = %d\n", $sdstorage.mStats.writesPerSec

echo \n[3] Checking Storage Status...\n
# Check if exFAT is ready
printf "g_exfat_ready = %d\n", g_exfat_ready

# Check if write semaphore is available (just print address)
printf "mWriteSem = %p\n", $sdstorage.mWriteSem
printf "mRunning = %d\n", $sdstorage.mRunning

echo \n[4] Recent write check...\n
# Set a breakpoint on appendRecord and continue
# This will show if records are being written
break SdStorageServiceImpl::appendRecord if $sdstorage.mDbReady == true

echo \nBreakpoint set on appendRecord (only when mDbReady is true)\n
echo Use 'continue' to run and see if it hits\n\n
echo ========================================\necho Debug Info Complete\necho ========================================\n\n
# Keep GDB session open
# User can now:
# - Type 'continue' to run
# - Check variables with 'print'
# - Set more breakpoints
