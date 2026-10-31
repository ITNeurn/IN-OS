// main.c 系统主入口
// 在无 C 编译器的当前环境下，此文件仅作为源码归档保留，
// 描述操作系统启动后从超级调度器初始化到用户层 CLI 终端的完整调用流。
// 当后续接入 gcc/clang 工具链时，可直接编译链接。
#include <stdint.h>
#include <stdio.h>

// 超级调度器对外入口（实现在 超级调度器/super_scheduler.c）
extern int  super_scheduler_init(void);
extern void super_scheduler_deinit(void);
extern int  super_run_all_priority_tasks(void);

// 用户层 CLI 终端入口（实现在 用户/用户命令行/user_cli.c）
extern void cli_run_terminal(void);

// 图形界面入口（实现在 用户/图形界面/gui_core.c）
extern int  gui_init(void);

// 内核启动横幅
static void kernel_banner(void)
{
    // 在真实硬件上此处的 printf 会路由到 display_gpu / serial 输出。
    // 在 stub 环境下使用宿主 libc 的 printf 即可看到输出。
    printf("==================================================\n");
    printf("  IN OS  (c) 2026  Booting...\n");
    printf("  super_scheduler + cli + gui + font + recovery\n");
    printf("==================================================\n");
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    kernel_banner();

    // 1. 初始化超级调度器（驱动+恢复器+备份管理器+镜像+字库）
    if (!super_scheduler_init()) {
        printf("[PANIC] super_scheduler_init failed\n");
        return 1;
    }

    // 2. 启动一次优先级批量调度，预热流水线
    super_run_all_priority_tasks();

    // 3. 进入用户层 CLI 终端（阻塞，直到用户输入 exit）
    cli_run_terminal();

    // 4. 清理退出
    super_scheduler_deinit();
    return 0;
}
