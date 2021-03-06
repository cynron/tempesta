/**
 *		Tempesta FW
 *
 * Copyright (C) 2016 Tempesta Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "procfs.h"

DEFINE_PER_CPU_ALIGNED(TfwPerfStat, tfw_perfstat);

static struct proc_dir_entry *tfw_procfs_tempesta;
static struct proc_dir_entry *tfw_procfs_perfstat;

void
tfw_perfstat_collect(TfwPerfStat *stat)
{
#define SADD(x)	stat->x += pcp_stat->x

	int cpu;

	/*
	 * Collecting values this way is safe on a 64-bit architecture.
	 * Note that CPU hot-plugging are not supported, at least not
	 * consciously. For that reason only online CPUs are examined.
	 */
	for_each_online_cpu(cpu) {
		TfwPerfStat *pcp_stat = per_cpu_ptr(&tfw_perfstat, cpu);

		/* Ss statistics. */
		SADD(ss.pfl_hits);
		SADD(ss.pfl_misses);

		/* Cache statistics. */
		SADD(cache.hits);
		SADD(cache.misses);

		/* Client related statistics. */
		SADD(clnt.rx_messages);
		SADD(clnt.msgs_forwarded);
		SADD(clnt.msgs_fromcache);
		SADD(clnt.msgs_parserr);
		SADD(clnt.msgs_filtout);
		SADD(clnt.msgs_otherr);
		SADD(clnt.conn_attempts);
		SADD(clnt.conn_disconnects);
		SADD(clnt.conn_established);
		SADD(clnt.rx_bytes);

		/* Server related statistics. */
		SADD(serv.rx_messages);
		SADD(serv.msgs_forwarded);
		SADD(serv.msgs_parserr);
		SADD(serv.msgs_filtout);
		SADD(serv.msgs_otherr);
		SADD(serv.conn_attempts);
		SADD(serv.conn_disconnects);
		SADD(serv.conn_established);
		SADD(serv.rx_bytes);
	}
#undef SADD
}

static int
tfw_perfstat_seq_show(struct seq_file *seq, void *off)
{
#define SPRNE(m, e)							\
	if ((ret = seq_printf(seq, m": %llu\n", e)))			\
		goto out;
#define SPRN(m, c)							\
	if ((ret = seq_printf(seq, m": %llu\n", stat.c)))		\
		goto out;

	int ret;
	TfwPerfStat stat;

	memset(&stat, 0, sizeof(stat));
	tfw_perfstat_collect(&stat);

	/* Ss statistics. */
	SPRN("SS pfl hits\t\t\t\t", ss.pfl_hits);
	SPRN("SS pfl misses\t\t\t\t", ss.pfl_misses);

	/* Cache statistics. */
	SPRN("Cache hits\t\t\t\t", cache.hits);
	SPRN("Cache misses\t\t\t\t", cache.misses);

	/* Client related statistics. */
	SPRN("Client messages received\t\t", clnt.rx_messages);
	SPRN("Client messages forwarded\t\t", clnt.msgs_forwarded);
	SPRN("Client messages served from cache\t", clnt.msgs_fromcache);
	SPRN("Client messages parsing errors\t\t", clnt.msgs_parserr);
	SPRN("Client messages filtered out\t\t", clnt.msgs_filtout);
	SPRN("Client messages other errors\t\t", clnt.msgs_otherr);
	SPRN("Client connection attempts\t\t", clnt.conn_attempts);
	SPRN("Client established connections\t\t", clnt.conn_established);
	SPRNE("Client connections active\t\t",
	      stat.clnt.conn_established - stat.clnt.conn_disconnects);
	SPRN("Client RX bytes\t\t\t\t", clnt.rx_bytes);

	/* Server related statistics. */
	SPRN("Server messages received\t\t", serv.rx_messages);
	SPRN("Server messages forwarded\t\t", serv.msgs_forwarded);
	SPRN("Server messages parsing errors\t\t", serv.msgs_parserr);
	SPRN("Server messages filtered out\t\t", serv.msgs_filtout);
	SPRN("Server messages other errors\t\t", serv.msgs_otherr);
	SPRN("Server connection attempts\t\t", serv.conn_attempts);
	SPRN("Server established connections\t\t", serv.conn_established);
	SPRNE("Server connections active\t\t",
	      stat.serv.conn_established - stat.serv.conn_disconnects);
	SPRN("Server RX bytes\t\t\t\t", serv.rx_bytes);

out:
	return ret;
#undef SPRN
#undef SPRNE
}

static int
tfw_perfstat_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, tfw_perfstat_seq_show, PDE_DATA(inode));
}

static struct file_operations tfw_perfstat_fops = {
	.owner		= THIS_MODULE,
	.open		= tfw_perfstat_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int
tfw_procfs_init(void)
{
	tfw_procfs_tempesta = proc_mkdir("tempesta", NULL);
	if (!tfw_procfs_tempesta)
		goto out;

	tfw_procfs_perfstat = proc_create("perfstat", S_IRUGO,
					  tfw_procfs_tempesta,
					  &tfw_perfstat_fops);
	if (!tfw_procfs_perfstat)
		goto out_tempesta;

	return 0;

out:
	return -ENOMEM;
out_tempesta:
	remove_proc_entry("tempesta", NULL);
	goto out;
}

void
tfw_procfs_exit(void)
{
	remove_proc_entry("perfstat", tfw_procfs_tempesta);
	remove_proc_entry("tempesta", NULL);
}
