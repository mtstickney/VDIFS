#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/dcache.h>
#include "vdifs.h"

static struct file_operations vdifs_file_ops = {
	.open = generic_file_open,
	.read = do_sync_read,
	.write = do_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
	.mmap = generic_file_mmap
};

struct inode *vdifs_make_inode(struct super_block *sb, int mode)
{
	struct inode *inode;
	
	inode = new_inode(sb);
	if (!inode)
		return NULL;
	inode->i_mode = mode;
	inode->i_uid = inode->i_gid = 0;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_blocks = VDIFS_SB(sb)->disk_blocks;
	inode->i_mtime = inode->i_ctime = inode->i_atime = CURRENT_TIME;
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
