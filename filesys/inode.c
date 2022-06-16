#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

// user addition
#include "filesys/fat.h"
//
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
 
/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk {
	disk_sector_t start;                /* First data sector. */
	off_t length;                       /* File size in bytes. */
	bool is_dir;
	unsigned magic;                     /* Magic number. */
	uint32_t unused[124];               /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
	struct list_elem elem;              /* Element in inode list. */
	disk_sector_t sector;               /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	// user addition
	bool is_dir;
	// bool is_dir_removed;
	bool is_symlink;
	//
	struct inode_disk data;             /* Inode content. */
};

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {

	ASSERT (inode != NULL);

	// inode					inode->sector = inode disk's location in disk
	// inode disk = inode.data  inode->data.start = inode disk's data(file) start location in disk

	// if inode.data points to file
	// inode->data.length = EOF
	if (pos < inode->data.length) {
		
		// sector should bigger than fat sector
		disk_sector_t sector = inode->data.start;
		

		// disk[0] = FAT_BOOT
		// fat table sector: disk[1] ~ disk[157] 
		// fat table[0] = FAT_BOOT (not be put and not be used)
		// fat table[1] = ROOT_DIR_CLUSTER
		// fat table[2] ~ fat_table[20001] (20096 => 20002)

		// fat_fs->data_start = 158
		// data sector = disk[158] ~ disk[20159] (20160)
		// fat table[0] <=> disk[158]
		// fat table[1] <=> disk[159]
		// fat table[20001] <=> disk[20159]


		// clst become start clst of that data
		cluster_t clst = sector_to_cluster(sector);

		int sector_cnt = pos / DISK_SECTOR_SIZE;
		while (sector_cnt > 0) {
			clst = fat_get(clst);
			sector_cnt--;
		} 
		return cluster_to_sector(clst);
	}
	
	// pos is over EOF
	else
		return -1;
} 

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);
	
	// disk inode: meta data for data
	disk_inode = calloc (1, sizeof *disk_inode);

	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC; 
		disk_inode->is_dir = is_dir;
		// allocate sectors for data
		if (fat_allocate (sectors, &disk_inode->start)) {
			// disk_inode->start become first data cluster number
			// ex) sectors: 3 disk_inode->start = 170 
			// fat[2] = 5		disk[170] = data 0 to 512B
			// fat[5] = 8 		disk[173] = data 512B to 1024B
			// fat[8] = -1		disk[176] = data 1024B to 1536B

			// so disk inode become meta data for actual data
			disk_write (filesys_disk, sector, disk_inode);

			// however, contents of data are not given
			// initialize corressponding data sector with 0
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;
				disk_sector_t data_sector = disk_inode->start;
				cluster_t clst;
				for (i = 0; i < sectors; i++) {
					disk_write (filesys_disk, data_sector, zeros);
					clst = sector_to_cluster(data_sector);
					
					// find next sector 
					clst = fat_get(clst);
					
					if (clst != EOChain) 
						data_sector = cluster_to_sector(clst);
					else 
						ASSERT(i == sectors - 1);
				}
			}
			success = true; 
		} 
		// disk write is done, so free it
		free (disk_inode);
	}
	return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) {
	// printf("inode open: sector %d\n", sector);
	struct list_elem *e;
	struct inode *inode;
	
	is_data_sector(sector);

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read (filesys_disk, inode->sector, &inode->data);
	inode->is_dir = inode->data.is_dir;
	inode->is_symlink = false;
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) {
	// printf("inode close %p\n", inode);
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);
		disk_write(filesys_disk, inode->sector, &inode->data);
		// printf("closeclose inode: %p sector: %d\n", inode, inode->sector);
		/* Deallocate blocks if removed. */
		if (inode->removed) {
			// inode					inode->sector = inode disk's location in disk
			// inode disk = inode.data  inode->data.start = inode disk's data(file) start location in disk
			// writing the data of disk
			// removing fat chain
			fat_remove_chain(inode->sector, 0);
			fat_remove_chain(inode->data.start, 0);
		}
		free (inode); 
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	// printf("inode: %p, size: %d offset: %d\n", inode, size, offset);
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;
	
	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		// to read the data at disk[secotr_idx] from sector_ofs

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		// inode's data offset ~ offset + size (or EOF)
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if end of file is reached or an error occurs.
 * (Normally a write at end of file would extend the inode, but
 * growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
		// printf("inode write: %p, size: %d offset: %d sector: %d data length: %d\n", inode, size, offset, inode->sector, inode->data.length);
	// if (inode_is_dir(inode)) return -1;
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;
	// there are two cases:
	// first, from buffer write inode's data 	offset부터 size 만큼 EOF 전에 만족
	// second, from buffer writhe inode's data 	offset부터 size 만큼 EOF 이후까지도

	// compare offset + size and inode's data length
	
	// write after EOF
	if (offset + size > inode->data.length) {
		// example
		// inode data length: 1000	현재 보유 sector: 2 (1024B)
		// offset: 1200			
		// size: 250				1450 원함 => sector 1개 증가 (1536B) 
		// EOF: 1000				inode data length = 1450
		// 450 => 1 sector
		// cnt = 0 

		size_t cnt = ((offset + size) - inode->data.length) / DISK_SECTOR_SIZE + 1;

		// find last data sector
		cluster_t last_cluster = traverse(sector_to_cluster(inode->data.start));
		
		static char zeros[DISK_SECTOR_SIZE];		

		// sector_to_cluster
		cluster_t new_cluster;
		
		for (size_t i = 0; i < cnt; i++) {
			new_cluster = fat_create_chain(last_cluster);	
			if (new_cluster == 0) {
				// error handling
				break;
			}
			disk_write(filesys_disk, cluster_to_sector(new_cluster), zeros);
			last_cluster = new_cluster;
		}
		inode->data.length += (offset + size) - inode->data.length;
	}
	// EOF이후 write를 위한 추가 할당 완료
	
	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;
		
		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;

		if (chunk_size <= 0)
			break;
		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Write full sector directly to disk. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* We need a bounce buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* If the sector contains data before or after the chunk
			   we're writing, then we need to read in the sector
			   first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);
	// printf("bytes written: %d\n", bytes_written);
	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}

bool inode_is_dir(const struct inode *inode) {
	return inode->is_dir;
}

void inode_tag_dir(struct inode *inode) {
	inode->is_dir = true;
}

bool inode_is_removed(const struct inode *inode){
	return inode->removed;
}

void inode_tag_sym_link(struct inode *inode) {
	inode->is_symlink = true;
}

bool inode_is_symlink(const struct inode *inode){
	return inode->is_symlink;
}