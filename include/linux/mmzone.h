#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>

/*
 * Free memory management - zoned buddy allocator.
 */

#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 10
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif

typedef struct free_area_struct {
	struct list_head	free_list;
	unsigned long		*map;
} free_area_t;

struct pglist_data;

/*
 * On machines where it is needed (eg PCs) we divide physical memory
 * into multiple physical zones. On a PC we have 3 zones:
 *
 * ZONE_DMA	  < 16 MB	ISA DMA capable memory
 * ZONE_NORMAL	16-896 MB	direct mapped by the kernel
 * ZONE_HIGHMEM	 > 896 MB	only page cache and user processes
 */
typedef struct zone_struct {	//管理区
	/*
	 * Commonly accessed fields:
	 */
	spinlock_t		lock;		// 并行访问时保护该管理区的自选锁
	unsigned long		free_pages;		// 该管理区中空闲页面的总数
	unsigned long		pages_min, pages_low, pages_high;	// 都是管理区的极值
	int			need_balance;	//该标志位通知页面换出kswapd，平衡该管理区。

	/*
	 * free areas of different sizes
	 */
	free_area_t		free_area[MAX_ORDER];		// 空闲区域位图，由伙伴分配器使用

	/*
	 * wait_table		-- the array holding the hash table
	 * wait_table_size	-- the size of the hash table array
	 * wait_table_shift	-- wait_table_size
	 * 				== BITS_PER_LONG (1 << wait_table_bits)
	 *
	 * The purpose of all these is to keep track of the people
	 * waiting for a page to become available and make them
	 * runnable again when possible. The trouble is that this
	 * consumes a lot of space, especially when so few things
	 * wait on pages at a given time. So instead of using
	 * per-page waitqueues, we use a waitqueue hash table.
	 *
	 * The bucket discipline is to sleep on the same queue when
	 * colliding and wake all in that wait queue when removing.
	 * When something wakes, it must check to be sure its page is
	 * truly available, a la thundering herd. The cost of a
	 * collision is great, but given the expected load of the
	 * table, they should be so rare as to be outweighed by the
	 * benefits from the saved space.
	 *
	 * __wait_on_page() and unlock_page() in mm/filemap.c, are the
	 * primary users of these fields, and in mm/page_alloc.c
	 * free_area_init_core() performs the initialization of them.
	 */
	wait_queue_head_t	* wait_table;		// 等待队列的哈希表，由等待页面释放的进程组成。
	unsigned long		wait_table_size;	// 该哈希表的大小，它是2的幂
	unsigned long		wait_table_shift;	// 定义为一个long型所对应的位数减去上述表大小的二进制对数

	/*
	 * Discontig memory support fields.
	 */
	struct pglist_data	*zone_pgdat;	// 指向父pg_data_t
	struct page		*zone_mem_map;			// 涉及的管理区在全局mem_map中的第一页
	unsigned long		zone_start_paddr;	// 同node_start_paddr		节点起始物理地址。
	unsigned long		zone_start_mapnr;	// 同node_start_mapnr		指出节点在全局mem_map中的页面偏移。在free_area_init_core()中，通过计算mem_map与该节点的局部mem_map中称为lmem_map之间的页面数，从而得到页面偏移

	/*
	 * rarely used fields:
	 */
	char			*name;		// 该管理区的字符串名字，“DMA”，“Normal”或者“HighMem”
	unsigned long		size;	// 该管理区的大小，以页面数为单位。
} zone_t;

#define ZONE_DMA		0
#define ZONE_NORMAL		1
#define ZONE_HIGHMEM		2
#define MAX_NR_ZONES		3

/*
 * One allocation request operates on a zonelist. A zonelist
 * is a list of zones, the first one is the 'goal' of the
 * allocation, the other zones are fallback zones, in decreasing
 * priority.
 *
 * Right now a zonelist takes up less than a cacheline. We never
 * modify it apart from boot-up, and only a few indices are used,
 * so despite the zonelist table being relatively big, the cache
 * footprint of this construct is very small.
 */
typedef struct zonelist_struct {
	zone_t * zones [MAX_NR_ZONES+1]; // NULL delimited
} zonelist_t;

#define GFP_ZONEMASK	0x0f

/*
 * The pg_data_t structure is used in machines with CONFIG_DISCONTIGMEM
 * (mostly NUMA machines?) to denote a higher-level memory zone than the
 * zone_struct denotes.
 *
 * On NUMA machines, each NUMA node would have a pg_data_t to describe
 * it's memory layout.
 *
 * XXX: we need to move the global memory statistics (active_list, ...)
 *      into the pg_data_t to properly support NUMA.
 */
