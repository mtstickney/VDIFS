#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include "vdifs.h"

#define SB_HEADER_SIZE 512
#define BLOCK_IMAGE_SIZE 1024
#define SB_OFFSET 0
#define BLOCKMAP_ENT_SIZE

#define MAGIC_STRING "<<< Sun xVM VirtualBox Disk Image >>>"

enum VDI_IMG_TYPE { VDI_DYNAMIC, VDI_FIXED };

static struct file_system_type vdifs_type = {
	.name = "vdifs",
	.owner = THIS_MODULE,
	.get_sb = vdi_get_superblock,
	.kill_sb = kill_block_super,
	.next = NULL
};

static int vdi_fill_superblock(struct super_block *sb, void *data, int silent)
{
	unsigned long offset;
	int blocksize = SUPERBLOCK_SIZE;
	int i;
	unsigned long logical_sb_block;
	unsigned long logical_map_block;
	size_t blockmap_size;
	struct buffer_head *bh;
	struct vdifs_header *vh;
	struct vdifs_sb_info *sbi;
	u_int32_t *le_bmap;
	
	
	sbi = kzalloc(sizeof(struct vdifs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	blocksize = sb_min_blocksize(sb, SUPERBLOCK_SIZE);
	if (blocksize <= 0) {
		printk(KERN_ERR "VDIfs: error: unable to set blocksize");
		goto failed_blocksize;
	}
	
	logical_sb_block=0;
	if (!(bh=sb_bread(sb, logical_sb_block))) {
		printk(KERN_ERR "VDIfs: failed to read superblock\n");
		goto failed_sb_load;
	}
	sb->s_fs_info = sbi;
	vh = (struct vdifs_header *) bh->b_data;
	/* FIXME: this string could be different (Sun vs Oracle), use magic no. */
	if (!strcmp(vh->header_string, MAGIC_STRING)) {
		printk(KERN_DEBUG "VDIfs: bad magic string, not a VDIFS superblock\n");
		goto failed_sb_load;
	}
	sbi->ver_major = le16_to_cpu(vh->ver_major);
	sbi->ver_minor = le16_to_cpu(vh->ver_minor);
	/* only supporting ver 1.1 right now */
	if (sbi->ver_major != 1 || sbi->ver_minor != 1) {
		printk(KERN_ERR "VDIfs: unsupported file version (version ");
		prinkt(KERN_ERR "%u.%u\n)", sbi->ver_major, sbi->ver_minor);
		goto failed_sb_load;
	}
	/* the vdi image block size is huge, so we use something smaller */
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sbi->img_type = le32_to_cpu(vh->img_type);
	sbi->block_offset = le32_to_cpu(vh->block_offset);
	sbi->map_offset = le32_to_cpu(vh->map_offset);
	sbi->disk_blocks = le32_to_cpu(vh->disk_blocks);
	
	/* sanity check */
	if (sbi->disk_blocks*sbi->block_bytes != sbi->disk_bytes) {
		printk(KERN_ERR "VDIfs: superblock appears to be corrupt");
		goto failed_sb_load;
	}

	sbi->blockmap = kzalloc(sbi->disk_blocks);
	if (!sbi->blockmap) {
		kfree(sbi);
		brelse(bh);
		return -ENOMEM;
	}
	if (sbi->image_type == VDI_DYNAMIC) {
		logical_map_block = sbi->block_offset / sb->s_bdev->bd_block_size;
		brelse(bh);
		/* FIXME: magic number 4 (= sizeof(u_int32_t)) */
		bh = __bread(sb->s_bdev, logical_map_block, sbi->disk_blocks*4);
		if (!bh) {
			pintk(KERN_ERR "VDIfs: failed to read block map for dynamic image\n");
			goto failed_sb_load;
		}
		le_bmap = (u_int32_t*)bh->bdata;
		for (i=0; i<sbi->disk_blocks; i++) {
			sbi->blockmap[i] = le32_to_cpu(le_bmap[i]);
		}
		brelse(bh);
	}
}

static struct super_block *vdi_get_superblock(struct file_sytem_type *fst,
  int flags, const char *devname, void *data, struct vfsmount *vmnt)
{
	return get_sb_bdev(fst, flags, devname, data, vdi_fill_superblock, vmnt);
}

static int __init vdifs_init()
{
	return register_filesystem(&vdifs_type);
}
module_init(vdifs_init);
