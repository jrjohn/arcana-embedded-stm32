# Lazy Virtual FAL + ADS1298 8ch 模擬測試 工作狀態

## 最後更新
2026-03-12

## 任務目標
1. 解決 STM32F103 SDIO DMA ~600 次寫入限制 → **已解決 (Lazy Virtual FAL)**
2. 擴大 TSDB → **已完成 (2MB→8MB)**
3. SDIO 軟重置 (OpenOCD flash 不需斷電) → **已解決**
4. 修復 erase bug (171K phantom records) → **已解決**
5. 解耦 sensor data 與 SD 寫入壓測 → **已解決**
6. 高頻壓測 80/s → **已驗證穩定**
7. **ADS1298 8ch 模擬測試** → **進行中**

## Git 狀態

### 已提交 (branch: debug-lcd-records)
```
a0e99ff Stabilize 10Hz stress test with debug logging and publish decimation
af5e0fe Fix FlashDB max_size for file mode and validate 80Hz stress test
820bf0f Fix 50Hz stress test: increase task priority, disable DELETE_FDB_ON_BOOT
dbc7fe3 Add SD write stress test decoupled from sensor data
db5d2d5 Fix Lazy Virtual FAL erase: write 0xFF to disk instead of bitmap-only
```

### 未提交的修改 (ADS1298 8ch 模擬實作中)
```
SdFalAdapter.hpp       : TSDB_SIZE 2MB → 8MB (2048 sectors)
SdStorageService.hpp   : + enableAdcStressTest() 虛函數
SdStorageServiceImpl.hpp: MAX_BATCH_SAMPLES 50→100, TASK_STACK 1024→1536, + mAdcStressTestSps
SdStorageServiceImpl.cpp: + appendDummyAdcBatch(), taskLoop ADC 壓測驅動
Controller.cpp          : enableStressTest(10) → enableAdcStressTest(1000, 100)
```

## 已驗證成功的里程碑
- [x] Lazy Virtual FAL 完整實作 (bitmap + virtual read + materialize on write)
- [x] 2MB TSDB (512 sectors) 啟動正常
- [x] 斷電再開正常 (bitmap 重建)
- [x] Records 可超過 379 條 (之前限制)
- [x] OpenOCD 燒錄後不需斷電 (SDIO 寄存器手動清除)
- [x] Deferred f_sync: 每 30 秒 sync
- [x] **Erase bug 修復**: erase() 寫 0xFF 到磁碟
- [x] **Stress test 解耦**: 內部 dummy writes
- [x] **10/s stress test**: 穩定運作 (commit a0e99ff)
- [x] **80/s stress test**: 穩定運作 (18700+ records)
- [ ] **ADS1298 8ch batch 模擬**: 8MB TSDB, 100 samples/batch, 10 writes/s

## 當前狀態: ADS1298 8ch 模擬測試實作

### 測試配置
| 項目 | 數值 | 說明 |
|------|------|------|
| **TSDB 大小** | 8MB | 2048 sectors, bitmap 256 bytes |
| **通道數** | 8 | 24-bit per channel |
| **取樣率** | 1000 SPS | 模擬 ADS1298 |
| **Batch size** | 100 samples | 每次 FlashDB 寫入 |
| **寫入頻率** | 10/sec | 1000 SPS ÷ 100 batch |
| **Blob 大小** | 2428 bytes | 12(nonce) + 16(metadata) + 2400(samples) |
| **每日資料量** | ~21MB | 2428 × 10/s × 86400s |
| **Task stack** | 6KB | 1536 words (blob 需 ~2.4KB stack) |

### Blob 格式 (ADC Batch)
```
[nonce:12][metadata:16][encrypted_samples:N]
  nonce:    [counter:4LE][tick:4LE][0x00:4]
  metadata: [ver:1][count:2LE][chan_mask:1][rate:2LE][timestamp:4LE][reserved:6]
  samples:  N × 24 bytes (8ch × 3 bytes per ch)
```

### RAM 影響估算
| 項目 | 舊值 | 新值 | 差異 |
|------|------|------|------|
| TSDB bitmap | 64 B | 256 B | +192 B |
| mAdcBatchBuffer | 1200 B | 2400 B | +1200 B |
| Task stack | 4096 B | 6144 B | +2048 B |
| **合計** | ~40KB | ~43.5KB | **+3.4KB** |

### 問題時間線

