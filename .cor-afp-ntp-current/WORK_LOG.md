# Lazy Virtual FAL + ADS1298 8ch 模拟测试

**日期**: 2026-03-12
**状态**: ✅ **测试成功**
**分支**: debug-lcd-records

---

## 测试配置

| 项目 | 数值 |
|------|------|
| TSDB 大小 | 8MB (2048 sectors) |
| KVDB 大小 | 64KB |
| 通道数 | 8ch (24-bit) |
| 取樣率 | 500 SPS |
| Batch size | 50 samples |
| 写入频率 | 10 writes/sec |
| Blob 大小 | 1214 bytes |

---

## 问题与解决

### 问题 1: FAL materialize 写入失败
**症状**: `[FAL] materialize: write sec XXX FAILED fr=1 wrote=0/256`

**原因**: 
1. SD 卡格式为 exFAT + 512KB 分配单元
2. FAL `fill` 缓冲区未 4 字节对齐

**解决**:
1. 格式化 SD 卡为 **FAT32 + 8KB 分配单元**
2. 修改 `SdFalAdapter.cpp` 添加对齐属性:
   ```cpp
   static uint8_t fill[256] __attribute__((aligned(4)));
   ```

---

## 测试结果

### 初始化
```
[SdFal] Creating tsdb.fdb, size=8388608 bytes...
[SdFal] Created tsdb.fdb OK, took 36 ms
[SdFal] Creating kvdb.fdb, size=65536 bytes...
[SdFal] Created kvdb.fdb OK, took 4 ms
[SdStorage] TSDB init OK, last_time=0
[SdStorage] Task started
```

### 长期稳定性 (60+ 秒)
```
Records: 524 → 1153 (增加 629 条)
Rate: 10-22 writes/sec
Samples: 450-1000 samples/sec
Batch: 50 (配置正确)
```

**状态**: ✅ **无错误，运行稳定**

---

## 关键修复

### 1. SdFalAdapter.cpp - 缓冲区对齐
```cpp
// DMA requires 4-byte aligned buffer
static uint8_t fill[256] __attribute__((aligned(4)));
memset(fill, 0xFF, sizeof(fill));
```

### 2. Controller.cpp - ADC stress test 配置
```cpp
// Use ADC stress test: 500 SPS, 50 samples/batch = 10 writes/sec
mSdStorage->enableAdcStressTest(500, 50);
```

### 3. SD 卡格式要求
- **文件系统**: FAT32
- **分配单元**: 8KB

---

## 结论

✅ **Lazy Virtual FAL + ADS1298 8ch 模拟测试成功**

- 8MB TSDB 正常工作
- 500 SPS 8ch ADC 模拟稳定运行
- 10 writes/sec 写入频率达标
- 无 HardFault 或写入错误

**下一步**: 可尝试提高 SPS 或 batch size 进行更高压力测试
