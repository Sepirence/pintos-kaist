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
	// printf("filesys create\n");
	// filesys create(/a/b/c);
	// /a/b : e, f, d
	// c가 /a/b로 가서 거기에 없어야함.

	// 새롭게 inode할당을 해주어서
	// c를 /a/b : e, f, d, c

	disk_sector_t inode_sector = 0;
	// struct dir *dir = dir_open_root ();

	// struct dir *root_dir = dir_open_root ();
	char *processed_path = path_pre_processing(name);
	struct dir *dir = get_prev_dir(processed_path);
	// printf("filesys create processed_path: %s dir: %p\n", processed_path, dir);
	char *last_path = get_last_path(processed_path);
	// printf("last path: %s\n", last_path);
	struct inode *inode;
	if (dir_lookup(dir, last_path, &inode)) {
		dir_close(dir);
		return false;
	}

	bool success = (dir != NULL
			&& fat_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
		
	if (!success && inode_sector != 0)
		fat_remove_chain(inode_sector, 0);
	dir_close (dir);
	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	// printf("filesys open\n");
	// case 1: /a/b/c 
	// case 2: c (/a/b)
	// struct dir *dir = dir_open_root ();
	// struct dir *root_dir = dir_open_root ();
	struct inode *inode = NULL;
	char *processed_path = path_pre_processing(name);
	
	struct dir *dir = get_prev_dir(processed_path);
	char *last_path = get_last_path(processed_path);

	// if(dir_lookup(dir, last_path, &inode)){}

	if (dir != NULL)
		dir_lookup (dir, last_path, &inode);
	// dir_close(dir);
	dir_close (dir);

	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	// printf("filesys remove\n");
	// struct dir *dir = dir_open_root ();
	// struct dir *root_dir = dir_open_root ();
	char *processed_path = path_pre_processing(name);

	struct dir *dir = get_prev_dir(processed_path);
	char *last_path = get_last_path(processed_path);
	
	bool success = dir != NULL && dir_remove (dir, last_path);
	// dir_close(dir);
	dir_close (dir);

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



bool is_absolute_path(const char *path) {
	if (path[0] == '/') {
		return true;
	}
	return false;
}

struct dir *get_prev_dir(const char *file_path) {
	// printf("get prev dir file path: %s\n", file_path);

	ASSERT(is_absolute_path(file_path));
	
	// printf("is absolut\n");
	char *copied_file_path = malloc(sizeof(char) * (strlen(file_path) + 1));
	strlcpy(copied_file_path, file_path, (strlen(file_path) + 1) );
	// printf("copied path: %s\n", copied_file_path);
	char *token, *save_ptr, *prev_token;	
	// token = strtok_r(copied_file_path, "/", &save_ptr);
	// printf("copied_file_path: %s token: %s\n", copied_file_path,token);
	struct dir *dir = dir_open_root(); // /
	// printf("root dir: %p\n", dir);
	struct inode *inode;
	for (token = strtok_r(copied_file_path, "/", &save_ptr); ;){
		/*	
		/args-none
		token = args-none

		/a/b/c

		a\0b/c
		b\0c
		
		*/
		prev_token = token;
		token = strtok_r(NULL, "/", &save_ptr); // become b	| become c | become \0
		
		// 이전 것이 마지막
		if (token == NULL) {
			return dir;
		}

		if (!dir_lookup(dir, prev_token, &inode)) {
			free(copied_file_path);
			return NULL;
		}
		ASSERT(inode_is_dir(inode));
		dir_close(dir);
		dir = dir_open(inode);




		
		// printf("TOKEN : %s\n", token);
		// if (!dir_lookup(dir, token, &inode)) {
		// 	printf("failed to lookup :%s\n",token);
		// 	free(copied_file_path);
		// 	return NULL; 	// inode는 a directory | b directory | inode는 c (file or directory)
		// }
		// token = strtok_r(NULL, "/", &save_ptr); // become b	| become c | become \0
		
		// if (token != NULL) {
		// 	ASSERT(inode_is_dir(inode));
		// 	dir_close(dir); // dir_close(/) | dir_close(a) | dir_close(b)
		// 	dir = dir_open(inode); // a를 열어주기 | b를 열어주기
		// }
		// else {
		// 	free(copied_file_path);
		// 	return dir;
		// }
	}
	NOT_REACHED();
	

	//   asd/rre
	// ./		/a/b/c/  ./d/e/f     /a/b/c/d/e/f
	// if (file_path[0] == '.' && file_path[1] == '/') {
	// 	char *current_dir_path = thread_current()->current_dir;
	// 	char *new_path = file_path + 2; 
		
	// 	char *concat_path = get_concat_path(current_dir_path, new_path);
	// 	struct dir *ret_dir = get_prev_dir(concat_path);
	// 	free(concat_path);
	// 	return ret_dir;
	// }
          
	// if (file_path[0] == '.' && file_path[1] == '.' && file_path[2] == '/') {
	// 	char *current_dir_path = get_prev_dir(thread_current()->current_dir);
	// 	char *new_path = file_path + 3; 
		
	// 	char *concat_path = get_concat_path(current_dir_path, new_path);
		
	// 	struct dir *ret_dir = get_prev_dir(concat_path);
	// 	free(concat_path);
	// 	return ret_dir;
	// }
	// printf("!!\n");
	// char *current_dir_path = thread_current()->current_dir;
	// printf("current dir path: %s\n", current_dir_path);
	// char *concat_path = get_concat_path(current_dir_path, file_path);
	// printf("concat path: %s\n", concat_path);
	// struct dir *ret_dir = get_prev_dir(concat_path);
	// free(concat_path);
	// return ret_dir;

	// return;
}

char *get_last_path(const char *file_path) {
	// printf("get last path file path: %s\n", file_path);
	
	char *token, *save_ptr, *_token;
	for (token = strtok_r(file_path, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr))
		_token = token;	
	return _token;
}


char *get_concat_path(char *path1, char* path2) {
	char *concat_path = malloc(sizeof(char) * (strlen(path1) + strlen(path2) + 1));
	strlcpy(concat_path, path1, strlen(path1) + 1);
	strlcat(concat_path, path2, strlen(path1) + strlen(path2) + 1);
	return concat_path;
} 

char *path_pre_processing(char *file_path) {
	if (is_absolute_path(file_path)) return file_path;
	
	if (file_path[0] == '.' && file_path[1] == '/') {
		char *current_dir_path = thread_current()->current_dir;
		char *new_path = file_path + 2; 
		
		char *concat_path = get_concat_path(current_dir_path, new_path);
		// struct dir *ret_dir = get_prev_dir(concat_path);
		// free(concat_path);
		return concat_path;
	}
          
	if (file_path[0] == '.' && file_path[1] == '.' && file_path[2] == '/') {
		char *current_dir_path = get_prev_dir(thread_current()->current_dir);
		char *new_path = file_path + 3; 
		
		char *concat_path = get_concat_path(current_dir_path, new_path);
		
		// struct dir *ret_dir = get_prev_dir(concat_path);
		// free(concat_path);
		return concat_path;
	}
	// printf("!!\n");
	char *current_dir_path = thread_current()->current_dir;
	// printf("current dir path: %s\n", current_dir_path);
	char *concat_path = get_concat_path(current_dir_path, file_path);
	// printf("concat path: %s\n", concat_path);
	// struct dir *ret_dir = get_prev_dir(concat_path);
	// free(concat_path);
	return concat_path;
}

// struct dir *
// find_target_dir(struct dir *root_dir, const char *name) {
// 	// example
// 	// /home/music/pop/abc.mp3
// 	// pop까지만 열기
	
// 	// /home/music/pop/korea
// 	// korea도 열어줘야함

// 	// 즉 마지막 parsed name이 dir인지 file인지 확인해야함

// 	// name => home
// 	char *parsed_name;
// 	// first, in root / find home dir
	
// 	struct inode *dir_inode;
// 	if (!dir_lookup(root_dir, parsed_name, &dir_inode)) {
// 		return NULL;
// 	}


// }