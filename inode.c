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
	.mmap = generic_file_mmap
};

int vdifs_write_page(struct page*, struct writeback_control*);
int vdifs_read_page(struct file*, struct page*);

static struct address_space_operations vdifs_aops = {
	.writepage = vdifs_write_page,
	.readpage = vdifs_read_page
};

int vdifs_get_block(struct inode *inode, sector_t iblock, 
	struct buffer_head *bh_result, int create)
{
	int32_t block_start;
	sector_t logical_block;
	struct super_block *sb;

	printk(KERN_DEBUG "VDIFS: vdifs_get_block called\n");	
	sb = inode->i_sb;
	if (iblock > VDIFS_SB(inode->i_sb)->disk_blocks)
		goto out;
	if (VDIFS_SB(sb)->image_type == VDI_STATIC) {
		block_start = iblock * sb->s_blocksize + VDIFS_SB(sb)->block_offset;
		return 0;
	} else {
		/* assuming sparse vdi format */
		block_start = VDIFS_SB(sb)->blockmap[iblock];
		if (block_start < 0) {
			if (!create)
				goto out;
			VDIFS_SB(sb)->alloced_blocks++;
			block_start = VDIFS_SB(sb)->block_offset;
			block_start += VDIFS_SB(sb)->alloced_blocks*sb->s_blocksize;
			VDIFS_SB(sb)->blockmap[iblock] = block_start;
			/* FIXME: assumes FS image block alignment */
			logical_block = block_start / sb->s_bdev->bd_block_size;
			map_bh(bh_result, sb, logical_block);
			memset(bh_result->b_data, 0, bh_result->b_size);
			mark_buffer_dirty(bh_result);
			return 0;
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
	
	inode = new_inode(sb);
	if (!inode)
		return NULL;
	inode->i_mode = mode;
	inode->i_uid = inode->i_gid = 0;
	/* looks like this is set with inode_init_always() */
	/* inode->i_blkbits = sb->s_blocksize_bits; */
	inode->i_blocks = VDIFS_SB(sb)->disk_blocks;
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
