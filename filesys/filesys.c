#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/fat.h"
#include "devices/disk.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();
	
#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();
	thread_current()->current_dir = dir_open_root();
	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	// printf("filesys create, %s %p\n", name, name);
	if (strlen(name) == 0)
		return false;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) 
		return false;
	// filesys create(/a/b/c);
	// /a/b : e, f, d
	// c가 /a/b로 가서 거기에 없어야함.
	char *last_name = malloc(sizeof(char) * (strlen(name) + 1));
	// printf("name: %s\n", name);
	struct dir *prev_dir = get_prev_dir(name, last_name);
	// printf("current dir inode: %p create: %s lastname: %s\n",dir_get_inode(thread_current()->current_dir), name, last_name);
	// printf("create is valid dir? :%d\n", is_valid_directory(prev_dir));
	if(!is_valid_directory(prev_dir)){
		free(last_name);
		return false;
	}
	// 새롭게 inode할당을 해주어서
	// c를 /a/b : e, f, d, c
	// printf("prev dir: %p curr dir: %p\n", prev_dir, thread_current()->current_dir);
	
	if (strcmp(last_name, ".") == 0 || strcmp(last_name, "..") == 0) {
		free(last_name);
		return false;
	}
	
	disk_sector_t inode_sector = 0;
	
	// bool success = fat_allocate(1, &inode_sector);
	// printf("success: %d\n", success);
	// success = success & inode_create (inode_sector, initial_size, false);
	// printf("success: %d\n", success);
	// success = success & dir_add (prev_dir, last_name, inode_sector);
	// printf("success: %d\n", success);
	
	bool success = (prev_dir != NULL
			&& fat_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size, false)
			&& dir_add (prev_dir, last_name, inode_sector));
	

	if (!success && inode_sector != 0)
		fat_remove_chain(inode_sector, 0);
	// if (!dir_lookup(dir, last_path, &inode)) {
	// 	dir_close(dir);
	// 	return false;
	// }

	// inode_tag_dir(inode);
	dir_close (prev_dir);
	free(last_name);
	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	// printf("filesys open %s in thread: %s\n", name, thread_name());
	// printf("curr dir: %p\n", thread_current()->current_dir);
	// case 1: /a/b/c 
	// case 2: c (/a/b)
	// printf("filesys open name: %s\n", name);
	// if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return NULL;
	// printf("filesys open2\n");
	if (strcmp(name, "/") == 0) {
		struct dir *root_dir = dir_open_root();
		struct inode *inode = dir_get_inode(root_dir);
		dir_close(root_dir);
		return file_reopen(inode);
	}
	// printf("filesys open3\n");

	char *last_name = malloc(sizeof(char) * (strlen(name) + 1));
	struct dir *prev_dir = get_prev_dir(name, last_name);
	
	// printf("filesys open: prev dir: %p name: %s lastname: %s\n", prev_dir,name, last_name);
	printf("filesys open: last name: %s\n", last_name);
	// if (strcmp(last_name, ".") == 0 || strcmp(last_name, "..") == 0) {
	// 	free(last_name);
	// 	return false;
	// }
	struct inode *inode = NULL;
	// char *processed_path = path_pre_processing(name);
	// printf("%s\n", processed_path);
	// struct dir *dir = get_prev_dir(name, last_name);
	// printf("root: %p dir: %p\n", root_dir, dir);
	// char *last_path = get_last_path(processed_path);
	// printf("%s\n", last_path);

	// if(dir_lookup(dir, last_path, &inode)){}

	if (prev_dir != NULL)
		dir_lookup (prev_dir, last_name, &inode);
	// dir_close(dir);
	dir_close (prev_dir);
	free(last_name);
	// printf("filesys open end\n");
	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	// printf("filesys remove current dir inode: %p name: %s\n", dir_get_inode(thread_current()->current_dir), name);
	// struct dir *dir = dir_open_root ();
	// struct dir *root_dir = dir_open_root ();
	// char *processed_path = path_pre_processing(name);
	// printf("processed path: %s thread path: %s\n", processed_path, thread_current()->current_dir);
	
	// if (strcmp(thread_current()->current_dir, processed_path) == 0) return false;
	char *last_name = malloc(sizeof(char) * (strlen(name) + 1));
	struct dir *prev_dir = get_prev_dir(name, last_name);
	// struct dir *dir = get_prev_dir(processed_path);
	// char *last_path = get_last_path(processed_path);
	// char n[15];
	// if (dir_readdir(dir, n)) return false;
	struct inode *inode;
	if (!dir_lookup(prev_dir, last_name, &inode)) {
		free(last_name);
		return false;
	}
	// /a prev / last name a
	// a 에대한 디렉토리를 들고왔고 
	// is empty a?
	// lookup

	if(inode_is_dir(inode)) {
		struct dir *_dir = dir_open(inode);
		char *name = malloc(sizeof(char) * 15);
		if(dir_readdir(_dir, name)){
			free(last_name);
			free(name);
			return false;
		}
		free(name);
	}

	bool success = prev_dir != NULL && dir_remove (prev_dir, last_name);
	// dir_close(dir);
	dir_close (prev_dir);
	free(last_name);
	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}


