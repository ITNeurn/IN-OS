// user_cli.c 全系统统一用户命令行交互协调终端
#ifndef USER_CLI_H
#define USER_CLI_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>

// 全局依赖顶层超级调度器，一键调度全系统所有子模块
#include "../../super_scheduler.c"

// CLI 常量定义
#define CLI_INPUT_BUF 2048
#define CLI_ARG_MAX 16
#define CLI_PROMPT "sys_sched > "

// 命令类型枚举，覆盖整套系统全部功能
typedef enum {
    // 跨架构编译调度
    CMD_CREATE_ARM_TASK,
    CMD_RUN_ALL_TASK,
    CMD_TASK_SNAPSHOT,
    CMD_TASK_RESUME,
    CMD_RECOVER_CRASH,
    // 系统备份/镜像校验
    CMD_SNAPSHOT_SAVE,
    CMD_HASH_VERIFY_CHECK,
    CMD_LOAD_SYS_IMAGE,
    // 字库图形管理
    CMD_FONT_LOAD,
    CMD_FONT_MODIFY,
    // 用户内存读写
    CMD_USER_MEM_RW,
    // 系统状态查询
    CMD_GET_STAT,
    CMD_GET_QUOTA,
    // 系统启停
    CMD_SYS_INIT,
    CMD_SYS_EXIT,
    CMD_UNKNOWN
} cli_cmd_t;

// 命令行参数结构
typedef struct {
    char cmd_name[64];
    int arg_count;
    char args[CLI_ARG_MAX][128];
} cli_parse_t;

static char cli_input_buf[CLI_INPUT_BUF];
static cli_parse_t cli_current_cmd;

static inline void cli_buf_reset(void)
{
    memset(cli_input_buf, 0, CLI_INPUT_BUF);
    memset(&cli_current_cmd, 0, sizeof(cli_parse_t));
}

/**
 * @brief 解析输入字符串，拆分命令与参数
 * @param input 原始输入字符串
 * @return 解析后命令结构体
 */
cli_parse_t cli_parse_input(char *input)
{
    cli_buf_reset();
    strcpy(cli_input_buf, input);
    cli_parse_t *res = &cli_current_cmd;
    char *tok = strtok(cli_input_buf, " \n\r");
    if(!tok) return *res;
    strcpy(res->cmd_name, tok);
    res->arg_count = 0;
    while((tok = strtok(NULL, " \n\r")) != NULL && res->arg_count < CLI_ARG_MAX)
    {
        strcpy(res->args[res->arg_count++], tok);
    }
    return *res;
}

/**
 * @brief 命令名称映射为内部指令枚举
 */
cli_cmd_t cli_map_cmd(char *cmd_name)
{
    if(strcmp(cmd_name, "create_task") == 0) return CMD_CREATE_ARM_TASK;
    if(strcmp(cmd_name, "run_all") == 0) return CMD_RUN_ALL_TASK;
    if(strcmp(cmd_name, "snap") == 0) return CMD_TASK_SNAPSHOT;
    if(strcmp(cmd_name, "resume") == 0) return CMD_TASK_RESUME;
    if(strcmp(cmd_name, "recover") == 0) return CMD_RECOVER_CRASH;
    if(strcmp(cmd_name, "snap_save") == 0) return CMD_SNAPSHOT_SAVE;
    if(strcmp(cmd_name, "hash_check") == 0) return CMD_HASH_VERIFY_CHECK;
    if(strcmp(cmd_name, "load_img") == 0) return CMD_LOAD_SYS_IMAGE;
    if(strcmp(cmd_name, "font_load") == 0) return CMD_FONT_LOAD;
    if(strcmp(cmd_name, "font_edit") == 0) return CMD_FONT_MODIFY;
    if(strcmp(cmd_name, "mem_op") == 0) return CMD_USER_MEM_RW;
    if(strcmp(cmd_name, "stat") == 0) return CMD_GET_STAT;
    if(strcmp(cmd_name, "quota") == 0) return CMD_GET_QUOTA;
    if(strcmp(cmd_name, "init") == 0) return CMD_SYS_INIT;
    if(strcmp(cmd_name, "exit") == 0) return CMD_SYS_EXIT;
    return CMD_UNKNOWN;
}

