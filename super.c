#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include "vdifs.h"

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Matthew Stickney");

#define SB_HEADER_SIZE 512
#define BLOCK_IMAGE_SIZE 1024
#define SB_OFFSET 0
#define BLOCKMAP_ENT_SIZE

#define MAGIC_STRING "<<< Oracle xVM VirtualBox Disk Image >>>"

static int vdi_get_superblock(struct file_system_type*,
  int, const char*, void*, struct vfsmount*);

static struct file_system_type vdifs_type = {
	.name = "vdifs",
	.owner = THIS_MODULE,
	.get_sb = vdi_get_superblock,
	.kill_sb = kill_block_super,
	.next = NULL
};

inline struct vdifs_sb_info *VDIFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static void vdifs_put_super(struct super_block *sb)
{
	struct vdifs_sb_info *sbi;
	
	sbi = VDIFS_SB(sb);
	if (sbi && sbi->blockmap)
		kfree(sbi->blockmap);
	if (sbi)
		kfree(sbi);
}

static struct super_operations vdifs_super_ops = {
	.statfs = simple_statfs,
	.put_super = vdifs_put_super,
};

static int vdi_fill_superblock(struct super_block *sb, void *data, int silent)
{
	int blocksize = SB_HEADER_SIZE;
	int i;
	unsigned long logical_sb_block;
	unsigned long logical_map_block;
	struct buffer_head *sb_bh, *bmap_bh;
	struct vdifs_header *vh;
	struct vdifs_sb_info *sbi;
	u_int32_t *le_bmap;
	struct inode *root;
	struct dentry *root_dentry;
	
	sbi = kzalloc(sizeof(struct vdifs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	blocksize = sb_min_blocksize(sb, SB_HEADER_SIZE);
	if (blocksize <= 0) {
		printk(KERN_ERR "VDIfs: error: unable to set blocksize");
		goto bad_sbi;
	}
	
	logical_sb_block=0;
	if (!(sb_bh=sb_bread(sb, logical_sb_block))) {
		printk(KERN_ERR "VDIfs: failed to read superblock\n");
		goto bad_sbi;
	}
	sb->s_fs_info = sbi;
	vh = (struct vdifs_header *) sb_bh->b_data;
	/* FIXME: this string could be different (Sun vs Oracle), use magic no. */
	if (!strcmp(vh->magic_string, MAGIC_STRING)) {
		printk(KERN_DEBUG "VDIfs: bad magic string, not a VDIFS superblock\n");
		goto bad_sb_buf;
	}
	sbi->blockmap = NULL;
	sbi->ver_major = le16_to_cpu(vh->ver_major);
	sbi->ver_minor = le16_to_cpu(vh->ver_minor);
	printk(KERN_DEBUG "VDIfs: image has version %u.%u\n", sbi->ver_major, sbi->ver_minor);
	/* only supporting ver 1.1 right now */
	if (sbi->ver_major != 1 || sbi->ver_minor != 1) {
		printk(KERN_ERR "VDIfs: unsupported file version (version ");
		printk(KERN_ERR "%u.%u\n)", sbi->ver_major, sbi->ver_minor);
		goto bad_sb_buf;
	}
	sb->s_blocksize = le32_to_cpu(vh->block_bytes);
	printk(KERN_DEBUG "VDIfs: blocksize %lu\n", sb->s_blocksize);
	sb->s_blocksize_bits = blksize_bits(sb->s_blocksize);
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sbi->img_type = le32_to_cpu(vh->img_type);
	printk(KERN_DEBUG "VDIfs: image type %u\n", sbi->img_type);
	sbi->block_offset = le32_to_cpu(vh->block_offset);
	printk(KERN_DEBUG "VDIfs: data offset %x\n", sbi->block_offset);
	sbi->map_offset = le32_to_cpu(vh->map_offset);
	printk(KERN_DEBUG "VDIfs: map_offset %x\n", sbi->map_offset);
	sbi->disk_blocks = le32_to_cpu(vh->disk_blocks);
	printk(KERN_DEBUG "VDIfs: %u blocks in image\n", sbi->disk_blocks);
	sbi->alloced_blocks = le32_to_cpu(vh->allocated_blocks);
	printk(KERN_DEBUG "VDIfs: %u allocated blocks in image\n", sbi->alloced_blocks);
	/* sanity check */
	if (sbi->disk_blocks*sbi->block_bytes != sbi->disk_bytes) {
		printk(KERN_ERR "VDIfs: superblock appears to be corrupt");
		goto bad_sb_buf;
	}

	if (sbi->image_type == VDI_DYNAMIC) {
		sbi->blockmap = kzalloc(sbi->disk_blocks, GFP_KERNEL);
		if (!sbi->blockmap) {
			kfree(sbi);
			brelse(sb_bh);
			return -ENOMEM;
		}
		logical_map_block = sbi->block_offset / sb->s_bdev->bd_block_size;
		/* FIXME: magic number 4 (= sizeof(u_int32_t)) */
		bmap_bh = __bread(sb->s_bdev, logical_map_block, sbi->disk_blocks*4);
		if (!bmap_bh) {
			printk(KERN_ERR "VDIfs: failed to read block map for dynamic image\n");
			goto bad_bmap;
		}
		le_bmap = (u_int32_t*)bmap_bh->b_data;
		for (i=0; i<sbi->disk_blocks; i++) {
			sbi->blockmap[i] = le32_to_cpu(le_bmap[i]);
		}
		brelse(bmap_bh);
	}
	brelse(sb_bh);
	
	sb->s_op = &vdifs_super_ops;
	
	root = vdifs_make_inode(sb, S_IFDIR | 0555);
	if (!root)
		goto bad_bmap;
	root->i_op = &simple_dir_inode_operations;
	root->i_fop = &simple_dir_operations;
	
	root_dentry = d_alloc_root(root);
	if (! root_dentry)
		goto bad_root_inode;
	if (!vdifs_create_file(sb, root_dentry, "image")) {
		printk(KERN_ERR "VDIfs: failed to create image file\n");
		goto bad_root_inode;
	}
	return 0;

bad_root_inode:
	iput(root);
bad_bmap:
	if (sbi && sbi->blockmap)
		kfree(sbi->blockmap);
bad_sb_buf:
	brelse(sb_bh);
bad_sbi:
	if (sbi)
		kfree(sbi);
	return 1;
}

static int vdi_get_superblock(struct file_system_type *fst,
  int flags, const char *devname, void *data, struct vfsmount *vmnt)
{
	return get_sb_bdev(fst, flags, devname, data, vdi_fill_superblock, vmnt);
} 

static int __init vdifs_init(void)
{
	int ret;
	printk(KERN_DEBUG "VDIFS: registering filesystem\n");
	ret = register_filesystem(&vdifs_type);
	printk(KERN_DEBUG "VDIFS: register_filesystem returned %d\n", ret);
	return ret;
}
module_init(vdifs_init);