/*
pg_data_t 结构体用于具有 CONFIG_DISCONTIGMEM 配置选项的机器，
这主要用于 NUMA（非一致内存访问）机器，用于表示比 zone_struct 更高级别的内存区域。
在 NUMA 机器上，每个 NUMA 节点都有一个 pg_data_t 结构体来描述其内存布局。
XXX：我们需要将全局内存统计信息（如 active_list）移动到 pg_data_t 中以正确支持 NUMA。
*/
//每个节点由pg_dat_t描述，而pg_data_t由struct pglist_data定义而来
struct bootmem_data;
typedef struct pglist_data {
	zone_t node_zones[MAX_NR_ZONES];	// 该节点所在管理区为ZONE_HIGHMEM，ZONE_NORMAL和ZONE_DMA
	zonelist_t node_zonelists[GFP_ZONEMASK+1];	// 按分配时的管理区顺序排列。调用free_area_init_core()时，通过mm/page_alloc.c文件中的build_zonelists()建立顺序。如果在ZONE_HIGHMEM中分配失败，就可能还原成ZONE_NORMAL和ZONE_DMA。
	int nr_zones;		// 表示该节点的管理区数目1-3之间，并不是所有节点都有3个管理区，有的CPU簇就可能没ZONE_DMA
	struct page *node_mem_map;	// 指struct page数组中的第一个页面，代表该节点中的每个物理帧。它被放置在全局mem_map数组中。
	unsigned long *valid_addr_bitmap;	// 一张描述内存节点“空洞”的位图，因为并没有实际内存空间存在，只在Sparc和Sparc64体系结构中使用
	struct bootmem_data *bdata;	// 指向内存引导程序
	unsigned long node_start_paddr;	// 节点起始物理地址。
	unsigned long node_start_mapnr;	// 指出节点在全局mem_map中的页面偏移。在free_area_init_core()中，通过计算mem_map与该节点的局部mem_map中称为lmem_map之间的页面数，从而得到页面偏移
	unsigned long node_size;	// 这个管理区中的页面总数
	int node_id;	//节点的ID号（NID），从0开始
	struct pglist_data *node_next;	//指向下一节点的，以NULL结束。
} pg_data_t;			//所有节点被一个称为pgdat_list的链表维护。这些节点都放在该链表中，均有函数init_bootmem_core()初始化节点。

extern int numnodes;
extern pg_data_t *pgdat_list;

#define memclass(pgzone, classzone)	(((pgzone)->zone_pgdat == (classzone)->zone_pgdat) \
			&& ((pgzone) <= (classzone)))

/*
 * The following two are not meant for general usage. They are here as
 * prototypes for the discontig memory code.
 */
struct page;
extern void show_free_areas_core(pg_data_t *pgdat);
extern void free_area_init_core(int nid, pg_data_t *pgdat, struct page **gmap,
  unsigned long *zones_size, unsigned long paddr, unsigned long *zholes_size,
  struct page *pmap);

extern pg_data_t contig_page_data;

/**
 * for_each_pgdat - helper macro to iterate over all nodes
 * @pgdat - pg_data_t * variable
 *
 * Meant to help with common loops of the form
 * pgdat = pgdat_list;
 * while(pgdat) {
 * 	...
 * 	pgdat = pgdat->node_next;
 * }
 */
#define for_each_pgdat(pgdat) \
	for (pgdat = pgdat_list; pgdat; pgdat = pgdat->node_next)


/*
 * next_zone - helper magic for for_each_zone()
 * Thanks to William Lee Irwin III for this piece of ingenuity.
 */
static inline zone_t *next_zone(zone_t *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;

	if (zone - pgdat->node_zones < MAX_NR_ZONES - 1)
		zone++;

	else if (pgdat->node_next) {
		pgdat = pgdat->node_next;
		zone = pgdat->node_zones;
	} else
		zone = NULL;

	return zone;
}

/**
 * for_each_zone - helper macro to iterate over all memory zones
 * @zone - zone_t * variable
 *
 * The user only needs to declare the zone variable, for_each_zone
 * fills it in. This basically means for_each_zone() is an
 * easier to read version of this piece of code:
 *
 * for(pgdat = pgdat_list; pgdat; pgdat = pgdat->node_next)
 * 	for(i = 0; i < MAX_NR_ZONES; ++i) {
 * 		zone_t * z = pgdat->node_zones + i;
 * 		...
 * 	}
 * }
 */
#define for_each_zone(zone) \
	for(zone = pgdat_list->node_zones; zone; zone = next_zone(zone))


#ifndef CONFIG_DISCONTIGMEM

#define NODE_DATA(nid)		(&contig_page_data)
#define NODE_MEM_MAP(nid)	mem_map
#define MAX_NR_NODES		1

#else /* !CONFIG_DISCONTIGMEM */

#include <asm/mmzone.h>

/* page->zone is currently 8 bits ... */
#ifndef MAX_NR_NODES
#define MAX_NR_NODES		(255 / MAX_NR_ZONES)
#endif

#endif /* !CONFIG_DISCONTIGMEM */

#define MAP_ALIGN(x)	((((x) % sizeof(mem_map_t)) == 0) ? (x) : ((x) + \
		sizeof(mem_map_t) - ((x) % sizeof(mem_map_t))))

#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _LINUX_MMZONE_H */
