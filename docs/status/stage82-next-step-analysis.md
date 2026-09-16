# Stage82 下一步分析

## 当前进度

Stage81 (`89f5597`) 已完成硬件验证并推送到 `origin/stage8-sgi-irq`。

Stage81 是第一个实际安装 Stage 自有 live pmap 并使 TLB 无效化的 Method-C 阶段。它成功证明了：

1. **候选 L1 页表构建**：16 KiB 对齐的专用 Stage 自有表
2. **TTBR0 切换**：`0x000b0000 → 0x000e4000 → 0x000b0000`
3. **TLB 无效化**：安装和恢复各一次
4. **实时转换验证**：高地址别名、RAM 控制台、GIC 全部通过
5. **故障闭合恢复**：任何检查失败都尝试恢复原始 TTBR0

Stage81 硬件验证标记包括：
- `stage81_xnu_arm_vm_init_live_pmap_status=0x81000001`
- `live_pmap_installed/verified/restored` 全部为 `0x00000001`
- `ttbr0_write_count=0x00000002`，`tlb_invalidate_count=0x00000002`
- `public_arm_vm_init_executed=0x00000000`
- `loader_status=0x81000001`，`kernel_entry returned success`

## 上游 XNU `arm_vm_init` 之后的真实路径

根据 `external/xnu-4570.1.46/osfmk/arm/arm_init.c` 分析：

```c
arm_vm_init(xmaxmem, args);              // <- Stage81 已通过 Stage 自有版本证明边界

// 之后立即：
printf_init();
panic_init();
#if __arm64__ && WITH_CLASSIC_S2R
sleep_token_buffer_init();
#endif

PE_consistent_debug_inherit();

// setup debugging output if one has been chosen
PE_init_kprintf(FALSE);

kprintf("kprintf initialized\n");

// serial mode setup...
if (serialmode & SERIALMODE_OUTPUT) {
    (void)switch_to_serial_console();
    disableConsoleOutput = FALSE;
}
PE_create_console();

// setup console output
PE_init_printf(FALSE);

cpu_machine_idle_init(TRUE);

PE_init_platform(TRUE, &BootCpuData);    // <- Stage79 已用 PE_init_platform(FALSE) 证明前半部分
cpu_timebase_init(TRUE);                 // <- Stage80 已注册 Stage 自有 timebase 回调
fiq_context_init(TRUE);

__stack_chk_guard = (unsigned long)early_random();
__stack_chk_guard &= ~(0xFFULL << 8);

machine_startup(args);                   // <- 下一个关键边界
```

`machine_startup()` 路径（`external/xnu-4570.1.46/osfmk/arm/machine_routines.c`）：

```c
machine_startup(__unused boot_args * args)
{
    // debug_boot_arg, mach_assert, preempt 解析
    PE_parse_boot_argn("debug", &debug_boot_arg, ...);
    PE_parse_boot_argn("assert", &mach_assert, ...);
    PE_parse_boot_argn("preempt", &boot_arg, ...);
    PE_parse_boot_argn("bg_preempt", &boot_arg, ...);

    machine_conf();

    /*
     * Kick off the kernel bootstrap.
     */
    kernel_bootstrap();
    /* NOTREACHED */
}
```

`kernel_bootstrap()` 是整个内核初始化的最终入口，负责：
- 线程/调度器/内存/IPC/时钟子系统初始化
- 第一个用户进程启动
- 永不返回（切换到正常的内核调度上下文）

## Stage81 留下的明确后续阻塞点

`docs/source-baseline.md` 第 157 行记录：

> Continue mapping the next blockers: stricter repeated/fault-injected restore recovery around pmap transitions, broader high-virtual `virtBase`/`physBase` pmap behavior, eventual safe cache policy, kernelcache/prelink details, and later userspace/code-signing policy.

具体展开：

### 1. **Stricter repeated/fault-injected restore recovery** （短期强化，可选）
   - Stage81 的故障闭合恢复路径已存在，但未经压力测试
   - 可以设计 Stage82a 注入人工故障并验证恢复健壮性
   - **优先级**：中（安全增强，但当前路径已证明基本恢复能力）

