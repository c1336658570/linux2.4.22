/*
 * Written by Kanoj Sarcar, SGI, Aug 1999
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/spinlock.h>

int numnodes = 1;	/* Initialized for UMA platforms */

static bootmem_data_t contig_bootmem_data;
pg_data_t contig_page_data = { bdata: &contig_bootmem_data };

#ifndef CONFIG_DISCONTIGMEM

/*
 * This is meant to be invoked by platforms whose physical memory starts
 * at a considerably higher value than 0. Examples are Super-H, ARM, m68k.
 * Should be invoked with paramters (0, 0, unsigned long *[], start_paddr).
 */
void __init free_area_init_node(int nid, pg_data_t *pgdat, struct page *pmap,
	unsigned long *zones_size, unsigned long zone_start_paddr, 
	unsigned long *zholes_size)
	// nid，被初始化管理区中节点的逻辑标识符，NodeID
	// pgdat，节点中被初始化的pa_data_t。在UMA结构里为contig_page_data。
	// pmap，被free_area_init_core函数用于设置指向分配给节点的局部lmem_map数组的指针UMA结构中，它往往被忽略掉，因为NUMA将mem_map处理为起始于PAGE_OFF-SET的虚拟数组。而在UMA中，该指针指向全局mem_map变量，目前还有mem_map，都在UMA中被初始化。
	// zones_size，一个包含每个管理区大小的数组，管理区大小以页面为单位计算
	// zone_start_paddr，第一个管理区起始物理地址
	// zone_holes，一个包含管理区中所有内存空洞大小的数组
{
	free_area_init_core(0, &contig_page_data, &mem_map, zones_size, 
				zone_start_paddr, zholes_size, pmap);
}

#endif /* !CONFIG_DISCONTIGMEM */

struct page * alloc_pages_node(int nid, unsigned int gfp_mask, unsigned int order)
{
#ifdef CONFIG_NUMA
	return __alloc_pages(gfp_mask, order, NODE_DATA(nid)->node_zonelists + (gfp_mask & GFP_ZONEMASK));
#else
	return alloc_pages(gfp_mask, order);
#endif
}

#ifdef CONFIG_DISCONTIGMEM

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

static spinlock_t node_lock = SPIN_LOCK_UNLOCKED;

void show_free_areas_node(pg_data_t *pgdat)
{
	unsigned long flags;

	spin_lock_irqsave(&node_lock, flags);
	show_free_areas_core(pgdat);
	spin_unlock_irqrestore(&node_lock, flags);
}

/*
 * Nodes can be initialized parallely, in no particular order.
 */
void __init free_area_init_node(int nid, pg_data_t *pgdat, struct page *pmap,
	unsigned long *zones_size, unsigned long zone_start_paddr, 
	unsigned long *zholes_size)
{
	// nid，被初始化管理区中节点的逻辑标识符，NodeID
	// pgdat，节点中被初始化的pa_data_t。在UMA结构里为contig_page_data。
	// pmap，被free_area_init_core函数用于设置指向分配给节点的局部lmem_map数组的指针UMA结构中，它往往被忽略掉，因为NUMA将mem_map处理为起始于PAGE_OFF-SET的虚拟数组。而在UMA中，该指针指向全局mem_map变量，目前还有mem_map，都在UMA中被初始化。
	// zones_size，一个包含每个管理区大小的数组，管理区大小以页面为单位计算
	// zone_start_paddr，第一个管理区起始物理地址
	// zone_holes，一个包含管理区中所有内存空洞大小的数组
	int i, size = 0;
	struct page *discard;

	if (mem_map == (mem_map_t *)NULL)
		mem_map = (mem_map_t *)PAGE_OFFSET;

	free_area_init_core(nid, pgdat, &discard, zones_size, zone_start_paddr,
					zholes_size, pmap);
	pgdat->node_id = nid;

	/*
	 * Get space for the valid bitmap.
	 */
	for (i = 0; i < MAX_NR_ZONES; i++)
		size += zones_size[i];
	size = LONG_ALIGN((size + 7) >> 3);
	pgdat->valid_addr_bitmap = (unsigned long *)alloc_bootmem_node(pgdat, size);
	memset(pgdat->valid_addr_bitmap, 0, size);
}

static struct page * alloc_pages_pgdat(pg_data_t *pgdat, unsigned int gfp_mask,
	unsigned int order)
{
	return __alloc_pages(gfp_mask, order, pgdat->node_zonelists + (gfp_mask & GFP_ZONEMASK));
}

/*
 * This can be refined. Currently, tries to do round robin, instead
 * should do concentratic circle search, starting from current node.
 */
struct page * _alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	struct page *ret = 0;
	pg_data_t *start, *temp;
#ifndef CONFIG_NUMA
	unsigned long flags;
	static pg_data_t *next = 0;
#endif

	if (order >= MAX_ORDER)
		return NULL;
#ifdef CONFIG_NUMA
	temp = NODE_DATA(numa_node_id());
#else
	spin_lock_irqsave(&node_lock, flags);
	if (!next) next = pgdat_list;
	temp = next;
	next = next->node_next;
	spin_unlock_irqrestore(&node_lock, flags);
#endif
	start = temp;
	while (temp) {
		if ((ret = alloc_pages_pgdat(temp, gfp_mask, order)))
			return(ret);
		temp = temp->node_next;
	}
	temp = pgdat_list;
	while (temp != start) {
		if ((ret = alloc_pages_pgdat(temp, gfp_mask, order)))
			return(ret);
		temp = temp->node_next;
	}
	return(0);
}

#endif /* CONFIG_DISCONTIGMEM */
