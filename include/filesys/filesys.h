#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Disk used for file system. */
extern struct disk *filesys_disk;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

bool filesys_create_dir(const char *name);
struct inode *get_symlink_inode(struct inode *inode);
// bool is_absolute_path(const char *path);
struct dir *get_prev_dir(const char *file_path, char *last_name);
bool is_valid_directory(struct dir *prev_dir);
int filesys_symlink(const char* target, const char* linkpath);
#endif /* filesys/filesys.h */
