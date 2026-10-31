// stub_glue.c
// 粘合层 stub：统一提供 super_scheduler.c 翻译单元所引用的、
// 但其他模块尚未实现的全部外部符号。
//
// 覆盖范围：
//   - fs_file_*         : recovery_rw.c / image_ro_reader.c / file_content_reader.c 等
//   - recovery_file_*   : hash_parser.c / hash_reader.c
//   - block_write_pos / mem_driver_rw / mem_block_rw : 内存与块设备粘合
//   - main_hash_check_status : 哈希校验总入口
//   - mem_write_data / mem_read_page : snap_mapper.c
//   - snapshot_backup_init / hash_verify_init / font_mgr_init / font_mgr_deinit /
//     super_task_resume_from_snap : super_scheduler.c 心跳/初始化阶段
//
// 所有 stub 使用一个进程内静态文件表模拟"快照文件"，行为可预测。
#include <stdint.h>
#include <stdio.h>
#include <string.h>

//------------- 文件模式常量（与 recovery_rw.c 保持一致）-------------
#ifndef FONT_READ
#define FONT_READ  0
#endif
#ifndef FONT_WRITE
#define FONT_WRITE 1
#endif

//------------- 虚拟快照文件表 -------------
// 真实实现应路由到 file_rw.c / 磁盘驱动；此处用一个 64KB 内存缓冲模拟。
#define STUB_FILE_MAX   8
#define STUB_FILE_SIZE  65536

typedef struct {
    int       used;
    int       writable;
    int       size;
    unsigned char data[STUB_FILE_SIZE];
} stub_file_t;

static stub_file_t g_files[STUB_FILE_MAX];

static int stub_alloc_fd(void)
{
    for (int i = 1; i < STUB_FILE_MAX; i++) {
        if (!g_files[i].used) {
            g_files[i].used = 1;
            g_files[i].writable = 0;
            g_files[i].size = 0;
            memset(g_files[i].data, 0, STUB_FILE_SIZE);
            return i;
        }
    }
    return 0; // 0 表示失败（与上层一致）
}

//------------- fs_file_* stub -------------
int fs_file_open(const char* path, int mode)
{
    (void)path;
    int fd = stub_alloc_fd();
    if (fd == 0) return 0;
    g_files[fd].writable = (mode == FONT_WRITE) ? 1 : 0;
    return fd;
}

int fs_file_open_ro(const char* path)
{
    return fs_file_open(path, FONT_READ);
}

int fs_file_read(unsigned int fid, unsigned int offset, unsigned char* buf, int len)
{
    if (fid == 0 || fid >= STUB_FILE_MAX || !g_files[fid].used || !buf) return 0;
    if ((int)offset >= g_files[fid].size) return 0;
    int avail = g_files[fid].size - (int)offset;
    int n = len > avail ? avail : len;
    memcpy(buf, g_files[fid].data + offset, n);
    return n;
}

int fs_file_write(unsigned int fid, unsigned int offset, unsigned char* buf, int len)
{
    if (fid == 0 || fid >= STUB_FILE_MAX || !g_files[fid].used || !buf) return 0;
    if (!g_files[fid].writable) return 0;
    if ((int)offset + len > STUB_FILE_SIZE) len = STUB_FILE_SIZE - (int)offset;
    if (len <= 0) return 0;
    memcpy(g_files[fid].data + offset, buf, len);
    int end = (int)offset + len;
    if (end > g_files[fid].size) g_files[fid].size = end;
    return len;
}

void fs_file_close(unsigned int fid)
{
    if (fid == 0 || fid >= STUB_FILE_MAX) return;
    g_files[fid].used = 0;
    g_files[fid].size = 0;
}

int fs_file_get_size(unsigned int fid)
{
    if (fid == 0 || fid >= STUB_FILE_MAX || !g_files[fid].used) return -1;
    return g_files[fid].size;
}

//------------- recovery_file_* stub -------------
// hash_reader.c / hash_parser.c 通过 recovery_file_* 接口访问快照。
// 直接转发到上面的 fs_file_* 实现，保持单一存储后端。
int recovery_file_open(const char* path, int mode)
{
    return fs_file_open(path, mode);
}

int recovery_file_read(unsigned int fid, unsigned int offset, unsigned char* buf, int len)
{
    return fs_file_read(fid, offset, buf, len);
}

void recovery_file_close(unsigned int fid)
{
    fs_file_close(fid);
}

//------------- 内存/块设备读写 stub -------------
// snap_mapper.c 依赖
int mem_read_page(unsigned int pid, unsigned long vaddr, unsigned char* out)
{
    (void)pid; (void)vaddr;
    if (out) memset(out, 0, 4096);
    return 1;
}

int mem_write_data(unsigned int pid, unsigned long vaddr, unsigned char* buf, int len)
{
    (void)pid; (void)vaddr; (void)buf; (void)len;
    return len;
}

// 通用块设备读写
int block_write_pos(unsigned int lba, unsigned int offset, int len, const unsigned char* buf)
{
    (void)lba; (void)offset; (void)len; (void)buf;
    return len;
}

int mem_driver_rw(unsigned int addr, unsigned char* buf, int len, int is_write)
{
    (void)addr;
    if (!is_write && buf) memset(buf, 0, len);
    return len;
}

int mem_block_rw(unsigned int block_id, unsigned char* buf, int len, int is_write)
{
    (void)block_id;
    if (!is_write && buf) memset(buf, 0, len);
    return len;
}

//------------- 哈希校验总入口 stub -------------
int main_hash_check_status(void)
{
    // stub 视为校验通过
    return 1;
}

//------------- 子系统初始化 stub -------------
// 这五个函数在 super_scheduler.c 中被 extern 声明，
// 原项目源码树未提供实现，此处补 stub。
int snapshot_backup_init(void)
{
    return 1;
}

int hash_verify_init(void)
{
    return 1;
}

int font_mgr_init(const char* path)
{
    (void)path;
    return 1;
}

int font_mgr_deinit(void)
{
    return 1;
}

int super_task_resume_from_snap(unsigned int pid)
{
    (void)pid;
    return 1;
}