#### Bug 1: 171K phantom records (已解決)
- **根因**: `erase()` 只清 RAM bitmap 不寫 0xFF 到磁碟
- **修復**: commit `db5d2d5`

#### Bug 2: Records 在 1026 停止 @ 50/s (已解決)
- **根因**: ObservableDispatcher 隊列溢出
- **修復**: publish decimation (每 10 次)

#### Bug 3: Records 卡在 15005 @ 50/s (已解決)
- **根因**: FlashDB file mode 未設 `max_size`
- **修復**: commit `af5e0fe`

#### Bug 4: FR_DISK_ERR 全面寫入失敗 (已解決)
- **根因**: 未提交的 TSDB_SIZE 64MB f_expand 損壞 SD 卡檔案系統
- **修復**: 還原 TSDB_SIZE + 重新格式化 SD 卡
- **教訓**: 大幅改動 TSDB_SIZE 前先備份 / 重新格式化

### 高頻測試結果
| 模式 | 頻率 | 狀態 | Records |
|------|------|------|---------|
| SensorData 26B | 10Hz | ✅ 穩定 | 持續增長 |
| SensorData 26B | 80Hz | ✅ 穩定 | 18700+ |
| SensorData 26B | 100Hz | ❌ 卡住 | materialize >10ms |
| **ADC 8ch batch 2.4KB** | **10Hz** | **待驗證** | — |

### SD 卡格式注意事項
- FatFS 設定 `FF_MIN_SS=FF_MAX_SS=512`
- SD 卡必須用 **預設配置大小** 格式化 (不要選 4KB 或其他)
- 格式化不對會導致 Records 完全不動
- **TSDB_SIZE 從 2MB 改為 8MB 前，建議重新格式化 SD 卡**

## 架構筆記

### Lazy Virtual FAL 核心機制
- `f_expand()` 分配檔案空間但不寫入資料 (零 DMA)
- RAM bitmap 追蹤已物化 vs 虛擬扇區
- Virtual read → memset 0xFF (無 SD I/O)
- Write → 先物化 (寫 0xFF + f_sync) 再寫資料
- **Erase → 已物化 sector 寫 0xFF 到磁碟** (保持 materialized，斷電安全)
- Periodic sync 每 30 秒 flush FatFS buffer

### ADC Batch 寫入架構
- `enableAdcStressTest(sps, batchSize)` 設定模擬參數
- `taskLoop()` 每 100ms 產生一批 dummy ADC 資料
- `appendDummyAdcBatch()` 填充 8ch×24bit 模擬資料
- `flushAdcBatch()` 加密 + 寫入 FlashDB TSDB blob
- 加密: ChaCha20 (nonce + metadata + samples 一起加密)
- Serial 輸出: `[SD] Records=N Rate=M/s Samples=S/s Batch=B`

### SDIO I/O 策略
- Read: Polling at ~3.8MHz (CLKDIV=17)
- Write: DMA at 24MHz (CLKDIV=1)
- `ensure_hal_ready()`: 每次操作前清除 DCTRL + flags

### 串口監看方法
```bash
picocom /dev/tty.usbserial-1120 -b 115200
# 退出: Ctrl+A 然後 Ctrl+X
```

## 下一步

### 待驗證
1. **ADS1298 8ch 模擬 @ 10 writes/sec** (2.4KB blobs)
2. **8MB TSDB f_expand 時間** (預估 ~2-4 秒)
3. **長期穩定性** (目標: 超過 8MB GC wrap)
4. **斷電重啟** 後 8MB TSDB 正常

### 後續目標
1. 移除 DELETE_FDB_ON_BOOT (測試完成後)
2. 每日 TSDB 切換 (午夜自動切換 tsdb_YYYYMMDD.fdb)
3. 實際 ADS1298 SPI 驅動整合

## 如何恢復

```bash
cd /Users/jrjohn/Documents/projects/arcana-embedded-stm32
git checkout debug-lcd-records

# 查看未提交修改
git diff

# 建置與燒錄
cd Targets/stm32f103ze/Debug
export PATH="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.100.202509120712/tools/bin:$PATH"
make -j8 all
openocd -f interface/cmsis-dap.cfg -c "transport select swd" -f target/stm32f1x.cfg \
  -c "program arcana-embedded-f103.elf verify reset exit"

# 注意: 8MB TSDB 需重新格式化 SD 卡 (舊 2MB 檔案會被 DELETE_FDB_ON_BOOT 刪除)
```

---
*COR+AFP+NTP 協議管理*
