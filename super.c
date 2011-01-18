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

#define MAGIC_STRING "<<< Oracle VM VirtualBox Disk Image >>>\n"
#define MAGIC_NO 0x7f10dabe

#define VDIFS_BLOCK_SIZE 512

static int vdifs_get_superblock(struct file_system_type*,
  int, const char*, void*, struct vfsmount*);
static void vdifs_write_super(struct super_block*);

static struct file_system_type vdifs_type = {
	.name = "vdifs",
	.owner = THIS_MODULE,
	.get_sb = vdifs_get_superblock,
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
	
	printk(KERN_DEBUG "VDIFS: putting superblock\n");
	sbi = VDIFS_SB(sb);
	brelse(sbi->super_bh);
	brelse(sbi->bmap_bh);
	if (sbi && sbi->blockmap)
		kfree(sbi->blockmap);
	if (sbi)
		kfree(sbi);
}

static struct super_operations vdifs_super_ops = {
	.statfs = simple_statfs,
	.put_super = vdifs_put_super,
	.write_super = vdifs_write_super
};

static void vdifs_write_super(struct super_block *sb)
{
	struct vdifs_sb_info *sbi;
	struct vdifs_header *sb_header;
	struct buffer_head *super_bh, *bmap_bh;
	int32_t *bmap;
	u_int32_t i;

	sbi = VDIFS_SB(sb);
	super_bh = sbi->super_bh;
	bmap_bh = sbi->bmap_bh;
	sb_header = (struct vdifs_header*)super_bh->b_data;
	bmap = (int32_t*)bmap_bh->b_data;

	if (sbi->img_type == VDI_STATIC || !sb->s_dirt)
		return;

	/* Note: allocated blocks should be the only changed fields */
	sb_header->allocated_blocks = cpu_to_le32(sbi->alloced_blocks);
	for (i=0; i<sbi->disk_blocks; i++) {
		
		bmap[i] = cpu_to_le32(sbi->blockmap[i]);
	}
	mark_buffer_dirty(super_bh);
	mark_buffer_dirty(bmap_bh);
	sync_dirty_buffer(super_bh);
	sync_dirty_buffer(bmap_bh);
}

