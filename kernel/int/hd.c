/************************************************************/
/*                       硬盘中断                           */
/************************************************************/
#include "type.h"
#include "global.h"
#include "klib.h"
#include "assert.h"
#include "hd.h"
static u8 hd_status;
static void hd_handler(int irq_no);
void init_hd()
{
	assert(hdinfo.hd_num>0);
	_disable_irq(AT_WINI_IRQ);
	irq_handler_table[AT_WINI_IRQ] = hd_handler;
	_enable_irq(CASCADE_IRQ);
	_enable_irq(AT_WINI_IRQ);
}
void hd_handler(int irq_no)
{
	hd_status = _in_byte(REG_STATUS);
	_disp_str("hd",23,0,COLOR_YELLOW);
}