/**
 * @brief 执行解析后的命令，协调全系统各模块
 * @param parsed 解析完成的命令结构体
 * @return 0正常，非0错误码
 */
int cli_exec_command(cli_parse_t parsed)
{
    cli_cmd_t cmd = cli_map_cmd(parsed.cmd_name);
    uint32_t pid, seg_id, addr, len;
    super_stat_t stat_out;
    super_quota_t quota_out;

    switch(cmd)
    {
        case CMD_SYS_INIT:
            printf("【系统】初始化全局超级调度器...\n");
            super_scheduler_init();
            printf("【系统】全部子模块加载完成\n");
            break;

        case CMD_CREATE_ARM_TASK:
            if(parsed.arg_count < 3)
            {
                printf("参数缺失：create_task [pid] [seg_id] [priority]\n");
                return -1;
            }
            pid = atoi(parsed.args[0]);
            seg_id = atoi(parsed.args[1]);
            task_prio_t prio = atoi(parsed.args[2]);
            unsigned char empty_ir[1024] = {0};
            int tid = super_create_arm_task(pid, prio, seg_id, empty_ir, 1024);
            if(tid >= 0)
                printf("【任务】创建成功，任务索引：%d PID：%u\n", tid, pid);
            else
                printf("【错误】任务创建失败，资源池已满\n");
            break;

        case CMD_RUN_ALL_TASK:
            printf("【调度】批量执行所有排队任务...\n");
            int ok = super_run_all_priority_tasks();
            printf("【调度】执行完成，成功任务数：%d\n", ok);
            break;

        case CMD_TASK_SNAPSHOT:
            if(parsed.arg_count < 3)
            {
                printf("参数缺失：snap [pid] [vaddr] [length]\n");
                return -1;
            }
            pid = atoi(parsed.args[0]);
            unsigned long vaddr = strtoul(parsed.args[1], NULL, 16);
            len = atoi(parsed.args[2]);
            int snap_byte = super_task_memory_snapshot(pid, vaddr, len);
            printf("【快照】读取完成，获取字节：%d\n", snap_byte);
            break;

        case CMD_TASK_RESUME:
            pid = atoi(parsed.args[0]);
            super_task_resume_from_snap(pid);
            printf("【快照】PID %u 从备份镜像恢复运行\n", pid);
            break;

        case CMD_RECOVER_CRASH:
            int rec = super_recover_crash_task();
            printf("【自愈】回收崩溃/超时任务数量：%d\n", rec);
            break;

        case CMD_GET_STAT:
            super_get_global_stat(&stat_out);
            printf("===== 全局性能统计 =====\n");
            printf("总创建任务：%llu | 完成任务：%llu | 崩溃任务：%llu\n",
                stat_out.total_task_create, stat_out.total_task_finish, stat_out.total_task_crash);
            printf("总转发指令字节：%llu | 总快照字节：%llu\n",
                stat_out.total_forward_byte, stat_out.total_snap_byte);
            break;

        case CMD_GET_QUOTA:
            super_get_global_quota(&quota_out);
            printf("===== 资源配额监控 =====\n");
            printf("运行任务：%d | 占用文件句柄：%d | 监听端口：%d\n",
                quota_out.running_task_count, quota_out.used_file_fd, quota_out.used_port);
            break;

        case CMD_SYS_EXIT:
            printf("【系统】开始销毁全部模块，释放资源...\n");
            super_scheduler_deinit();
            printf("【系统】已安全退出\n");
            return 1;

        case CMD_UNKNOWN:
            printf("【错误】未知命令：%s\n", parsed.cmd_name);
            printf("可用指令：init create_task run_all snap resume recover stat quota exit\n");
            break;

        default:
            printf("该命令暂未开放交互接口\n");
            break;
    }
    return 0;
}

/**
 * @brief 持续运行CLI交互终端主循环
 */
void cli_run_terminal(void)
{
    printf("===== 全局系统协调命令行终端 =====\n");
    printf("输入 init 初始化系统，exit 退出\n");
    while(1)
    {
        printf(CLI_PROMPT);
        fgets(cli_input_buf, CLI_INPUT_BUF, stdin);
        cli_parse_t cmd = cli_parse_input(cli_input_buf);
        int exit_flag = cli_exec_command(cmd);
        if(exit_flag == 1) break;
    }
}

#endif