### 2. **Broader high-virtual `virtBase`/`physBase` pmap behavior** （架构关键）
   - Stage81 候选 pmap 仅映射了生存所需的最小段
   - 真实 XNU pmap 需要完整的内核虚拟地址空间映射
   - 包括：
     - 完整内核代码/数据/BSS 高虚拟映射
     - 物理内存的高虚拟别名窗口
     - 设备 MMIO 映射
     - 内核堆/zone 分配器地址空间
   - **优先级**：高（`machine_startup`/`kernel_bootstrap` 之前必须完成）

### 3. **Eventual safe cache policy** （架构关键）
   - Stage81 明确保持缓存策略不变
   - ARMv7 描述符支持多种缓存属性（Strongly-Ordered, Device, Normal Cacheable/Non-cacheable）
   - 真实 XNU 需要针对不同区域设置正确的缓存策略
   - 包括：
     - 代码/数据：Normal Cacheable
     - 设备 MMIO：Device/Strongly-Ordered
     - DMA 缓冲区：可能需要 Non-cacheable 或显式 cache-flush
   - **优先级**：高（`machine_startup`/`kernel_bootstrap` 之前必须验证）

### 4. **kernelcache/prelink details** （后期，iOS 特有）
   - iOS 6 使用 kernelcache（预链接的内核+kext）
   - 需要理解 iBoot 如何加载 kernelcache
   - 需要处理 kext 重定位/符号解析
   - **优先级**：低（当前 Method-C 路径使用独立 Stage 镜像，不依赖真实 kernelcache）

### 5. **Later userspace/code-signing policy** （远期）
   - 用户进程启动
   - 代码签名验证
   - 沙箱/权限管理
   - **优先级**：极低（`kernel_bootstrap` 完成后的远期目标）

## 推荐的 Stage82 路径选择

基于当前 Method-C 进度和上游 XNU 代码分析，有 **三条** 可能的 Stage82 路径：

### **选项 A：Stage82 = 扩展候选 pmap 到完整内核虚拟地址空间**

**目标**：
- 扩展 Stage81 的候选 L1/L2 页表，覆盖完整的内核虚拟地址空间
- 映射所有内核代码/数据/BSS 段到高虚拟地址
- 映射完整的物理内存窗口
- 映射所有必需的设备 MMIO 区域
- 保持 Stage 自有，不调用公共 `pmap_bootstrap`/`arm_vm_init`
- 安装、验证、恢复，不持久化

**技术要点**：
- 需要 L2 页表支持（4KB 页粒度）
- 需要计算正确的 `virtBase`（通常是 `0x80000000` 或更高）
- 需要处理内核代码/数据重定位（ELF 到高虚拟地址）
- 需要验证高虚拟地址下的代码执行和数据访问
- 候选 pmap 大小会显著增加（L1 16KB + 多个 L2 表各 1KB）

**优点**：
- 直接解决阻塞点 #2（高虚拟 pmap 行为）
- 为后续 `machine_startup`/`kernel_bootstrap` 铺平道路
- 保持 Method-C 的 Stage 自有、fail-closed、非持久化原则

**缺点**：
- 复杂度高，需要正确计算所有映射关系
- 候选 pmap 构建/验证时间增加
- 可能需要多个 Stage82 子阶段（82a 只验证 L2 表构建，82b 验证高虚拟执行）

**推荐度**：★★★★★（最直接、最关键）

---

### **选项 B：Stage82 = printf/kprintf/console 初始化模型**

**目标**：
- 模拟 `arm_vm_init` 后立即执行的 `printf_init()`/`PE_init_kprintf(FALSE)` 路径
- 建立 Stage 自有的 kprintf 缓冲区和格式化输出能力
- 不依赖真实 XNU printf 运行时，但模拟其 ABI/数据结构
- 验证通过 kprintf 输出简单测试消息

**技术要点**：
- 需要理解 XNU kprintf 缓冲区布局
- 需要实现简化的格式化字符串处理（或复用现有 `xnu_log.c` 机制）
- 需要处理 console/serial 重定向逻辑
- 可能需要调用真实 `PE_init_kprintf` 或完全 Stage 自有

**优点**：
- 提供更丰富的调试输出能力
- 为后续复杂初始化提供日志基础
- 相对独立，不依赖完整 pmap

