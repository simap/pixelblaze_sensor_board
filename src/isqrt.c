#include "stdint.h"

/*Integer square root - Obtained from Stack Overflow (14/6/15):
 * http://stackoverflow.com/questions/1100090/looking-for-an-efficient-integer-square-root-algorithm-for-arm-thumb2
 * User: Gutskalk
 */
uint16_t isqrt(uint32_t x)
{
	uint16_t res=0;
	uint16_t add= 0x8000;
	int i;
	for(i=0;i<16;i++)
	{
		uint16_t temp=res | add;
		uint32_t g2=temp*temp;
		if (x>=g2)
		{
			res=temp;
		}
		add>>=1;
	}
	return res;
}
