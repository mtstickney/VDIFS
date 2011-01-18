#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/dcache.h>
#include <linux/mpage.h>
#include "vdifs.h"

static struct file_operations vdifs_file_ops = {
	.open = generic_file_open,
	.read = do_sync_read,
	.write = do_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
	.mmap = generic_file_mmap,
	.llseek = generic_file_llseek,
};

int vdifs_write_page(struct page*, struct writeback_control*);
int vdifs_read_page(struct file*, struct page*);

static struct address_space_operations vdifs_aops = {
	.writepage = vdifs_write_page,
	.readpage = vdifs_read_page
};

/* Allocate an image block for dynamic files. */
/* NB: this will fail unless the device can be resized */
static int alloc_image_block(struct super_block *sb, unsigned int img_block)
{
	struct vdifs_sb_info *sbi;
	int32_t block_start;
	
	printk(KERN_DEBUG "VDIFS: alloc_block called\n");
	sbi = VDIFS_SB(sb);
	if (img_block > sbi->disk_blocks)
		return 1;
	block_start = sbi->blockmap[img_block];
	if (block_start < 0) {
		sbi->alloced_blocks++;
		block_start = sbi->block_offset;
		block_start += sbi->alloced_blocks*sbi->block_bytes;
		sbi->blockmap[img_block] = block_start;
		sb->s_dirt = 1;
	}
	return 0;
}