static int vdifs_fill_superblock(struct super_block *sb, void *data, int silent)
{
	int i;
	unsigned long logical_map_block;
	u_int64_t disk_bytes;
	struct buffer_head *sb_bh, *bmap_bh;
	struct vdifs_header *vh;
	struct vdifs_sb_info *sbi;
	u_int32_t *le_bmap;
	struct inode *root;
	struct dentry *root_dentry;
	
	printk(KERN_DEBUG "VDIFS: fill_superblock called\n");
	
	sbi = kzalloc(sizeof(struct vdifs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	printk(KERN_DEBUG "VDIFS: sbi alloced\n");
	if (sb_min_blocksize(sb, VDIFS_BLOCK_SIZE) < 0) {
		printk(KERN_ERR "VDIfs: error: unable to set sb blocksize");
		goto bad_sbi;
	}
	printk(KERN_DEBUG "VDIFS: blocksize set (%lu)\n", sb->s_blocksize);
	if (!(sb_bh=sb_bread(sb, 0))) {
		printk(KERN_ERR "VDIfs: failed to read superblock\n");
		goto bad_sbi;
	}
	sbi->super_bh = sb_bh;
	printk(KERN_DEBUG "VDIFS: superblock read completed (size %zu)\n", sb_bh->b_size);
	sb->s_fs_info = sbi;
	vh = (struct vdifs_header *) sb_bh->b_data;
	printk(KERN_DEBUG "VDIFS: magic string \"%s\"\n", vh->magic_string);
	/* FIXME: this string could be different (Sun vs Oracle), use magic no. */
	if (strcmp(vh->magic_string, MAGIC_STRING) != 0) {
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
	sb->s_blocksize = VDIFS_BLOCK_SIZE;
	printk(KERN_DEBUG "VDIfs: blocksize %lu\n", sb->s_blocksize);
	sb->s_blocksize_bits = blksize_bits(VDIFS_BLOCK_SIZE);
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sbi->img_type = le32_to_cpu(vh->img_type);
	printk(KERN_DEBUG "VDIfs: image type %u\n", sbi->img_type);
	sbi->block_offset = le32_to_cpu(vh->block_offset);
	printk(KERN_DEBUG "VDIfs: data offset %x\n", sbi->block_offset);
	sbi->map_offset = le32_to_cpu(vh->map_offset);
	printk(KERN_DEBUG "VDIfs: map_offset %x\n", sbi->map_offset);
	sbi->disk_blocks = le32_to_cpu(vh->disk_blocks);
	printk(KERN_DEBUG "VDIfs: %u blocks in image\n", sbi->disk_blocks);
	sbi->disk_bytes = le64_to_cpu(vh->disk_bytes);
	printk("VDIFS: %llu disk bytes in image\n", sbi->disk_bytes);
	sbi->alloced_blocks = le32_to_cpu(vh->allocated_blocks);
	printk(KERN_DEBUG "VDIfs: %u allocated blocks in image\n", sbi->alloced_blocks);
	sbi->block_bytes = le32_to_cpu(vh->block_bytes);
	printk(KERN_DEBUG "VDIFS: %u bytes per image block\n", sbi->block_bytes);
	/* sanity check */
	disk_bytes = sbi->disk_blocks*sb->s_blocksize;
#if 0
	if (disk_bytes != sbi->disk_bytes) {
		printk(KERN_DEBUG "VDIFS: disk_blocks=%u, block_bytes=%lu, disk_bytes=%llu\n",
			sbi->disk_blocks, sb->s_blocksize, sbi->disk_bytes);
		printk(KERN_ERR "VDIfs: superblock appears to be corrupt");
		printk(KERN_ERR "VDIFS: calculated disk size is %llu\n", disk_bytes);
		goto bad_sb_buf;
	}
#endif
	printk(KERN_DEBUG "VDIFS: sanity check passed\n");

	if (sbi->img_type == VDI_DYNAMIC) {
		printk(KERN_DEBUG "VDIFS: detected dynamic format\n");
		sbi->blockmap = kzalloc(sbi->disk_blocks, GFP_KERNEL);
		if (!sbi->blockmap) {
			kfree(sbi);
			brelse(sb_bh);
			return -ENOMEM;
		}
		printk(KERN_DEBUG "VDIFS: blockmap allocated ()");
		logical_map_block = sbi->block_offset / sb->s_bdev->bd_block_size;
		printk(KERN_DEBUG "VDIFS: logical map block is %lu\n", logical_map_block);
		/* FIXME: magic number 4 (= sizeof(u_int32_t)) */
		bmap_bh = __bread(sb->s_bdev, logical_map_block, sbi->disk_blocks*4);
		if (!bmap_bh) {
			printk(KERN_ERR "VDIfs: failed to read block map for dynamic image\n");
			goto bad_bmap;
		}
		sbi->bmap_bh = bmap_bh;
		printk(KERN_DEBUG "VDIFS: block map read (size %zu)\n", bmap_bh->b_size);
		le_bmap = (int32_t*)bmap_bh->b_data;
		for (i=0; i<sbi->disk_blocks; i++) {
			sbi->blockmap[i] = le32_to_cpu(le_bmap[i]);
		}
		printk(KERN_DEBUG "VDIFS: blockmap loaded\n");
	}
	
	sb->s_op = &vdifs_super_ops;
	
	root = vdifs_make_inode(sb, S_IFDIR | 0555);
	if (!root)
		goto bad_bmap;
	root->i_op = &simple_dir_inode_operations;
	root->i_fop = &simple_dir_operations;
	printk(KERN_DEBUG "VDIFS: root inode created\n");
	
	root_dentry = d_alloc_root(root);
	if (! root_dentry)
		goto bad_root_inode;
	printk(KERN_DEBUG "VDIFS: root dentry allocated\n");
	if (!vdifs_create_file(sb, root_dentry, "image")) {
		printk(KERN_ERR "VDIfs: failed to create image file\n");
		goto bad_root_inode;
	}
	printk(KERN_DEBUG "VDIFS: image file created\n");
	printk(KERN_DEBUG "VDIFS: block device has size %llu\n", sb->s_bdev->bd_inode->i_size);
	sb->s_root = root_dentry;
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

static int vdifs_get_superblock(struct file_system_type *fst,
  int flags, const char *devname, void *data, struct vfsmount *vmnt)
{
	return get_sb_bdev(fst, flags, devname, data, vdifs_fill_superblock, vmnt);
} 

static int __init vdifs_init(void)
{
	int ret;
	printk(KERN_DEBUG "VDIFS: registering filesystem\n");
	ret = register_filesystem(&vdifs_type);
	printk(KERN_DEBUG "VDIFS: register_filesystem returned %d\n", ret);
	return ret;
}

static void __exit vdifs_exit(void)
{
	unregister_filesystem(&vdifs_type);
}
module_init(vdifs_init);
module_exit(vdifs_exit)