bool filesys_create_dir(const char *name) {
	// dir entry = 16
	if (strlen(name) == 0)
		return false;
	
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) 
		return false;
	
	bool success = false;
	char *last_name = malloc(sizeof(char) * (strlen(name) + 1));
	struct dir *prev_dir = get_prev_dir(name, last_name);

	if (strcmp(last_name, ".") == 0 || strcmp(last_name, "..") == 0) {
		free(last_name);
		return false;
	}
	
	if (prev_dir == NULL) {
		dir_close(prev_dir);
		free(last_name);
		return false;
	}

	disk_sector_t inode_sector = 0;
	// struct dir *dir;
	struct inode *sub_inode;
	struct dir *sub_dir = NULL;
	
	
	// 현재 /
	// a 만들기
	// /a : . -> /a .. -> / 
	success = (
		prev_dir != NULL
		// 새롭게 폴더 하나 할당
		&& fat_allocate(1, &inode_sector)
		// 폴더 만들기
		&& dir_create(inode_sector, 16)
		// 이전 폴더에 만든 폴더 연결
		&& dir_add(prev_dir, last_name, inode_sector)
		// 만든 폴더로 이동
		&& dir_lookup(prev_dir, last_name, &sub_inode)
		// 만든 폴더를 열고 . 추가하고 현재 sector로 연결
		&& dir_add(sub_dir = dir_open(sub_inode), ".", inode_sector)
		// 만든 폴더에 .. 추가하고 이전 폴더의 sector로 연결
		&& dir_add(sub_dir, "..", inode_get_inumber(dir_get_inode(prev_dir)))
	);
	if (!success && inode_sector != 0)
		fat_remove_chain(inode_sector, 0);
	inode_tag_dir(sub_inode);
	// printf("inode is dir: subinode: %d\n", sub_inode->is_dir);
	// printf("mkdir inode %p\n", sub_inode);
	dir_close (prev_dir);
	dir_close(sub_dir);
	return success;
}

struct dir *get_prev_dir(const char *file_path, char *last_name) {
	// printf("get prev dir filepath: %s\n", file_path);
	char *cp_path = malloc(sizeof(char) * (strlen(file_path) + 1));
	strlcpy(cp_path, file_path, (strlen(file_path) + 1) );
	
	char *token, *save_ptr, *prev_token;	
	struct dir *dir = NULL;
	if (cp_path[0] == '/') {
		// printf("get prev dir root dir open\n");
		dir = dir_open_root();
	}
	else {
		dir = dir_reopen(thread_current()->current_dir);
	}
	// printf("first get prev dir dir: %p\n", dir);
	token = strtok_r(cp_path, "/", &save_ptr);
	// printf("token: %s\n", token);
	if (token == NULL) {
		token = malloc(sizeof(char) * 2);
		token[0] = '.';
		token[1] = '\0';
	}
	// printf("token: %s, cppath: %s\n", token, cp_path);
	struct inode *inode;
	// token = strtok_r(cp_path, "/", &save_ptr)
	for (; ;){

		prev_token = token;
		token = strtok_r(NULL, "/", &save_ptr); // become b	| become c | become \0
		// printf("tokentoken: %s\n", token);
		// 이전 것이 마지막
		if (token == NULL) {
			strlcpy(last_name, prev_token, strlen(prev_token) + 1);
			return dir;
		}

		if (!dir_lookup(dir, prev_token, &inode)) {
			free(cp_path);
			return NULL;
		}
		// if (!inode_is_dir(inode)) {
		// 	free(cp_path);
		// }
		dir_close(dir);
		dir = dir_open(inode);
	}
	NOT_REACHED();
}

bool is_valid_directory(struct dir *prev_dir) {
	struct inode *inode = dir_get_inode(prev_dir);
	// if (!dir_lookup(prev_dir,last_name,&inode)) return false;
	if (inode_is_dir(inode)) {
		if (inode_is_removed(inode)) return false;
	}
	return true;
}