/* Initialize a newly allocated image block */
int init_image_block(struct super_block *sb, unsigned int img_block)
{
	sector_t logical_block;
	struct buffer_head *bh;
	int32_t block_start;
	struct vdifs_sb_info *sbi;
	unsigned int i;
	unsigned int whole_blocks, remainder;
	unsigned disk_blocks;
	
	printk(KERN_DEBUG "VDIFS: block_init called\n");
	
	sbi = VDIFS_SB(sb);
	disk_blocks = sbi->disk_bytes / sb->s_blocksize;
	if (img_block > disk_blocks)
		return 1;
	if (sbi->img_type == VDI_STATIC)
		return 0;

	block_start = sbi->blockmap[img_block];
	if (block_start < 0)
		return 1;
	whole_blocks = sbi->block_bytes / sb->s_blocksize;
	remainder = sbi->block_bytes % sb->s_blocksize;
	/* zero the new block in sb->s_blocksize increments */
	for (i=0; i<whole_blocks; i++) {
		logical_block = (block_start+sb->s_blocksize*i)/sb->s_bdev->bd_block_size;
		bh = __getblk(sb->s_bdev, logical_block, sb->s_blocksize);
		if (!bh)
			return 1;
		memset(bh->b_data, 0, bh->b_size);
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	if (remainder) {
		/* zero the remainder of the image block */
		logical_block = (block_start+sb->s_blocksize*i)/sb->s_bdev->bd_block_size;
		bh = __getblk(sb->s_bdev, logical_block, remainder);
		if (!bh)
			return 1;
		memset(bh->b_data, 0, bh->b_size);
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	return 0;
}

static int vdifs_get_block(struct inode *inode, sector_t iblock, 
	struct buffer_head *bh_result, int create)
{
	int32_t block_start;
	unsigned int img_block;
	unsigned int img_block_start;
	unsigned int disk_blocks;
	sector_t logical_block;
	struct super_block *sb;

	printk(KERN_DEBUG "VDIFS: vdifs_get_block called\n");	
	sb = inode->i_sb;
	disk_blocks = VDIFS_SB(sb)->disk_bytes / sb->s_blocksize;
	if (iblock > disk_blocks) {
		printk(KERN_DEBUG "VDIFS: attempt to get block past end of disk\n");
		goto out;
	}
	if (VDIFS_SB(sb)->img_type == VDI_STATIC) {
		sector_t maxblocks;
		sector_t req_blocks;
		
		printk(KERN_DEBUG "VDIFS: getting static block\n");
		printk(KERN_DEBUG "VDIFS: iblock=%llu\n", iblock);
		printk(KERN_DEBUG "VDIFS: offset=%u\n", VDIFS_SB(sb)->block_offset);
		printk(KERN_DEBUG "VDIFS: blocksize=%lu\n", sb->s_blocksize);
		block_start = VDIFS_SB(sb)->block_offset+iblock*sb->s_blocksize;
		printk(KERN_DEBUG "VDIFS: block start is at %d bytes\n", block_start);
		logical_block = block_start / sb->s_blocksize;
		maxblocks = sb->s_bdev->bd_inode->i_size / sb->s_bdev->bd_block_size;
		req_blocks = bh_result->b_size / sb->s_bdev->bd_block_size;
		if (logical_block+req_blocks > maxblocks) {
			printk(KERN_DEBUG "VDIFS: logical block is larger than max disk blocks\n");
			return 1;
		}
		printk(KERN_DEBUG "VDIFS: logical block is %llu\n", logical_block);
		printk(KERN_DEBUG "VDIFS: request size is %zu\n", bh_result->b_size);
		map_bh(bh_result, sb, logical_block);
		return 0;
	} else { /* Dynamic format */
		struct vdifs_sb_info *sbi;

		sbi = VDIFS_SB(sb);
		printk(KERN_DEBUG "VDIFS: getting dynamic block\n");
		block_start = iblock * sb->s_blocksize + VDIFS_SB(sb)->block_offset;
		img_block = block_start / VDIFS_SB(sb)->block_bytes;
		img_block_start = sbi->blockmap[img_block]*sbi->block_bytes+sbi->block_offset;
		if (img_block_start < 0) {
			if (!create) {
				printk(KERN_DEBUG "VDIFS: not allocating block\n");
				goto out;
			}
			if (alloc_image_block(sb, img_block)) {
				printk(KERN_DEBUG "VDIFS: block allocation failed\n");
				goto out;
			}
			if (init_image_block(sb, img_block)) {
				printk(KERN_DEBUG "VDIFS: failed to initialize block\n");
				goto out;
			}
		}
		logical_block = block_start / sb->s_bdev->bd_block_size;
		map_bh(bh_result, sb, logical_block);
		return 0;
	}
out:
	return 1;
}

int vdifs_write_page(struct page *page, struct writeback_control *wbc)
{
	printk(KERN_DEBUG "VDIFS: vdifs_write_page called\n");
	return mpage_writepage(page, vdifs_get_block, wbc);
}

int vdifs_read_page(struct file *file, struct page *page)
{
	printk(KERN_DEBUG "VDIFS: vdifs_read_page called\n");
	return mpage_readpage(page, vdifs_get_block);
}

struct inode *vdifs_make_inode(struct super_block *sb, int mode)
{
	struct inode *inode;
	struct vdifs_sb_info *sbi;
	
	sbi = VDIFS_SB(sb);
	inode = new_inode(sb);
	if (!inode)
		return NULL;
	inode->i_mode = mode;
	inode->i_uid = inode->i_gid = 0;
	inode->i_size = sbi->disk_bytes;
	inode->i_blocks = sbi->disk_bytes / sb->s_blocksize;
	inode->i_mtime = inode->i_ctime = inode->i_atime = CURRENT_TIME;
	inode->i_mapping->a_ops = &vdifs_aops;
	return inode; 
}

struct dentry *vdifs_create_file(struct super_block *sb,
	struct dentry *dir, const char *name)
{
	struct dentry *dentry;
	struct inode *inode;
	struct qstr qname;
	
	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(name, qname.len);
	dentry = d_alloc(dir, &qname);
	if (!dentry)
		goto out;
	inode = vdifs_make_inode(sb, S_IFREG | 0644);
	if (!inode)
		goto out_dput;
	inode->i_fop = &vdifs_file_ops;
	/* stick the dentry in the cache so it doesn't need to be looked up */
	d_add(dentry, inode);
	return dentry;

out_dput:
	dput(dentry);
out:
	return NULL;
}