**缺点**：
- 不解决核心的 pmap/虚拟地址空间问题
- `machine_startup`/`kernel_bootstrap` 需要完整 pmap 支持，printf 初始化无法绕过
- 优先级低于选项 A

**推荐度**：★★☆☆☆（辅助功能，不解决关键路径阻塞）

---

### **选项 C：Stage82 = PE_init_platform(TRUE) 完整平台初始化**

**目标**：
- Stage79 已证明 `PE_init_platform(FALSE, args)` 前半部分
- Stage82 可以证明 `PE_init_platform(TRUE, &BootCpuData)` 完整路径
- 包括：平台特定硬件初始化、中断控制器最终配置、时钟源验证

**技术要点**：
- 需要在 Stage81 live pmap 之后调用
- 可能触发更多硬件访问（中断控制器、定时器寄存器写入）
- 需要验证是否会尝试持久化配置（如果会，需要拦截）

**优点**：
- 完成 PE 子系统的完整初始化证明
- 可能暴露硬件初始化的边界情况
- 为 `machine_startup` 铺平 PE 子系统部分

**缺点**：
- 仍然不解决完整 pmap 问题
- `machine_startup` 调用 `kernel_bootstrap` 时需要完整的虚拟地址空间
- 可能触发预期之外的硬件副作用

**推荐度**：★★★☆☆（重要，但不如选项 A 关键）

---

## 最终推荐：Stage82 路径

**推荐选项 A**：**扩展候选 pmap 到完整内核虚拟地址空间**

**理由**：
1. **关键路径必须**：`machine_startup()`/`kernel_bootstrap()` 需要完整的内核虚拟地址空间支持，无法绕过
2. **Method-C 一致性**：保持 Stage 自有、非持久化、fail-closed 原则
3. **技术可行性**：Stage81 已证明 TTBR0 切换/TLB 无效化/恢复机制，扩展到完整 pmap 是自然演进
4. **阻塞解除**：直接解决 `docs/source-baseline.md` 列出的阻塞点 #2

**建议分阶段实施**：

- **Stage82a**（候选）：扩展候选 pmap 到 L2 页表支持，验证 4KB 页映射
  - 构建候选 L1 + L2 表
  - 仅映射小窗口（如 Stage81 低代码段的高虚拟别名）
  - 验证 L2 转换工作
  - 安装/验证/恢复，不持久化

- **Stage82**（主线）：完整内核虚拟地址空间候选 pmap
  - 计算 `virtBase`/`physBase`/内核虚拟窗口
  - 映射完整内核代码/数据/BSS 到高虚拟地址
  - 映射完整物理内存窗口
  - 映射所有设备 MMIO
  - 安装候选 pmap
  - 验证高虚拟地址代码执行和数据访问
  - 可选：尝试从高虚拟地址执行简单 Stage 自有函数
  - 恢复原始 TTBR0
  - 不持久化

- **Stage83**（后续）：持久化 pmap 安装 + `PE_init_platform(TRUE)` + `machine_startup` 边界

**缓存策略处理**：
- Stage82 可以先保持 Stage81 的保守策略（所有段 Strongly-Ordered 或 Normal Cacheable 统一）
- Stage82a/82b（候选）可以引入差异化缓存策略（代码/数据 Cacheable，设备 Device）

**技术风险**：
- L2 表构建复杂度
- 高虚拟地址重定位计算错误可能导致挂起
- 候选 pmap 大小增加可能超出当前静态分配空间

**缓解措施**：
- 先做 Stage82a 小窗口验证
- 使用已知的 XNU `virtBase` 值（参考 `external/xnu-4570.1.46` 或反汇编真实 iOS 6 kernelcache）
- 保持故障闭合恢复路径
- 保持非持久化 `fastboot boot` 验证

## 下一步操作

如果同意选项 A，下一步是：
1. 创建 `stage82/` 目录，从 `stage81/` 复制/重编号
2. 研究 `external/xnu-4570.1.46/osfmk/arm/pmap.c` 中的 `pmap_bootstrap()` L2 表构建逻辑
3. 设计 Stage82 候选 L1/L2 表布局和映射计划
4. 实现 `stage82_xnu_arm_vm_init_full_pmap_run()`
5. 本地验证、文档、硬件验证

如果有其他考虑或偏好其他路径，请告知。
